//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//

#ifndef MPORB_TRANSPORTLAYER_TCP_FLAVOURS_MPORBSEMICOUPLED_H_
#define MPORB_TRANSPORTLAYER_TCP_FLAVOURS_MPORBSEMICOUPLED_H_

#include <vector>

#include "MpOrbUncoupled.h"

namespace inet {
namespace tcp {

class MpTcpConnection;

class MpOrbSemiCoupled : public MpOrbUncoupled
{
  protected:
    static simsignal_t connectionRateSignal;
    static simsignal_t deliveryRateSignal;
    static simsignal_t connectionCountSignal;
    static simsignal_t fairRateSignal;
    static simsignal_t relativeOpportunitySignal;
    static simsignal_t utilizationSafetySignal;
    static simsignal_t pathWeightSignal;
    static simsignal_t aiRateBudgetSignal;
    static simsignal_t adjustedAiSignal;

    double smoothedDeliveryRate = 0.0;
    simtime_t deliveryRateUpdatedAt = SIMTIME_ZERO;
    std::vector<bool> observedPathId;

    bool hasAllocation = false;
    double lastConnectionRate = 0.0;
    double lastDeliveryRate = 0.0;
    double lastConnectionCount = 1.0;
    double lastFairRate = 0.0;
    double lastRelativeOpportunity = 0.0;
    double lastUtilizationSafety = 0.0;
    double lastPathWeight = 0.0;
    double lastAiRateBudget = 0.0;
    uint32_t lastAdjustedAi = 0;

    virtual MpTcpConnection *getMetaConnection() const;
    virtual void refreshTelemetry();
    virtual void adjustAdditiveIncrease() override;

  public:
    virtual size_t getConnId() override;
    virtual uint32_t computeWnd(double u, bool updateWc) override;
};

} // namespace tcp
} // namespace inet

#endif
