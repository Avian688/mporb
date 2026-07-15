//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//

#include "MpOrbSemiCoupled.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>

#include "../MpOrbSubflowConnection.h"
#include "../../../../../mptcp/src/transportlayer/tcp/MpTcpConnection.h"
#include "../../../../../tcpPaced/src/transportlayer/tcp/TcpPacedConnection.h"

namespace inet {
namespace tcp {

Register_Class(MpOrbSemiCoupled);

simsignal_t MpOrbSemiCoupled::connectionRateSignal = cComponent::registerSignal("semiCoupledConnectionRate");
simsignal_t MpOrbSemiCoupled::deliveryRateSignal = cComponent::registerSignal("semiCoupledDeliveryRate");
simsignal_t MpOrbSemiCoupled::connectionCountSignal = cComponent::registerSignal("semiCoupledConnectionCount");
simsignal_t MpOrbSemiCoupled::fairRateSignal = cComponent::registerSignal("semiCoupledFairRate");
simsignal_t MpOrbSemiCoupled::relativeOpportunitySignal = cComponent::registerSignal("semiCoupledRelativeOpportunity");
simsignal_t MpOrbSemiCoupled::utilizationSafetySignal = cComponent::registerSignal("semiCoupledUtilizationSafety");
simsignal_t MpOrbSemiCoupled::pathWeightSignal = cComponent::registerSignal("semiCoupledPathWeight");
simsignal_t MpOrbSemiCoupled::aiRateBudgetSignal = cComponent::registerSignal("semiCoupledAiRateBudget");
simsignal_t MpOrbSemiCoupled::adjustedAiSignal = cComponent::registerSignal("semiCoupledAdjustedAi");

namespace {
constexpr double DELIVERY_RATE_EWMA_GAIN = 0.125;
}

MpTcpConnection *MpOrbSemiCoupled::getMetaConnection() const
{
    auto *subflow = dynamic_cast<MpOrbSubflowConnection *>(conn);
    return subflow != nullptr ? subflow->getMetaConnection() : nullptr;
}

void MpOrbSemiCoupled::refreshTelemetry()
{
    if (!pathId.empty()) {
        if (!observedPathId.empty() && observedPathId != pathId) {
            smoothedDeliveryRate = 0.0;
            deliveryRateUpdatedAt = SIMTIME_ZERO;
        }
        observedPathId = pathId;
    }

    double deliveryRate = 0.0;
    auto *pacedConnection = dynamic_cast<TcpPacedConnection *>(conn);
    if (pacedConnection != nullptr) {
        const auto sample = pacedConnection->getRateSample();
        if (sample.m_deliveryRate > 0 && sample.m_interval > SIMTIME_ZERO)
            deliveryRate = sample.m_deliveryRate;
    }
    if (deliveryRate == 0.0 && state != nullptr && state->srtt > SIMTIME_ZERO)
        deliveryRate = state->snd_cwnd / state->srtt.dbl();

    if (deliveryRate > 0.0 && std::isfinite(deliveryRate)) {
        smoothedDeliveryRate = smoothedDeliveryRate > 0.0 ?
                (1.0 - DELIVERY_RATE_EWMA_GAIN) * smoothedDeliveryRate +
                DELIVERY_RATE_EWMA_GAIN * deliveryRate : deliveryRate;
        deliveryRateUpdatedAt = simTime();
    }
}

size_t MpOrbSemiCoupled::getConnId()
{
    auto *meta = getMetaConnection();
    if (meta == nullptr)
        return MpOrbUncoupled::getConnId();

    const std::string key = meta->getLocalAddressForSubflows().str() + "/" +
            std::to_string(meta->getLocalPortNumber()) + "/" +
            meta->getRemoteAddressForSubflows().str() + "/" +
            std::to_string(meta->getRemotePortNumber());
    return std::hash<std::string>{}(key);
}

uint32_t MpOrbSemiCoupled::computeWnd(double u, bool updateWc)
{
    const uint32_t result = OrbtcpFlavour::computeWnd(u, updateWc);
    if (updateWc && hasAllocation) {
        conn->emit(connectionRateSignal, lastConnectionRate);
        conn->emit(deliveryRateSignal, lastDeliveryRate);
        conn->emit(connectionCountSignal, lastConnectionCount);
        conn->emit(fairRateSignal, lastFairRate);
        conn->emit(relativeOpportunitySignal, lastRelativeOpportunity);
        conn->emit(utilizationSafetySignal, lastUtilizationSafety);
        conn->emit(pathWeightSignal, lastPathWeight);
        conn->emit(aiRateBudgetSignal, lastAiRateBudget);
        conn->emit(adjustedAiSignal, static_cast<double>(lastAdjustedAi));
    }
    return result;
}

void MpOrbSemiCoupled::adjustAdditiveIncrease()
{
    refreshTelemetry();
    hasAllocation = false;
    if (firstRTT || state == nullptr || state->initialPhase)
        return;

    MpTcpConnection *metaConnection = getMetaConnection();
    if (metaConnection == nullptr)
        return;

    double connectionRate = 0.0;
    double aiRateBudget = 0.0;
    double maximumFairRate = 0.0;
    double weightSum = 0.0;
    size_t validSubflows = 0;

    bool currentSubflowFound = false;
    double currentDeliveryRate = 0.0;
    double currentConnectionCount = 1.0;
    double currentFairRate = 0.0;
    double currentUtilizationSafety = 0.0;
    double currentWeight = 0.0;
    double currentSrtt = 0.0;

    for (auto *subflow : metaConnection->getSubflows()) {
        if (subflow == nullptr)
            continue;

        auto *algorithm = dynamic_cast<MpOrbSemiCoupled *>(subflow->getTcpAlgorithm());
        if (algorithm == nullptr)
            continue;

        const auto *subflowState = static_cast<const OrbtcpStateVariables *>(subflow->getState());
        if (subflowState == nullptr || algorithm->observedPathId.empty() ||
                subflowState->bottBW <= 0.0 || subflowState->srtt <= SIMTIME_ZERO ||
                !std::isfinite(subflowState->u))
            continue;

        double deliveryRate = algorithm->smoothedDeliveryRate;
        if (deliveryRate == 0.0) {
            const auto sample = subflow->getRateSample();
            if (sample.m_deliveryRate > 0 && sample.m_interval > SIMTIME_ZERO)
                deliveryRate = sample.m_deliveryRate;
            else
                deliveryRate = subflowState->snd_cwnd / subflowState->srtt.dbl();
        }
        if (deliveryRate <= 0.0)
            continue;

        double connectionCount = std::max(1.0, static_cast<double>(subflowState->sharingFlows));
        double fairRate = subflowState->eta * subflowState->bottBW / connectionCount;
        double utilizationSafety = subflowState->eta /
                std::max(subflowState->eta, subflowState->u);
        // Normalizing every fair rate by the same maximum would cancel when
        // the weights are divided by weightSum.
        double weight = fairRate * fairRate * utilizationSafety;

        connectionRate += deliveryRate;
        aiRateBudget = std::max(aiRateBudget,
                subflowState->bottBW * subflowState->additiveIncreasePercent);
        maximumFairRate = std::max(maximumFairRate, fairRate);
        weightSum += weight;
        validSubflows++;

        if (algorithm == this) {
            currentSubflowFound = true;
            currentDeliveryRate = deliveryRate;
            currentConnectionCount = connectionCount;
            currentFairRate = fairRate;
            currentUtilizationSafety = utilizationSafety;
            currentWeight = weight;
            currentSrtt = subflowState->srtt.dbl();
        }
    }

    if (!currentSubflowFound || validSubflows <= 1 || maximumFairRate <= 0.0 ||
            aiRateBudget <= 0.0 || currentWeight <= 0.0 || weightSum <= 0.0 ||
            !std::isfinite(connectionRate) || !std::isfinite(maximumFairRate) ||
            !std::isfinite(weightSum))
        return;

    double relativeOpportunity = currentFairRate / maximumFairRate;
    double pathWeight = relativeOpportunity * relativeOpportunity *
            currentUtilizationSafety;
    double rateFraction = currentWeight / weightSum;
    // Convert this subflow's share of the connection-wide rate increase into bytes.
    double ai = aiRateBudget * rateFraction * currentSrtt;
    if (!std::isfinite(ai) || ai <= 0.0)
        return;

    uint32_t adjustedAi = ai;
    if (adjustedAi == 0)
        adjustedAi = 1;

    state->additiveIncrease = adjustedAi;
    hasAllocation = true;
    lastConnectionRate = connectionRate;
    lastDeliveryRate = currentDeliveryRate;
    lastConnectionCount = currentConnectionCount;
    lastFairRate = currentFairRate;
    lastRelativeOpportunity = relativeOpportunity;
    lastUtilizationSafety = currentUtilizationSafety;
    lastPathWeight = pathWeight;
    lastAiRateBudget = aiRateBudget;
    lastAdjustedAi = adjustedAi;
}

} // namespace tcp
} // namespace inet
