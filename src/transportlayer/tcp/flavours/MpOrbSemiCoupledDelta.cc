//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//

#include "MpOrbSemiCoupledDelta.h"

#include <algorithm>
#include <cmath>

#include "../../../../../mptcp/src/transportlayer/tcp/MpTcpConnection.h"
#include "../../../../../tcpPaced/src/transportlayer/tcp/TcpPacedConnection.h"

namespace inet {
namespace tcp {

Register_Class(MpOrbSemiCoupledDelta);

simsignal_t MpOrbSemiCoupledDelta::baseAiRateSignal = cComponent::registerSignal("semiCoupledDeltaBaseAiRate");
simsignal_t MpOrbSemiCoupledDelta::alphaAiRateSignal = cComponent::registerSignal("semiCoupledDeltaAlphaAiRate");
simsignal_t MpOrbSemiCoupledDelta::targetShareSignal = cComponent::registerSignal("semiCoupledDeltaTargetShare");
simsignal_t MpOrbSemiCoupledDelta::rateShareSignal = cComponent::registerSignal("semiCoupledDeltaRateShare");
simsignal_t MpOrbSemiCoupledDelta::responsivenessSignal = cComponent::registerSignal("semiCoupledDeltaResponsiveness");
simsignal_t MpOrbSemiCoupledDelta::aiShareSignal = cComponent::registerSignal("semiCoupledDeltaAiShare");

namespace {
constexpr double DELIVERY_RATE_EWMA_GAIN = 0.125;
}

void MpOrbSemiCoupledDelta::refreshDeliveryRate()
{
    if (!pathId.empty()) {
        if (!observedPathId.empty() && observedPathId != pathId) {
            smoothedDeliveryRate = 0.0;
            deliveryRateUpdatedAt = SIMTIME_ZERO;
        }
        observedPathId = pathId;
    }

    auto *pacedConnection = dynamic_cast<TcpPacedConnection *>(conn);
    if (pacedConnection == nullptr)
        return;

    const auto sample = pacedConnection->getRateSample();
    const double deliveryRate = sample.m_deliveryRate;
    if (deliveryRate <= 0.0 || sample.m_interval <= SIMTIME_ZERO ||
            !std::isfinite(deliveryRate))
        return;

    smoothedDeliveryRate = smoothedDeliveryRate > 0.0 ?
            (1.0 - DELIVERY_RATE_EWMA_GAIN) * smoothedDeliveryRate +
            DELIVERY_RATE_EWMA_GAIN * deliveryRate : deliveryRate;
    deliveryRateUpdatedAt = simTime();
}

double MpOrbSemiCoupledDelta::getDeliveryRate(const MpOrbSemiCoupledDelta *algorithm,
        const OrbtcpStateVariables *subflowState) const
{
    if (algorithm == nullptr || subflowState == nullptr ||
            algorithm->deliveryRateUpdatedAt == SIMTIME_ZERO ||
            subflowState->srtt <= SIMTIME_ZERO)
        return -1.0;

    double deliveryRate = algorithm->smoothedDeliveryRate;
    if (!std::isfinite(deliveryRate) || deliveryRate < 0.0)
        return -1.0;

    // A subflow that has stopped delivering should not retain its old share.
    if (simTime() - algorithm->deliveryRateUpdatedAt > subflowState->srtt * 2)
        deliveryRate = 0.0;

    // txRate is the aggregate bottleneck service rate derived from INT
    // txBytes. It is not a per-subflow rate, but it is a valid upper bound.
    if (subflowState->txRate > 0.0 && std::isfinite(subflowState->txRate))
        deliveryRate = std::min(deliveryRate, subflowState->txRate);

    return deliveryRate;
}

uint32_t MpOrbSemiCoupledDelta::computeWnd(double u, bool updateWc)
{
    const uint32_t result = MpOrbSemiCoupled::computeWnd(u, updateWc);
    if (updateWc && hasAllocation) {
        conn->emit(baseAiRateSignal, lastBaseAiRate);
        conn->emit(alphaAiRateSignal, lastAlphaAiRate);
        conn->emit(targetShareSignal, lastTargetShare);
        conn->emit(rateShareSignal, lastRateShare);
        conn->emit(responsivenessSignal, lastResponsiveness);
        conn->emit(aiShareSignal, lastAiShare);
    }
    return result;
}

void MpOrbSemiCoupledDelta::adjustAdditiveIncrease()
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
    double maximumFairRate = 0.0;
    size_t validSubflows = 0;

    // First establish the connection rate and normalize Gamma's INT target.
    for (auto *subflow : metaConnection->getSubflows()) {
        if (subflow == nullptr)
            continue;

        const int tcpState = subflow->getFsmState();
        if (tcpState != TCP_S_ESTABLISHED && tcpState != TCP_S_CLOSE_WAIT)
            continue;

        auto *algorithm = dynamic_cast<MpOrbSemiCoupledDelta *>(subflow->getTcpAlgorithm());
        if (algorithm == nullptr)
            continue;

        const auto *subflowState = static_cast<const OrbtcpStateVariables *>(subflow->getState());
        if (subflowState == nullptr || algorithm->observedPathId.empty() ||
                subflowState->bottBW <= 0.0 || subflowState->srtt <= SIMTIME_ZERO ||
                algorithm->rtt <= SIMTIME_ZERO ||
                !std::isfinite(subflowState->u))
            continue;

        const double rate = getDeliveryRate(algorithm, subflowState);
        const double connectionCount = std::max(1.0, static_cast<double>(subflowState->sharingFlows));
        const double fairRate = subflowState->eta * subflowState->bottBW / connectionCount;
        const double utilizationSafety = subflowState->eta /
                std::max(subflowState->eta, subflowState->u);
        const double targetWeight = fairRate * fairRate * utilizationSafety;

        if (!std::isfinite(rate) || rate < 0.0 || !std::isfinite(fairRate) ||
                fairRate <= 0.0 || !std::isfinite(targetWeight) || targetWeight <= 0.0)
            continue;

        connectionRate += rate;
        targetWeightSum += targetWeight;
        maximumFairRate = std::max(maximumFairRate, fairRate);
        validSubflows++;
    }

    if (validSubflows <= 1 || connectionRate <= 0.0 || targetWeightSum <= 0.0 ||
            maximumFairRate <= 0.0 || !std::isfinite(connectionRate) ||
            !std::isfinite(targetWeightSum))
        return;

    double alphaAiRateBudget = 0.0;
    double responsiveWeightSum = 0.0;
    bool currentSubflowFound = false;
    double currentRate = 0.0;
    double currentConnectionCount = 1.0;
    double currentFairRate = 0.0;
    double currentUtilizationSafety = 0.0;
    double currentBaseAiRate = 0.0;
    double currentAlphaAiRate = 0.0;
    double currentTargetShare = 0.0;
    double currentRateShare = 0.0;
    double currentResponsiveness = 0.0;
    double currentResponsiveWeight = 0.0;
    double currentRtt = 0.0;

    // Alpha sets the connection's total AI-rate budget. Gamma's smooth target
    // allocator redistributes that budget without inheriting Alpha's rate bias.
    for (auto *subflow : metaConnection->getSubflows()) {
        if (subflow == nullptr)
            continue;

        const int tcpState = subflow->getFsmState();
        if (tcpState != TCP_S_ESTABLISHED && tcpState != TCP_S_CLOSE_WAIT)
            continue;

        auto *algorithm = dynamic_cast<MpOrbSemiCoupledDelta *>(subflow->getTcpAlgorithm());
        if (algorithm == nullptr)
            continue;

        const auto *subflowState = static_cast<const OrbtcpStateVariables *>(subflow->getState());
        if (subflowState == nullptr || algorithm->observedPathId.empty() ||
                subflowState->bottBW <= 0.0 || subflowState->srtt <= SIMTIME_ZERO ||
                algorithm->rtt <= SIMTIME_ZERO ||
                !std::isfinite(subflowState->u))
            continue;

        const double rate = getDeliveryRate(algorithm, subflowState);
        const double connectionCount = std::max(1.0, static_cast<double>(subflowState->sharingFlows));
        const double fairRate = subflowState->eta * subflowState->bottBW / connectionCount;
        const double utilizationSafety = subflowState->eta /
                std::max(subflowState->eta, subflowState->u);
        const double targetWeight = fairRate * fairRate * utilizationSafety;
        const double baseAiRate = subflowState->bottBW /
                connectionCount * subflowState->additiveIncreasePercent;

        if (!std::isfinite(rate) || rate < 0.0 || !std::isfinite(fairRate) ||
                fairRate <= 0.0 || !std::isfinite(targetWeight) || targetWeight <= 0.0 ||
                !std::isfinite(baseAiRate) || baseAiRate <= 0.0)
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
            currentRate = rate;
            currentConnectionCount = connectionCount;
            currentFairRate = fairRate;
            currentUtilizationSafety = utilizationSafety;
            currentBaseAiRate = baseAiRate;
            currentAlphaAiRate = alphaAiRate;
            currentTargetShare = targetShare;
            currentRateShare = rateShare;
            currentResponsiveness = responsiveness;
            currentResponsiveWeight = responsiveWeight;
            currentRtt = algorithm->rtt.dbl();
        }
    }

    if (!currentSubflowFound || alphaAiRateBudget <= 0.0 || responsiveWeightSum <= 0.0 ||
            currentResponsiveWeight <= 0.0 || !std::isfinite(alphaAiRateBudget) ||
            !std::isfinite(responsiveWeightSum))
        return;

    // Preserve Alpha's aggregate rate increase, then convert this subflow's
    // allocation back to a window increment using OrbCC's measured RTT.
    const double aiShare = currentResponsiveWeight / responsiveWeightSum;
    const double aiRate = alphaAiRateBudget * aiShare;
    const double ai = aiRate * currentRtt;
    if (!std::isfinite(ai) || ai <= 0.0)
        return;

    uint32_t adjustedAi = ai;
    if (adjustedAi == 0)
        adjustedAi = 1;

    state->additiveIncrease = adjustedAi;
    hasAllocation = true;
    lastConnectionRate = connectionRate;
    lastDeliveryRate = currentRate;
    lastConnectionCount = currentConnectionCount;
    lastFairRate = currentFairRate;
    lastRelativeOpportunity = currentFairRate / maximumFairRate;
    lastUtilizationSafety = currentUtilizationSafety;
    lastPathWeight = lastRelativeOpportunity * lastRelativeOpportunity *
            currentUtilizationSafety;
    lastAiRateBudget = alphaAiRateBudget;
    lastAdjustedAi = adjustedAi;
    lastBaseAiRate = currentBaseAiRate;
    lastAlphaAiRate = currentAlphaAiRate;
    lastTargetShare = currentTargetShare;
    lastRateShare = currentRateShare;
    lastResponsiveness = currentResponsiveness;
    lastAiShare = aiShare;
}

} // namespace tcp
} // namespace inet
