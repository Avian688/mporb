// SPDX-License-Identifier: LGPL-3.0-or-later
#include "MpOrbSemiCoupledDelta.h"
#include "../../../../../mptcp/src/transportlayer/tcp/MpTcpConnection.h"

#include <algorithm>
#include <cmath>

namespace inet {
namespace tcp {

Register_Class(MpOrbSemiCoupledDelta);

simsignal_t MpOrbSemiCoupledDelta::eligibleSubflowsSignal = cComponent::registerSignal("mpOrbDeltaEligibleSubflows");
simsignal_t MpOrbSemiCoupledDelta::weightSignal = cComponent::registerSignal("mpOrbDeltaWeight");
simsignal_t MpOrbSemiCoupledDelta::uncoupledAiSignal = cComponent::registerSignal("mpOrbDeltaUncoupledAi");
simsignal_t MpOrbSemiCoupledDelta::weightedAiSignal = cComponent::registerSignal("mpOrbDeltaWeightedAi");

void MpOrbSemiCoupledDelta::adjustAdditiveIncrease()
{
    if (state == nullptr || firstRTT || state->initialPhase || state->srtt <= SIMTIME_ZERO)
        return; // Preserve OrbCC's startup rule.

    auto *meta = getMetaConnection();
    if (meta == nullptr)
        return;

    double connectionRate = 0, ownRate = 0;
    uint32_t eligibleSubflows = 0;
    for (auto *subflow : meta->getSubflows()) {
        // Keep Alpha's pool, counting exactly the rates in the denominator.
        if (subflow == nullptr ||
                (subflow->getFsmState() != TCP_S_ESTABLISHED &&
                 subflow->getFsmState() != TCP_S_CLOSE_WAIT) ||
                dynamic_cast<MpOrbSemiCoupledAlpha *>(subflow->getTcpAlgorithm()) == nullptr)
            continue;
        const auto *s = static_cast<const OrbtcpStateVariables *>(subflow->getState());
        if (s == nullptr || s->srtt <= SIMTIME_ZERO)
            continue;
        const double rate = s->snd_cwnd / s->srtt.dbl();
        if (!std::isfinite(rate) || rate <= 0)
            continue;
        connectionRate += rate;
        ++eligibleSubflows;
        if (subflow == conn)
            ownRate = rate;
    }
    if (!std::isfinite(connectionRate) || connectionRate <= 0 || ownRate <= 0)
        return;

    const double rateShare = ownRate / connectionRate;
    const double weight = std::min(1.0, eligibleSubflows * rateShare);
    const uint32_t uncoupledAi = state->additiveIncrease;
    if (uncoupledAi > 0) {
        // Use the parent's uncoupled AI directly, not Alpha's already-scaled AI
        // or its experimental boost. Retain fractional-byte credit as in Alpha.
        const double exactAi = uncoupledAi * weight + additiveIncreaseResidual;
        state->additiveIncrease = static_cast<uint32_t>(exactAi);
        additiveIncreaseResidual = exactAi - state->additiveIncrease;
    }

    conn->emit(subflowRateSignal, ownRate);
    conn->emit(connectionRateSignal, connectionRate);
    conn->emit(rateShareSignal, rateShare);
    conn->emit(eligibleSubflowsSignal, static_cast<unsigned long>(eligibleSubflows));
    conn->emit(weightSignal, weight);
    conn->emit(uncoupledAiSignal, static_cast<unsigned long>(uncoupledAi));
    conn->emit(weightedAiSignal, static_cast<unsigned long>(state->additiveIncrease));
}

} // namespace tcp
} // namespace inet
