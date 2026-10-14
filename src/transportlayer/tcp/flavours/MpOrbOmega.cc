// SPDX-License-Identifier: LGPL-3.0-or-later
#include "MpOrbOmega.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace inet { namespace tcp {
Register_Class(MpOrbOmega);

uint32_t MpOrbOmega::computeWnd(double u, bool updateWc)
{
    const uint32_t alphaAi = state->additiveIncrease;
    // Fast ACKs and startup use Alpha unchanged. B/N is in payload bytes/s;
    // use the same RTT as OrbCC's AI calculation to convert rate to bytes.
    if (updateWc && !firstRTT && !state->initialPhase &&
            std::isfinite(u) && u >= 0 && u < 0.9 &&
            state->sharingFlows > 0 && rtt > SIMTIME_ZERO) {
        const double fastAi = 0.2 * (static_cast<double>(state->bottBW) /
                state->sharingFlows) * (0.95 - u) * rtt.dbl();
        state->additiveIncrease = static_cast<uint32_t>(std::min(
                std::max(static_cast<double>(alphaAi), fastAi),
                static_cast<double>(std::numeric_limits<uint32_t>::max())));
    }
    const uint32_t extraAi = state->additiveIncrease - alphaAi;
    // Parent retains the growth gate, local MD, fairness correction and floor.
    const uint32_t window = MpOrbSenderDelta::computeWnd(u, updateWc);
    state->additiveIncrease = alphaAi; // never leak the boost into fast feedback
    if (updateWc) {
        static const simsignal_t signal = cComponent::registerSignal("omegaAdditionalIncrease");
        conn->emit(signal, extraAi);
    }
    return window;
}
} }
