//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//

#include "MpOrbSemiCoupledOmega.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>

#include "../../../../../mptcp/src/transportlayer/tcp/MpTcpConnection.h"

namespace inet {
namespace tcp {

Register_Class(MpOrbSemiCoupledOmega);

simsignal_t MpOrbSemiCoupledOmega::marginalValueSignal = cComponent::registerSignal("semiCoupledMarginalValue");
simsignal_t MpOrbSemiCoupledOmega::pathPriceSignal = cComponent::registerSignal("semiCoupledPathPrice");
simsignal_t MpOrbSemiCoupledOmega::windowDeltaSignal = cComponent::registerSignal("semiCoupledWindowDelta");

namespace {

struct BottleneckGroup
{
    double bandwidth = 0.0;
    double priceSum = 0.0;
    double additiveIncreasePercentSum = 0.0;
    size_t members = 0;
};

} // namespace

double MpOrbSemiCoupledOmega::getDeliveryRate(const MpOrbSemiCoupledOmega *algorithm,
        const OrbtcpStateVariables *subflowState) const
{
    if (algorithm->deliveryRateUpdatedAt != SIMTIME_ZERO) {
        const double age = (simTime() - algorithm->deliveryRateUpdatedAt).dbl();
        if (age > 2.0 * subflowState->srtt.dbl())
            return 0.0;
    }

    if (algorithm->smoothedDeliveryRate > 0.0 && std::isfinite(algorithm->smoothedDeliveryRate))
        return algorithm->smoothedDeliveryRate;

    const double rate = subflowState->snd_cwnd / subflowState->srtt.dbl();
    return std::isfinite(rate) && rate > 0.0 ? rate : 0.0;
}

void MpOrbSemiCoupledOmega::updatePathPrice()
{
    const int currentBottleneckId = getBottleneckId();
    if (currentBottleneckId < 0 || state == nullptr || state->bottBW == 0 ||
            !std::isfinite(state->u))
        return;

    if (pricedBottleneckId != currentBottleneckId) {
        pricedBottleneckId = currentBottleneckId;
        pathPrice = 0.0;
    }

    // updateWindow is raised once per RTT. The projected integral retains
    // bottleneck scarcity even while OrbCC holds queues close to empty.
    if (updateWindow) {
        pathPrice += (state->u - state->eta) / state->bottBW;
        pathPrice = std::max(0.0, pathPrice);
    }
}

void MpOrbSemiCoupledOmega::adjustAdditiveIncrease()
{
    refreshTelemetry();
    updatePathPrice();
    hasAllocation = false;
    hasOmegaAdjustment = false;
    pendingWindowDelta = 0.0;

    if (firstRTT || state == nullptr || state->initialPhase || state->srtt <= SIMTIME_ZERO)
        return;

    MpTcpConnection *metaConnection = getMetaConnection();
    if (metaConnection == nullptr)
        return;

    std::map<int, BottleneckGroup> groups;
    double connectionRate = 0.0;
    double currentDeliveryRate = 0.0;
    int currentGroupKey = 0;
    bool currentSubflowFound = false;

    for (auto *subflow : metaConnection->getSubflows()) {
        if (subflow == nullptr)
            continue;

        const int tcpState = subflow->getFsmState();
        if (tcpState != TCP_S_ESTABLISHED && tcpState != TCP_S_CLOSE_WAIT)
            continue;

        auto *algorithm = dynamic_cast<MpOrbSemiCoupledOmega *>(subflow->getTcpAlgorithm());
        if (algorithm == nullptr)
            continue;

        const auto *subflowState = static_cast<const OrbtcpStateVariables *>(subflow->getState());
        if (subflowState == nullptr || subflowState->bottBW == 0 ||
                subflowState->srtt <= SIMTIME_ZERO || !std::isfinite(subflowState->u))
            continue;

        const double deliveryRate = getDeliveryRate(algorithm, subflowState);
        connectionRate += deliveryRate;

        int groupKey = algorithm->getBottleneckId();
        if (groupKey < 0)
            groupKey = -subflow->getId() - 1;

        auto& group = groups[groupKey];
        group.bandwidth = group.members == 0 ? subflowState->bottBW :
                std::min(group.bandwidth, static_cast<double>(subflowState->bottBW));
        group.priceSum += algorithm->pathPrice;
        group.additiveIncreasePercentSum += subflowState->additiveIncreasePercent;
        group.members++;

        if (algorithm == this) {
            currentSubflowFound = true;
            currentDeliveryRate = deliveryRate;
            currentGroupKey = groupKey;
        }
    }

    if (!currentSubflowFound || groups.empty() || connectionRate <= 0.0 ||
            !std::isfinite(connectionRate))
        return;

    double weightedPriceSum = 0.0;
    double gainSum = 0.0;
    for (const auto& entry : groups) {
        const auto& group = entry.second;
        const double groupPrice = group.priceSum / group.members;
        const double groupAiPercent = group.additiveIncreasePercentSum / group.members;
        const double groupGain = groups.size() * groupAiPercent * group.bandwidth * group.bandwidth;
        weightedPriceSum += groupGain * groupPrice;
        gainSum += groupGain;
    }

    auto currentGroupIt = groups.find(currentGroupKey);
    if (currentGroupIt == groups.end() || gainSum <= 0.0 || !std::isfinite(gainSum))
        return;

    const auto& currentGroup = currentGroupIt->second;
    const double averagePrice = weightedPriceSum / gainSum;
    const double currentGroupPrice = currentGroup.priceSum / currentGroup.members;
    const double currentGroupAiPercent = currentGroup.additiveIncreasePercentSum / currentGroup.members;
    const double currentGroupGain = groups.size() * currentGroupAiPercent *
            currentGroup.bandwidth * currentGroup.bandwidth;
    const double marginalValue = 1.0 / connectionRate;

    // The growth term controls aggregate connection aggressiveness. Centering
    // the path-price term makes the shift exactly zero-sum across groups.
    const double growthRate = currentGroupGain * (marginalValue - averagePrice);
    const double shiftRate = currentGroupGain * (averagePrice - currentGroupPrice);
    const double rateDelta = (growthRate + shiftRate) / currentGroup.members;
    const double windowDelta = rateDelta * state->srtt.dbl();
    if (!std::isfinite(windowDelta))
        return;

    if (windowDelta > 0.0) {
        const double boundedDelta = std::min(windowDelta,
                static_cast<double>(std::numeric_limits<uint32_t>::max()));
        state->additiveIncrease = boundedDelta;
        if (state->additiveIncrease == 0)
            state->additiveIncrease = 1;
    }
    else {
        state->additiveIncrease = 0;
    }

    pendingWindowDelta = windowDelta;
    hasAllocation = true;
    hasOmegaAdjustment = true;
    lastConnectionRate = connectionRate;
    lastDeliveryRate = currentDeliveryRate;
    lastConnectionCount = std::max(1.0, static_cast<double>(state->sharingFlows));
    lastFairRate = state->eta * state->bottBW / lastConnectionCount;
    lastRelativeOpportunity = 1.0 / (1.0 + currentGroup.bandwidth * currentGroupPrice);
    lastUtilizationSafety = state->eta / std::max(state->eta, state->u);
    lastPathWeight = marginalValue - currentGroupPrice;
    lastAiRateBudget = growthRate / currentGroup.members;
    lastAdjustedAi = state->additiveIncrease;
    lastMarginalValue = marginalValue;
    lastPathPrice = currentGroupPrice;
}

uint32_t MpOrbSemiCoupledOmega::computeWnd(double u, bool updateWc)
{
    uint32_t result = MpOrbSemiCoupled::computeWnd(u, updateWc);
    double appliedWindowDelta = hasOmegaAdjustment && pendingWindowDelta > 0.0 ?
            state->additiveIncrease : 0.0;

    // OrbCC's utilization decrease takes priority while the path is overloaded.
    // A negative utility correction is applied once utilization is below target.
    if (hasOmegaAdjustment && pendingWindowDelta < 0.0 && u < state->eta) {
        const double reducedWindow = static_cast<double>(result) + pendingWindowDelta;
        const uint32_t minimumWindow = std::max(1U, state->snd_mss);
        result = reducedWindow > minimumWindow ? static_cast<uint32_t>(reducedWindow) : minimumWindow;
        appliedWindowDelta = static_cast<double>(result) - state->prevWnd;
        if (updateWc)
            state->prevWnd = result;
    }

    lastWindowDelta = appliedWindowDelta;
    if (updateWc && hasOmegaAdjustment) {
        conn->emit(marginalValueSignal, lastMarginalValue);
        conn->emit(pathPriceSignal, lastPathPrice);
        conn->emit(windowDeltaSignal, lastWindowDelta);
    }

    return result;
}

} // namespace tcp
} // namespace inet
