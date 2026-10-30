// SPDX-License-Identifier: LGPL-3.0-or-later
#ifndef INET_MPORB_DELTA_H
#define INET_MPORB_DELTA_H
#include "MpOrbSemiCoupledAlpha.h"
#include "../../../common/DeltaShuffleTag.h"
namespace inet { namespace tcp {
// Alpha AI/MD plus a total-connection-rate fairness correction.
class MpOrbDelta : public MpOrbSemiCoupledAlpha {
  protected:
    unsigned long long nextSerial = 0, lastShuffleSerial = 0;
    double pendingShuffleBytes = 0; // accumulated since the last committed window
  public:
    virtual void stampTelemetry(DeltaShuffleTag& tag);
    virtual double measureInflight(const IntDataVec& data) override;
    virtual uint32_t computeWnd(double u, bool updateWc) override;
};
} }
#endif
