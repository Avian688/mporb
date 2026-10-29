// SPDX-License-Identifier: LGPL-3.0-or-later
#ifndef INET_MPORB_DELTA_QUEUE_H
#define INET_MPORB_DELTA_QUEUE_H
#include "../../../../orbtcp/src/queueing/queue/PintQueue.h"
#include "../../../../orbtcp/src/common/DeltaShuffle.h"
namespace inet { namespace queueing {
// One extension for the two Delta feedback modes; Omega uses sender mode.
class MpOrbDeltaQueue : public PintQueue {
  protected:
    delta::LinkShuffle deltaShuffle;
    cMessage *deltaShuffleTimer = nullptr;
    simtime_t deltaInterval; // Frozen for the current shuffle round.
    simtime_t deltaIntervalOverride; // 0 selects the queue's average RTT.
    void startDeltaShuffleRound();
    double deltaShuffleFraction = 0, deltaRateFloor = 0, deltaNeedGain = 0;
    virtual void initialize(int stage) override;
    virtual void handleMessage(cMessage *message) override;
    virtual void processTelemetryExtension(const Ptr<tcp::TcpHeader>& tcpHeader,
            uint64_t tcpPayloadBytes, double bandwidthBytesPerSecond, bool selectedBottleneck) override;
  public:
    virtual ~MpOrbDeltaQueue();
};
} }
#endif
