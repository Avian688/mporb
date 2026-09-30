// SPDX-License-Identifier: LGPL-3.0-or-later
#include "MpOrbU.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "../../../../../mptcp/src/transportlayer/tcp/MpTcpConnection.h"

namespace inet {
namespace tcp {

Register_Class(MpOrbU);

simsignal_t MpOrbU::smoothedUSignal = cComponent::registerSignal("mpOrbUSmoothedU");
simsignal_t MpOrbU::weightSignal = cComponent::registerSignal("mpOrbUWeight");
simsignal_t MpOrbU::freshSubflowsSignal = cComponent::registerSignal("mpOrbUFreshSubflows");
simsignal_t MpOrbU::uncoupledAiSignal = cComponent::registerSignal("mpOrbUUncoupledAi");
simsignal_t MpOrbU::weightedAiSignal = cComponent::registerSignal("mpOrbUWeightedAi");

void MpOrbU::initialize()
{
    OrbtcpPintFlavour::initialize();
    beta = conn->getTcpMain()->par("mpOrbUBeta").doubleValue();
    smoothingRtts = conn->getTcpMain()->par("mpOrbUSmoothingRtts").doubleValue();
    if (!std::isfinite(beta) || beta < 0 ||
            !std::isfinite(smoothingRtts) || smoothingRtts <= 0 ||
            !std::isfinite(state->eta) || state->eta <= 0)
        throw cRuntimeError("MpOrbU requires finite beta >= 0, smoothingRtts > 0 and eta > 0");
}

bool MpOrbU::hasFreshUtilization() const
{
    return hasUtilization && state->srtt > SIMTIME_ZERO &&
            utilizationSampleTime <= simTime() &&
            simTime() - utilizationSampleTime <= 2 * state->srtt &&
            std::isfinite(smoothedU / state->eta);
}

double MpOrbU::measureInflight(const IntDataVec& intData)
{
    if (intData.empty())
        return 0;
    const auto sampleTime = intData.front().getTs();
    if (sampleTime < SIMTIME_ZERO || sampleTime > simTime() ||
            (hasUtilization && sampleTime < utilizationSampleTime) ||
            (state->srtt > SIMTIME_ZERO && simTime() - sampleTime > 2 * state->srtt))
        return 0;

    pendingSampleTime = sampleTime;
    const bool hadDigest = hasPathDigest;
    const auto previousDigest = lastPathDigest;
    // The parent validates and samples PINT, then calls adjustAdditiveIncrease
    // only after installing the accepted U/B/N record.
    const double u = OrbtcpPintFlavour::measureInflight(intData);
    if (hadDigest && lastPathDigest != previousDigest) {
        hasUtilization = false;
        additiveIncreaseResidual = 0;
        // The parent's path-change branch returns the previous path's U.
        // Do not apply it to the new path; wait for its next accepted record.
        return 0;
    }
    return u;
}

void MpOrbU::adjustAdditiveIncrease()
{
    // Smooth in measurement time, not per ACK: ACK frequency must not set the
    // smoothing horizon. Reinitialise after a telemetry gap or route change.
    if (!hasFreshUtilization()) {
        smoothedU = state->u;
        additiveIncreaseResidual = 0;
    }
    else {
        const double elapsed = (pendingSampleTime - utilizationSampleTime).dbl();
        const double gain = -std::expm1(-elapsed / (smoothingRtts * state->srtt.dbl()));
        smoothedU += gain * (state->u - smoothedU);
    }
    utilizationSampleTime = pendingSampleTime;
    hasUtilization = true;
    conn->emit(smoothedUSignal, smoothedU);

    const uint32_t uncoupledAi = state->additiveIncrease;
    double weight = 1;
    std::vector<double> loads;
    auto *meta = getMetaConnection();
    // As in Alpha, leave startup uncoupled. No special growth boosts or window
    // withdrawal are introduced; this method only replaces the AI multiplier.
    if (meta != nullptr && !firstRTT && !state->initialPhase &&
            !state->lossRecovery && !state->afterRto && hasFreshUtilization()) {
        for (auto *subflow : meta->getSubflows()) {
            if (subflow == nullptr)
                continue;
            const int tcpState = subflow->getFsmState();
            auto *algorithm = dynamic_cast<MpOrbU *>(subflow->getTcpAlgorithm());
            if ((tcpState != TCP_S_ESTABLISHED && tcpState != TCP_S_CLOSE_WAIT) ||
                    algorithm == nullptr || algorithm->firstRTT ||
                    algorithm->state->initialPhase || algorithm->state->lossRecovery ||
                    algorithm->state->afterRto || !algorithm->hasFreshUtilization())
                continue;
            loads.push_back(algorithm->smoothedU / algorithm->state->eta);
        }
        if (!loads.empty()) {
            // Subtract the minimum before exponentiation: the lowest load has
            // score 1 even when all U values are large. This is the same softmax.
            const double minimumLoad = *std::min_element(loads.begin(), loads.end());
            double totalScore = 0;
            for (double load : loads)
                totalScore += std::exp(-beta * (load - minimumLoad));
            weight = std::exp(-beta * (smoothedU / state->eta - minimumLoad)) / totalScore;
            weight = std::clamp(weight, 0.0, 1.0);
        }
    }

    const double exactAi = uncoupledAi * weight + additiveIncreaseResidual;
    state->additiveIncrease = static_cast<uint32_t>(exactAi);
    // Fast ACK updates use the same committed window. Carry rounding credit
    // forward only on a window commit, rather than once per arriving ACK.
    if (updateWindow)
        additiveIncreaseResidual = exactAi - state->additiveIncrease;

    conn->emit(weightSignal, weight);
    conn->emit(freshSubflowsSignal, static_cast<unsigned long>(loads.size()));
    conn->emit(uncoupledAiSignal, static_cast<unsigned long>(uncoupledAi));
    conn->emit(weightedAiSignal, static_cast<unsigned long>(state->additiveIncrease));
}

} // namespace tcp
} // namespace inet
