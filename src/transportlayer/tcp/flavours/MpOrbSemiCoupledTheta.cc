// SPDX-License-Identifier: LGPL-3.0-or-later
#include "MpOrbSemiCoupledTheta.h"
#include "../MpOrbSubflowConnection.h"
#include "../../../common/ThetaShuffle.h"
#include <cmath>
namespace inet { namespace tcp {
Register_Class(MpOrbSemiCoupledTheta);

void MpOrbSemiCoupledTheta::stampTelemetry(DeltaShuffleTag& tag)
{
    MpOrbSenderDelta::stampTelemetry(tag);
    tag.rateControlled = true;
}

double MpOrbSemiCoupledTheta::measureInflight(const IntDataVec& data)
{
    auto tag = check_and_cast<MpOrbSubflowConnection *>(conn)->getCurrentDeltaShuffle();
    if (tag && tag->senderControlled && tag->rateControlled && tag->echoed &&
            tag->hops > 0 && !data.empty() &&
            tag->bottleneckId == data.front().getHopId() && tag->serial > lastShuffleSerial) {
        lastShuffleSerial = tag->serial;
        // All rates are payload bytes/s. Use the packet's send-time X and the
        // chosen bottleneck's PREVIOUS interval mean. No inverse-need gain.
        const double correction = theta::rateCorrection(tag->packetMass,
                tag->rateAllowance, tag->connectionRate, tag->meanConnectionRate);
        if (!std::isfinite(correction))
            throw cRuntimeError("Invalid Theta fairness feedback");
        if (state->srtt > SIMTIME_ZERO)
            pendingShuffleBytes += correction * state->srtt.dbl();
        static const simsignal_t correctionSignal = cComponent::registerSignal("mpOrbThetaCorrection");
        conn->emit(correctionSignal, 8 * correction);
    }
    // Bypass Sender Delta's inverse-need correction, retaining Alpha AI/MD.
    return MpOrbSemiCoupledAlpha::measureInflight(data);
}
} }
