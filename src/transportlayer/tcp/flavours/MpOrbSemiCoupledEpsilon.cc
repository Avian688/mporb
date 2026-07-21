//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//

#include "MpOrbSemiCoupledEpsilon.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include "../../../../../mptcp/src/transportlayer/tcp/MpTcpConnection.h"

namespace inet {
namespace tcp {

Register_Class(MpOrbSemiCoupledEpsilon);

simsignal_t MpOrbSemiCoupledEpsilon::pathCostSignal = cComponent::registerSignal("semiCoupledEpsilonPathCost");
simsignal_t MpOrbSemiCoupledEpsilon::desiredShareSignal = cComponent::registerSignal("semiCoupledEpsilonDesiredShare");
simsignal_t MpOrbSemiCoupledEpsilon::rateShareSignal = cComponent::registerSignal("semiCoupledEpsilonRateShare");
simsignal_t MpOrbSemiCoupledEpsilon::redistributionSignal = cComponent::registerSignal("semiCoupledEpsilonRedistribution");

namespace {

constexpr double PATH_COST_SHARPNESS = 4.0;

struct SubflowMetrics {
    MpOrbSemiCoupledEpsilon *algorithm;
    double rate;
    double rtt;
    double baseAiRate;
    double pathCost = 0.0;
    double preference = 0.0;
};

struct HopPriceAggregate {
    double total = 0.0;
    size_t samples = 0;
};

} // namespace

uint32_t MpOrbSemiCoupledEpsilon::computeWnd(double u, bool updateWc)
{
    uint32_t result = OrbtcpFlavour::computeWnd(u, updateWc);
    if (!hasAllocation)
        return result;

    const long double minimumWindow = std::max(state->snd_mss, 1U);
    const long double adjustedWindow = std::clamp(
            static_cast<long double>(result) + pendingRedistribution,
            minimumWindow,
            static_cast<long double>(std::numeric_limits<uint32_t>::max()));
    result = static_cast<uint32_t>(adjustedWindow);

    if (updateWc) {
        state->prevWnd = result;
        conn->emit(pathCostSignal, lastPathCost);
        conn->emit(desiredShareSignal, lastDesiredShare);
        conn->emit(rateShareSignal, lastRateShare);
        conn->emit(redistributionSignal, pendingRedistribution);
    }
    return result;
}

void MpOrbSemiCoupledEpsilon::updateHopPrices()
{
    if (state == nullptr || state->eta <= 0.0 || pathId.empty() || pathHopMetrics.empty())
        return;

    if (pricedPathId != pathId) {
        hopPrices.clear();
        pricedPathId = pathId;
    }

    for (const auto& hop : pathHopMetrics) {
        if (hop.hopId < 0 || hop.fairRate <= 0.0 || hop.averageRtt <= 0.0 ||
                hop.sampleInterval <= 0.0 || !std::isfinite(hop.utilization) ||
                !std::isfinite(hop.fairRate) || !std::isfinite(hop.sampleInterval) ||
                !std::isfinite(hop.averageRtt))
            continue;

        const double basePrice = 1.0 / hop.fairRate;
        auto insertion = hopPrices.emplace(hop.hopId, basePrice);
        double& price = insertion.first->second;
        const double gain = std::clamp(hop.sampleInterval / hop.averageRtt, 0.0, 1.0);
        const double loadError = hop.utilization / state->eta - 1.0;
        price = std::max(0.0, price + gain * basePrice * loadError);
    }
}

void MpOrbSemiCoupledEpsilon::adjustAdditiveIncrease()
{
    hasAllocation = false;
    pendingRedistribution = 0.0;
    if (state == nullptr)
        return;

    refreshDeliveryRate();
    updateHopPrices();
    if (firstRTT || state->initialPhase)
        return;

    MpTcpConnection *metaConnection = getMetaConnection();
    if (metaConnection == nullptr)
        return;

    std::vector<SubflowMetrics> subflows;
    std::map<int, HopPriceAggregate> connectionHopPrices;
    double connectionRate = 0.0;
    for (auto *subflow : metaConnection->getSubflows()) {
        if (subflow == nullptr)
            continue;

        const int tcpState = subflow->getFsmState();
        if (tcpState != TCP_S_ESTABLISHED && tcpState != TCP_S_CLOSE_WAIT)
            continue;

        auto *algorithm = dynamic_cast<MpOrbSemiCoupledEpsilon *>(subflow->getTcpAlgorithm());
        if (algorithm == nullptr)
            continue;

        const auto *subflowState = static_cast<const OrbtcpStateVariables *>(subflow->getState());
        if (subflowState == nullptr || algorithm->pathHopMetrics.empty() ||
                subflowState->bottBW <= 0.0 || algorithm->rtt <= SIMTIME_ZERO)
            continue;

        const double rate = getDeliveryRate(algorithm, subflowState);
        const double connectionCount = std::max(1.0,
                static_cast<double>(subflowState->sharingFlows));
        const double baseAiRate = subflowState->bottBW /
                connectionCount * subflowState->additiveIncreasePercent;
        if (!std::isfinite(rate) || rate < 0.0 ||
                !std::isfinite(baseAiRate) || baseAiRate <= 0.0)
            continue;

        subflows.push_back({algorithm, rate, algorithm->rtt.dbl(), baseAiRate});
        connectionRate += rate;
        for (const auto& hop : algorithm->pathHopMetrics) {
            auto price = algorithm->hopPrices.find(hop.hopId);
            const double value = price != algorithm->hopPrices.end() ?
                    price->second : 1.0 / hop.fairRate;
            if (!std::isfinite(value) || value < 0.0)
                continue;
            auto& aggregate = connectionHopPrices[hop.hopId];
            aggregate.total += value;
            aggregate.samples++;
        }
    }

    if (subflows.size() <= 1 || connectionRate <= 0.0 ||
            !std::isfinite(connectionRate))
        return;

    double minimumCost = std::numeric_limits<double>::infinity();
    double averageCost = 0.0;
    for (auto& subflow : subflows) {
        for (const auto& hop : subflow.algorithm->pathHopMetrics) {
            auto price = connectionHopPrices.find(hop.hopId);
            if (price == connectionHopPrices.end() || price->second.samples == 0)
                continue;
            subflow.pathCost += price->second.total / price->second.samples;
        }
        minimumCost = std::min(minimumCost, subflow.pathCost);
        averageCost += subflow.pathCost;
    }

    averageCost /= subflows.size();
    if (!std::isfinite(minimumCost) || !std::isfinite(averageCost) || averageCost < 0.0)
        return;
    const double costScale = averageCost > 0.0 ? averageCost : 1.0;

    double preferenceSum = 0.0;
    double aiRateBudget = 0.0;
    for (auto& subflow : subflows) {
        const double relativeCost = (subflow.pathCost - minimumCost) / costScale;
        subflow.preference = std::exp(-relativeCost);
        preferenceSum += subflow.preference;
        aiRateBudget += subflow.baseAiRate * (subflow.rate / connectionRate);
    }

    if (!std::isfinite(preferenceSum) || preferenceSum <= 0.0 ||
            !std::isfinite(aiRateBudget) || aiRateBudget <= 0.0)
        return;

    auto current = std::find_if(subflows.begin(), subflows.end(),
            [this](const SubflowMetrics& subflow) { return subflow.algorithm == this; });
    if (current == subflows.end())
        return;

    const double desiredShare = current->preference / preferenceSum;
    const double rateShare = current->rate / connectionRate;
    const double ai = aiRateBudget * desiredShare * current->rtt;
    if (!std::isfinite(ai) || ai < 0.0)
        return;

    const long double boundedAi = std::clamp(static_cast<long double>(ai), 1.0L,
            static_cast<long double>(std::numeric_limits<uint32_t>::max()));
    const double redistribution = state->additiveIncreasePercent * connectionRate *
            (desiredShare - rateShare) * current->rtt;
    if (!std::isfinite(redistribution))
        return;

    state->additiveIncrease = static_cast<uint32_t>(boundedAi);
    pendingRedistribution = redistribution;
    hasAllocation = true;
    lastPathCost = current->pathCost;
    lastDesiredShare = desiredShare;
    lastRateShare = rateShare;
}

} // namespace tcp
} // namespace inet
