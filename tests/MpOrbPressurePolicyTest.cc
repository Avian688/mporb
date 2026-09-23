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

constexpr bool aiBudgetChecks()
{
    // Carry fractional AI across changing base budgets and weights; no call
    // may exceed Uncoupled, and residual must never leave [0, 1).
    double residual = 0;
    for (uint32_t budget = 0; budget <= 100; budget++) {
        for (unsigned int part = 0; part <= 100; part++) {
            const auto actual = scaleAdditiveIncrease(budget, part / 100.0, residual);
            if (actual > budget || residual < 0 || residual >= 1)
                return false;
        }
    }
    residual = 0;
    uint32_t total = 0;
    for (int i = 0; i < 100; i++)
        total += scaleAdditiveIncrease(1, 0.25, residual);
    if (total != 25 || residual != 0)
        return false;
    residual = 0.75;
    if (scaleAdditiveIncrease(0, 0.5, residual) != 0 || residual != 0)
        return false;
    residual = 0.75;
    if (scaleAdditiveIncrease(1, 1, residual) != 1 || residual != 0)
        return false;
    residual = 0.75;
    if (scaleAdditiveIncrease(1, 0, residual) != 0 || residual != 0)
        return false;
    residual = 0.75;
    constexpr uint32_t largestBudget = std::numeric_limits<uint32_t>::max();
    if (scaleAdditiveIncrease(largestBudget, 1 - 1e-16, residual) > largestBudget)
        return false;
    residual = std::numeric_limits<double>::quiet_NaN();
    if (scaleAdditiveIncrease(100, 0.5, residual) != 50 || residual != 0)
        return false;
    return scaleAdditiveIncrease(100, std::numeric_limits<double>::quiet_NaN(), residual) == 100;
}

static_assert(aiBudgetChecks(), "Weighted AI must stay within the uncoupled budget");
