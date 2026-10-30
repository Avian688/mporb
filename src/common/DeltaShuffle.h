// SPDX-License-Identifier: LGPL-3.0-or-later
#ifndef INET_DELTA_SHUFFLE_H
#define INET_DELTA_SHUFFLE_H
#include <cstdint>
namespace inet { namespace delta {
// Rates are payload bytes/s. Packet mass L/(x*T) is dimensionless.
struct LinkShuffle {
    int64_t epoch = -1;
    double mass = 0, rateMass = 0;
    double previousMass = 0, previousMeanRate = 0;

    constexpr void beginRound(int64_t nextEpoch) {
        if (nextEpoch != epoch) {
            const bool adjacent = nextEpoch == epoch + 1 && mass > 0;
            previousMass = adjacent ? mass : 0;
            previousMeanRate = adjacent ? rateMass / mass : 0;
            mass = rateMass = 0;
            epoch = nextEpoch;
        }
    }

    constexpr void observe(double packetMass, double connectionRate) {
        mass += packetMass;
        rateMass += packetMass * connectionRate;
    }
};

// allowance = beta*B/Mprev. An uninitialised/idle population gives no correction.
constexpr double rateCorrection(double mass, double allowance, double X, double meanX) {
    return meanX > 0 ? mass * allowance * (1 - X / meanX) : 0;
}
} }
#endif
