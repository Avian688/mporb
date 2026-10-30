// SPDX-License-Identifier: LGPL-3.0-or-later
#ifndef INET_MPORB_OMEGA_H
#define INET_MPORB_OMEGA_H
#include "MpOrbDelta.h"
namespace inet { namespace tcp {
// Delta plus a once-per-RTT underutilisation AI extension.
class MpOrbOmega : public MpOrbDelta {
  public:
    virtual uint32_t computeWnd(double u, bool updateWc) override;
};
} }
#endif
