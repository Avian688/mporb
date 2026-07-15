//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//

#include "MpOrbSemiCoupledBeta.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>

#include "../MpOrbSubflowConnection.h"
#include "../../../../../mptcp/src/transportlayer/tcp/MpTcpConnection.h"

namespace inet {
namespace tcp {

Register_Class(MpOrbSemiCoupledBeta);

simsignal_t MpOrbSemiCoupledBeta::fairRateSignal = cComponent::registerSignal("semiCoupledBetaFairRate");
simsignal_t MpOrbSemiCoupledBeta::totalFairRateSignal = cComponent::registerSignal("semiCoupledBetaTotalFairRate");
simsignal_t MpOrbSemiCoupledBeta::fairRateShareSignal = cComponent::registerSignal("semiCoupledBetaFairRateShare");
simsignal_t MpOrbSemiCoupledBeta::uncoupledAiSignal = cComponent::registerSignal("semiCoupledBetaUncoupledAi");
simsignal_t MpOrbSemiCoupledBeta::adjustedAiSignal = cComponent::registerSignal("semiCoupledBetaAdjustedAi");

MpTcpConnection *MpOrbSemiCoupledBeta::getMetaConnection() const
{
    auto *subflow = dynamic_cast<MpOrbSubflowConnection *>(conn);
    return subflow != nullptr ? subflow->getMetaConnection() : nullptr;
}

size_t MpOrbSemiCoupledBeta::getConnId()
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

void MpOrbSemiCoupledBeta::adjustAdditiveIncrease()
{
    if (state == nullptr || firstRTT || state->initialPhase)
        return;

    MpTcpConnection *metaConnection = getMetaConnection();
    if (metaConnection == nullptr)
        return;

    double totalFairRate = 0.0;
    double fairRate = 0.0;
    size_t validSubflows = 0;

    for (auto *subflow : metaConnection->getSubflows()) {
        if (subflow == nullptr)
            continue;

        auto *algorithm = dynamic_cast<MpOrbSemiCoupledBeta *>(subflow->getTcpAlgorithm());
        if (algorithm == nullptr)
            continue;

        const auto *subflowState = static_cast<const OrbtcpStateVariables *>(subflow->getState());
        if (subflowState == nullptr || algorithm->pathId.empty() ||
                subflowState->bottBW <= 0.0 || subflowState->srtt <= SIMTIME_ZERO)
            continue;

        double connectionCount = std::max(1.0, static_cast<double>(subflowState->sharingFlows));
        double subflowFairRate = subflowState->eta * subflowState->bottBW /
                connectionCount;
        if (!std::isfinite(subflowFairRate) || subflowFairRate <= 0.0)
            continue;

        totalFairRate += subflowFairRate;
        validSubflows++;

        if (algorithm == this)
            fairRate = subflowFairRate;
    }

    if (validSubflows <= 1 || fairRate <= 0.0 || totalFairRate <= 0.0 ||
            !std::isfinite(totalFairRate))
        return;

    double fairRateShare = fairRate / totalFairRate;
    uint32_t uncoupledAi = state->additiveIncrease;
    double ai = uncoupledAi * fairRateShare;
    if (!std::isfinite(ai) || ai <= 0.0)
        return;

    state->additiveIncrease = ai;
    if (state->additiveIncrease == 0)
        state->additiveIncrease = 1;

    conn->emit(fairRateSignal, fairRate);
    conn->emit(totalFairRateSignal, totalFairRate);
    conn->emit(fairRateShareSignal, fairRateShare);
    conn->emit(uncoupledAiSignal, uncoupledAi);
    conn->emit(adjustedAiSignal, state->additiveIncrease);
}

} // namespace tcp
} // namespace inet
