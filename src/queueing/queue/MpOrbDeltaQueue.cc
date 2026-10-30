// SPDX-License-Identifier: LGPL-3.0-or-later
#include "MpOrbDeltaQueue.h"
#include "../../common/DeltaShuffleTag.h"
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
    deltaRateFloor = par("deltaRateFloor").doubleValue() / 8;
    if (deltaIntervalOverride < SIMTIME_ZERO || !std::isfinite(deltaShuffleFraction) ||
            deltaShuffleFraction < 0 || deltaShuffleFraction > 1 ||
            !std::isfinite(deltaRateFloor) || deltaRateFloor <= 0)
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
    if (tcpPayloadBytes == 0 || !tcpHeader->findTag<DeltaShuffleTag>())
        return;
    tcpHeader->addTagIfAbsent<DeltaShuffleTag>(); // Detach COW region tags.
    auto tag = tcpHeader->addTagIfAbsent<DeltaShuffleTag>();
    if (tag->echoed)
        return;
    if (selectedBottleneck) {
        // A new winner replaces earlier feedback, even if shuffle is disabled here.
        tag->hops = 0;
        tag->bottleneckId = getParentModule()->getParentModule()->getId();
        tag->meanConnectionRate = tag->rateAllowance = tag->packetMass = 0;
    }
    if (deltaShuffleFraction <= 0)
        return;
    if (!deltaShuffleTimer->isScheduled())
        startDeltaShuffleRound();
    if (!std::isfinite(tag->subflowRate) || tag->subflowRate < 0 ||
            !std::isfinite(tag->connectionRate) || tag->connectionRate < tag->subflowRate)
        throw cRuntimeError("Invalid Delta sender rate telemetry");

    // Observe every tagged data packet; inverse nominal rate compensates packet
    // frequency. The round duration is frozen at its boundary.
    const double mass = tcpPayloadBytes /
            (std::max(tag->subflowRate, deltaRateFloor) * deltaInterval.dbl());
    deltaShuffle.observe(mass, tag->connectionRate);
    if (selectedBottleneck) {
        tag->packetMass = mass;
        tag->meanConnectionRate = deltaShuffle.previousMeanRate;
        tag->rateAllowance = deltaShuffle.previousMass > 0 ?
                deltaShuffleFraction * bandwidthBytesPerSecond / deltaShuffle.previousMass : 0;
        tag->hops = 1;
        static const simsignal_t meanSignal = registerSignal("mpOrbDeltaMeanConnectionRate");
        static const simsignal_t allowanceSignal = registerSignal("mpOrbDeltaRateAllowance");
        cSimpleModule::emit(meanSignal, 8 * tag->meanConnectionRate);
        cSimpleModule::emit(allowanceSignal, 8 * tag->rateAllowance);
    }
}
} }
