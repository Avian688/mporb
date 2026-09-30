// SPDX-License-Identifier: LGPL-3.0-or-later
#ifndef INET_MPORB_U_H
#define INET_MPORB_U_H

#include "MpOrbSemiCoupledBase.h"

namespace inet {
namespace tcp {

// OrbCC/PINT with AI weights proportional to exp(-beta * smoothed U / eta).
// The underlying window decrease, startup and pacing remain OrbCC's.
class MpOrbU : public MpOrbSemiCoupledBase
{
  protected:
    static simsignal_t smoothedUSignal;
    static simsignal_t weightSignal;
    static simsignal_t freshSubflowsSignal;
    static simsignal_t uncoupledAiSignal;
    static simsignal_t weightedAiSignal;

    double beta = 20;
    double smoothingRtts = 1;
    double smoothedU = 0;
    double additiveIncreaseResidual = 0;
    bool hasUtilization = false;
    simtime_t pendingSampleTime = SIMTIME_ZERO;
    simtime_t utilizationSampleTime = SIMTIME_ZERO;

    void initialize() override;
    void adjustAdditiveIncrease() override;
    bool hasFreshUtilization() const;

  public:
    double measureInflight(const IntDataVec& intData) override;
};

} // namespace tcp
} // namespace inet
#endif
