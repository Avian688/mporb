// SPDX-License-Identifier: LGPL-3.0-or-later
#include "MpOrbSenderDelta.h"
#include "../MpOrbSubflowConnection.h"
#include "../../../../../mptcp/src/transportlayer/tcp/MpTcpConnection.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace inet { namespace tcp {
Register_Class(MpOrbSenderDelta);

void MpOrbSenderDelta::stampTelemetry(DeltaShuffleTag& tag)
{
    tag = DeltaShuffleTag();
    tag.serial = ++nextSerial;
    tag.senderControlled = true;
    auto rate = [](const OrbtcpStateVariables *s) {
        // Nominal cwnd/RTT rate, not a measured delivery rate. Bootstrap before RTT.
        return s->snd_cwnd / (s->srtt > SIMTIME_ZERO ? s->srtt.dbl() : 0.02);
    };
    tag.subflowRate = rate(state);
    if (auto *meta = check_and_cast<MpOrbSubflowConnection *>(conn)->getMetaConnection()) {
        for (auto *sf : meta->getSubflows()) {
            if (dynamic_cast<MpOrbSenderDelta *>(sf->getTcpAlgorithm()) &&
                    sf->isTransportActiveForScheduler() && !sf->isSchedulerStale())
                tag.connectionRate += rate(static_cast<const OrbtcpStateVariables *>(sf->getState()));
        }
    }
    tag.connectionRate = std::max(tag.subflowRate, tag.connectionRate);
    static const simsignal_t xSignal = cComponent::registerSignal("mpOrbDeltaSubflowRate");
    static const simsignal_t totalSignal = cComponent::registerSignal("mpOrbDeltaConnectionRate");
    conn->emit(xSignal, 8 * tag.subflowRate);
    conn->emit(totalSignal, 8 * tag.connectionRate);
}

double MpOrbSenderDelta::measureInflight(const IntDataVec& data)
{
    auto tag = check_and_cast<MpOrbSubflowConnection *>(conn)->getCurrentDeltaShuffle();
    if (tag && tag->senderControlled && tag->echoed && tag->hops > 0 &&
            tag->serial > lastShuffleSerial) {
        lastShuffleSerial = tag->serial;
        double correction = 0;
        // Compare the packet's send-time connection need with the chosen bottleneck's
        // previous population. L/(x*T) compensates its packet frequency.
        for (const auto& m : tag->measurements)
            correction += m.mass * m.coefficient * (m.need - m.meanNeed);
        if (!std::isfinite(correction))
            throw cRuntimeError("Invalid sender Delta fairness feedback");
        if (state->srtt > SIMTIME_ZERO)
            pendingShuffleBytes += correction * state->srtt.dbl();
        static const simsignal_t needSignal = cComponent::registerSignal("mpOrbDeltaNeed");
        static const simsignal_t correctionSignal = cComponent::registerSignal("mpOrbSenderDeltaCorrection");
        conn->emit(needSignal, tag->need);
        conn->emit(correctionSignal, 8 * correction);
    }
    // Inherit Alpha's virtual AI hook (x_r/X), startup and local U-based MD.
    return MpOrbSemiCoupledAlpha::measureInflight(data);
}

uint32_t MpOrbSenderDelta::computeWnd(double u, bool updateWc)
{
    const uint32_t orbWindow = MpOrbSemiCoupledAlpha::computeWnd(u, updateWc);
    // Fast Alpha feedback remains unchanged. Its growth gate may return the
    // current cwnd, which already includes an earlier adjustment; adding the
    // pending shuffle on every ACK would then spend it repeatedly.
    if (!updateWc)
        return orbWindow;

    const double wholeBytes = std::trunc(pendingShuffleBytes);
    const uint32_t window = static_cast<uint32_t>(std::clamp(orbWindow + wholeBytes,
            static_cast<double>(state->snd_mss),
            static_cast<double>(std::numeric_limits<uint32_t>::max())));
    // Apply and consume once at OrbCC's normal RTT-paced commit.
    state->prevWnd = window;
    pendingShuffleBytes -= wholeBytes; // keep sub-byte rounding only
    static const simsignal_t appliedSignal = cComponent::registerSignal("mpOrbDeltaAppliedShuffle");
    if (state->srtt > SIMTIME_ZERO)
        conn->emit(appliedSignal, 8 * (static_cast<double>(window) - orbWindow) / state->srtt.dbl());
    return window;
}
} }
