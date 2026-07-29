//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//

#ifndef MPORB_TRANSPORTLAYER_TCP_FLAVOURS_MPORBSEMICOUPLEDEPSILON_H_
#define MPORB_TRANSPORTLAYER_TCP_FLAVOURS_MPORBSEMICOUPLEDEPSILON_H_

#include "MpOrbSemiCoupledBase.h"

namespace inet {
namespace tcp {

class MpOrbSemiCoupledEpsilon : public MpOrbSemiCoupledBase
{
  protected:
    static simsignal_t pathCostSignal;
    static simsignal_t desiredShareSignal;
    static simsignal_t rateShareSignal;
    static simsignal_t redistributionSignal;

    simtime_t telemetryUpdatedAt = SIMTIME_ZERO;
    int pricedBottleneckId = -1;
    double bottleneckPrice = 0.0;

    virtual void updateBottleneckPrice();
    virtual void adjustAdditiveIncrease() override;
};

} // namespace tcp
} // namespace inet

#endif
