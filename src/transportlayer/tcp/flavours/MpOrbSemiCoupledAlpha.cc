//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//

#include "MpOrbSemiCoupledAlpha.h"

#include <cmath>

#include "../MpOrbSubflowConnection.h"
#include "../../../../../mptcp/src/transportlayer/tcp/MpTcpConnection.h"

namespace inet {
namespace tcp {

Register_Class(MpOrbSemiCoupledAlpha);

simsignal_t MpOrbSemiCoupledAlpha::subflowRateSignal = cComponent::registerSignal("semiCoupledAlphaSubflowRate");
simsignal_t MpOrbSemiCoupledAlpha::connectionRateSignal = cComponent::registerSignal("semiCoupledAlphaConnectionRate");
simsignal_t MpOrbSemiCoupledAlpha::rateShareSignal = cComponent::registerSignal("semiCoupledAlphaRateShare");

MpTcpConnection *MpOrbSemiCoupledAlpha::getMetaConnection() const
{
    auto *subflow = dynamic_cast<MpOrbSubflowConnection *>(conn);
    return subflow != nullptr ? subflow->getMetaConnection() : nullptr;
}

void MpOrbSemiCoupledAlpha::adjustAdditiveIncrease()
{
    // Keep OrbCC's startup behavior unchanged; couple only steady-state AI.
    if (state == nullptr || firstRTT || state->initialPhase || state->srtt <= SIMTIME_ZERO)
        return;

    double rate = state->snd_cwnd / state->srtt.dbl();
    MpTcpConnection *metaConnection = getMetaConnection();
    if (metaConnection == nullptr || !std::isfinite(rate) || rate <= 0.0)
        return;

    double connectionRate = 0.0;
    for (auto *subflow : metaConnection->getSubflows()) {
        if (subflow == nullptr)
            continue;

        const int tcpState = subflow->getFsmState();
        if (tcpState != TCP_S_ESTABLISHED && tcpState != TCP_S_CLOSE_WAIT)
            continue;

        if (dynamic_cast<MpOrbSemiCoupledAlpha *>(subflow->getTcpAlgorithm()) == nullptr)
            continue;

        const auto *subflowState = static_cast<const OrbtcpStateVariables *>(subflow->getState());
        if (subflowState == nullptr || subflowState->srtt <= SIMTIME_ZERO)
            continue;

        double subflowRate = subflowState->snd_cwnd / subflowState->srtt.dbl();
        if (std::isfinite(subflowRate) && subflowRate > 0.0)
            connectionRate += subflowRate;
    }

    if (!std::isfinite(connectionRate) || connectionRate <= 0.0)
        return;

    double rateShare = rate / connectionRate;
    const uint32_t uncoupledAi = state->additiveIncrease;
    if (uncoupledAi > 0) {
        state->additiveIncrease = uncoupledAi * rateShare;
        if (state->additiveIncrease == 0)
            state->additiveIncrease = 1;
    }

    conn->emit(subflowRateSignal, rate);
    conn->emit(connectionRateSignal, connectionRate);
    conn->emit(rateShareSignal, rateShare);
}

} // namespace tcp
} // namespace inet
