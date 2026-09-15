// SPDX-License-Identifier: LGPL-3.0-or-later
#ifndef MPORB_TRANSPORTLAYER_TCP_FLAVOURS_MPORBPRESSUREPOLICY_H_
#define MPORB_TRANSPORTLAYER_TCP_FLAVOURS_MPORBPRESSUREPOLICY_H_

#include <cstdint>
#include <limits>

namespace inet {
namespace tcp {
namespace mporbpressure {

constexpr bool positiveFinite(double value)
{
    return value > 0 && value <= std::numeric_limits<double>::max();
}

// B and N come from the SAME PINT-selected hop. B is in bytes/second.
constexpr double fairRate(double bandwidth, uint32_t flows)
{
    if (!positiveFinite(bandwidth) || flows == 0)
        return 0;
    return bandwidth / flows;
}

// Pressure is inverse nominal fair rate, in seconds/byte. U is still used by
// OrbCC's congestion response and PINT hop selection, but not by these weights.
constexpr double pressure(double bandwidth, uint32_t flows)
{
    const double rate = fairRate(bandwidth, flows);
    if (!positiveFinite(rate) || rate < 1 / std::numeric_limits<double>::max())
        return 0;
    return 1 / rate;
}

struct RateAllocation {
    double weight = 1;
    double weightedFairRate = 0;
};

struct RateBudget {
    double totalRate = 0;
    double fairRateTimesRate = 0;
    unsigned int paths = 0;

    constexpr bool add(double rate, double nominalFairRate)
    {
        if (!positiveFinite(rate) || !positiveFinite(nominalFairRate) ||
                totalRate > std::numeric_limits<double>::max() - rate ||
                (rate > 1 && nominalFairRate > std::numeric_limits<double>::max() / rate))
            return false;
        const double product = nominalFairRate * rate;
        if (fairRateTimesRate > std::numeric_limits<double>::max() - product)
            return false;
        totalRate += rate;
        fairRateTimesRate += product;
        paths++;
        return true;
    }

    constexpr RateAllocation allocate(double ownRate) const
    {
        if (!positiveFinite(ownRate) || !positiveFinite(totalRate) || ownRate > totalRate)
            return {};
        return {ownRate / totalRate, fairRateTimesRate / totalRate};
    }
};

constexpr double rateEstimate(double committedWindow, double rtt)
{
    if (!positiveFinite(committedWindow) || !positiveFinite(rtt))
        return 0;
    if (rtt < 1 && committedWindow > std::numeric_limits<double>::max() * rtt)
        return 0;
    const double rate = committedWindow / rtt;
    return positiveFinite(rate) ? rate : 0;
}

constexpr double orbTarget(double committedWindow, double utilization, double eta, double ai)
{
    return utilization >= eta ? committedWindow * (eta / utilization) + ai : committedWindow + ai;
}

constexpr double withdraw(double committedWindow, double coupledTarget, double uncoupledTarget,
        double probeFloor, double gain, double maxDecreaseFraction)
{
    double target = coupledTarget;
    if (coupledTarget < committedWindow) {
        const double accelerated = committedWindow + gain * (coupledTarget - committedWindow);
        const double limited = committedWindow * (1 - maxDecreaseFraction);
        const double bounded = accelerated > limited ? accelerated : limited;
        // Do not weaken an ordinary OrbCC decrease larger than our step bound.
        target = coupledTarget < bounded ? coupledTarget : bounded;
    }
    if (target < probeFloor)
        target = probeFloor;
    // Even the probe floor may not exceed the same-state Uncoupled target.
    return target < uncoupledTarget ? target : uncoupledTarget;
}

constexpr bool reentryOpportunity(double share, unsigned int paths, double utilization,
        double eta, bool fairRateImproved)
{
    return paths > 1 && share > 0 && share < 0.25 / paths &&
            ((positiveFinite(utilization) && utilization < 0.9 * eta) || fairRateImproved);
}

constexpr bool fresh(double now, double sampledAt, double receivedAt, double rtt)
{
    return positiveFinite(rtt) && sampledAt >= 0 && receivedAt >= sampledAt &&
            now >= receivedAt && now - receivedAt <= 2 * rtt && now - sampledAt <= 2 * rtt;
}

// Carry sub-byte increases without imposing a one-byte floor. The cap holds
// on every call, including when the uncoupled budget changes between ACKs.
constexpr uint32_t attenuate(uint32_t uncoupledAi, double fraction, double& residual)
{
    if (uncoupledAi == 0 || !(fraction >= 0 && fraction < 1)) {
        residual = 0;
        return uncoupledAi;
    }
    if (fraction == 0) {
        residual = 0;
        return 0;
    }
    if (!(residual >= 0 && residual < 1))
        residual = 0;
    const double exact = uncoupledAi * fraction + residual;
    if (exact >= uncoupledAi) {
        residual = 0;
        return uncoupledAi;
    }
    const uint32_t result = static_cast<uint32_t>(exact);
    residual = exact - result;
    return result;
}

} // namespace mporbpressure
} // namespace tcp
} // namespace inet

#endif
