#!/usr/bin/env python3
"""Constant-evaluate the production Omega equations (no binary or OMNeT++ run).

The numerical checks use ideal payload-rate links, fixed RTT feedback delays,
and a fluid queue. They do not validate the packet scheduler or ACK plumbing.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
POLICY = ROOT.parent / 'orbtcp/src/common/OmegaPrice.h'

SOURCE = r'''
#include "OmegaPrice.h"
#include <array>
using namespace inet;
constexpr bool near(double a, double b, double tolerance = .002) {
    return a - b < tolerance && b - a < tolerance;
}
constexpr bool priceChecks() {
    omega::LinkPrice p{2, 2};
    p.update(95, 100, 0, .005, .02, .95, .2, 1);
    if (!near(p.memory, 2) || !near(p.advertised, 2)) return false;
    p.update(120, 100, 0, .005, .02, .95, .2, 1);
    if (!(p.memory > 2 && p.advertised > p.memory)) return false;
    for (int i = 0; i < 100; ++i)
        p.update(0, 100, 0, .005, .02, .95, .2, 1);
    return p.memory == 0 && p.advertised == 0;
}
static_assert(priceChecks(), "price integrates overload, holds at target, decays while idle");
static_assert(omega::nextRate(.01, 1, .5, 1, .1, .005, .02, .0001, 1) > .01);
static_assert(omega::nextRate(.8, 1, 2, 1, .1, .005, .02, .0001, 1) < .8);
static_assert(near(omega::nextRate(.01, 1, .5, 1, .1, .005, .02, .0001, 1) - .01,
                  omega::nextRate(.6, 1, .5, 1, .1, .005, .02, .0001, 1) - .6),
              "a small existing share must not suppress the rate correction");
static_assert(omega::nextRate(.1, .1, 1e6, 1, .1, 1, .02, .001, 1) == .001);
static_assert(omega::nextRate(.9, .9, 0, 1, .1, 1, .02, .001, 1) == 1);
static_assert(omega::windowForRate(12500000, .02, 1448) == 250000,
              "100 Mbps at 20ms maps to a 250000-byte cwnd without pacing slack");
static_assert(omega::windowForRate(0, .02, 1448) == 1448);
static_assert(omega::windowForRate(100, .02, 1488) == 1488);
static_assert(omega::windowForRate(1e20, .02, 1448) == 0xffffffffU);

struct Network {
    int links, paths;
    std::array<std::array<int, 8>, 4> A{};
    std::array<int, 8> owner{};
    std::array<double, 8> x{};
    std::array<double, 4> totals{};
    std::array<omega::LinkPrice, 4> price{};
    std::array<double, 4> queue{};
    std::array<std::array<double, 8>, 40> rateHistory{};
    std::array<std::array<double, 4>, 40> priceHistory{};

    constexpr void run(int steps = 5000, int halfDelay = 2) {
        const double dt = .005, T = 2 * halfDelay * dt;
        for (auto& rates : rateHistory) rates = x;
        for (auto& prices : priceHistory)
            for (int l = 0; l < links; ++l) prices[l] = price[l].advertised;
        for (int k = 0; k < steps; ++k) {
            const auto pastRate = rateHistory[k % halfDelay];
            const auto pastPrice = priceHistory[k % halfDelay];
            rateHistory[k % halfDelay] = x;
            for (auto& total : totals) total = 0;
            for (int r = 0; r < paths; ++r) totals[owner[r]] += x[r];
            for (int l = 0; l < links; ++l) {
                double y = 0;
                for (int r = 0; r < paths; ++r) y += A[l][r] * pastRate[r];
                queue[l] = std::max(0.0, queue[l] + dt * (y - 1));
                price[l].update(y, 1, queue[l], dt, T, .95, .2, 1);
                priceHistory[k % halfDelay][l] = price[l].advertised;
            }
            for (int r = 0; r < paths; ++r) {
                double p = 0;
                for (int l = 0; l < links; ++l) p += A[l][r] * pastPrice[l];
                unsigned int peers = 0;
                for (int j = 0; j < paths; ++j) peers += owner[j] == owner[r];
                x[r] = omega::nextRate(x[r], totals[owner[r]], p, 1, .1,
                                      dt, T, .0001, 1, peers);
            }
        }
        for (auto& total : totals) total = 0;
        for (int r = 0; r < paths; ++r) totals[owner[r]] += x[r];
    }
};

constexpr bool onePath() {
    Network n{1, 1}; n.A[0][0] = 1; n.x[0] = .1;
    n.run();
    return near(n.x[0], .95) && near(n.price[0].memory, 1/.95) && n.queue[0] == 0;
}
constexpr bool twoUsers() {
    Network n{1, 2}; n.A[0][0] = n.A[0][1] = 1;
    n.owner[1] = 1; n.x[0] = .8; n.x[1] = .001;
    n.run();
    return near(n.x[0], .475) && near(n.x[1], .475);
}
constexpr bool disjoint(int delay) {
    Network n{2, 2}; n.A[0][0] = n.A[1][1] = 1;
    n.x[0] = .8; n.x[1] = .001;
    n.run(10000, delay);
    return near(n.x[0], .95) && near(n.x[1], .95) && near(n.totals[0], 1.9);
}
constexpr bool sharedAccess() {
    Network n{3, 2}; n.A[0][0] = n.A[1][1] = 1;
    n.A[2][0] = n.A[2][1] = 1; n.x[0] = n.x[1] = .1;
    n.run();
    return near(n.totals[0], .95) && n.price[2].memory > 1 &&
           n.price[0].memory == 0 && n.price[1].memory == 0;
}
constexpr bool hotspot() {
    // A can move between links 0/1; B only has link 0. PF moves A to link 1.
    Network n{2, 3}; n.A[0][0] = n.A[0][2] = n.A[1][1] = 1;
    n.owner[2] = 1; n.x[0] = n.x[1] = n.x[2] = .1;
    n.run();
    if (!near(n.totals[0], .95) || !near(n.totals[1], .95) || n.x[0] > .002)
        return false;
    // B leaves. A's almost-empty path must refill without a rate-share multiplier.
    n.paths = 2; n.run();
    return near(n.totals[0], 1.9) && near(n.x[0], .95);
}
constexpr bool cascade() {
    // A uses links 0/1; B uses 1/2; C only uses 0. All get .95 by shifting A,
    // which in turn pushes B to 2, despite C never sharing a link with B.
    Network n{3, 5}; n.A[0][0] = n.A[1][1] = n.A[1][2] = n.A[2][3] = n.A[0][4] = 1;
    n.owner = {0,0,1,1,2}; n.x = {.4,.1,.4,.1,.1};
    n.run(10000);
    return near(n.totals[0], .95) && near(n.totals[1], .95) && near(n.totals[2], .95)
           && n.x[0] < .003 && n.x[2] < .003;
}
constexpr bool parkingLot() {
    // Spine crosses three links; each rib crosses just one. Two equal lanes
    // scale these totals by two. PF is .2375 vs .7125, not max-min .475 each.
    Network n{3, 4};
    for (int l = 0; l < 3; ++l) { n.A[l][0] = 1; n.A[l][l+1] = 1; }
    n.owner = {0,1,2,3}; n.x = {.1,.1,.1,.1}; n.run();
    return near(n.totals[0], .2375) && near(n.totals[1], .7125) &&
           near(n.totals[2], .7125) && near(n.totals[3], .7125);
}
constexpr bool manyUsers(int n) {
    // Symmetric n-user link, with two half-RTT delays; no packet simulation.
    double x = .1/n, q = 0;
    omega::LinkPrice p;
    std::array<double, 2> rates{x,x}, prices{};
    for (int k = 0; k < 10000; ++k) {
        const double y = n * rates[k % 2], oldPrice = prices[k % 2];
        rates[k % 2] = x;
        q = std::max(0.0, q + .005 * (y - 1));
        p.update(y, 1, q, .005, .02, .95, .2, 1);
        prices[k % 2] = p.advertised;
        x = omega::nextRate(x, x, oldPrice, 1, .1, .005, .02, .0001, 1);
    }
    return near(n*x, .95) && near(p.memory, n/.95, .02) && q == 0;
}
static_assert(onePath(), "one path reaches target capacity with persistent price");
static_assert(twoUsers(), "two users share a bottleneck despite unequal starts");
static_assert(disjoint(2), "two disjoint paths pool capacity at 20ms RTT");
static_assert(disjoint(20), "two disjoint paths pool capacity at 200ms RTT");
static_assert(sharedAccess(), "a shared access bottleneck must also contribute price");
static_assert(hotspot(), "hotspot withdrawal and recovery after departure");
static_assert(cascade(), "redistribution propagates to a connection on another link");
static_assert(parkingLot(), "summed link prices produce connection PF on a parking lot");
static_assert(manyUsers(6), "the background-flow case must remain stable");
static_assert(manyUsers(20), "small per-connection rates must not cause oversized updates");
static_assert(manyUsers(100), "price adaptation must remain useful with many competitors");
'''


def main():
    with tempfile.TemporaryDirectory(prefix='omega-policy-') as directory:
        source = Path(directory) / 'checks.cc'
        source.write_text(SOURCE)
        subprocess.run(['clang++', '-std=c++17', '-fsyntax-only',
                        '-fconstexpr-steps=100000000', '-I', str(POLICY.parent),
                        str(source)], check=True)
    print('Omega production equations: all constant-evaluated numerical checks passed.')


if __name__ == '__main__':
    main()
