//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//

#ifndef MPORB_TRANSPORTLAYER_TCP_FLAVOURS_MPORBOLIA_H_
#define MPORB_TRANSPORTLAYER_TCP_FLAVOURS_MPORBOLIA_H_

#include "MpOrbSemiCoupledAlpha.h"

namespace inet {
namespace tcp {

class MpOrbOlia : public MpOrbSemiCoupledAlpha
{
  protected:
    static simsignal_t bestPathSignal;
    static simsignal_t maxWindowPathSignal;
    static simsignal_t correctionSignal;
    static simsignal_t pathPriceSignal;
    static simsignal_t pathOpportunitySignal;
    static simsignal_t normalizedWindowSignal;

    virtual bool getPathQuality(double& bottleneckFairRate,
            double& resourceCost) const;
    virtual void adjustAdditiveIncrease() override;
};

} // namespace tcp
} // namespace inet

#endif
