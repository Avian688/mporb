// SPDX-License-Identifier: LGPL-3.0-or-later
#ifndef MPORB_TRANSPORTLAYER_TCP_FLAVOURS_MPORBSEMICOUPLEDBETA_H_
#define MPORB_TRANSPORTLAYER_TCP_FLAVOURS_MPORBSEMICOUPLEDBETA_H_

#include <utility>
#include <vector>

#include "MpOrbSemiCoupledAlpha.h"

namespace inet {
namespace tcp {

// Plain Alpha AI plus a centred U-error correction; no Alpha boost or B/N target.
// Clip negative weights and normalise one shared snapshot per control RTT.
class MpOrbSemiCoupledBeta : public MpOrbSemiCoupledAlpha
{
  protected:
    static simsignal_t weightSignal, correctionSignal, smoothedUSignal, errorSignal;
    static simsignal_t currentRateShareSignal, baselineShareSignal, activeSignal;
    static simsignal_t alphaAiSignal, uncoupledAiSignal, weightedAiSignal;

    double redistributionGain = 0.5, utilizationDeadband = 0.05;
    double smoothingRtts = 1, smoothedU = 0;
    bool hasFeedback = false;
    simtime_t sampleTime = SIMTIME_ZERO, pendingSampleTime = SIMTIME_ZERO;
    uint64_t feedbackEpoch = 0;

    // All participating Beta siblings receive the same membership/expiry and
    // their own weight. Any sibling's next ACK can refresh the shared snapshot.
    std::vector<std::pair<int, uint64_t>> roundMembers;
    simtime_t roundUntil = SIMTIME_ZERO;
    double roundWeight = 1, roundBaseline = 1, roundError = 0;
    bool roundActive = false;

    bool hasFreshFeedback() const;
    void invalidateFeedback();
    void initialize() override;
    void adjustAdditiveIncrease() override;
    void processRexmitTimer(TcpEventCode& event) override;
    void rackLossDetected() override;

  public:
    double measureInflight(const IntDataVec& intData) override;
};

} // namespace tcp
} // namespace inet
#endif
