//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//

#include "MpOrbSemiCoupledEpsilon.h"

#include <algorithm>
#include <cmath>

#include "../../../../../mptcp/src/transportlayer/tcp/MpTcpConnection.h"

namespace inet {
namespace tcp {

Register_Class(MpOrbSemiCoupledEpsilon);

simsignal_t MpOrbSemiCoupledEpsilon::pathPriceSignal = cComponent::registerSignal("semiCoupledEpsilonPathPrice");
simsignal_t MpOrbSemiCoupledEpsilon::opportunitySignal = cComponent::registerSignal("semiCoupledEpsilonOpportunity");
simsignal_t MpOrbSemiCoupledEpsilon::targetShareSignal = cComponent::registerSignal("semiCoupledEpsilonTargetShare");
simsignal_t MpOrbSemiCoupledEpsilon::rateShareSignal = cComponent::registerSignal("semiCoupledEpsilonRateShare");
simsignal_t MpOrbSemiCoupledEpsilon::responsivenessSignal = cComponent::registerSignal("semiCoupledEpsilonResponsiveness");
simsignal_t MpOrbSemiCoupledEpsilon::aiShareSignal = cComponent::registerSignal("semiCoupledEpsilonAiShare");
simsignal_t MpOrbSemiCoupledEpsilon::aiRateBudgetSignal = cComponent::registerSignal("semiCoupledEpsilonAiRateBudget");
simsignal_t MpOrbSemiCoupledEpsilon::adjustedAiSignal = cComponent::registerSignal("semiCoupledEpsilonAdjustedAi");

uint32_t MpOrbSemiCoupledEpsilon::computeWnd(double u, bool updateWc)
{
    const uint32_t result = OrbtcpFlavour::computeWnd(u, updateWc);
    if (updateWc && hasAllocation) {
        conn->emit(pathPriceSignal, lastPathPrice);
        conn->emit(opportunitySignal, lastOpportunity);
        conn->emit(targetShareSignal, lastTargetShare);
        conn->emit(rateShareSignal, lastRateShare);
        conn->emit(responsivenessSignal, lastResponsiveness);
        conn->emit(aiShareSignal, lastAiShare);
        conn->emit(aiRateBudgetSignal, lastAiRateBudget);
        conn->emit(adjustedAiSignal, static_cast<double>(lastAdjustedAi));
    }
    return result;
}

void MpOrbSemiCoupledEpsilon::adjustAdditiveIncrease()
{
    refreshDeliveryRate();
    hasAllocation = false;
    if (firstRTT || state == nullptr || state->initialPhase)
        return;

    MpTcpConnection *metaConnection = getMetaConnection();
    if (metaConnection == nullptr)
        return;

    double connectionRate = 0.0;
    double targetWeightSum = 0.0;
    size_t validSubflows = 0;

    // A path's opportunity is the inverse of the summed scarcity price of
    // every INT-reporting hop it consumes.
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
        const double pathPrice = algorithm->getPathPrice();
        if (subflowState == nullptr || algorithm->observedPathId.empty() ||
                subflowState->srtt <= SIMTIME_ZERO || algorithm->rtt <= SIMTIME_ZERO ||
                !std::isfinite(subflowState->u) ||
                !std::isfinite(pathPrice) || pathPrice <= 0.0)
            continue;

        const double rate = getDeliveryRate(algorithm, subflowState);
        const double opportunity = 1.0 / pathPrice;
        const double utilizationSafety = subflowState->eta /
                std::max(subflowState->eta, subflowState->u);
        // On one hop opportunity equals fairRate, reproducing Delta's target.
        const double targetWeight = opportunity * opportunity * utilizationSafety;
        if (!std::isfinite(rate) || rate < 0.0 ||
                !std::isfinite(targetWeight) || targetWeight <= 0.0)
            continue;

        connectionRate += rate;
        targetWeightSum += targetWeight;
        validSubflows++;
    }

    if (validSubflows <= 1 || connectionRate <= 0.0 || targetWeightSum <= 0.0 ||
            !std::isfinite(connectionRate) || !std::isfinite(targetWeightSum))
        return;

    double alphaAiRateBudget = 0.0;
    double responsiveWeightSum = 0.0;
    bool currentSubflowFound = false;
    double currentPathPrice = 0.0;
    double currentOpportunity = 0.0;
    double currentTargetShare = 0.0;
    double currentRateShare = 0.0;
    double currentResponsiveness = 0.0;
    double currentResponsiveWeight = 0.0;
    double currentRtt = 0.0;

    // Retain Alpha's aggregate AI-rate budget, then redistribute it toward
    // under-served subflows with low total path price.
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
        const double pathPrice = algorithm->getPathPrice();
        if (subflowState == nullptr || algorithm->observedPathId.empty() ||
                subflowState->bottBW <= 0.0 || subflowState->srtt <= SIMTIME_ZERO ||
                algorithm->rtt <= SIMTIME_ZERO || !std::isfinite(subflowState->u) ||
                !std::isfinite(pathPrice) || pathPrice <= 0.0)
            continue;

        const double rate = getDeliveryRate(algorithm, subflowState);
        const double connectionCount = std::max(1.0,
                static_cast<double>(subflowState->sharingFlows));
        const double baseAiRate = subflowState->bottBW /
                connectionCount * subflowState->additiveIncreasePercent;
        const double opportunity = 1.0 / pathPrice;
        const double utilizationSafety = subflowState->eta /
                std::max(subflowState->eta, subflowState->u);
        const double targetWeight = opportunity * opportunity * utilizationSafety;
        if (!std::isfinite(rate) || rate < 0.0 ||
                !std::isfinite(baseAiRate) || baseAiRate <= 0.0 ||
                !std::isfinite(targetWeight) || targetWeight <= 0.0)
            continue;

        const double targetShare = targetWeight / targetWeightSum;
        const double rateShare = rate / connectionRate;
        const double alphaAiRate = baseAiRate * rateShare;
        const double responsiveness = targetShare / (targetShare + rateShare);
        const double responsiveWeight = targetShare * responsiveness * responsiveness;
        if (!std::isfinite(alphaAiRate) || alphaAiRate < 0.0 ||
                !std::isfinite(responsiveness) || responsiveness <= 0.0 ||
                !std::isfinite(responsiveWeight) || responsiveWeight <= 0.0)
            continue;

        alphaAiRateBudget += alphaAiRate;
        responsiveWeightSum += responsiveWeight;

        if (algorithm == this) {
            currentSubflowFound = true;
            currentPathPrice = pathPrice;
            currentOpportunity = opportunity;
            currentTargetShare = targetShare;
            currentRateShare = rateShare;
            currentResponsiveness = responsiveness;
            currentResponsiveWeight = responsiveWeight;
            currentRtt = algorithm->rtt.dbl();
        }
    }

    if (!currentSubflowFound || alphaAiRateBudget <= 0.0 ||
            responsiveWeightSum <= 0.0 || currentResponsiveWeight <= 0.0 ||
            !std::isfinite(alphaAiRateBudget) || !std::isfinite(responsiveWeightSum))
        return;

    const double aiShare = currentResponsiveWeight / responsiveWeightSum;
    const double ai = alphaAiRateBudget * aiShare * currentRtt;
    if (!std::isfinite(ai) || ai <= 0.0)
        return;

    uint32_t adjustedAi = ai;
    if (adjustedAi == 0)
        adjustedAi = 1;

    state->additiveIncrease = adjustedAi;
    hasAllocation = true;
    lastPathPrice = currentPathPrice;
    lastOpportunity = currentOpportunity;
    lastTargetShare = currentTargetShare;
    lastRateShare = currentRateShare;
    lastResponsiveness = currentResponsiveness;
    lastAiShare = aiShare;
    lastAiRateBudget = alphaAiRateBudget;
    lastAdjustedAi = adjustedAi;
}

} // namespace tcp
} // namespace inet
