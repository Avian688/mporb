// SPDX-License-Identifier: LGPL-3.0-or-later
#include "MpOrbIntScheduler.h"
#include "MpOrbConnection.h"
#include "MpOrbSubflowConnection.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace inet {
namespace tcp {

MpOrbIntScheduler::MpOrbIntScheduler(MpTcpConnection *connection) :
    MpTcpPacketScheduler(connection)
{
    schedulingMode = "intBurst";
}

SubflowConnection *MpOrbIntScheduler::selectDefaultSubflow(uint32_t bytes)
{
    // Recheck unsent allowance on every assignment, including cached bursts.
    if (lastSubflow != nullptr && remainingBurstBytes >= bytes &&
            lastSubflow->canUseDefaultScheduler(bytes) &&
            getBoundedAssignmentSpace(lastSubflow, bytes) >= bytes)
        return lastSubflow;

    // 1. Rank active paths with write-memory space and unsent allowance.
    SubflowConnection *bestSubflow = nullptr;
    SubflowConnection *overdueSubflow = nullptr;
    PathEstimate bestPath, overduePath;
    std::vector<SubflowConnection *> eligible;

    for (SubflowConnection *subflow : connection->getSubflows()) {
        if (subflow == nullptr)
            continue;

        if (!subflow->isActiveForDefaultScheduler()) {
            skippedCwndBursts.erase(subflow);
            continue;
        }
        const auto path = evaluatePath(subflow, DEFAULT_SEND_BURST_SIZE);
        if (path.space < bytes || !path.hasRate) {
            skippedCwndBursts.erase(subflow);
            continue;
        }

        eligible.push_back(subflow);
        const uint32_t skipped = skippedCwndBursts[subflow];
        // Rank the same prospective burst on every path; break exact ties
        // with the path that has missed more selections.
        if (bestSubflow == nullptr || path.score < bestPath.score ||
                (path.score == bestPath.score && skipped > skippedCwndBursts[bestSubflow])) {
            bestSubflow = subflow;
            bestPath = path;
        }
        if (skipped >= CWND_MAX_SKIPPED_BURSTS &&
                (overdueSubflow == nullptr || skipped > skippedCwndBursts[overdueSubflow])) {
            overdueSubflow = subflow;
            overduePath = path;
        }
    }

    if (bestSubflow == nullptr)
        return nullptr;
    // 2. Honour an overdue eligible path with one segment, not a full burst.
    if (overdueSubflow != nullptr) {
        bestSubflow = overdueSubflow;
        bestPath = overduePath;
    }

    // 3. Bound the burst by remaining unsent allowance, then round to segments.
    // An overdue path receives only one segment.
    uint32_t burstLimit = std::min(std::max(DEFAULT_SEND_BURST_SIZE, bytes), bestPath.space);
    burstLimit -= burstLimit % bytes;
    if (overdueSubflow != nullptr)
        burstLimit = bytes;
    startBurst(bestSubflow, bestSubflow->getSchedulerQueuedBytes(),
            bestSubflow->getSchedulerPacingRateBytesPerSecond(), burstLimit);

    static const simsignal_t scoreSignal = cComponent::registerSignal("intSchedulerScore");
    static const simsignal_t burstSignal = cComponent::registerSignal("intSchedulerBurstBytes");
    static const simsignal_t freshSignal = cComponent::registerSignal("intSchedulerFreshFeedback");
    static const simsignal_t probeSignal = cComponent::registerSignal("intSchedulerProbe");
    bestSubflow->emit(scoreSignal, bestPath.score);
    bestSubflow->emit(burstSignal, static_cast<unsigned long>(remainingBurstBytes));
    bestSubflow->emit(freshSignal, bestPath.freshFeedback);
    bestSubflow->emit(probeSignal, overdueSubflow != nullptr);

    for (SubflowConnection *subflow : eligible) {
        uint32_t& skipped = skippedCwndBursts[subflow];
        if (subflow == bestSubflow)
            skipped = 0;
        else if (skipped < std::numeric_limits<uint32_t>::max())
            ++skipped;
    }

    EV_INFO << "MpORB intBurst scheduler selected subflow " << bestSubflow->getSocketId()
            << " with burst=" << remainingBurstBytes
            << " bytes, fairness turn=" << (overdueSubflow != nullptr) << "\n";
    return bestSubflow;
}

uint32_t MpOrbIntScheduler::getBoundedAssignmentSpace(SubflowConnection *subflow,
        uint32_t /*segmentBytes*/) const
{
    const uint32_t cwnd = check_and_cast<TcpPacedFamily *>(subflow->getTcpAlgorithm())->getCwnd();
    const uint32_t limit = std::max(cwnd, subflow->getState()->snd_mss);
    const uint32_t unsent = subflow->getSchedulerUnsentBytes();
    const uint32_t allowance = unsent < limit ? limit - unsent : 0;
    const uint32_t queued = subflow->getSchedulerQueuedBytes();
    const uint32_t writeLimit = subflow->getDefaultSchedulerWriteLimit();
    // Bytes in flight do not consume this unsent allowance. TCP enforces its
    // flight window separately; a reduced cwnd simply stops further assignment.
    return std::min(allowance, queued < writeLimit ? writeLimit - queued : 0);
}

MpOrbIntScheduler::PathEstimate MpOrbIntScheduler::evaluatePath(
        SubflowConnection *subflow, uint32_t referenceBytes) const
{
    PathEstimate path;
    const uint32_t unsent = subflow->getSchedulerUnsentBytes();
    const double currentRate = subflow->getSchedulerPacingRateBytesPerSecond();
    const auto average = avgPacingRates.find(subflow);
    const double averageRate = average != avgPacingRates.end() ? average->second : currentRate;
    const double windowRate = subflow->getSchedulerWindowRateBytesPerSecond();
    // A recent rate reduction or small cwnd must constrain a stale pacing average.
    double rate = std::min({averageRate, currentRate, windowRate});
    if (!(averageRate > 0) || !(currentRate > 0) || !(windowRate > 0) || !std::isfinite(rate))
        rate = 0;
    path.hasRate = rate > 0;

    path.space = getBoundedAssignmentSpace(subflow, referenceBytes);

    simtime_t forwardDelay;
    path.freshFeedback = check_and_cast<MpOrbSubflowConnection *>(subflow)->getSchedulerForwardDelay(forwardDelay);
    if (!path.freshFeedback) {
        const simtime_t rtt = subflow->getSchedulingRtt();
        if (rtt <= SIMTIME_ZERO || rtt == SIMTIME_MAX)
            return path;
        forwardDelay = rtt / 2;
    }
    // Rank the SAME prospective burst on every path. An empty slow path must
    // still pay its service time instead of automatically receiving a zero score.
    if (path.hasRate)
        path.score = forwardDelay.dbl() + (static_cast<double>(unsent) + referenceBytes) / rate;
    return path;
}

SubflowConnection *MpOrbIntScheduler::selectRetransmissionSubflow(SubflowConnection *source,
        uint32_t bytes, bool requireIdle) const
{
    if (connection == nullptr || bytes == 0)
        return nullptr;
    SubflowConnection *best = nullptr;
    double bestScore = std::numeric_limits<double>::infinity();
    for (auto *subflow : connection->getSubflows()) {
        if (subflow == nullptr || (!requireIdle && subflow == source))
            continue;
        // Keep the existing idle-path rule for timer reinjection. Close/failover
        // can use any remaining writable path. Neither is proactive HoL rescue.
        const bool available = requireIdle ? subflow->canAcceptRetransmission(bytes) :
                subflow->canUseDefaultScheduler(bytes);
        if (!available)
            continue;
        // Meta recovery bounds the actual fragment by write space after selection.
        const auto path = evaluatePath(subflow, bytes);
        if (path.space < bytes)
            continue;
        if (path.score < bestScore) {
            best = subflow;
            bestScore = path.score;
        }
    }
    return best;
}

} // namespace tcp
} // namespace inet
