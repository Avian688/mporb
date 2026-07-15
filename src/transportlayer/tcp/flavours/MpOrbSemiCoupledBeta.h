//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//

#ifndef MPORB_TRANSPORTLAYER_TCP_FLAVOURS_MPORBSEMICOUPLEDBETA_H_
#define MPORB_TRANSPORTLAYER_TCP_FLAVOURS_MPORBSEMICOUPLEDBETA_H_

#include "MpOrbUncoupled.h"

namespace inet {
namespace tcp {

class MpTcpConnection;

class MpOrbSemiCoupledBeta : public MpOrbUncoupled
{
  protected:
    static simsignal_t fairRateSignal;
    static simsignal_t totalFairRateSignal;
    static simsignal_t fairRateShareSignal;
    static simsignal_t uncoupledAiSignal;
    static simsignal_t adjustedAiSignal;

    virtual MpTcpConnection *getMetaConnection() const;
    virtual void adjustAdditiveIncrease() override;

  public:
    virtual size_t getConnId() override;
};

} // namespace tcp
} // namespace inet

#endif
