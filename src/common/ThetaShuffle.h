// SPDX-License-Identifier: LGPL-3.0-or-later
#ifndef INET_THETA_SHUFFLE_H
#define INET_THETA_SHUFFLE_H
namespace inet { namespace theta {
// mass: dimensionless; allowance = beta*B/M, X and meanX: payload bytes/s.
// Zero mean denotes an uninitialised/idle previous interval.
constexpr double rateCorrection(double mass, double allowance, double X, double meanX)
{
    return meanX > 0 ? mass * allowance * (1 - X / meanX) : 0;
}
} }
#endif
