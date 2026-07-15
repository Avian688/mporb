//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//

#ifndef MPORB_TRANSPORTLAYER_TCP_FLAVOURS_MPORBSEMICOUPLEDOMEGA_H_
#define MPORB_TRANSPORTLAYER_TCP_FLAVOURS_MPORBSEMICOUPLEDOMEGA_H_

#include "MpOrbSemiCoupled.h"

namespace inet {
namespace tcp {

class MpOrbSemiCoupledOmega : public MpOrbSemiCoupled
{
  protected:
    static simsignal_t marginalValueSignal;
    static simsignal_t pathPriceSignal;
    static simsignal_t windowDeltaSignal;

    double pathPrice = 0.0;
    int pricedBottleneckId = -1;
    bool hasOmegaAdjustment = false;
    double pendingWindowDelta = 0.0;
    double lastMarginalValue = 0.0;
    double lastPathPrice = 0.0;
    double lastWindowDelta = 0.0;

    virtual void adjustAdditiveIncrease() override;
    virtual double getDeliveryRate(const MpOrbSemiCoupledOmega *algorithm,
            const OrbtcpStateVariables *subflowState) const;
    virtual void updatePathPrice();

  public:
    virtual uint32_t computeWnd(double u, bool updateWc) override;
};

} // namespace tcp
} // namespace inet

#endif
