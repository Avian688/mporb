//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//

#ifndef MPORB_TRANSPORTLAYER_TCP_FLAVOURS_MPORBSEMICOUPLEDBETA_H_
#define MPORB_TRANSPORTLAYER_TCP_FLAVOURS_MPORBSEMICOUPLEDBETA_H_

#include "MpOrbSemiCoupledBase.h"

namespace inet {
namespace tcp {

class MpOrbSemiCoupledBeta : public MpOrbSemiCoupledBase
{
  protected:
    static simsignal_t fairRateSignal;
    static simsignal_t totalFairRateSignal;
    static simsignal_t fairRateShareSignal;

    virtual void adjustAdditiveIncrease() override;
};

} // namespace tcp
} // namespace inet

#endif
