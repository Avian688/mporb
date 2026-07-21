//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//

#ifndef MPORB_TRANSPORTLAYER_TCP_FLAVOURS_MPORBSEMICOUPLEDEPSILON_H_
#define MPORB_TRANSPORTLAYER_TCP_FLAVOURS_MPORBSEMICOUPLEDEPSILON_H_

#include <map>

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

    std::map<int, double> hopPrices;
    std::vector<bool> pricedPathId;
    bool hasAllocation = false;
    double lastPathCost = 0.0;
    double lastDesiredShare = 0.0;
    double lastRateShare = 0.0;
    double pendingRedistribution = 0.0;

    virtual void updateHopPrices();
    virtual void adjustAdditiveIncrease() override;

  public:
    virtual uint32_t computeWnd(double u, bool updateWc) override;
};

} // namespace tcp
} // namespace inet

#endif
