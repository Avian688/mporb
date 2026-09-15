// SPDX-License-Identifier: LGPL-3.0-or-later
#ifndef MPORB_TRANSPORTLAYER_TCP_FLAVOURS_MPORBPRESSURE_H_
#define MPORB_TRANSPORTLAYER_TCP_FLAVOURS_MPORBPRESSURE_H_

#include "MpOrbUncoupled.h"

namespace inet {
namespace tcp {

class MpOrbConnection;

/** Aggregate-rate coupling with bounded withdrawal and weak-path re-entry. */
class MpOrbPressure : public MpOrbUncoupled
{
  protected:
    static simsignal_t pressureSignal;
    static simsignal_t weightSignal;
    static simsignal_t fairRateSignal;
    static simsignal_t aiScaleSignal;
    static simsignal_t connectionAiRateSignal;
    static simsignal_t freshSubflowsSignal;
    static simsignal_t uncoupledAiSignal;
    static simsignal_t weightedAiSignal;
    static simsignal_t subflowRateSignal;
    static simsignal_t connectionRateSignal;
    static simsignal_t reentryBoostSignal;
    static simsignal_t windowDeltaSignal;
    static simsignal_t extraWithdrawalSignal;

    const IntMetaData *feedbackBeingProcessed = nullptr;
    bool feedbackAccepted = false;
    bool hasSampleTime = false;
    simtime_t lastSampleTime = SIMTIME_ZERO;
    double additiveIncreaseResidual = 0;
    uint32_t uncoupledAdditiveIncrease = 0;
    bool couplingActive = false;
    double decreaseGain = 1;
    double maxDecreaseFraction = 0.25;
    int probeIntervalRtts = 4;
    double previousFairRate = 0;
    simtime_t fairRateImprovedUntil = SIMTIME_ZERO;
    simtime_t reentryUntil = SIMTIME_ZERO;
    simtime_t nextReentryAllowed = SIMTIME_ZERO;

    virtual void initialize() override;
    MpOrbConnection *getPressureMetaConnection() const;
    void forgetPressureFeedback();
    virtual void adjustAdditiveIncrease() override;
    virtual void processRexmitTimer(TcpEventCode& event) override;
    virtual void rackLossDetected() override;

  public:
    virtual double measureInflight(const IntDataVec& intData) override;
    virtual uint32_t computeWnd(double u, bool updateWc) override;
    bool isPressureCouplingReady() const;
    double getPressureRateEstimate() const;
};

} // namespace tcp
} // namespace inet

#endif
