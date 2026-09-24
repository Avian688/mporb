#!/usr/bin/env python3
"""Constant-evaluate the production path calculation with transport stubs.

No simulator or executable is built. This checks ranking and admission arithmetic,
not packet timing, fairness turns, or telemetry ingestion.
"""
from pathlib import Path
import subprocess
import tempfile

base = Path(__file__).resolve().parents[1] / "src/transportlayer/tcp"
source = (base / "MpOrbIntScheduler.cc").read_text()
shared = base.parents[3] / "mptcp/src/transportlayer/tcp"
shared_source = (shared / "MpTcpPacketScheduler.cc").read_text()
bound_start = shared_source.index("uint32_t MpTcpPacketScheduler::getBoundedAssignmentSpace(")
bound_end = shared_source.index("\nSubflowConnection *MpTcpPacketScheduler::schedulePacket", bound_start)
start = source.index("MpOrbIntScheduler::PathEstimate MpOrbIntScheduler::evaluatePath(")
end = source.index("\nSubflowConnection *MpOrbIntScheduler::selectRetransmissionSubflow", start)
method = source[start:end].replace(
    "MpOrbIntScheduler::PathEstimate MpOrbIntScheduler::evaluatePath(",
    "constexpr PathEstimate evaluatePath(", 1)
bound_method = shared_source[bound_start:bound_end].replace("uint32_t MpTcpPacketScheduler::getBoundedAssignmentSpace(", "constexpr uint32_t getBoundedAssignmentSpace(", 1)
subflow_source = (shared / "SubflowConnection.cc").read_text()
a = subflow_source.index("uint32_t SubflowConnection::getSchedulerAvailableBytes() const")
b = subflow_source.index("\ndouble SubflowConnection::getSchedulerPacingRate", a)
available_method = subflow_source[a:b].replace("state == nullptr", "false").replace("uint32_t SubflowConnection::getSchedulerAvailableBytes() const", "constexpr uint32_t getSchedulerAvailableBytes() const", 1)
header = (base / "MpOrbIntScheduler.h").read_text()
assert "getBoundedAssignmentSpace" not in header, "INT must inherit the shared admission method"
start = header.index("    struct PathEstimate {")
end = header.index("    };", start) + len("    };")
estimate = header[start:end]

stubs = r"""
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
struct simtime_t {
    double value = 0;
    constexpr simtime_t(double value = 0) : value(value) {}
    constexpr operator double() const { return value; }
    constexpr double dbl() const { return value; }
};
constexpr simtime_t SIMTIME_ZERO=0, SIMTIME_MAX=1e99;
struct TcpPacedFamily {
    uint32_t cwnd=1000000;
    constexpr uint32_t getCwnd() const { return cwnd; }
};
struct SubflowConnection {
    TcpPacedFamily algorithm;
    struct State { uint32_t snd_mss=1448; } state;
    constexpr TcpPacedFamily *getTcpAlgorithm() { return &algorithm; }
    constexpr const State *getState() const { return &state; }
    double pacing=12500000, windowRate=12500000, rtt=0.04, forwardDelay=0.01;
    uint32_t unsent=0, space=1000000, queued=0, rwnd=0xffffffff;
    const void *sendQueue=this;
    bool fresh=true;
    constexpr double getSchedulerPacingRateBytesPerSecond() const { return pacing; }
    constexpr double getSchedulerWindowRateBytesPerSecond() const { return windowRate; }
    constexpr uint32_t getSchedulerUnsentBytes() const { return unsent; }
    constexpr uint32_t getDefaultSchedulerWriteLimit() const { return space; }
    constexpr uint32_t getSchedulerQueuedBytes() const { return queued; }
    constexpr uint32_t getSchedulerQueueLimit() const { return std::min(algorithm.cwnd, rwnd); }
__AVAILABLE_METHOD__
    constexpr simtime_t getSchedulingRtt() const { return rtt; }
    constexpr bool getSchedulerForwardDelay(simtime_t& delay) const {
        delay=forwardDelay; return fresh;
    }
};
using MpOrbSubflowConnection=SubflowConnection;
template<class T, class U> constexpr T check_and_cast(U *p) { return static_cast<T>(p); }
struct RateMap {
    struct Entry { double second=0; } entry;
    bool present=false;
    constexpr const Entry *find(SubflowConnection *) const { return present ? &entry : nullptr; }
    constexpr const Entry *end() const { return nullptr; }
};
struct Scheduler {
    RateMap avgPacingRates;
"""
checks = r"""
};
constexpr bool checkPaths() {
    Scheduler s;
    SubflowConnection p;
    auto fast=s.evaluatePath(&p,65428);
    if (fast.space != p.space || !fast.freshFeedback) return false;
    p.pacing=125000;
    if (!(fast.score < s.evaluatePath(&p,65428).score)) return false;
    p.pacing=12500000; p.forwardDelay=0.04;
    if (!(fast.score < s.evaluatePath(&p,65428).score)) return false;
    p.forwardDelay=0.01; p.unsent=50000;
    if (!(fast.score < s.evaluatePath(&p,65428).score)) return false;
    // Backlogs beyond the former 10 ms limit must not impose a second cap.
    for (uint32_t backlog : {120000U,125000U,200000U}) {
        p.unsent=backlog; p.queued=backlog;
        if (s.evaluatePath(&p,65428).space != p.algorithm.cwnd - backlog) return false;
    }
    p.unsent=0; p.queued=0; p.pacing=1000;
    if (s.evaluatePath(&p,65428).space != p.space) return false;
    p.pacing=12500000; p.space=500;
    if (s.evaluatePath(&p,65428).space != 500) return false;
    p.space=0;
    if (s.evaluatePath(&p,65428).space != 0) return false;
    p.space=1000000; p.queued=900000;
    if (s.evaluatePath(&p,65428).space != 100000) return false;
    p.queued=1000000;
    if (s.evaluatePath(&p,65428).space != 0) return false;
    p.queued=1100000;
    if (s.evaluatePath(&p,65428).space != 0) return false;
    p.queued=0;
    p.fresh=false;
    auto fallback=s.evaluatePath(&p,65428);
    if (fallback.freshFeedback || fallback.score != 0.02 + 65428.0/12500000) return false;
    p.rtt=0;
    if (s.evaluatePath(&p,65428).score != std::numeric_limits<double>::infinity()) return false;
    return true;
}
static_assert(checkPaths(), "INT ranking, shared cwnd burst cap and write memory; fallback rules");

constexpr bool checkRateBounds() {
    Scheduler s;
    SubflowConnection p;
    s.avgPacingRates.present=true;
    s.avgPacingRates.entry.second=100;
    p.pacing=20; p.windowRate=30; p.unsent=0; p.forwardDelay=0;
    if (s.evaluatePath(&p,100).score != 5) return false;
    p.pacing=100; p.windowRate=10;
    if (s.evaluatePath(&p,100).score != 10 || s.evaluatePath(&p,100).space != p.space) return false;
    s.avgPacingRates.entry.second=5; p.windowRate=100;
    if (s.evaluatePath(&p,100).score != 20) return false;
    p.windowRate=0;
    if (s.evaluatePath(&p,100).hasRate || s.evaluatePath(&p,100).space != p.space) return false;
    p.windowRate=100; p.pacing=std::numeric_limits<double>::quiet_NaN();
    if (s.evaluatePath(&p,100).hasRate) return false;
    p.pacing=1e300; p.windowRate=1e300; s.avgPacingRates.entry.second=1e300;
    p.space=std::numeric_limits<uint32_t>::max();
    p.algorithm.cwnd=p.space;
    if (s.evaluatePath(&p,65428).space != p.space) return false;
    return true;
}
constexpr bool checkSharedAllowance() {
    Scheduler s;
    SubflowConnection p;
    p.algorithm.cwnd=1448;
    // Each selection is capped, but repeated selections may queue > cwnd.
    for (int i=0; i<100; ++i) {
        if (s.getBoundedAssignmentSpace(&p,1448) >= 1448) {
            p.unsent += 1448;
            p.queued += 1448;
        }
    }
    if (p.unsent != 144800) return false;
    p.unsent=0;
    if (s.getBoundedAssignmentSpace(&p,1448) != 1448) return false;
    p.algorithm.cwnd=250000; p.queued=200000; p.unsent=100000;
    if (s.getBoundedAssignmentSpace(&p,1448) != 250000) return false;
    p.rwnd=0; // TCP, not this scheduler cap, enforces the receive window.
    if (s.getBoundedAssignmentSpace(&p,1448) != 250000) return false;
    p.algorithm.cwnd=400;
    if (s.getBoundedAssignmentSpace(&p,65428) != 1448) return false;
    p.queued=p.space-500;
    if (s.getBoundedAssignmentSpace(&p,1448) != 500) return false;
    p.queued=p.space;
    if (s.getBoundedAssignmentSpace(&p,1448) != 0) return false;
    return true;
}
static_assert(checkSharedAllowance(), "Shared burst-only cap: reselection, rwnd independence, MSS floor and write memory");
static_assert(checkRateBounds(), "Current, average and cwnd rate bounds; invalid rates; overflow");
"""
with tempfile.TemporaryDirectory(prefix="int-scheduler-check-") as temp:
    path = Path(temp) / "scheduler.cc"
    path.write_text(stubs.replace("__AVAILABLE_METHOD__", available_method) + estimate + bound_method + method + checks)
    subprocess.run(["clang++", "-std=c++23", "-fsyntax-only", str(path)], check=True)
print("INT scheduler compile-time checks passed (no simulator build or run).")
