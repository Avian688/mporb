//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//

#include "MpOrbSemiCoupledBeta.h"

#include <algorithm>
#include <cmath>

#include "../../../../../mptcp/src/transportlayer/tcp/MpTcpConnection.h"

namespace inet {
namespace tcp {

Register_Class(MpOrbSemiCoupledBeta);

simsignal_t MpOrbSemiCoupledBeta::fairRateSignal = cComponent::registerSignal("semiCoupledBetaFairRate");
simsignal_t MpOrbSemiCoupledBeta::totalFairRateSignal = cComponent::registerSignal("semiCoupledBetaTotalFairRate");
simsignal_t MpOrbSemiCoupledBeta::fairRateShareSignal = cComponent::registerSignal("semiCoupledBetaFairRateShare");

void MpOrbSemiCoupledBeta::adjustAdditiveIncrease()
{
    if (state == nullptr || firstRTT || state->initialPhase)
        return;

    MpTcpConnection *metaConnection = getMetaConnection();
    if (metaConnection == nullptr)
        return;

    double fairRate = 0.0;
    double totalFairRate = 0.0;
    for (auto *subflow : metaConnection->getSubflows()) {
        if (subflow == nullptr)
            continue;

        const int tcpState = subflow->getFsmState();
        if (tcpState != TCP_S_ESTABLISHED && tcpState != TCP_S_CLOSE_WAIT)
            continue;

        auto *algorithm = dynamic_cast<MpOrbSemiCoupledBeta *>(subflow->getTcpAlgorithm());
        if (algorithm == nullptr)
            continue;

        const auto *subflowState = static_cast<const OrbtcpStateVariables *>(subflow->getState());
        if (subflowState == nullptr || subflowState->bottBW <= 0.0)
            continue;

        const double connectionCount = std::max(1.0,
                static_cast<double>(subflowState->sharingFlows));
        const double subflowFairRate = subflowState->eta * subflowState->bottBW /
                connectionCount;
        if (!std::isfinite(subflowFairRate) || subflowFairRate <= 0.0)
            continue;

        totalFairRate += subflowFairRate;
        if (algorithm == this)
            fairRate = subflowFairRate;
    }

    if (!std::isfinite(totalFairRate) || totalFairRate <= 0.0 || fairRate <= 0.0)
        return;

    const double fairRateShare = fairRate / totalFairRate;
    const uint32_t uncoupledAi = state->additiveIncrease;
    if (uncoupledAi > 0) {
        state->additiveIncrease = uncoupledAi * fairRateShare;
        if (state->additiveIncrease == 0)
            state->additiveIncrease = 1;
    }

    conn->emit(fairRateSignal, fairRate);
    conn->emit(totalFairRateSignal, totalFairRate);
    conn->emit(fairRateShareSignal, fairRateShare);
}

} // namespace tcp
} // namespace inet
