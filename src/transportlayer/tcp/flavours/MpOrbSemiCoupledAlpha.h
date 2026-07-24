//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//

#ifndef MPORB_TRANSPORTLAYER_TCP_FLAVOURS_MPORBSEMICOUPLEDALPHA_H_
#define MPORB_TRANSPORTLAYER_TCP_FLAVOURS_MPORBSEMICOUPLEDALPHA_H_

#include "MpOrbSemiCoupledBase.h"

namespace inet {
namespace tcp {

class MpOrbSemiCoupledAlpha : public MpOrbSemiCoupledBase
{
  protected:
    static simsignal_t subflowRateSignal;
    static simsignal_t connectionRateSignal;
    static simsignal_t rateShareSignal;

    virtual void adjustAdditiveIncrease() override;
};

} // namespace tcp
} // namespace inet

#endif
