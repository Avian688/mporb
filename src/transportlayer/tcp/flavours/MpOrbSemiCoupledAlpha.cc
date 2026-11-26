//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//

#include "MpOrbSemiCoupledAlpha.h"

#include <cmath>

#include "../../../../../mptcp/src/transportlayer/tcp/MpTcpConnection.h"
#include "../../../../../tcpPaced/src/transportlayer/tcp/TcpPacedConnection.h"

namespace inet {
namespace tcp {

Register_Class(MpOrbSemiCoupledAlpha);

simsignal_t MpOrbSemiCoupledAlpha::subflowRateSignal = cComponent::registerSignal("semiCoupledAlphaSubflowRate");
simsignal_t MpOrbSemiCoupledAlpha::connectionRateSignal = cComponent::registerSignal("semiCoupledAlphaConnectionRate");
simsignal_t MpOrbSemiCoupledAlpha::rateShareSignal = cComponent::registerSignal("semiCoupledAlphaRateShare");
const simsignal_t MpOrbSemiCoupledAlpha::windowUpdateSignal = cComponent::registerSignal("mpOrbWindowUpdate");

void MpOrbSemiCoupledAlpha::adjustAdditiveIncrease()
{
    if (windowUpdate && state) {
        windowUpdate->pintIncrease = state->additiveIncrease;
        windowUpdate->alphaIncrease = state->additiveIncrease;
    }
    // Coupled increase needs a valid RTT estimate.
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

    // Plain Alpha: only the current subflow rate share scales OrbCC AI.
    const double rateShare = rate / connectionRate;
    const double coupledWeight = std::min(1.0, rateShare);

    const uint32_t uncoupledAi = state->additiveIncrease;

    if (uncoupledAi > 0) {
        const double exactAi = static_cast<double>(uncoupledAi) * coupledWeight + additiveIncreaseResidual;
        const uint32_t coupledAi = static_cast<uint32_t>(exactAi);

        additiveIncreaseResidual = exactAi - coupledAi;
        state->additiveIncrease = coupledAi;
    }
    if (windowUpdate)
        windowUpdate->alphaIncrease = state->additiveIncrease;

    conn->emit(subflowRateSignal, rate);
    conn->emit(connectionRateSignal, connectionRate);
    conn->emit(rateShareSignal, rateShare);
}

void MpOrbSemiCoupledAlpha::enableWindowUpdateTrace()
{
    if (!windowUpdate) {
        windowUpdate = std::make_unique<MpOrbWindowUpdate>();
        windowUpdate->pintIncrease = state->additiveIncrease;
        windowUpdate->alphaIncrease = state->additiveIncrease;
    }
}

uint32_t MpOrbSemiCoupledAlpha::computeWnd(double u, bool updateWc)
{
    if (!windowUpdate || !updateWc)
        return MpOrbSemiCoupledBase::computeWnd(u, updateWc);

    auto& trace = *windowUpdate;
    trace.u = u;
    trace.cwndLimited = isCwndLimited();
    trace.cwndBefore = state->snd_cwnd;
    trace.previousCommitted = state->prevWnd > 0 ? state->prevWnd : state->snd_cwnd;
    trace.increaseWithBoost = state->additiveIncrease;
    // Evaluate both alternatives on this ACK with the same growth gate and window floor.
    const auto pintWindow = targetWindowForIncrease(u, trace.pintIncrease, trace.cwndLimited);
    const auto alphaWindow = targetWindowForIncrease(u, trace.alphaIncrease, trace.cwndLimited);
    trace.windowBeforeShuffle = MpOrbSemiCoupledBase::computeWnd(u, updateWc);
    trace.alphaRemoved = static_cast<int64_t>(pintWindow) - alphaWindow;
    trace.omegaRestored = static_cast<int64_t>(trace.windowBeforeShuffle) - alphaWindow;
    windowUpdatePending = true;
    return trace.windowBeforeShuffle;
}

void MpOrbSemiCoupledAlpha::updatePacingInterval()
{
    MpOrbSemiCoupledBase::updatePacingInterval();
    if (!windowUpdatePending)
        return;
    windowUpdatePending = false;
    // At this point the ACK handler has installed the final cwnd, including Delta's shuffle.
    auto& trace = *windowUpdate;
    trace.cwndAfter = state->snd_cwnd;
    const auto interval = check_and_cast<TcpPacedConnection *>(conn)->getPacingRate();
    trace.pacingRateMbps = interval > SIMTIME_ZERO ? 8e-6 * state->snd_mss / interval.dbl() : 0;
    conn->emit(windowUpdateSignal, &trace);
}

} // namespace tcp
} // namespace inet
