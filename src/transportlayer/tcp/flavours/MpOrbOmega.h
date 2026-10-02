// SPDX-License-Identifier: LGPL-3.0-or-later
#ifndef INET_MPORB_OMEGA_H
#define INET_MPORB_OMEGA_H

#include "MpOrbUncoupled.h"

namespace inet { namespace tcp {

// TCP/INT plumbing and cwnd actuator only. The allocation algorithm is the
// single, simultaneous meta update in MpOrbConnection::updateOmegaRates().
class MpOrbOmega : public MpOrbUncoupled
{
    friend class MpOrbConnection;
  protected:
    double targetRate = 0; // TCP payload bytes/s
    double pathPrice = 0;
    double capacity = 0;
    double fairRate = 0; // B/N, startup and diagnostic reference only
    double deliveredRate = 0;
    uint32_t previousDelivered = 0;
    uint32_t pricePathDigest = 0;
    simtime_t priceSampleTime = SIMTIME_ZERO;
    simtime_t priceReceivedTime = SIMTIME_ZERO;
    simtime_t recoveryUntil = SIMTIME_ZERO;
    bool hasPrice = false;

    virtual void initialize() override;
    virtual void processRexmitTimer(TcpEventCode& event) override;
    virtual void rackLossDetected() override;
    void installTargetRate(double rate);

  public:
    virtual void established(bool active) override;
    virtual double measureInflight(const IntDataVec& data) override;
    virtual bool getInitialPhase() override { return false; }
};

} } // namespace inet::tcp
#endif
