// SPDX-License-Identifier: LGPL-3.0-or-later
#include "MpOrbOmega.h"
#include "../MpOrbConnection.h"
#include "../MpOrbSubflowConnection.h"
#include "../../../../../orbtcp/src/common/PintFlowCount.h"
#include "../../../../../orbtcp/src/common/OmegaPrice.h"

#include <algorithm>
#include <cmath>

namespace inet { namespace tcp {
Register_Class(MpOrbOmega);

void MpOrbOmega::initialize()
{
    MpOrbUncoupled::initialize();
    auto *tcp = conn->getTcpMain();
    if (!tcp->par("pacingEnabled").boolValue() || !tcp->par("updatedSackEnabled").boolValue())
        throw cRuntimeError("MpOrbOmega requires pacingEnabled and updatedSackEnabled");
    if (state->delayed_acks_enabled)
        throw cRuntimeError("MpOrbOmega requires delayedAcksEnabled=false to echo each price measurement");
    firstRTT = false;
    state->initialPhase = false;
    state->additiveIncrease = 0;
}

void MpOrbOmega::established(bool active)
{
    connId = std::hash<std::string>{}(conn->localAddr.str() + "/" + std::to_string(conn->localPort) +
            "/" + conn->remoteAddr.str() + "/" + std::to_string(conn->remotePort));
    initPackets = true;
    state->ssthresh = 0;
    const double rtt = state->srtt > SIMTIME_ZERO ? state->srtt.dbl() :
            conn->getTcpMain()->par("omegaMinRtt").doubleValue();
    // Same pre-RTT pacing bootstrap as OrbCC; its normal cwnd/RTT pacing
    // calculation takes over as soon as an RTT measurement is available.
    check_and_cast<TcpPacedConnection *>(conn)->changeIntersendingTime(0.000001);
    installTargetRate(5.0 * state->snd_mss / rtt);
    auto *subflow = check_and_cast<MpOrbSubflowConnection *>(conn);
    check_and_cast<MpOrbConnection *>(subflow->getMetaConnection())->startOmegaControl();
    if (active) {
        sendData(false);
        conn->sendAck();
    }
}

double MpOrbOmega::measureInflight(const IntDataVec& data)
{
    // Reuse ACK/recovery machinery, but never run OrbCC's window/AI controller.
    auto price = check_and_cast<MpOrbSubflowConnection *>(conn)->getCurrentOmegaPrice();
    if (price != nullptr && !price->echoed)
        return 0; // Incoming data measures the opposite direction, not this sender's path.
    if (price == nullptr && data.empty())
        return 0; // Handshake/window-update ACK with no measurement.
    if (price == nullptr || price->hops == 0 || price->pricedHops != price->hops || data.empty())
        throw cRuntimeError("MpOrbOmega requires omegaPriceEnabled=true on every forward PintQueue");
    if (!std::isfinite(price->price) || price->price < 0 ||
            !std::isfinite(price->capacity) || price->capacity <= 0 ||
            price->sampledAt > simTime() || (hasPrice && price->sampledAt < priceSampleTime))
        return 0;
    const auto& sample = data.front();
    fairRate = sample.getB() / static_cast<double>(std::max(1U,
            pint::decodeFlowCount(sample.getPintTotalFlowCountCode(), pintFlowCountBits, pintMaxFlowCount)));
    // B/N seeds the initial rate once. It is never a rate entitlement or coupling weight.
    if (!hasPrice && fairRate > 0)
        targetRate = std::max(targetRate, 0.1 * fairRate);
    if (hasPrice && pricePathDigest != sample.getPathDigest())
        targetRate = std::min(targetRate, std::max(0.1 * fairRate, 5.0 * state->snd_mss /
                std::max(state->srtt.dbl(), 0.001)));
    hasPrice = true;
    pricePathDigest = sample.getPathDigest();
    pathPrice = price->price;
    capacity = price->capacity;
    priceSampleTime = price->sampledAt;
    priceReceivedTime = simTime();
    state->u = sample.getPintUtilization();
    state->sharingFlows = std::max(1U, pint::decodeFlowCount(
            sample.getPintTotalFlowCountCode(), pintFlowCountBits, pintMaxFlowCount));
    state->bottBW = sample.getB();
    conn->emit(USignal, state->u);
    conn->emit(sharingFlowsSignal, state->sharingFlows);
    conn->emit(bottleneckBandwidthSignal, state->bottBW);
    static const simsignal_t priceSignal = cComponent::registerSignal("omegaPathPrice");
    conn->emit(priceSignal, pathPrice);
    return 0;
}

void MpOrbOmega::installTargetRate(double rate)
{
    targetRate = rate;
    const double rtt = state->srtt > SIMTIME_ZERO ? state->srtt.dbl() :
            conn->getTcpMain()->par("omegaMinRtt").doubleValue();
    // Cwnd is the actuator: x [bytes/s] * measured RTT [s] = window [bytes].
    // Keep OrbCC's pacing rule unchanged, including its in-flight drain rule.
    // Old flight above a reduced cwnd drains before more data may be sent.
    uint32_t window = omega::windowForRate(targetRate, rtt, state->snd_mss);
    if (state->lossRecovery || state->afterRto || simTime() < recoveryUntil)
        window = std::min(window, state->snd_cwnd);
    state->snd_cwnd = std::max(state->snd_mss, window);
    state->prevWnd = state->snd_cwnd;
    updatePacingInterval();
    conn->emit(cwndSignal, state->snd_cwnd);
    static const simsignal_t targetSignal = cComponent::registerSignal("omegaTargetRate");
    conn->emit(targetSignal, targetRate * 8);
}

void MpOrbOmega::processRexmitTimer(TcpEventCode& event)
{
    MpOrbUncoupled::processRexmitTimer(event);
    if (event == TCP_E_ABORT)
        return;
    const double rtt = state->srtt > SIMTIME_ZERO ? state->srtt.dbl() :
            conn->getTcpMain()->par("omegaMinRtt").doubleValue();
    recoveryUntil = simTime() + SimTime(rtt);
    installTargetRate(std::min(targetRate, state->snd_mss / rtt));
}

void MpOrbOmega::rackLossDetected()
{
    const bool wasRecovering = state->lossRecovery;
    MpOrbUncoupled::rackLossDetected();
    if (!wasRecovering && state->lossRecovery) {
        const double rtt = state->srtt > SIMTIME_ZERO ? state->srtt.dbl() :
                conn->getTcpMain()->par("omegaMinRtt").doubleValue();
        recoveryUntil = simTime() + SimTime(rtt);
        installTargetRate(std::max(state->snd_mss / rtt, targetRate / 2));
    }
}
} } // namespace inet::tcp
