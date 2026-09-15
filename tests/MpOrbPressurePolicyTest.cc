// Compile-time checks: no simulator or executable is needed.
// From the workspace root:
// clang++ -std=c++17 -fsyntax-only samples/mporb/tests/MpOrbPressurePolicyTest.cc
#include "../src/transportlayer/tcp/flavours/MpOrbPressurePolicy.h"

using namespace inet::tcp::mporbpressure;

static_assert(fairRate(100, 2) == 50 && fairRate(200, 2) == 100, "Bandwidth increases opportunity");
static_assert(fairRate(100, 4) == 25, "Flow count divides opportunity once");
static_assert(pressure(100, 2) == 0.02 && pressure(200, 2) == 0.01, "Pressure is N/B");
static_assert(fairRate(0, 2) == 0 && fairRate(1, 0) == 0 && fairRate(-1, 2) == 0, "Reject invalid feedback");
static_assert(fairRate(std::numeric_limits<double>::infinity(), 2) == 0, "Reject infinity");
static_assert(fairRate(std::numeric_limits<double>::quiet_NaN(), 2) == 0, "Reject NaN");
static_assert(RateBudget{}.allocate(0).weight == 1, "Missing feedback falls back to uncoupled");
static_assert(fresh(1, 0.875, 1, 0.125), "A fresh sample is eligible");
static_assert(fresh(1, 0.75, 0.875, 0.125), "Two RTT boundary is inclusive");
static_assert(!fresh(1, 0.5, 1, 0.125), "A delayed ACK cannot rejuvenate an old sample");
static_assert(!fresh(1, 0.5, 0.625, 0.125), "Expire idle paths");
static_assert(!fresh(1, 1.125, 1, 0.125), "Reject future samples");
static_assert(!fresh(1, 1, 1, 0), "RTT must be known");
static_assert(rateEstimate(4000, 0.04) == 100000, "Use window/RTT, not application goodput");
static_assert(rateEstimate(4000, 0) == 0, "Unknown RTT is not an active rate");
static_assert(rateEstimate(1e308, 1e-308) == 0, "Reject overflowing rates");

constexpr bool close(double a, double b)
{
    return a - b < 1e-9 && b - a < 1e-9;
}

constexpr bool rateBudgetChecks()
{
    RateBudget single;
    single.add(75, 100.0 / 6);
    if (single.allocate(75).weight != 1)
        return false;
    RateBudget mixed;
    mixed.add(10, 50);
    mixed.add(20, 100);
    mixed.add(30, 20);
    double shares = 0;
    for (int i = 1; i <= 3; i++)
        shares += mixed.allocate(10 * i).weight;
    if (!close(shares, 1) || !close(mixed.allocate(10).weightedFairRate, 3100.0 / 60))
        return false;
    // A path's growth declines when its connection already gets more elsewhere.
    RateBudget poor;
    poor.add(50, 50);
    poor.add(50, 50);
    RateBudget rich;
    rich.add(50, 50);
    rich.add(150, 100);
    if (poor.allocate(50).weight != 0.5 || rich.allocate(50).weight != 0.25)
        return false;
    RateBudget invalid;
    return !invalid.add(0, 100) && !invalid.add(100, 0) &&
            !invalid.add(std::numeric_limits<double>::quiet_NaN(), 10) && invalid.paths == 0;
}
static_assert(rateBudgetChecks(), "Couple to the connection's aggregate window-rate estimate");

// The production window equation has the PF stationarity condition for one
// shared bottleneck per route: 1/X = d/(gamma*q), where d = 1 - eta/U.
// These checks are an equilibrium calculation, not an OMNeT++ simulation.
constexpr double netChange(double pathRate, double connectionRate, double fair,
        double price, double rtt = 1)
{
    const double gamma = 0.05;
    const double eta = 0.95;
    const double window = pathRate * rtt;
    RateBudget budget;
    budget.add(pathRate, fair);
    if (connectionRate > pathRate)
        budget.add(connectionRate - pathRate, fair);
    const double ai = gamma * fair * rtt * budget.allocate(pathRate).weight;
    const double u = eta / (1 - gamma * fair * price);
    const double coupled = orbTarget(window, u, eta, ai);
    const double uncoupled = orbTarget(window, u, eta, gamma * fair * rtt);
    return withdraw(window, coupled, uncoupled, 0, 4, 0.25) - window;
}

constexpr bool equilibriumChecks()
{
    const double total = 800.0 / 3;
    // No background: A gets 2/3 of each shared link; B/C get 1/3 plus private links.
    if (!close(netChange(200.0 / 3, total, 50, 1 / total), 0) ||
            !close(netChange(100.0 / 3, total, 50, 1 / total), 0) ||
            !close(netChange(100, total, 100, 1 / total), 0))
        return false;
    // The old 240/280/280 allocation is NOT stationary: A grows and B/C yield
    // for a shared-link price between their connection marginal utilities.
    if (!(netChange(60, 240, 50, 1.0 / 260) > 0) ||
            !(netChange(40, 280, 50, 1.0 / 260) < 0))
        return false;
    // Either background phase: main connections use their remaining routes
    // at aggregate 200; five background connections share each occupied link.
    if (!close(netChange(100, 200, 100, 1.0 / 200), 0) ||
            !close(netChange(20, 20, 20, 1.0 / 20), 0))
        return false;
    // A tiny foreground allocation on a background-dominated link withdraws.
    if (!(netChange(1, 200, 100.0 / 6, 1.0 / 20) < 0))
        return false;
    // Different RTTs preserve these stationary points, not necessarily their speed.
    return close(netChange(100, 200, 100, 1.0 / 200, 0.04), 0) &&
            close(netChange(100, 200, 100, 1.0 / 200, 0.16), 0);
}
static_assert(equilibriumChecks(), "ABC PF allocations satisfy the idealized control equation");
// Scope regression: max-bottleneck PINT does not sum two simultaneous prices.
// In the two-link X/Y/Z example, its global PF allocation is not stationary.
static_assert(netChange(100.0 / 3, 100.0 / 3, 50, 3.0 / 200) > 0,
        "Do not claim general multi-bottleneck proportional fairness");

constexpr bool withdrawalChecks()
{
    if (withdraw(10000, 9900, 10100, 2000, 4, 0.25) != 9600)
        return false;
    if (withdraw(10000, 8000, 11000, 2000, 4, 0.25) != 7500)
        return false;
    if (withdraw(10000, 1000, 1500, 2000, 4, 0.25) != 1500)
        return false; // probe floor cannot override the uncoupled safety bound
    if (withdraw(10000, 10100, 10500, 2000, 4, 0.25) != 10100)
        return false; // do not accelerate positive growth
    for (unsigned int utilization = 1; utilization <= 200; utilization++) {
        for (unsigned int share = 0; share <= 100; share++) {
            const double own = orbTarget(10000, utilization / 100.0, 0.95, 500 * share / 100.0);
            const double uncoupled = orbTarget(10000, utilization / 100.0, 0.95, 500);
            const double target = withdraw(10000, own, uncoupled, 2000, 4, 0.25);
            if (target < 0 || target > uncoupled)
                return false;
        }
    }
    return true;
}
static_assert(withdrawalChecks(), "Withdrawal and probing must respect the uncoupled target");
static_assert(reentryOpportunity(0.001, 4, 0.5, 0.95, false), "Weak underloaded path can re-enter");
static_assert(reentryOpportunity(0.001, 4, 1, 0.95, true), "New B/N opportunity can trigger re-entry");
static_assert(!reentryOpportunity(0.001, 4, 0.96, 0.95, false), "No steady-state boost without improvement");
static_assert(!reentryOpportunity(0.25, 4, 0.5, 0.95, false), "Only weak paths need a boost");
static_assert(!reentryOpportunity(1, 1, 0.5, 0.95, true), "Keep single-path behavior uncoupled");

constexpr bool aiBudgetChecks()
{
    // Carry fractional AI across changing base budgets and weights; no call
    // may exceed Uncoupled, and residual must never leave [0, 1).
    double residual = 0;
    for (uint32_t budget = 0; budget <= 100; budget++) {
        for (unsigned int part = 0; part <= 100; part++) {
            const auto actual = attenuate(budget, part / 100.0, residual);
            if (actual > budget || residual < 0 || residual >= 1)
                return false;
        }
    }
    residual = 0;
    uint32_t total = 0;
    for (int i = 0; i < 100; i++)
        total += attenuate(1, 0.25, residual);
    if (total != 25 || residual != 0)
        return false;
    residual = 0.75;
    if (attenuate(0, 0.5, residual) != 0 || residual != 0)
        return false;
    residual = 0.75;
    if (attenuate(1, 1, residual) != 1 || residual != 0)
        return false;
    residual = 0.75;
    if (attenuate(1, 0, residual) != 0 || residual != 0)
        return false;
    residual = 0.75;
    constexpr uint32_t largestBudget = std::numeric_limits<uint32_t>::max();
    if (attenuate(largestBudget, 1 - 1e-16, residual) > largestBudget)
        return false;
    residual = std::numeric_limits<double>::quiet_NaN();
    if (attenuate(100, 0.5, residual) != 50 || residual != 0)
        return false;
    return attenuate(100, std::numeric_limits<double>::quiet_NaN(), residual) == 100;
}

static_assert(aiBudgetChecks(), "Weighted AI must stay within the uncoupled budget");
