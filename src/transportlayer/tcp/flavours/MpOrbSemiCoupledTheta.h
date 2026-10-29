// SPDX-License-Identifier: LGPL-3.0-or-later
#ifndef INET_MPORB_THETA_H
#define INET_MPORB_THETA_H
#include "MpOrbSenderDelta.h"
namespace inet { namespace tcp {
// Alpha plus an XCP-inspired equal-increase / total-rate-proportional shuffle.
// Reuse Delta's rate stamping, deduplication state and once-per-RTT window commit.
class MpOrbSemiCoupledTheta : public MpOrbSenderDelta {
  public:
    virtual void stampTelemetry(DeltaShuffleTag& tag) override;
    virtual double measureInflight(const IntDataVec& data) override;
};
} }
#endif
