// SPDX-License-Identifier: LGPL-3.0-or-later
#include "MpOrbOmega.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace inet { namespace tcp {
Register_Class(MpOrbOmega);
Register_Class(MpOrbOmegaNonEdgeDisjoint);

void MpOrbOmegaNonEdgeDisjoint::stampTelemetry(DeltaShuffleTag& tag)
{
    MpOrbDelta::stampTelemetry(tag);
    tag.useConnectionRateForMass = true;
}

uint32_t MpOrbOmega::computeWnd(double u, bool updateWc)
{
    const uint32_t alphaAi = state->additiveIncrease;
    // Fast ACKs use Alpha unchanged. B/N is in payload bytes/s;
    // use the same RTT as OrbCC's AI calculation to convert rate to bytes.
    // Reserve the boost for a clear deficit: 0.20 below the 0.95 target U.
    if (updateWc && !firstRTT && !state->initialPhase &&
            std::isfinite(u) && u >= 0 && u < 0.75 &&
            state->sharingFlows > 0 && rtt > SIMTIME_ZERO) {
        const double fairWindow = (static_cast<double>(state->bottBW) /
                state->sharingFlows) * rtt.dbl();
        const double gain = std::min(0.2 * (0.95 - u), state->additiveIncreasePercent);
        const double fastAi = gain * fairWindow;
        state->additiveIncrease = static_cast<uint32_t>(std::min(
                std::max(static_cast<double>(alphaAi), fastAi),
                static_cast<double>(std::numeric_limits<uint32_t>::max())));
    }
    const uint32_t extraAi = state->additiveIncrease - alphaAi;
    // Parent retains the growth gate, local MD, fairness correction and floor.
    const uint32_t window = MpOrbDelta::computeWnd(u, updateWc);
    state->additiveIncrease = alphaAi; // never leak the boost into fast feedback
    if (updateWc) {
        static const simsignal_t signal = cComponent::registerSignal("omegaAdditionalIncrease");
        conn->emit(signal, extraAi);
    }
    return window;
}
} }
