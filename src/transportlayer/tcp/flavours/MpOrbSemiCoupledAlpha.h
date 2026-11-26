//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//

#ifndef MPORB_TRANSPORTLAYER_TCP_FLAVOURS_MPORBSEMICOUPLEDALPHA_H_
#define MPORB_TRANSPORTLAYER_TCP_FLAVOURS_MPORBSEMICOUPLEDALPHA_H_

#include "MpOrbSemiCoupledBase.h"

#include <memory>

namespace inet {
namespace tcp {

struct MpOrbWindowUpdate : omnetpp::cObject
{
    double u = 0;
    double pacingRateMbps = 0;
    bool cwndLimited = false;
    uint32_t pintIncrease = 0;
    uint32_t alphaIncrease = 0;
    uint32_t increaseWithBoost = 0;
    uint32_t cwndBefore = 0;
    uint32_t previousCommitted = 0;
    uint32_t cwndAfter = 0;
    uint32_t windowBeforeShuffle = 0;
    int64_t alphaRemoved = 0;
    int64_t omegaRestored = 0;
};

class MpOrbSemiCoupledAlpha : public MpOrbSemiCoupledBase
{
  protected:
    static simsignal_t subflowRateSignal;
    static simsignal_t connectionRateSignal;
    static simsignal_t rateShareSignal;

    virtual void adjustAdditiveIncrease() override;
    virtual void updatePacingInterval() override;

    double additiveIncreaseResidual = 0.0;
    std::unique_ptr<MpOrbWindowUpdate> windowUpdate;
    bool windowUpdatePending = false;

  public:
    static const simsignal_t windowUpdateSignal;
    void enableWindowUpdateTrace();
    uint32_t computeWnd(double u, bool updateWc) override;
};

} // namespace tcp
} // namespace inet

#endif
