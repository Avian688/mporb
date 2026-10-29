// SPDX-License-Identifier: LGPL-3.0-or-later
#include "MpOrbDeltaQueue.h"
#include "../../../../orbtcp/src/common/DeltaShuffleTag.h"
#include <algorithm>
#include <cmath>
namespace inet {
Register_Class(DeltaShuffleTag);
namespace queueing {
Define_Module(MpOrbDeltaQueue);
MpOrbDeltaQueue::~MpOrbDeltaQueue()
{
    if (deltaShuffleTimer != nullptr)
        cancelAndDelete(deltaShuffleTimer);
}
void MpOrbDeltaQueue::initialize(int stage)
{
    PintQueue::initialize(stage);
    if (stage != INITSTAGE_LOCAL)
        return;
    deltaIntervalOverride = par("deltaShuffleInterval");
    deltaShuffleFraction = par("deltaShuffleFraction");
    deltaNeedGain = par("deltaNeedGain");
    deltaRateFloor = par("deltaRateFloor").doubleValue() / 8;
    if (deltaIntervalOverride < SIMTIME_ZERO || !std::isfinite(deltaShuffleFraction) ||
            deltaShuffleFraction < 0 || deltaShuffleFraction > 1 ||
            !std::isfinite(deltaRateFloor) || deltaRateFloor <= 0 ||
            !std::isfinite(deltaNeedGain) || deltaNeedGain < 0)
        throw cRuntimeError("Invalid Delta shuffle parameters");
    if (deltaShuffleFraction > 0)
        deltaShuffleTimer = new cMessage("Delta shuffle round");

}
void MpOrbDeltaQueue::startDeltaShuffleRound()
{
    // Sample the latest queue estimate once. Mid-round RTT updates never move
    // this deadline or change the normalisation of packets in the current round.
    deltaInterval = deltaIntervalOverride > SIMTIME_ZERO ? deltaIntervalOverride :
            (avgRtt > SIMTIME_ZERO ? avgRtt :
             (fixedAvgRtt > SIMTIME_ZERO ? fixedAvgRtt : pintInitialRtt));
    deltaShuffle.beginRound(deltaShuffle.epoch + 1);
    scheduleAt(simTime() + deltaInterval, deltaShuffleTimer);
    static const simsignal_t intervalSignal = registerSignal("mpOrbDeltaRoundInterval");
    cSimpleModule::emit(intervalSignal, deltaInterval);
}

void MpOrbDeltaQueue::handleMessage(cMessage *message)
{
    if (message == deltaShuffleTimer) {
        if (deltaShuffle.mass > 0)
            startDeltaShuffleRound();
        else
            // One complete idle round: expire old averages and stop the timer.
            // The next Delta data packet starts a fresh observation round.
            deltaShuffle.beginRound(deltaShuffle.epoch + 1);
    }

    else
        PintQueue::handleMessage(message);
}
void MpOrbDeltaQueue::processTelemetryExtension(const Ptr<tcp::TcpHeader>& tcpHeader,
        uint64_t tcpPayloadBytes, double bandwidthBytesPerSecond, bool selectedBottleneck)
{
    if (tcpPayloadBytes > 0 && tcpHeader->findTag<DeltaShuffleTag>()) {
        tcpHeader->addTagIfAbsent<DeltaShuffleTag>(); // Detach COW region tags.
        auto tag = tcpHeader->addTagIfAbsent<DeltaShuffleTag>();
        if (!tag->echoed && selectedBottleneck) {
            tag->need = tag->rawShuffle = tag->shuffle = 0;
            tag->hops = 0;
            tag->bottleneckId = getParentModule()->getParentModule()->getId();
            tag->measurements.clear();
            tag->meanConnectionRate = tag->rateAllowance = tag->packetMass = 0;
        }
        if (!tag->echoed && deltaShuffleFraction > 0) {
            if (!deltaShuffleTimer->isScheduled())
                startDeltaShuffleRound();
            if (!std::isfinite(tag->subflowRate) || tag->subflowRate < 0 ||
                    !std::isfinite(tag->connectionRate) || tag->connectionRate < tag->subflowRate)
                throw cRuntimeError("Invalid Delta sender rate telemetry");
            const double need = 1.0 / std::max(tag->connectionRate, deltaRateFloor);
            // L/x compensates packet frequency; dividing by this round's
            // frozen duration keeps sample totals comparable when RTT changes.
            const double mass = tcpPayloadBytes /
                    (std::max(tag->subflowRate, deltaRateFloor) * deltaInterval.dbl());
            // Observe X for all tagged participants, including mixed Delta/Theta runs.
            deltaShuffle.rateMass += mass * tag->connectionRate;
            if (tag->senderControlled) {
                const auto feedback = deltaShuffle.measure(deltaShuffle.epoch,
                        deltaShuffleFraction * bandwidthBytesPerSecond, mass, need, deltaNeedGain);
                if (selectedBottleneck) {
                    if (tag->rateControlled) {
                        tag->packetMass = mass;
                        tag->meanConnectionRate = deltaShuffle.previousMeanRate;
                        tag->rateAllowance = deltaShuffle.previousMass > 0 ?
                                deltaShuffleFraction * bandwidthBytesPerSecond / deltaShuffle.previousMass : 0;
                        static const simsignal_t meanRateSignal = registerSignal("mpOrbThetaMeanConnectionRate");
                        static const simsignal_t allowanceSignal = registerSignal("mpOrbThetaRateAllowance");
                        cSimpleModule::emit(meanRateSignal, 8 * tag->meanConnectionRate);
                        cSimpleModule::emit(allowanceSignal, 8 * tag->rateAllowance);
                    }
                    else {
                        tag->measurements.push_back({mass, need, feedback.meanNeed, feedback.coefficient});
                        tag->need = need;
                    }
                    tag->hops = 1;
                }
                static const simsignal_t meanSignal = registerSignal("mpOrbDeltaMeanNeed");
                static const simsignal_t coefficientSignal = registerSignal("mpOrbDeltaNeedCoefficient");
                cSimpleModule::emit(meanSignal, feedback.meanNeed);
                cSimpleModule::emit(coefficientSignal, feedback.coefficient);
            }
            else {
                const auto feedback = deltaShuffle.issue(deltaShuffle.epoch,
                        deltaShuffleFraction * bandwidthBytesPerSecond, mass, need, deltaNeedGain, selectedBottleneck);
                if (selectedBottleneck) {
                    tag->need = need;
                    tag->rawShuffle = feedback.raw;
                    tag->shuffle = feedback.returned;
                    tag->hops = 1;
                }
                static const simsignal_t xSignal = registerSignal("mpOrbDeltaSubflowRate");
                static const simsignal_t totalSignal = registerSignal("mpOrbDeltaConnectionRate");
                static const simsignal_t needSignal = registerSignal("mpOrbDeltaNeed");
                static const simsignal_t meanSignal = registerSignal("mpOrbDeltaMeanNeed");
                static const simsignal_t rawSignal = registerSignal("mpOrbDeltaRawShuffle");
                static const simsignal_t shuffleSignal = registerSignal("mpOrbDeltaReturnedShuffle");
                static const simsignal_t netSignal = registerSignal("mpOrbDeltaRoundShuffle");
                static const simsignal_t magnitudeSignal = registerSignal("mpOrbDeltaRoundShuffleMagnitude");
                cSimpleModule::emit(xSignal, 8 * tag->subflowRate);
                cSimpleModule::emit(totalSignal, 8 * tag->connectionRate);
                cSimpleModule::emit(needSignal, need);
                cSimpleModule::emit(meanSignal, feedback.meanNeed);
                cSimpleModule::emit(rawSignal, 8 * feedback.raw);
                cSimpleModule::emit(shuffleSignal, 8 * feedback.returned);
                cSimpleModule::emit(netSignal, 8 * deltaShuffle.net);
                cSimpleModule::emit(magnitudeSignal, 8 * deltaShuffle.magnitude);
            }
        }
    }
}
} }
