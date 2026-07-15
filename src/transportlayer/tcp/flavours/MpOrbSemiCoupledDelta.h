//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//

#ifndef MPORB_TRANSPORTLAYER_TCP_FLAVOURS_MPORBSEMICOUPLEDDELTA_H_
#define MPORB_TRANSPORTLAYER_TCP_FLAVOURS_MPORBSEMICOUPLEDDELTA_H_

#include "MpOrbSemiCoupled.h"

namespace inet {
namespace tcp {

class MpOrbSemiCoupledDelta : public MpOrbSemiCoupled
{
  protected:
    static simsignal_t baseAiRateSignal;
    static simsignal_t alphaAiRateSignal;
    static simsignal_t targetShareSignal;
    static simsignal_t rateShareSignal;
    static simsignal_t responsivenessSignal;
    static simsignal_t aiShareSignal;

    double lastBaseAiRate = 0.0;
    double lastAlphaAiRate = 0.0;
    double lastTargetShare = 0.0;
    double lastRateShare = 0.0;
    double lastResponsiveness = 0.0;
    double lastAiShare = 0.0;

    virtual void refreshDeliveryRate();
    virtual double getDeliveryRate(const MpOrbSemiCoupledDelta *algorithm,
            const OrbtcpStateVariables *subflowState) const;
    virtual void adjustAdditiveIncrease() override;

  public:
    virtual uint32_t computeWnd(double u, bool updateWc) override;
};

} // namespace tcp
} // namespace inet

#endif
