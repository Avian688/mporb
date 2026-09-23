// SPDX-License-Identifier: LGPL-3.0-or-later
#include "MpOrbPressure.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "MpOrbPressurePolicy.h"
#include "../MpOrbConnection.h"
#include "../MpOrbSubflowConnection.h"
#include "../../../../../orbtcp/src/common/PintFlowCount.h"

namespace inet {
namespace tcp {

Register_Class(MpOrbPressure);

simsignal_t MpOrbPressure::pressureSignal = cComponent::registerSignal("mpOrbPressure");
simsignal_t MpOrbPressure::weightSignal = cComponent::registerSignal("mpOrbPressureWeight");
simsignal_t MpOrbPressure::fairRateSignal = cComponent::registerSignal("mpOrbPressureFairRate");
simsignal_t MpOrbPressure::aiScaleSignal = cComponent::registerSignal("mpOrbPressureAiScale");
simsignal_t MpOrbPressure::connectionAiRateSignal = cComponent::registerSignal("mpOrbPressureConnectionAiRate");
simsignal_t MpOrbPressure::freshSubflowsSignal = cComponent::registerSignal("mpOrbPressureFreshSubflows");
simsignal_t MpOrbPressure::uncoupledAiSignal = cComponent::registerSignal("mpOrbPressureUncoupledAi");
simsignal_t MpOrbPressure::weightedAiSignal = cComponent::registerSignal("mpOrbPressureWeightedAi");
simsignal_t MpOrbPressure::subflowRateSignal = cComponent::registerSignal("mpOrbPressureSubflowRate");
simsignal_t MpOrbPressure::connectionRateSignal = cComponent::registerSignal("mpOrbPressureConnectionRate");
simsignal_t MpOrbPressure::reentryBoostSignal = cComponent::registerSignal("mpOrbPressureReentryBoost");
simsignal_t MpOrbPressure::windowDeltaSignal = cComponent::registerSignal("mpOrbPressureWindowDelta");
simsignal_t MpOrbPressure::extraWithdrawalSignal = cComponent::registerSignal("mpOrbPressureExtraWithdrawalBytes");

void MpOrbPressure::initialize()
{
    OrbtcpPintFlavour::initialize();
    auto *tcp = conn->getTcpMain();
    decreaseGain = tcp->par("mpOrbPressureDecreaseGain").doubleValue();
    maxDecreaseFraction = tcp->par("mpOrbPressureMaxDecreaseFraction").doubleValue();
    probeIntervalRtts = tcp->par("mpOrbPressureProbeIntervalRtts").intValue();
    if (!std::isfinite(decreaseGain) || decreaseGain < 1 ||
            !std::isfinite(maxDecreaseFraction) || maxDecreaseFraction <= 0 || maxDecreaseFraction > 1 ||
            probeIntervalRtts < 1)
        throw cRuntimeError("MpOrbPressure requires decreaseGain >= 1, maxDecreaseFraction in (0, 1], and probeIntervalRtts >= 1");
}

MpOrbConnection *MpOrbPressure::getPressureMetaConnection() const
{
    const auto *subflow = dynamic_cast<MpOrbSubflowConnection *>(conn);
    return subflow != nullptr ? dynamic_cast<MpOrbConnection *>(subflow->getMetaConnection()) : nullptr;
}

bool MpOrbPressure::isPressureCouplingReady() const
{
    return state != nullptr && !firstRTT && !state->initialPhase &&
            !state->lossRecovery && state->srtt > SIMTIME_ZERO;
}

double MpOrbPressure::getPressureRateEstimate() const
{
    if (state == nullptr)
        return 0;
    // A committed window avoids feeding fast, uncommitted ACK reactions back
    // into their own allocation. Application goodput would collapse under HoL.
    const double window = state->prevWnd > 0 ? state->prevWnd : state->snd_cwnd;
    const simtime_t controllerRtt = rtt > SIMTIME_ZERO ? rtt : state->srtt;
    return mporbpressure::rateEstimate(window, controllerRtt.dbl());
}

void MpOrbPressure::forgetPressureFeedback()
{
    if (auto *meta = getPressureMetaConnection())
        meta->forgetPressureFeedback(check_and_cast<MpOrbSubflowConnection *>(conn));
    additiveIncreaseResidual = 0;
    couplingActive = false;
    previousFairRate = 0;
    fairRateImprovedUntil = SIMTIME_ZERO;
    reentryUntil = SIMTIME_ZERO;
    nextReentryAllowed = SIMTIME_ZERO;
}

double MpOrbPressure::measureInflight(const IntDataVec& intData)
{
    if (intData.empty())
        return 0;
    const auto& feedback = intData.front();
    const auto flows = pint::decodeFlowCount(feedback.getPintTotalFlowCountCode(),
            pintFlowCountBits, pintMaxFlowCount);
    if (!mporbpressure::positiveFinite(feedback.getPintUtilization()) ||
            !mporbpressure::positiveFinite(mporbpressure::fairRate(feedback.getB(), flows)) ||
            feedback.getTs() < SIMTIME_ZERO || feedback.getTs() > simTime() ||
            (hasSampleTime && feedback.getTs() < lastSampleTime))
        return 0;
    if (state->srtt > SIMTIME_ZERO && simTime() - feedback.getTs() > 2 * state->srtt)
        return 0;

    const bool previouslyHadDigest = hasPathDigest;
    const auto previousDigest = lastPathDigest;
    feedbackAccepted = false;
    feedbackBeingProcessed = &feedback;
    const double utilization = OrbtcpPintFlavour::measureInflight(intData);
    feedbackBeingProcessed = nullptr;

    if (previouslyHadDigest && lastPathDigest != previousDigest) {
        // The inherited PINT path-change branch returns the previous U without
        // installing this ACK's bottleneck. Skip that control update entirely.
        forgetPressureFeedback();
        hasSampleTime = true;
        lastSampleTime = feedback.getTs();
        return 0;
    }
    if (feedbackAccepted) {
        hasSampleTime = true;
        lastSampleTime = feedback.getTs();
    }
    return utilization;
}

void MpOrbPressure::adjustAdditiveIncrease()
{
    if (state == nullptr || feedbackBeingProcessed == nullptr)
        return;
    feedbackAccepted = true;
    auto *meta = getPressureMetaConnection();
    if (meta == nullptr)
        throw cRuntimeError("MpOrbPressure requires MpOrbConnection");
    auto *subflow = check_and_cast<MpOrbSubflowConnection *>(conn);
    meta->recordPressureFeedback(subflow, *feedbackBeingProcessed,
            pintFlowCountBits, pintMaxFlowCount);
    const auto allocation = meta->getPressureAllocation(subflow);
    const uint32_t uncoupledAi = state->additiveIncrease;
    uncoupledAdditiveIncrease = uncoupledAi;
    const bool ready = isPressureCouplingReady();
    couplingActive = ready && allocation.freshSubflows > 1 && allocation.subflowRate > 0;
    const double bandwidth = feedbackBeingProcessed->getB();
    const double nominalFairRate = mporbpressure::fairRate(bandwidth, state->sharingFlows);
    const simtime_t controllerRtt = rtt > SIMTIME_ZERO ? rtt : state->srtt;
    if (previousFairRate > 0 && nominalFairRate > 1.25 * previousFairRate && controllerRtt > SIMTIME_ZERO)
        fairRateImprovedUntil = simTime() + controllerRtt * probeIntervalRtts;
    previousFairRate = nominalFairRate;

    const bool weakPath = couplingActive && allocation.weight > 0 &&
            allocation.weight < 0.25 / allocation.freshSubflows;
    const bool pathImproved = (state->u > 0 && state->u < 0.9 * state->eta) ||
            simTime() < fairRateImprovedUntil;
    if (weakPath && pathImproved && simTime() >= nextReentryAllowed) {
        // A weak path with new opportunity gets one RTT of uncoupled AI, at
        // most once per configured interval. This never exceeds its base AI.
        reentryUntil = simTime() + controllerRtt;
        nextReentryAllowed = simTime() + controllerRtt * probeIntervalRtts;
        fairRateImprovedUntil = SIMTIME_ZERO;
    }
    const bool boost = couplingActive && simTime() < reentryUntil;
    const double scale = couplingActive && !boost ? allocation.weight : 1;
    state->additiveIncrease = mporbpressure::scaleAdditiveIncrease(uncoupledAi, scale,
            additiveIncreaseResidual);

    conn->emit(pressureSignal, mporbpressure::pressure(bandwidth, state->sharingFlows));
    conn->emit(fairRateSignal, nominalFairRate);
    conn->emit(weightSignal, ready ? allocation.weight : 1);
    conn->emit(aiScaleSignal, scale);
    conn->emit(connectionAiRateSignal, ready ?
            state->additiveIncreasePercent * allocation.weightedFairRate : 0);
    conn->emit(subflowRateSignal, allocation.subflowRate);
    conn->emit(connectionRateSignal, allocation.connectionRate);
    conn->emit(reentryBoostSignal, boost);
    conn->emit(freshSubflowsSignal, static_cast<unsigned long>(allocation.freshSubflows));
    conn->emit(uncoupledAiSignal, static_cast<unsigned long>(uncoupledAi));
    conn->emit(weightedAiSignal, static_cast<unsigned long>(state->additiveIncrease));
}

uint32_t MpOrbPressure::computeWnd(double u, bool updateWc)
{
    // Like OrbCC, every fast ACK starts from the last committed window.
    const double committedWindow = state->prevWnd > 0 ? state->prevWnd : state->snd_cwnd;
    const bool inInitialPhase = state->initialPhase && state->ssthresh > 0;
    const bool coupled = couplingActive && isPressureCouplingReady();
    double targetWindow;
    double extraWithdrawal = 0;

    if (inInitialPhase) {
        // Keep PINT's bounded startup rule, without a cwnd-limited growth gate.
        targetWindow = std::min(committedWindow + state->additiveIncrease,
                static_cast<double>(state->ssthresh));
    }
    else {
        // OrbCC: W_next = W * eta/U + AI when U >= eta, otherwise W + AI.
        // adjustAdditiveIncrease() has already applied Pressure's rate share.
        const double reducedWindow = u >= state->eta ?
                committedWindow * (state->eta / u) : committedWindow;
        targetWindow = reducedWindow + state->additiveIncrease;

        if (coupled) {
            const double coupledWindow = targetWindow;
            if (targetWindow < committedWindow) {
                // Gain 1 leaves the ordinary OrbCC decrease unchanged. A larger
                // gain accelerates withdrawal, subject to the extra-step bound.
                const double decrease = committedWindow - targetWindow;
                const double acceleratedWindow = committedWindow - decreaseGain * decrease;
                const double stepFloor = committedWindow * (1 - maxDecreaseFraction);
                targetWindow = std::min(targetWindow, std::max(acceleratedWindow, stepFloor));
            }

            // Leave two MSS for probing, but never exceed the window produced
            // by the same OrbCC equation with the full, uncoupled AI.
            const double uncoupledWindow = reducedWindow + uncoupledAdditiveIncrease;
            targetWindow = std::min(std::max(targetWindow, 2.0 * state->snd_mss), uncoupledWindow);
            extraWithdrawal = std::max(0.0, coupledWindow - targetWindow);
        }
    }

    const uint32_t targetWnd = !std::isfinite(targetWindow) || targetWindow <= 0 ? 0 :
            static_cast<uint32_t>(std::min(targetWindow, static_cast<double>(std::numeric_limits<uint32_t>::max())));
    conn->emit(windowDeltaSignal, static_cast<double>(targetWnd) - committedWindow);
    conn->emit(extraWithdrawalSignal, extraWithdrawal);

    // Pressure uses telemetry on each ACK without testing whether queued demand
    // fills cwnd. Commit once per react interval, as in OrbCC/PINT.
    if (updateWc) {
        updateWindow = false;
        state->prevWnd = targetWnd;
        if (state->initialPhase && state->ssthresh > 0 && targetWnd >= state->ssthresh)
            state->endInitialPhase = true;
        if (state->endInitialPhase) {
            state->initialPhase = false;
            state->endInitialPhase = false;
        }
        conn->emit(txRateSignal, state->txRate);
    }
    // The inherited ACK handler assigns snd_cwnd and then updates pacing.
    return targetWnd;
}

void MpOrbPressure::processRexmitTimer(TcpEventCode& event)
{
    forgetPressureFeedback();
    MpOrbUncoupled::processRexmitTimer(event);
}

void MpOrbPressure::rackLossDetected()
{
    forgetPressureFeedback();
    OrbtcpPintFlavour::rackLossDetected();
}

} // namespace tcp
} // namespace inet
