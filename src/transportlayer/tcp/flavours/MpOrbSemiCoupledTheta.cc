//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//

#include "MpOrbSemiCoupledTheta.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "../../../../../mptcp/src/transportlayer/tcp/MpTcpConnection.h"

namespace inet {
namespace tcp {

Register_Class(MpOrbSemiCoupledTheta);

simsignal_t MpOrbSemiCoupledTheta::fairRateSignal = cComponent::registerSignal("semiCoupledThetaFairRate");
simsignal_t MpOrbSemiCoupledTheta::headroomRateSignal = cComponent::registerSignal("semiCoupledThetaHeadroomRate");
simsignal_t MpOrbSemiCoupledTheta::aiShareSignal = cComponent::registerSignal("semiCoupledThetaAiShare");
simsignal_t MpOrbSemiCoupledTheta::connectionAiRateSignal = cComponent::registerSignal("semiCoupledThetaConnectionAiRate");

void MpOrbSemiCoupledTheta::adjustAdditiveIncrease()
{
    if (state == nullptr)
        return;

    if (!pathHopMetrics.empty() && bottleneckId >= 0)
        telemetryUpdatedAt = simTime();

    // Keep OrbCC's startup unchanged and couple only its steady-state AI.
    if (firstRTT || state->initialPhase || state->srtt <= SIMTIME_ZERO)
        return;

    MpTcpConnection *metaConnection = getMetaConnection();
    if (metaConnection == nullptr)
        return;

    double connectionRate = 0.0;
    double currentRate = 0.0;
    size_t activeSubflows = 0;
    for (auto *subflow : metaConnection->getSubflows()) {
        if (subflow == nullptr)
            continue;

        const int tcpState = subflow->getFsmState();
        if (tcpState != TCP_S_ESTABLISHED && tcpState != TCP_S_CLOSE_WAIT)
            continue;

        auto *algorithm = dynamic_cast<MpOrbSemiCoupledTheta *>(subflow->getTcpAlgorithm());
        const auto *subflowState = static_cast<const OrbtcpStateVariables *>(subflow->getState());
        if (algorithm == nullptr || subflowState == nullptr ||
                subflowState->srtt <= SIMTIME_ZERO)
            return;

        const double rate = subflowState->snd_cwnd / subflowState->srtt.dbl();
        if (!std::isfinite(rate) || rate <= 0.0)
            return;

        connectionRate += rate;
        activeSubflows++;
        if (algorithm == this)
            currentRate = rate;
    }

    if (activeSubflows <= 1 || !std::isfinite(connectionRate) ||
            connectionRate <= 0.0 || currentRate <= 0.0)
        return;

    // Alpha is the safe fallback whenever the connection lacks a complete,
    // recent INT view. Theta only redistributes this same growth budget.
    const double alphaRateShare = currentRate / connectionRate;
    const uint32_t uncoupledAi = state->additiveIncrease;
    if (uncoupledAi > 0) {
        state->additiveIncrease = uncoupledAi * alphaRateShare;
        if (state->additiveIncrease == 0)
            state->additiveIncrease = 1;
    }

    double alphaAiRateNumerator = 0.0;
    double opportunityWeightSum = 0.0;
    double currentFairRate = 0.0;
    double currentHeadroomRate = 0.0;
    double currentOpportunityWeight = 0.0;
    double currentRtt = 0.0;

    for (auto *subflow : metaConnection->getSubflows()) {
        if (subflow == nullptr)
            continue;

        const int tcpState = subflow->getFsmState();
        if (tcpState != TCP_S_ESTABLISHED && tcpState != TCP_S_CLOSE_WAIT)
            continue;

        auto *algorithm = dynamic_cast<MpOrbSemiCoupledTheta *>(subflow->getTcpAlgorithm());
        const auto *subflowState = static_cast<const OrbtcpStateVariables *>(subflow->getState());
        if (algorithm == nullptr || subflowState == nullptr ||
                algorithm->telemetryUpdatedAt == SIMTIME_ZERO ||
                subflowState->srtt <= SIMTIME_ZERO || algorithm->rtt <= SIMTIME_ZERO ||
                algorithm->pathHopMetrics.empty() || algorithm->bottleneckId < 0 ||
                simTime() - algorithm->telemetryUpdatedAt > subflowState->srtt * 2 ||
                !std::isfinite(subflowState->u) || !std::isfinite(subflowState->eta) ||
                subflowState->eta <= 0.0 || !std::isfinite(subflowState->bottBW) ||
                subflowState->bottBW <= 0.0 ||
                !std::isfinite(subflowState->additiveIncreasePercent) ||
                subflowState->additiveIncreasePercent <= 0.0)
            return;

        const double rate = subflowState->snd_cwnd / subflowState->srtt.dbl();
        const double connectionCount = std::max(1.0,
                static_cast<double>(subflowState->sharingFlows));
        const double fairRate = subflowState->eta * subflowState->bottBW /
                connectionCount;
        const double uncoupledAiRate = subflowState->bottBW / connectionCount *
                subflowState->additiveIncreasePercent;
        const double headroomFraction = std::clamp(
                1.0 - subflowState->u / subflowState->eta, 0.0, 1.0);
        const double headroomRate = fairRate * headroomFraction;
        const double opportunityWeight = uncoupledAiRate * (rate + headroomRate);

        if (!std::isfinite(rate) || rate <= 0.0 || !std::isfinite(fairRate) ||
                fairRate <= 0.0 || !std::isfinite(uncoupledAiRate) ||
                uncoupledAiRate <= 0.0 || !std::isfinite(opportunityWeight) ||
                opportunityWeight <= 0.0)
            return;

        alphaAiRateNumerator += uncoupledAiRate * rate;
        opportunityWeightSum += opportunityWeight;
        if (algorithm == this) {
            currentFairRate = fairRate;
            currentHeadroomRate = headroomRate;
            currentOpportunityWeight = opportunityWeight;
            currentRtt = algorithm->rtt.dbl();
        }
    }

    if (!std::isfinite(alphaAiRateNumerator) || alphaAiRateNumerator <= 0.0 ||
            !std::isfinite(opportunityWeightSum) || opportunityWeightSum <= 0.0 ||
            currentOpportunityWeight <= 0.0 || currentRtt <= 0.0)
        return;

    const double connectionAiRate = alphaAiRateNumerator / connectionRate;
    const double aiShare = currentOpportunityWeight / opportunityWeightSum;
    const double additiveIncrease = connectionAiRate * aiShare * currentRtt;
    if (!std::isfinite(additiveIncrease) || additiveIncrease <= 0.0)
        return;

    const double maximumAi = std::numeric_limits<uint32_t>::max();
    state->additiveIncrease = static_cast<uint32_t>(std::min(additiveIncrease, maximumAi));
    if (state->additiveIncrease == 0)
        state->additiveIncrease = 1;

    conn->emit(fairRateSignal, currentFairRate);
    conn->emit(headroomRateSignal, currentHeadroomRate);
    conn->emit(aiShareSignal, aiShare);
    conn->emit(connectionAiRateSignal, connectionAiRate);
}

} // namespace tcp
} // namespace inet
