#!/usr/bin/env python3
"""Check the actual Pressure window-update body without building a simulator.

The method is extracted from the source and constant-evaluated with minimal
state/signal stubs. C++23 is used only for constexpr std::isfinite; production
sources should also be syntax-checked with the project's C++17 settings.
This does not exercise ACK timing, telemetry ingestion or the scheduler.
"""

from pathlib import Path
import subprocess
import tempfile


source = (Path(__file__).resolve().parents[1] /
          "src/transportlayer/tcp/flavours/MpOrbPressure.cc").read_text()
start = source.index("uint32_t MpOrbPressure::computeWnd(")
end = source.index("\nvoid MpOrbPressure::processRexmitTimer", start)
method = source[start:end].replace(
    "uint32_t MpOrbPressure::computeWnd(", "constexpr uint32_t computeWnd(", 1)

# No parent controller or cwnd-limited method is supplied: the startup and
# single-path branches must be self-contained as well as the coupled branch.
preamble = r"""
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
struct State {
    uint32_t prevWnd=10000, snd_cwnd=10000, snd_mss=1000;
    uint32_t additiveIncrease=100, ssthresh=0;
    double eta=0.95, txRate=0;
    bool initialPhase=false, endInitialPhase=false;
};
struct Connection { constexpr void emit(int, double) {} };
struct Controller {
    State storage;
    State *state=&storage;
    Connection connection;
    Connection *conn=&connection;
    bool couplingActive=true, ready=true, updateWindow=true;
    double decreaseGain=1, maxDecreaseFraction=0.25;
    uint32_t uncoupledAdditiveIncrease=500;
    static constexpr int windowDeltaSignal=1, extraWithdrawalSignal=2, txRateSignal=3;
    constexpr bool isPressureCouplingReady() const { return ready; }
"""

checks = r"""
};
constexpr bool checkWindows() {
    Controller c;
    if (c.computeWnd(0.5, false) != 10100 || c.state->prevWnd != 10000 || !c.updateWindow) return false;
    if (c.computeWnd(0.5, false) != 10100) return false; // ACKs do not compound AI
    if (c.computeWnd(1, true) != 9600 || c.state->prevWnd != 9600 || c.updateWindow) return false;

    Controller fast;
    fast.decreaseGain=4;
    if (fast.computeWnd(1, false) != 8400) return false;
    fast.state->additiveIncrease=0;
    if (fast.computeWnd(1.1875, false) != 7500) return false; // extra-step bound
    if (fast.computeWnd(2, false) != 4750) return false; // ordinary reduction may be larger
    fast.state->prevWnd=1000; fast.state->snd_cwnd=1000;
    fast.uncoupledAdditiveIncrease=100;
    if (fast.computeWnd(1, false) != 1050) return false; // uncoupled cap wins over 2-MSS floor

    Controller startup;
    startup.state->initialPhase=true; startup.ready=false;
    startup.state->additiveIncrease=500; startup.state->ssthresh=10200;
    if (startup.computeWnd(2, false) != 10200 || !startup.state->initialPhase) return false;
    if (startup.computeWnd(2, true) != 10200 || startup.state->initialPhase) return false;

    Controller single; single.couplingActive=false; single.state->additiveIncrease=500;
    if (single.computeWnd(0.5, false) != 10500) return false;
    Controller fallback; fallback.ready=false; fallback.state->additiveIncrease=500;
    if (fallback.computeWnd(0.5, false) != 10500) return false;
    Controller noAnchor; noAnchor.state->prevWnd=0;
    if (noAnchor.computeWnd(0.5, false) != 10100) return false;

    Controller tiny;
    tiny.state->prevWnd=1000; tiny.state->snd_cwnd=1000;
    tiny.uncoupledAdditiveIncrease=2000;
    if (tiny.computeWnd(0.5, false) != 2000) return false; // probe floor
    Controller large;
    large.state->prevWnd=std::numeric_limits<uint32_t>::max();
    if (large.computeWnd(0.5, false) != std::numeric_limits<uint32_t>::max()) return false;
    return true;
}
static_assert(checkWindows(), "Pressure window, startup, fallback and commit rules");

constexpr bool checkCap() {
    for (int gain : {1, 4}) {
        for (int utilization : {50, 95, 100, 125, 200}) {
            for (uint32_t ai : {0U, 100U, 250U, 500U}) {
                Controller c;
                c.decreaseGain=gain; c.state->additiveIncrease=ai;
                const double u=utilization / 100.0;
                const double fullAiWindow=10000 * std::min(1.0, 0.95 / u) + 500;
                if (c.computeWnd(u, false) > fullAiWindow) return false;
            }
        }
    }
    return true;
}
static_assert(checkCap(), "The full-AI OrbCC equation bounds Pressure's target");
"""

with tempfile.TemporaryDirectory(prefix="pressure-window-check-") as temp:
    path = Path(temp) / "window.cc"
    path.write_text(preamble + method + checks)
    subprocess.run(["clang++", "-std=c++23", "-fsyntax-only", str(path)], check=True)

print("Pressure window compile-time checks passed (no simulator build or run).")
