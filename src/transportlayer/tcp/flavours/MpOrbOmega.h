// SPDX-License-Identifier: LGPL-3.0-or-later
#ifndef INET_MPORB_OMEGA_H
#define INET_MPORB_OMEGA_H
#include "MpOrbSenderDelta.h"
namespace inet { namespace tcp {
// Sender Delta plus a once-per-RTT underutilisation AI extension.
class MpOrbOmega : public MpOrbSenderDelta {
  public:
    virtual uint32_t computeWnd(double u, bool updateWc) override;
};
} }
#endif
