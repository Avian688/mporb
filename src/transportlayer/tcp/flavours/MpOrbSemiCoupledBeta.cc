// SPDX-License-Identifier: LGPL-3.0-or-later
#include "MpOrbSemiCoupledBeta.h"
#include "../../../../../mptcp/src/transportlayer/tcp/MpTcpConnection.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace inet {
namespace tcp {

Register_Class(MpOrbSemiCoupledBeta);

simsignal_t MpOrbSemiCoupledBeta::weightSignal = cComponent::registerSignal("mpOrbBetaWeight");
simsignal_t MpOrbSemiCoupledBeta::correctionSignal = cComponent::registerSignal("mpOrbBetaWeightCorrection");
simsignal_t MpOrbSemiCoupledBeta::smoothedUSignal = cComponent::registerSignal("mpOrbBetaSmoothedU");
simsignal_t MpOrbSemiCoupledBeta::errorSignal = cComponent::registerSignal("mpOrbBetaUtilizationError");
simsignal_t MpOrbSemiCoupledBeta::currentRateShareSignal = cComponent::registerSignal("mpOrbBetaRateShare");
simsignal_t MpOrbSemiCoupledBeta::baselineShareSignal = cComponent::registerSignal("mpOrbBetaBaselineShare");
simsignal_t MpOrbSemiCoupledBeta::activeSignal = cComponent::registerSignal("mpOrbBetaRedistributionActive");
simsignal_t MpOrbSemiCoupledBeta::alphaAiSignal = cComponent::registerSignal("mpOrbBetaAlphaAi");
simsignal_t MpOrbSemiCoupledBeta::uncoupledAiSignal = cComponent::registerSignal("mpOrbBetaUncoupledAi");
simsignal_t MpOrbSemiCoupledBeta::weightedAiSignal = cComponent::registerSignal("mpOrbBetaWeightedAi");

void MpOrbSemiCoupledBeta::initialize()
{
    MpOrbSemiCoupledAlpha::initialize();
    redistributionGain = conn->getTcpMain()->par("mpOrbBetaRedistributionGain").doubleValue();
    utilizationDeadband = conn->getTcpMain()->par("mpOrbBetaUtilizationDeadband").doubleValue();
    smoothingRtts = conn->getTcpMain()->par("mpOrbBetaSmoothingRtts").doubleValue();
    if (!std::isfinite(redistributionGain) || redistributionGain < 0 || redistributionGain > 1 ||
            !std::isfinite(utilizationDeadband) || utilizationDeadband < 0 ||
            !std::isfinite(smoothingRtts) || smoothingRtts <= 0 ||
            !std::isfinite(state->eta) || state->eta <= 0)
        throw cRuntimeError("MpOrb Beta requires finite gain in [0,1], deadband >= 0, smoothing RTTs > 0 and eta > 0");
}

bool MpOrbSemiCoupledBeta::hasFreshFeedback() const
{
    return hasFeedback && state->srtt > SIMTIME_ZERO && sampleTime <= simTime() &&
            simTime() - sampleTime <= 2 * state->srtt && std::isfinite(smoothedU) && smoothedU > 0;
}

void MpOrbSemiCoupledBeta::invalidateFeedback()
{
    hasFeedback = false;
    ++feedbackEpoch;
    roundMembers.clear();
    roundActive = false;
}

double MpOrbSemiCoupledBeta::measureInflight(const IntDataVec& intData)
{
    if (intData.empty())
        return 0;
    const auto timestamp = intData.front().getTs();
    if (hasFeedback && timestamp < sampleTime)
        return 0; // An older ACK must not replace the accepted U observation.
    if (timestamp < SIMTIME_ZERO || timestamp > simTime() ||
            (state->srtt > SIMTIME_ZERO && simTime() - timestamp > 2 * state->srtt)) {
        invalidateFeedback();
        return 0;
    }
    pendingSampleTime = timestamp;
    const bool hadDigest = hasPathDigest;
    const auto previousDigest = lastPathDigest;
    // The PINT parent validates/decodes B,N,U before invoking our AI hook.
    const double u = MpOrbSemiCoupledAlpha::measureInflight(intData);
    if (hadDigest && lastPathDigest != previousDigest) {
        invalidateFeedback();
        return 0; // Do not apply the previous route's U on a route-change ACK.
    }
    return u;
}

void MpOrbSemiCoupledBeta::adjustAdditiveIncrease()
{
    // Average the accepted, decoded U in measurement time (not ACK count).
    // This only affects coupling; OrbCC still reacts to the original U.
    if (!hasFreshFeedback()) {
        smoothedU = state->u;
        ++feedbackEpoch;
    }
    else {
        const double elapsed = (pendingSampleTime - sampleTime).dbl();
        const double gain = std::clamp(elapsed / (smoothingRtts * state->srtt.dbl()), 0.0, 1.0);
        smoothedU += gain * (state->u - smoothedU);
    }
    sampleTime = pendingSampleTime;
    hasFeedback = true;
    conn->emit(smoothedUSignal, smoothedU);
    if (firstRTT || state->initialPhase || state->srtt <= SIMTIME_ZERO)
        return; // Keep OrbCC's startup rule.

    auto *meta = getMetaConnection();
    if (meta == nullptr)
        return;

    struct Path {
        MpOrbSemiCoupledBeta *beta;
        double rate, error, weight;
    };
    std::vector<Path> paths;
    std::vector<std::pair<int, uint64_t>> members;
    double connectionRate = 0, ownRate = 0, meanError = 0;
    simtime_t controlRtt = SIMTIME_ZERO;
    bool ready = true;
    for (auto *subflow : meta->getSubflows()) {
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
        if (subflow == conn)
            ownRate = rate;

        auto *beta = dynamic_cast<MpOrbSemiCoupledBeta *>(subflow->getTcpAlgorithm());
        paths.push_back({beta, rate, 0, 0});
        // The entire Alpha rate pool must support the same valid U correction.
        if (beta == nullptr || beta->redistributionGain != redistributionGain ||
                beta->utilizationDeadband != utilizationDeadband || beta->smoothingRtts != smoothingRtts ||
                s->eta != state->eta || beta->firstRTT || s->initialPhase || s->lossRecovery ||
                s->afterRto || !beta->hasFreshFeedback()) {
            ready = false;
            continue;
        }
        const double deviation = beta->smoothedU - s->eta;
        if (deviation > utilizationDeadband)
            paths.back().error = deviation - utilizationDeadband;
        else if (deviation < -utilizationDeadband)
            paths.back().error = deviation + utilizationDeadband;
        meanError += paths.back().error;
        controlRtt = std::max(controlRtt, s->srtt);
        members.emplace_back(subflow->getId(), beta->feedbackEpoch);
    }
    if (!std::isfinite(connectionRate) || connectionRate <= 0 || ownRate <= 0)
        return;

    const double rateShare = ownRate / connectionRate;
    ready = ready && std::isfinite(meanError);
    if (ready && (members != roundMembers || simTime() >= roundUntil)) {
        // Recompute from current Alpha shares, never from the previous weights.
        meanError /= paths.size();
        double totalWeight = 0;
        const bool active = redistributionGain > 0 && std::any_of(paths.begin(), paths.end(),
                [&](const Path& path) { return path.error != paths.front().error; });
        for (auto& path : paths) {
            const double correction = active ? redistributionGain * (meanError - path.error) : 0;
            path.weight = std::max(0.0, path.rate / connectionRate + correction);
            totalWeight += path.weight;
        }
        ready = std::isfinite(totalWeight) && totalWeight > 0;
        if (ready) {
            // Install one coherent snapshot for all siblings. No new timer or
            // coordinator is needed: the first eligible ACK refreshes the set.
            for (const auto& path : paths) {
                path.beta->roundMembers = members;
                path.beta->roundUntil = simTime() + controlRtt;
                path.beta->roundWeight = path.weight / totalWeight;
                path.beta->roundBaseline = path.rate / connectionRate;
                path.beta->roundError = path.error;
                path.beta->roundActive = active;
            }
        }
    }
    if (!ready) {
        for (const auto& path : paths) {
            if (path.beta != nullptr) {
                path.beta->roundMembers.clear();
                path.beta->roundActive = false;
            }
        }
    }

    const bool active = ready && roundActive;
    // When correction is inactive, use LIVE plain Alpha shares, not frozen ones.
    const double weight = active ? roundWeight : rateShare;
    const double baseline = active ? roundBaseline : rateShare;
    const uint32_t uncoupledAi = state->additiveIncrease;
    const uint32_t alphaAi = static_cast<uint32_t>(std::min(static_cast<double>(uncoupledAi),
            uncoupledAi * rateShare + additiveIncreaseResidual));
    if (uncoupledAi > 0) {
        // Fractional AI credit is consumed once; Beta never calls Alpha's boost.
        const double weightedAi = uncoupledAi * weight;
        state->additiveIncrease = static_cast<uint32_t>(weightedAi);
        additiveIncreaseResidual += weightedAi - state->additiveIncrease;
        if (additiveIncreaseResidual >= 1 && state->additiveIncrease < uncoupledAi) {
            ++state->additiveIncrease;
            additiveIncreaseResidual -= 1;
        }
    }

    conn->emit(subflowRateSignal, ownRate);
    conn->emit(connectionRateSignal, connectionRate);
    conn->emit(rateShareSignal, rateShare);
    conn->emit(currentRateShareSignal, rateShare);
    conn->emit(baselineShareSignal, baseline);
    conn->emit(errorSignal, ready ? roundError : std::numeric_limits<double>::quiet_NaN());
    conn->emit(correctionSignal, weight - baseline);
    conn->emit(weightSignal, weight);
    conn->emit(activeSignal, active);
    conn->emit(alphaAiSignal, static_cast<unsigned long>(alphaAi));
    conn->emit(uncoupledAiSignal, static_cast<unsigned long>(uncoupledAi));
    conn->emit(weightedAiSignal, static_cast<unsigned long>(state->additiveIncrease));
}

void MpOrbSemiCoupledBeta::processRexmitTimer(TcpEventCode& event)
{
    invalidateFeedback();
    MpOrbSemiCoupledAlpha::processRexmitTimer(event);
}

void MpOrbSemiCoupledBeta::rackLossDetected()
{
    invalidateFeedback();
    MpOrbSemiCoupledAlpha::rackLossDetected();
}

} // namespace tcp
} // namespace inet
