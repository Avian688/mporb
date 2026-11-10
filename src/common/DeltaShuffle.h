// SPDX-License-Identifier: LGPL-3.0-or-later
#ifndef INET_DELTA_SHUFFLE_H
#define INET_DELTA_SHUFFLE_H
#include <algorithm>
#include <cstdint>
namespace inet { namespace delta {
// Rates are payload bytes/s. Packet mass L/(x*T) is dimensionless.
struct LinkShuffle {
    int64_t epoch = -1;
    double mass = 0, rateMass = 0;
    double previousMass = 0, previousMeanRate = 0;
    double remainingPositiveRate = 0, remainingNegativeRate = 0;

    constexpr void beginRound(int64_t nextEpoch, double rateBudget) {
        if (nextEpoch != epoch) {
            const bool adjacent = nextEpoch == epoch + 1 && mass > 0;
            previousMass = adjacent ? mass : 0;
            previousMeanRate = adjacent ? rateMass / mass : 0;
            mass = rateMass = 0;
            remainingPositiveRate = remainingNegativeRate = rateBudget;
            epoch = nextEpoch;
        }
    }

    constexpr void observe(double packetMass, double connectionRate) {
        mass += packetMass;
        rateMass += packetMass * connectionRate;
    }

    constexpr double grantCorrection(double requested) {
        const bool negative = requested < 0;
        double& remaining = negative ? remainingNegativeRate : remainingPositiveRate;
        const double magnitude = negative ? -requested : requested;
        const double granted = std::min(magnitude, remaining);
        remaining -= granted;
        return negative ? -granted : granted;
    }
};

// allowance = beta*B/Mprev. An uninitialised/idle population gives no correction.
constexpr double rateCorrection(double mass, double allowance, double X, double meanX) {
    return meanX > 0 ? mass * allowance * (1 - X / meanX) : 0;
}
} }
#endif
