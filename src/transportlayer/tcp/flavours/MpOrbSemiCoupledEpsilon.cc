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

simsignal_t MpOrbSemiCoupledEpsilon::pathCostSignal =
        cComponent::registerSignal("semiCoupledEpsilonPathCost");
simsignal_t MpOrbSemiCoupledEpsilon::desiredShareSignal =
        cComponent::registerSignal("semiCoupledEpsilonDesiredShare");
simsignal_t MpOrbSemiCoupledEpsilon::rateShareSignal =
        cComponent::registerSignal("semiCoupledEpsilonRateShare");
simsignal_t MpOrbSemiCoupledEpsilon::redistributionSignal =
        cComponent::registerSignal("semiCoupledEpsilonRedistribution");

namespace {

struct SubflowMetrics {
    MpOrbSemiCoupledEpsilon *algorithm;
    double rate;
    double rtt;
    double fairRate;
    double uncoupledAiRate;
    double bottleneckPrice;
    double demandGain = 0.0;
    double targetShare = 0.0;
    double rateShare = 0.0;
    double aiWeight = 0.0;
};

} // namespace

void MpOrbSemiCoupledEpsilon::updateBottleneckPrice()
{
    if (state == nullptr || bottleneckId < 0 ||
            !std::isfinite(state->u) ||
            !std::isfinite(state->eta) || state->eta <= 0.0 ||
            !std::isfinite(state->alpha) ||
            !std::isfinite(state->bottBW) || state->bottBW <= 0.0)
        return;

    if (pricedBottleneckId != bottleneckId) {
        pricedBottleneckId = bottleneckId;
        bottleneckPrice = 0.0;
    }

    const double gain = std::clamp(state->alpha, 0.0, 1.0);
    const double priceScale = 1.0 / (state->eta * state->bottBW);
    const double loadError = state->u / state->eta - 1.0;
    // p_b <- [p_b + gain / (eta * B_b) * (U_b / eta - 1)]+
    const double updatedPrice =
            bottleneckPrice + gain * priceScale * loadError;
    if (std::isfinite(updatedPrice))
        bottleneckPrice = std::max(0.0, updatedPrice);
}

void MpOrbSemiCoupledEpsilon::adjustAdditiveIncrease()
{
    if (state == nullptr)
        return;

    refreshDeliveryRate();
    if (!pathHopMetrics.empty() && bottleneckId >= 0) {
        telemetryUpdatedAt = simTime();
        updateBottleneckPrice();
    }

    // Learn scarcity during startup, but leave OrbCC's startup window unchanged.
    if (firstRTT || state->initialPhase)
        return;

    MpTcpConnection *metaConnection = getMetaConnection();
    if (metaConnection == nullptr)
        return;

    std::vector<SubflowMetrics> subflows;
    double connectionRate = 0.0;
    double totalFairRate = 0.0;
    double aiRateNumerator = 0.0;

    for (auto *subflow : metaConnection->getSubflows()) {
        if (subflow == nullptr)
            continue;

        const int tcpState = subflow->getFsmState();
        if (tcpState != TCP_S_ESTABLISHED && tcpState != TCP_S_CLOSE_WAIT)
            continue;

        auto *algorithm = dynamic_cast<MpOrbSemiCoupledEpsilon *>(
                subflow->getTcpAlgorithm());
        const auto *subflowState =
                static_cast<const OrbtcpStateVariables *>(subflow->getState());

        // Do not calculate connection shares from a partial INT view.
        if (algorithm == nullptr || subflowState == nullptr ||
                algorithm->firstRTT || subflowState->initialPhase ||
                subflowState->srtt <= SIMTIME_ZERO ||
                algorithm->telemetryUpdatedAt == SIMTIME_ZERO ||
                simTime() - algorithm->telemetryUpdatedAt > subflowState->srtt * 2 ||
                algorithm->pathHopMetrics.empty() || algorithm->bottleneckId < 0 ||
                !std::isfinite(algorithm->bottleneckPrice) ||
                algorithm->bottleneckPrice < 0.0 ||
                !std::isfinite(subflowState->eta) || subflowState->eta <= 0.0 ||
                !std::isfinite(subflowState->bottBW) || subflowState->bottBW <= 0.0 ||
                !std::isfinite(subflowState->additiveIncreasePercent) ||
                subflowState->additiveIncreasePercent <= 0.0)
            return;

        const double rtt = subflowState->srtt.dbl();
        const double rate = getDeliveryRate(algorithm, subflowState);
        const double connectionCount = std::max(
                1.0, static_cast<double>(subflowState->sharingFlows));
        const double fairRate =
                subflowState->eta * subflowState->bottBW / connectionCount;
        const double uncoupledAiRate =
                subflowState->bottBW / connectionCount *
                subflowState->additiveIncreasePercent;

        if (!std::isfinite(rate) || rate < 0.0 ||
                !std::isfinite(fairRate) || fairRate <= 0.0 ||
                !std::isfinite(uncoupledAiRate) || uncoupledAiRate <= 0.0)
            return;

        subflows.push_back(
                {algorithm, rate, rtt, fairRate, uncoupledAiRate,
                        algorithm->bottleneckPrice});
        connectionRate += rate;
        totalFairRate += fairRate;
        aiRateNumerator += uncoupledAiRate * rate;
    }

    if (subflows.size() <= 1 ||
            !std::isfinite(connectionRate) || connectionRate <= 0.0 ||
            !std::isfinite(totalFairRate) || totalFairRate <= 0.0)
        return;

    double targetScoreSum = 0.0;
    for (auto& subflow : subflows) {
        // This is the normalized positive part of 1/R - price, where
        // 1/R is the marginal utility of log(connectionRate).
        subflow.demandGain = std::max(
                0.0, 1.0 - subflow.bottleneckPrice * connectionRate);
        const double targetScore = subflow.fairRate * subflow.demandGain;
        targetScoreSum += targetScore;
    }

    double totalAiWeight = 0.0;
    for (auto& subflow : subflows) {
        if (targetScoreSum > 0.0)
            subflow.targetShare =
                    subflow.fairRate * subflow.demandGain / targetScoreSum;
        else
            subflow.targetShare = subflow.fairRate / totalFairRate;

        subflow.rateShare = subflow.rate / connectionRate;
        // Correct a path deficit without directly moving or capping its window.
        subflow.aiWeight = std::max(
                0.0, 2.0 * subflow.targetShare - subflow.rateShare);
        totalAiWeight += subflow.aiWeight;
    }

    // Demand scales the connection-wide AI budget outside the path normalization.
    const double baseConnectionAiRate = aiRateNumerator / connectionRate;
    const double connectionDemandGain =
            targetScoreSum / totalFairRate;
    const double connectionAiRate =
            baseConnectionAiRate * connectionDemandGain;
    if (!std::isfinite(totalAiWeight) || totalAiWeight <= 0.0 ||
            !std::isfinite(connectionDemandGain) ||
            connectionDemandGain < 0.0 ||
            !std::isfinite(connectionAiRate) || connectionAiRate < 0.0)
        return;

    auto current = std::find_if(subflows.begin(), subflows.end(),
            [this](const SubflowMetrics& subflow) {
                return subflow.algorithm == this;
            });
    if (current == subflows.end())
        return;

    const double aiShare = current->aiWeight / totalAiWeight;
    const double additiveIncrease =
            connectionAiRate * aiShare * current->rtt;
    if (!std::isfinite(aiShare) || aiShare < 0.0 ||
            !std::isfinite(additiveIncrease) || additiveIncrease < 0.0)
        return;

    state->additiveIncrease = static_cast<uint32_t>(std::min(
            additiveIncrease,
            static_cast<double>(std::numeric_limits<uint32_t>::max())));
    if (state->additiveIncrease == 0)
        state->additiveIncrease = 1;

    conn->emit(pathCostSignal, current->bottleneckPrice * connectionRate);
    conn->emit(desiredShareSignal, current->targetShare);
    conn->emit(rateShareSignal, current->rateShare);
    conn->emit(redistributionSignal, aiShare - current->rateShare);
}

} // namespace tcp
} // namespace inet
