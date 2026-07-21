//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//

#ifndef MPORB_TRANSPORTLAYER_TCP_FLAVOURS_MPORBSEMICOUPLEDZETA_H_
#define MPORB_TRANSPORTLAYER_TCP_FLAVOURS_MPORBSEMICOUPLEDZETA_H_

#include "MpOrbSemiCoupledBase.h"

namespace inet {
namespace tcp {

class MpOrbSemiCoupledZeta : public MpOrbSemiCoupledBase
{
  protected:
    static simsignal_t pathCostSignal;
    static simsignal_t pathWeightSignal;
    static simsignal_t connectionAiRateSignal;

    virtual void adjustAdditiveIncrease() override;
};

} // namespace tcp
} // namespace inet

#endif
