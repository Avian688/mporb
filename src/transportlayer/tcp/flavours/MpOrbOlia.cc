//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//

#include "MpOrbOlia.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include "../../../../../mptcp/src/transportlayer/tcp/MpTcpConnection.h"

namespace inet {
namespace tcp {

Register_Class(MpOrbOlia);

simsignal_t MpOrbOlia::bestPathSignal = cComponent::registerSignal("mpOrbOliaBestPath");
simsignal_t MpOrbOlia::maxWindowPathSignal = cComponent::registerSignal("mpOrbOliaMaxWindowPath");
simsignal_t MpOrbOlia::correctionSignal = cComponent::registerSignal("mpOrbOliaCorrection");
simsignal_t MpOrbOlia::pathPriceSignal = cComponent::registerSignal("mpOrbOliaPathPrice");
simsignal_t MpOrbOlia::pathOpportunitySignal = cComponent::registerSignal("mpOrbOliaPathOpportunity");
simsignal_t MpOrbOlia::normalizedWindowSignal = cComponent::registerSignal("mpOrbOliaNormalizedWindow");

namespace {

struct SubflowMetrics {
    MpOrbOlia *algorithm;
    double rate;
    double rtt;
    double uncoupledAiRate;
    double pathPrice;
    double pathOpportunity;
    double normalizedWindow;
};

bool nearlyEqual(double first, double second)
{
    const double scale = std::max({1.0, std::fabs(first), std::fabs(second)});
    return std::fabs(first - second) <=
            std::numeric_limits<double>::epsilon() * 32.0 * scale;
}

} // namespace

uint32_t MpOrbOlia::computeWnd(double u, bool updateWc)
{
    uint32_t result = OrbtcpFlavour::computeWnd(u, updateWc);
    if (!hasOliaState)
        return result;

    const long double minimumWindow = std::max(state->snd_mss, 1U);
    const long double adjustedWindow = std::clamp(
            static_cast<long double>(result) + pendingCorrection,
            minimumWindow,
            static_cast<long double>(std::numeric_limits<uint32_t>::max()));
    result = static_cast<uint32_t>(adjustedWindow);

    if (updateWc) {
        state->prevWnd = result;
        conn->emit(bestPathSignal, lastBestPath ? 1L : 0L);
        conn->emit(maxWindowPathSignal, lastMaxWindowPath ? 1L : 0L);
        conn->emit(correctionSignal, pendingCorrection);
        conn->emit(pathPriceSignal, lastPathPrice);
        conn->emit(pathOpportunitySignal, lastPathOpportunity);
        conn->emit(normalizedWindowSignal, lastNormalizedWindow);
    }
    return result;
}

void MpOrbOlia::adjustAdditiveIncrease()
{
    // Alpha supplies the base AI: uncoupled OrbCC AI multiplied by this
    // subflow's cwnd/srtt share of the whole multipath connection.
    MpOrbSemiCoupledAlpha::adjustAdditiveIncrease();
    if (state == nullptr || firstRTT || state->initialPhase) {
        hasOliaState = false;
        pendingCorrection = 0.0;
        return;
    }

    // OrbCC commits a window update once per RTT. Keep the same OLIA set
    // decision between those updates while Alpha refreshes its base AI.
    if (!updateWindow && hasOliaState)
        return;

    hasOliaState = false;
    pendingCorrection = 0.0;

    MpTcpConnection *metaConnection = getMetaConnection();
    if (metaConnection == nullptr)
        return;

    std::vector<SubflowMetrics> subflows;
    double connectionRate = 0.0;
    for (auto *subflow : metaConnection->getSubflows()) {
        if (subflow == nullptr)
            continue;

        const int tcpState = subflow->getFsmState();
        if (tcpState != TCP_S_ESTABLISHED && tcpState != TCP_S_CLOSE_WAIT)
            continue;

        auto *algorithm = dynamic_cast<MpOrbOlia *>(subflow->getTcpAlgorithm());
        if (algorithm == nullptr)
            continue;

        const auto *subflowState = static_cast<const OrbtcpStateVariables *>(subflow->getState());
        if (subflowState == nullptr || subflowState->srtt <= SIMTIME_ZERO ||
                subflowState->bottBW <= 0.0 || !std::isfinite(subflowState->eta) ||
                subflowState->eta <= 0.0 ||
                !std::isfinite(subflowState->additiveIncreasePercent) ||
                subflowState->additiveIncreasePercent <= 0.0 ||
                algorithm->pathHopMetrics.empty() ||
                algorithm->pathHopMetrics.size() != subflowState->L.size())
            continue;

        const double connectionCount = std::max(1.0,
                static_cast<double>(subflowState->sharingFlows));
        const double rate = subflowState->snd_cwnd / subflowState->srtt.dbl();
        const double uncoupledAiRate = subflowState->bottBW / connectionCount *
                subflowState->additiveIncreasePercent;

        double pathPrice = 0.0;
        bool validPath = true;
        for (const auto& hop : algorithm->pathHopMetrics) {
            if (!std::isfinite(hop.utilization) ||
                    !std::isfinite(hop.fairRate) || hop.fairRate <= 0.0) {
                validPath = false;
                break;
            }

            // A serial route pays the scarcity price of every hop it uses.
            // Utilization below eta keeps the base price; overload raises it.
            const double pressure = std::max(1.0,
                    hop.utilization / subflowState->eta);
            pathPrice += pressure / hop.fairRate;
        }

        if (!validPath || !std::isfinite(pathPrice) || pathPrice <= 0.0)
            continue;

        const double pathOpportunity = 1.0 / pathPrice;
        const double normalizedWindow = rate / pathOpportunity;

        if (!std::isfinite(rate) || rate <= 0.0 ||
                !std::isfinite(uncoupledAiRate) || uncoupledAiRate <= 0.0 ||
                !std::isfinite(pathOpportunity) || pathOpportunity <= 0.0 ||
                !std::isfinite(normalizedWindow))
            continue;

        subflows.push_back({algorithm, rate, subflowState->srtt.dbl(),
                uncoupledAiRate, pathPrice, pathOpportunity, normalizedWindow});
        connectionRate += rate;
    }

    if (subflows.size() <= 1 || !std::isfinite(connectionRate) ||
            connectionRate <= 0.0)
        return;

    double bestPathPrice = std::numeric_limits<double>::infinity();
    double maximumNormalizedWindow = 0.0;
    double connectionAiRate = 0.0;
    for (const auto& subflow : subflows) {
        bestPathPrice = std::min(bestPathPrice, subflow.pathPrice);
        maximumNormalizedWindow = std::max(maximumNormalizedWindow,
                subflow.normalizedWindow);
        connectionAiRate += subflow.uncoupledAiRate *
                (subflow.rate / connectionRate);
    }

    if (!std::isfinite(connectionAiRate) || connectionAiRate <= 0.0)
        return;

    size_t maxWindowPaths = 0;
    size_t collectedPaths = 0;
    for (const auto& subflow : subflows) {
        const bool bestPath = nearlyEqual(subflow.pathPrice, bestPathPrice);
        const bool maxWindowPath = nearlyEqual(subflow.normalizedWindow,
                maximumNormalizedWindow);
        if (maxWindowPath)
            maxWindowPaths++;
        if (bestPath && !maxWindowPath)
            collectedPaths++;
    }

    auto current = std::find_if(subflows.begin(), subflows.end(),
            [this](const SubflowMetrics& subflow) {
                return subflow.algorithm == this;
            });
    if (current == subflows.end())
        return;

    const bool bestPath = nearlyEqual(current->pathPrice, bestPathPrice);
    const bool maxWindowPath = nearlyEqual(current->normalizedWindow,
            maximumNormalizedWindow);
    const bool collectedPath = bestPath && !maxWindowPath;

    // This is OLIA's zero-sum opportunistic term translated to OrbCC's
    // once-per-RTT AI rate. It moves 1/N of Alpha's connection AI budget
    // from paths with the largest normalized windows to underused best paths.
    double correctionRate = 0.0;
    if (collectedPaths > 0) {
        if (collectedPath) {
            correctionRate = connectionAiRate /
                    (subflows.size() * collectedPaths);
        }
        else if (maxWindowPath) {
            correctionRate = -connectionAiRate /
                    (subflows.size() * maxWindowPaths);
        }
    }

    pendingCorrection = correctionRate * current->rtt;
    hasOliaState = std::isfinite(pendingCorrection);
    lastBestPath = bestPath;
    lastMaxWindowPath = maxWindowPath;
    lastPathPrice = current->pathPrice;
    lastPathOpportunity = current->pathOpportunity;
    lastNormalizedWindow = current->normalizedWindow;
}

} // namespace tcp
} // namespace inet
