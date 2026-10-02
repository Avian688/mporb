#!/usr/bin/env python3
"""Constant-evaluate Delta's production AI hook; no executable or simulator built.

Transport plumbing is stubbed. The Alpha hook intentionally doubles AI, to
detect accidentally scaling Alpha's output instead of the incoming OrbCC AI.
"""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
FLAVOURS = ROOT / 'src/transportlayer/tcp/flavours'
source = (FLAVOURS / 'MpOrbSemiCoupledDelta.cc').read_text()
start = source.index('void MpOrbSemiCoupledDelta::adjustAdditiveIncrease()')
method = source[start:source.index('\n} // namespace tcp', start)]
method = method.replace('void MpOrbSemiCoupledDelta::', 'constexpr void MpOrbSemiCoupledDelta::')
header = (FLAVOURS / 'MpOrbSemiCoupledDelta.h').read_text()
header = header[header.index('class MpOrbSemiCoupledDelta'):header.index('\n};') + 3]
header = header.replace('  protected:', '  public:').replace('void adjustAdditiveIncrease()',
                                                         'constexpr void adjustAdditiveIncrease()')
signal_ids = {}


def signal(match):
    name = match.group(1)
    signal_ids[name] = len(signal_ids)
    return f'static constexpr int {name} = {signal_ids[name]};'


header = re.sub(r'static simsignal_t (\w+);', signal, header)
preamble = r'''
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>
struct Time {
    double value = 0;
    constexpr double dbl() const { return value; }
    constexpr operator double() const { return value; }
};
constexpr Time SIMTIME_ZERO{};
constexpr int TCP_S_ESTABLISHED = 1, TCP_S_CLOSE_WAIT = 2;
struct Algorithm { virtual constexpr ~Algorithm() = default; };
struct OrbtcpStateVariables {
    uint32_t snd_cwnd = 1000, additiveIncrease = 1000;
    Time srtt{.1};
    bool initialPhase = false;
};
struct Connection {
    Algorithm *algorithm = nullptr;
    OrbtcpStateVariables *state = nullptr;
    int fsm = TCP_S_ESTABLISHED;
    double signals[8] = {};
    constexpr auto getTcpAlgorithm() const { return algorithm; }
    constexpr auto getState() const { return state; }
    constexpr auto getFsmState() const { return fsm; }
    constexpr void emit(int id, double value) { signals[id] = value; }
};
struct MpTcpConnection {
    std::vector<Connection *> subflows;
    constexpr const auto& getSubflows() const { return subflows; }
};
struct MpOrbSemiCoupledAlpha : Algorithm {
    static constexpr int subflowRateSignal = 4, connectionRateSignal = 5, rateShareSignal = 6;
    OrbtcpStateVariables storage;
    OrbtcpStateVariables *state = &storage;
    Connection connection{this, state};
    Connection *conn = &connection;
    MpTcpConnection *meta = nullptr;
    bool firstRTT = false;
    double additiveIncreaseResidual = 0;
    virtual constexpr void adjustAdditiveIncrease() { state->additiveIncrease *= 2; }
    constexpr auto getMetaConnection() const { return meta; }
};
'''
checks = r'''
using Delta = MpOrbSemiCoupledDelta;
constexpr bool near(double a, double b) { return a-b < 1e-9 && b-a < 1e-9; }
constexpr double weight(const Delta& d) { return d.conn->signals[Delta::weightSignal]; }
constexpr double count(const Delta& d) { return d.conn->signals[Delta::eligibleSubflowsSignal]; }
constexpr uint32_t update(Delta& d, uint32_t incoming=1000) {
    d.state->additiveIncrease = incoming;
    d.adjustAdditiveIncrease();
    return d.state->additiveIncrease;
}
constexpr bool equalPaths() {
    for (int n : {1,2,4,8}) {
        MpTcpConnection meta;
        std::array<Delta,8> paths;
        for (int i=0;i<n;++i) {
            paths[i].meta=&meta;
            meta.subflows.push_back(paths[i].conn);
        }
        for (int i=0;i<n;++i) {
            if (update(paths[i])!=1000 || count(paths[i])!=n || weight(paths[i])!=1) return false;
        }
    }
    return true;
}
static_assert(equalPaths(), "Equal paths retain full local AI; no extra Alpha multiplier");

constexpr bool asymmetricAndUnequalRtt() {
    MpTcpConnection meta;
    Delta a,b;
    a.meta=b.meta=&meta; meta.subflows={a.conn,b.conn};
    a.state->snd_cwnd=800; b.state->snd_cwnd=200;
    if (update(a)!=1000 || update(b)!=400 || !near(weight(a),1) || !near(weight(b),.4)) return false;
    // Equal estimated rates with unequal RTTs and unequal local B/N-based AI.
    a.state->srtt={.08}; b.state->srtt={.04}; b.state->snd_cwnd=400;
    if (update(a,100)!=100 || update(b,350)!=350) return false;
    // When the sibling leaves, count only the remaining established path.
    b.conn->fsm=0;
    return update(a)==1000 && count(a)==1 && weight(a)==1;
}
static_assert(asymmetricAndUnequalRtt(), "Cap local AI, use cwnd/RTT, adapt K after departure");

constexpr bool invalidPeers() {
    MpTcpConnection meta;
    Delta a,b;
    Algorithm foreign;
    OrbtcpStateVariables foreignState;
    Connection other{&foreign,&foreignState};
    a.meta=b.meta=&meta; meta.subflows={a.conn,b.conn,nullptr,&other};
    for (int invalid=0;invalid<5;++invalid) {
        b.conn->fsm=TCP_S_ESTABLISHED; b.conn->state=b.state;
        b.state->srtt={.1}; b.state->snd_cwnd=1000;
        if (invalid==0) b.conn->fsm=0;
        if (invalid==1) b.conn->state=nullptr;
        if (invalid==2) b.state->srtt={0};
        if (invalid==3) b.state->snd_cwnd=0;
        if (invalid==4) b.state->srtt={-.1};
        if (update(a)!=1000 || count(a)!=1) return false;
    }
    b.state->srtt={.1}; b.conn->fsm=TCP_S_CLOSE_WAIT;
    return update(a)==1000 && count(a)==2;
}
static_assert(invalidPeers(), "Count exactly valid Alpha-family rates; retain CLOSE_WAIT semantics");

constexpr bool startupAndMissingOwnState() {
    MpTcpConnection meta;
    Delta a,b;
    a.meta=b.meta=&meta; meta.subflows={a.conn,b.conn};
    a.state->snd_cwnd=1; b.state->snd_cwnd=999;
    a.firstRTT=true;
    if (update(a)!=1000) return false;
    a.firstRTT=false; a.state->initialPhase=true;
    if (update(a)!=1000) return false;
    a.state->initialPhase=false; a.meta=nullptr;
    if (update(a)!=1000) return false;
    a.meta=&meta; meta.subflows={b.conn};
    if (update(a)!=1000) return false;
    a.state=nullptr;
    a.adjustAdditiveIncrease();
    return true;
}
static_assert(startupAndMissingOwnState(), "Preserve startup and handle incomplete membership safely");

constexpr bool byteCreditAndBounds() {
    MpTcpConnection meta;
    Delta a,b;
    a.meta=b.meta=&meta; meta.subflows={a.conn,b.conn};
    a.state->snd_cwnd=1; b.state->snd_cwnd=999;
    unsigned total=0;
    for (int i=0;i<1000;++i) {
        auto ai=update(a,1);
        if (ai>1 || a.additiveIncreaseResidual<0 || a.additiveIncreaseResidual>=1) return false;
        total+=ai;
    }
    if (total<1 || total>2) return false; // 1000 * (2/1000), within one byte.
    double saved=a.additiveIncreaseResidual;
    if (update(a,0)!=0 || a.additiveIncreaseResidual!=saved) return false;
    for (uint32_t window : {1,10,100,200,500,800,1000}) {
        a.state->snd_cwnd=window; b.state->snd_cwnd=1000-window;
        for (uint32_t ai : {1U,19U,1000U,UINT32_MAX}) {
            const auto value=update(a,ai);
            const double share=a.conn->signals[Delta::rateShareSignal];
            if (value>ai || weight(a)<share || weight(a)>1) return false;
        }
    }
    return true;
}
static_assert(byteCreditAndBounds(), "Fractional AI survives; local output stays within uncoupled AI");
'''

with tempfile.TemporaryDirectory(prefix='delta-policy-check-') as temp:
    path = Path(temp) / 'delta.cc'
    path.write_text(preamble + header + method + checks)
    subprocess.run(['clang++', '-std=c++23', '-fsyntax-only', str(path)], check=True)
print('Delta production-hook constant-evaluation checks passed; no executable or simulation built/run.')
