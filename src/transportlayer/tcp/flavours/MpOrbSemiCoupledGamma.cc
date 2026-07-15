//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//

#include "MpOrbSemiCoupledGamma.h"

#include <algorithm>
#include <cmath>

#include "../../../../../mptcp/src/transportlayer/tcp/MpTcpConnection.h"

namespace inet {
namespace tcp {

Register_Class(MpOrbSemiCoupledGamma);

simsignal_t MpOrbSemiCoupledGamma::targetShareSignal = cComponent::registerSignal("semiCoupledGammaTargetShare");
simsignal_t MpOrbSemiCoupledGamma::rateShareSignal = cComponent::registerSignal("semiCoupledGammaRateShare");
simsignal_t MpOrbSemiCoupledGamma::responsivenessSignal = cComponent::registerSignal("semiCoupledGammaResponsiveness");
simsignal_t MpOrbSemiCoupledGamma::aiShareSignal = cComponent::registerSignal("semiCoupledGammaAiShare");

uint32_t MpOrbSemiCoupledGamma::computeWnd(double u, bool updateWc)
{
    const uint32_t result = MpOrbSemiCoupled::computeWnd(u, updateWc);
    if (updateWc && hasAllocation) {
        conn->emit(targetShareSignal, lastTargetShare);
        conn->emit(rateShareSignal, lastRateShare);
        conn->emit(responsivenessSignal, lastResponsiveness);
        conn->emit(aiShareSignal, lastAiShare);
    }
    return result;
}

void MpOrbSemiCoupledGamma::adjustAdditiveIncrease()
{
    refreshTelemetry();
    hasAllocation = false;
    if (firstRTT || state == nullptr || state->initialPhase)
        return;

    MpTcpConnection *metaConnection = getMetaConnection();
    if (metaConnection == nullptr)
        return;

    double connectionRate = 0.0;
    double targetWeightSum = 0.0;
    double aiRateBudget = 0.0;
    double maximumFairRate = 0.0;
    size_t validSubflows = 0;

    for (auto *subflow : metaConnection->getSubflows()) {
        if (subflow == nullptr)
            continue;

        const int tcpState = subflow->getFsmState();
        if (tcpState != TCP_S_ESTABLISHED && tcpState != TCP_S_CLOSE_WAIT)
            continue;

        auto *algorithm = dynamic_cast<MpOrbSemiCoupledGamma *>(subflow->getTcpAlgorithm());
        if (algorithm == nullptr)
            continue;

        const auto *subflowState = static_cast<const OrbtcpStateVariables *>(subflow->getState());
        if (subflowState == nullptr || algorithm->observedPathId.empty() ||
                subflowState->bottBW <= 0.0 || subflowState->srtt <= SIMTIME_ZERO ||
                !std::isfinite(subflowState->u))
            continue;

        double rate = subflowState->snd_cwnd / subflowState->srtt.dbl();
        double connectionCount = std::max(1.0, static_cast<double>(subflowState->sharingFlows));
        double fairRate = subflowState->eta * subflowState->bottBW / connectionCount;
        double utilizationSafety = subflowState->eta /
                std::max(subflowState->eta, subflowState->u);
        double targetWeight = fairRate * fairRate * utilizationSafety;

        if (!std::isfinite(rate) || rate < 0.0 || !std::isfinite(fairRate) ||
                fairRate <= 0.0 || !std::isfinite(targetWeight) || targetWeight <= 0.0)
            continue;

        connectionRate += rate;
        targetWeightSum += targetWeight;
        aiRateBudget = std::max(aiRateBudget,
                subflowState->bottBW * subflowState->additiveIncreasePercent);
        maximumFairRate = std::max(maximumFairRate, fairRate);
        validSubflows++;
    }

    if (validSubflows <= 1 || targetWeightSum <= 0.0 || aiRateBudget <= 0.0 ||
            maximumFairRate <= 0.0 || !std::isfinite(connectionRate) ||
            !std::isfinite(targetWeightSum))
        return;

    double responsiveWeightSum = 0.0;
    bool currentSubflowFound = false;
    double currentRate = 0.0;
    double currentConnectionCount = 1.0;
    double currentFairRate = 0.0;
    double currentUtilizationSafety = 0.0;
    double currentTargetShare = 0.0;
    double currentRateShare = 0.0;
    double currentResponsiveness = 0.0;
    double currentResponsiveWeight = 0.0;
    double currentSrtt = 0.0;

    for (auto *subflow : metaConnection->getSubflows()) {
        if (subflow == nullptr)
            continue;

        const int tcpState = subflow->getFsmState();
        if (tcpState != TCP_S_ESTABLISHED && tcpState != TCP_S_CLOSE_WAIT)
            continue;

        auto *algorithm = dynamic_cast<MpOrbSemiCoupledGamma *>(subflow->getTcpAlgorithm());
        if (algorithm == nullptr)
            continue;

        const auto *subflowState = static_cast<const OrbtcpStateVariables *>(subflow->getState());
        if (subflowState == nullptr || algorithm->observedPathId.empty() ||
                subflowState->bottBW <= 0.0 || subflowState->srtt <= SIMTIME_ZERO ||
                !std::isfinite(subflowState->u))
            continue;

        double rate = subflowState->snd_cwnd / subflowState->srtt.dbl();
        double connectionCount = std::max(1.0, static_cast<double>(subflowState->sharingFlows));
        double fairRate = subflowState->eta * subflowState->bottBW / connectionCount;
        double utilizationSafety = subflowState->eta /
                std::max(subflowState->eta, subflowState->u);
        double targetWeight = fairRate * fairRate * utilizationSafety;

        if (!std::isfinite(rate) || rate < 0.0 || !std::isfinite(fairRate) ||
                fairRate <= 0.0 || !std::isfinite(targetWeight) || targetWeight <= 0.0)
            continue;

        double targetShare = targetWeight / targetWeightSum;
        double rateShare = connectionRate > 0.0 ? rate / connectionRate : 0.0;
        double responsiveness = targetShare / (targetShare + rateShare);
        double responsiveWeight = targetShare * responsiveness;
        responsiveWeightSum += responsiveWeight;

        if (algorithm == this) {
            currentSubflowFound = true;
            currentRate = rate;
            currentConnectionCount = connectionCount;
            currentFairRate = fairRate;
            currentUtilizationSafety = utilizationSafety;
            currentTargetShare = targetShare;
            currentRateShare = rateShare;
            currentResponsiveness = responsiveness;
            currentResponsiveWeight = responsiveWeight;
            currentSrtt = subflowState->srtt.dbl();
        }
    }

    if (!currentSubflowFound || responsiveWeightSum <= 0.0 ||
            currentResponsiveWeight <= 0.0 || !std::isfinite(responsiveWeightSum))
        return;

    double aiShare = currentResponsiveWeight / responsiveWeightSum;
    double ai = aiRateBudget * aiShare * currentSrtt;
    if (!std::isfinite(ai) || ai <= 0.0)
        return;

    uint32_t adjustedAi = ai;
    if (adjustedAi == 0)
        adjustedAi = 1;

    state->additiveIncrease = adjustedAi;
    hasAllocation = true;
    lastConnectionRate = connectionRate;
    lastDeliveryRate = smoothedDeliveryRate > 0.0 ? smoothedDeliveryRate : currentRate;
    lastConnectionCount = currentConnectionCount;
    lastFairRate = currentFairRate;
    lastRelativeOpportunity = currentFairRate / maximumFairRate;
    lastUtilizationSafety = currentUtilizationSafety;
    lastPathWeight = lastRelativeOpportunity * lastRelativeOpportunity *
            currentUtilizationSafety;
    lastAiRateBudget = aiRateBudget;
    lastAdjustedAi = adjustedAi;
    lastTargetShare = currentTargetShare;
    lastRateShare = currentRateShare;
    lastResponsiveness = currentResponsiveness;
    lastAiShare = aiShare;
}

} // namespace tcp
} // namespace inet
