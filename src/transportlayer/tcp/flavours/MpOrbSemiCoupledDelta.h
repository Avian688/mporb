// SPDX-License-Identifier: LGPL-3.0-or-later
#ifndef MPORB_TRANSPORTLAYER_TCP_FLAVOURS_MPORBSEMICOUPLEDDELTA_H_
#define MPORB_TRANSPORTLAYER_TCP_FLAVOURS_MPORBSEMICOUPLEDDELTA_H_

#include "MpOrbSemiCoupledAlpha.h"

namespace inet {
namespace tcp {

// Relax plain Alpha's AI suppression: weight = min(1, K * rate / totalRate).
// Equal-rate paths each retain their uncoupled AI; no path exceeds that AI.
class MpOrbSemiCoupledDelta : public MpOrbSemiCoupledAlpha
{
  protected:
    static simsignal_t eligibleSubflowsSignal;
    static simsignal_t weightSignal;
    static simsignal_t uncoupledAiSignal;
    static simsignal_t weightedAiSignal;

    void adjustAdditiveIncrease() override;
};

} // namespace tcp
} // namespace inet

#endif
