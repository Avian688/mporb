// SPDX-License-Identifier: LGPL-3.0-or-later
#include "MpOrbConnection.h"
#include "flavours/MpOrbOmega.h"
#include "../../../../orbtcp/src/common/OmegaPrice.h"

#include <algorithm>
#include <cmath>

namespace inet { namespace tcp {

MpOrbConnection::~MpOrbConnection()
{
    if (omegaTimer != nullptr)
        cancelAndDelete(omegaTimer);
}

void MpOrbConnection::prepareForRemoval()
{
    Enter_Method_Silent("prepareForRemoval");
    if (omegaTimer != nullptr && omegaTimer->isScheduled())
        cancelEvent(omegaTimer);
    MpTcpConnection::prepareForRemoval();
}

void MpOrbConnection::startOmegaControl()
{
    Enter_Method_Silent("startOmegaControl");
    if (omegaTimer != nullptr || teardownInProgress)
        return;
    omegaInterval = tcpMain->par("omegaControlInterval");
    omegaRateScale = tcpMain->par("omegaRateScale").doubleValue() / 8;
    omegaRateGain = tcpMain->par("omegaRateGain");
    omegaMinRtt = tcpMain->par("omegaMinRtt");
    if (omegaInterval <= SIMTIME_ZERO || !std::isfinite(omegaMinRtt) ||
            omegaMinRtt < omegaInterval.dbl() || !std::isfinite(omegaRateScale) ||
            omegaRateScale <= 0 || !std::isfinite(omegaRateGain) || omegaRateGain <= 0)
        throw cRuntimeError("Invalid Omega meta-controller parameters");
    omegaTimer = new cMessage("Omega meta rate update");
    omegaLastUpdate = simTime();
    scheduleAt(simTime() + omegaInterval, omegaTimer);
}

bool MpOrbConnection::processTimer(cMessage *msg)
{
    if (msg != omegaTimer)
        return MpTcpConnection::processTimer(msg);
    if (!teardownInProgress) {
        updateOmegaRates();
        scheduleAt(simTime() + omegaInterval, omegaTimer);
    }
    return true;
}

void MpOrbConnection::updateOmegaRates()
{
    struct Path {
        SubflowConnection *connection;
        MpOrbOmega *algorithm;
        double rtt;
        double next;
    };
    std::vector<Path> paths;
    double totalRate = 0;
    double referenceRtt = omegaMinRtt;
    bool demand = getBytesAvailable() > 0;
    const double dt = (simTime() - omegaLastUpdate).dbl();
    omegaLastUpdate = simTime();

    // Snapshot all current targets before changing any of them. X is endogenous.
    for (auto *subflow : getSubflows()) {
        auto *algorithm = dynamic_cast<MpOrbOmega *>(subflow->getTcpAlgorithm());
        if (algorithm == nullptr || !subflow->isTransportActiveForScheduler())
            continue;
        const double rtt = algorithm->state->srtt > SIMTIME_ZERO ?
                algorithm->state->srtt.dbl() : omegaMinRtt;
        paths.push_back({subflow, algorithm, rtt, algorithm->targetRate});
        totalRate += algorithm->targetRate;
        referenceRtt = std::max(referenceRtt, rtt);
        demand = demand || subflow->getSchedulerUnsentBytes() > 0;
    }
    if (totalRate <= 0 || dt <= 0)
        return;

    double newTotal = 0;
    for (auto& path : paths) {
        auto *algorithm = path.algorithm;
        const double probeRate = algorithm->state->snd_mss / path.rtt;
        const double freshness = 4 * std::max(omegaMinRtt, path.rtt);
        const bool fresh = algorithm->hasPrice &&
                (simTime() - algorithm->priceSampleTime).dbl() <= freshness &&
                (simTime() - algorithm->priceReceivedTime).dbl() <= freshness;
        const bool recovering = algorithm->state->lossRecovery || algorithm->state->afterRto ||
                simTime() < algorithm->recoveryUntil || path.connection->isSchedulerStale();
        if (!fresh)
            path.next = algorithm->hasPrice ? probeRate : algorithm->targetRate;
        else {
            path.next = omega::nextRate(algorithm->targetRate, totalRate, algorithm->pathPrice,
                    omegaRateScale, omegaRateGain, dt, referenceRtt, probeRate, algorithm->capacity,
                    paths.size());
            if (!demand || recovering)
                path.next = std::min(path.next, algorithm->targetRate);
        }
        newTotal += path.next;

        // Achieved transport delivery, including SACK accounting. Diagnostic only:
        // do not mistake scheduler limitation or HoL for a new fairness entitlement.
        const uint32_t delivered = path.connection->getDelivered();
        const double sampleRate = static_cast<uint32_t>(delivered - algorithm->previousDelivered) / dt;
        algorithm->previousDelivered = delivered;
        const double g = 1 - std::exp(-dt / referenceRtt);
        algorithm->deliveredRate += g * (sampleRate - algorithm->deliveredRate);
        static const simsignal_t deliverySignal = registerSignal("omegaDeliveryRate");
        static const simsignal_t fairSignal = registerSignal("omegaFairShareReference");
        static const simsignal_t freshSignal = registerSignal("omegaPriceFresh");
        path.connection->emit(deliverySignal, 8 * algorithm->deliveredRate);
        path.connection->emit(fairSignal, 8 * algorithm->fairRate);
        path.connection->emit(freshSignal, fresh);
    }

    // Install together, then wake the existing paced transport/scheduler.
    for (auto& path : paths) {
        cMethodCallContextSwitcher context(path.connection);
        path.algorithm->installTargetRate(path.next);
    }
    static const simsignal_t totalSignal = registerSignal("omegaConnectionTargetRate");
    emit(totalSignal, 8 * newTotal);
    for (auto& path : paths) {
        cMethodCallContextSwitcher context(path.connection);
        path.connection->sendPendingData();
    }
}
} } // namespace inet::tcp
