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

struct Path {
    MpOrbOlia *algorithm;
    double window;
    double bottleneckFairRate;
    double resourceCost;
    double rtt;
};

bool nearlyEqual(double first, double second)
{
    const double scale = std::max({1.0, std::fabs(first), std::fabs(second)});
    return std::fabs(first - second) <=
            std::numeric_limits<double>::epsilon() * 32.0 * scale;
}

bool isBetterPath(const Path& candidate, const Path& currentBest)
{
    if (!nearlyEqual(candidate.bottleneckFairRate, currentBest.bottleneckFairRate))
        return candidate.bottleneckFairRate > currentBest.bottleneckFairRate;
    if (!nearlyEqual(candidate.resourceCost, currentBest.resourceCost))
        return candidate.resourceCost < currentBest.resourceCost;
    return candidate.rtt < currentBest.rtt;
}

bool isSameBestPath(const Path& candidate, const Path& best)
{
    return nearlyEqual(candidate.bottleneckFairRate, best.bottleneckFairRate) &&
            nearlyEqual(candidate.resourceCost, best.resourceCost) &&
            nearlyEqual(candidate.rtt, best.rtt);
}

} // namespace

bool MpOrbOlia::getPathQuality(double& bottleneckFairRate,
        double& resourceCost) const
{
    double inverseFairRate = 0.0;
    bottleneckFairRate = std::numeric_limits<double>::infinity();
    for (const auto& hop : pathHopMetrics) {
        if (!std::isfinite(hop.fairRate) || hop.fairRate <= 0.0)
            return false;
        bottleneckFairRate = std::min(bottleneckFairRate, hop.fairRate);
        inverseFairRate += 1.0 / hop.fairRate;
    }

    resourceCost = bottleneckFairRate * inverseFairRate;
    return std::isfinite(bottleneckFairRate) && bottleneckFairRate > 0.0 &&
            std::isfinite(resourceCost) && resourceCost > 0.0;
}

void MpOrbOlia::adjustAdditiveIncrease()
{
    // Alpha supplies OrbCC AI multiplied by this subflow's share of the
    // connection rate. With equal RTTs, this is w_r / sum(w).
    MpOrbSemiCoupledAlpha::adjustAdditiveIncrease();
    if (state == nullptr || firstRTT || state->initialPhase ||
            state->snd_mss == 0 || state->srtt <= SIMTIME_ZERO)
        return;

    MpTcpConnection *metaConnection = getMetaConnection();
    if (metaConnection == nullptr)
        return;

    std::vector<Path> paths;
    double sumWindows = 0.0;
    double maximumWindow = 0.0;
    Path bestPath = {};

    for (auto *subflow : metaConnection->getSubflows()) {
        if (subflow == nullptr)
            continue;

        const int tcpState = subflow->getFsmState();
        if (tcpState != TCP_S_ESTABLISHED && tcpState != TCP_S_CLOSE_WAIT)
            continue;

        auto *algorithm = dynamic_cast<MpOrbOlia *>(subflow->getTcpAlgorithm());
        const auto *subflowState = static_cast<const OrbtcpStateVariables *>(subflow->getState());
        if (algorithm == nullptr || subflowState == nullptr ||
                subflowState->snd_mss == 0 || subflowState->srtt <= SIMTIME_ZERO)
            continue;

        double bottleneckFairRate = 0.0;
        double resourceCost = 0.0;
        if (!algorithm->getPathQuality(bottleneckFairRate, resourceCost))
            return; // Wait until every active subflow has usable INT feedback.

        const double window = std::max(
                static_cast<double>(subflowState->snd_cwnd) / subflowState->snd_mss,
                1.0);
        const double rtt = subflowState->srtt.dbl();
        paths.push_back({algorithm, window, bottleneckFairRate, resourceCost, rtt});
        if (paths.size() == 1 || isBetterPath(paths.back(), bestPath))
            bestPath = paths.back();
        sumWindows += window;
        maximumWindow = std::max(maximumWindow, window);
    }

    if (paths.size() <= 1 || sumWindows <= 0.0)
        return;

    const Path *currentPath = nullptr;
    size_t maximumWindowPaths = 0;
    size_t collectedPaths = 0;
    for (const auto& path : paths) {
        const bool isBestPath = isSameBestPath(path, bestPath);
        const bool maximumWindowPath = nearlyEqual(path.window, maximumWindow);
        if (maximumWindowPath)
            maximumWindowPaths++;
        if (isBestPath && !maximumWindowPath)
            collectedPaths++;
        if (path.algorithm == this)
            currentPath = &path;
    }

    if (currentPath == nullptr)
        return;

    const bool isBestPath = isSameBestPath(*currentPath, bestPath);
    const bool maximumWindowPath = nearlyEqual(currentPath->window, maximumWindow);

    // This is OLIA's a_r definition with B supplied by INT opportunity:
    // C = B - M receives positive credit and M supplies the same total credit.
    double alphaR = 0.0;
    if (collectedPaths > 0) {
        if (isBestPath && !maximumWindowPath)
            alphaR = 1.0 / (paths.size() * collectedPaths);
        else if (maximumWindowPath)
            alphaR = -1.0 / (paths.size() * maximumWindowPaths);
    }

    // OLIA applies a_r / w_r per ACK. OrbCC commits one window update per RTT,
    // during which roughly w_r packets are acknowledged, so the equivalent
    // correction is a_r MSS bytes per RTT.
    const double correctionPerAck = alphaR / currentPath->window;
    const double correction = correctionPerAck * currentPath->window * state->snd_mss;
    const double adjustedIncrease = std::clamp(
            static_cast<double>(state->additiveIncrease) + correction,
            0.0,
            static_cast<double>(std::numeric_limits<uint32_t>::max()));
    state->additiveIncrease = static_cast<uint32_t>(adjustedIncrease);

    conn->emit(bestPathSignal, isBestPath ? 1L : 0L);
    conn->emit(maxWindowPathSignal, maximumWindowPath ? 1L : 0L);
    conn->emit(correctionSignal, correction);
    conn->emit(pathPriceSignal, currentPath->resourceCost);
    conn->emit(pathOpportunitySignal, currentPath->bottleneckFairRate);
    conn->emit(normalizedWindowSignal, currentPath->window / sumWindows);
}

} // namespace tcp
} // namespace inet
