#!/usr/bin/env python3
"""Constant-evaluate production Beta hooks; no executable/simulation is built.

OMNeT++ state/connection plumbing and PINT acceptance are stubbed. The Alpha
hook is deliberately a boost, to catch accidentally invoking it from Beta.
"""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
flavours = root / 'src/transportlayer/tcp/flavours'
source = (flavours / 'MpOrbSemiCoupledBeta.cc').read_text()
start = source.index('bool MpOrbSemiCoupledBeta::hasFreshFeedback')
methods = source[start:source.index('\n} // namespace tcp', start)]
methods = methods.replace('double MpOrbSemiCoupledBeta::', 'constexpr double MpOrbSemiCoupledBeta::')
methods = methods.replace('void MpOrbSemiCoupledBeta::', 'constexpr void MpOrbSemiCoupledBeta::')
methods = methods.replace('bool MpOrbSemiCoupledBeta::', 'constexpr bool MpOrbSemiCoupledBeta::')
header = (flavours / 'MpOrbSemiCoupledBeta.h').read_text()
header = header[header.index('class MpOrbSemiCoupledBeta'):header.index('\n};') + 3]
header = header.replace('  protected:', '  public:')
signal_ids = {}


def signals(match):
    declarations = []
    for name in match.group(1).split(','):
        name = name.strip()
        signal_ids[name] = len(signal_ids)
        declarations.append(f'{name}={signal_ids[name]}')
    return 'static constexpr int ' + ', '.join(declarations) + ';'


header = re.sub(r'static simsignal_t ([^;]+);', signals, header)
header = header.replace('void initialize() override;', '')
header = header.replace('void ', 'constexpr void ').replace('double measureInflight(', 'constexpr double measureInflight(')
header = header.replace('bool hasFreshFeedback()', 'constexpr bool hasFreshFeedback()')

preamble = r'''
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>
struct Time {
    double value = 0;
    constexpr double dbl() const { return value; }
    constexpr operator double() const { return value; }
    friend constexpr Time operator-(Time a, Time b) { return {a.value-b.value}; }
    friend constexpr Time operator+(Time a, Time b) { return {a.value+b.value}; }
};
using simtime_t = Time;
constexpr Time SIMTIME_ZERO{};
constexpr int TCP_S_ESTABLISHED = 1, TCP_S_CLOSE_WAIT = 2;
using TcpEventCode = int;
struct Record {
    Time time;
    double bandwidth = 1000, u = .95;
    uint32_t flows = 1, ai = 1000, digest = 1;
    bool accepted = true;
    constexpr Time getTs() const { return time; }
    constexpr double getB() const { return bandwidth; }
};
using IntDataVec = std::vector<Record>;
struct OrbtcpStateVariables {
    uint32_t additiveIncrease = 0, snd_cwnd = 10, sharingFlows = 1;
    Time srtt{.1};
    double u = .95, eta = .95;
    bool initialPhase = false, lossRecovery = false, afterRto = false;
};
struct MpOrbSemiCoupledAlpha;
struct Connection {
    int fsm = TCP_S_ESTABLISHED;
    MpOrbSemiCoupledAlpha* algorithm = nullptr;
    OrbtcpStateVariables* state = nullptr;
    double signals[24] = {};
    int id = 0;
    constexpr int getId() const { return id; }
    constexpr void emit(int id, double value) { signals[id] = value; }
    constexpr int getFsmState() const { return fsm; }
    constexpr MpOrbSemiCoupledAlpha* getTcpAlgorithm() const { return algorithm; }
    constexpr OrbtcpStateVariables* getState() const { return state; }
};
struct MpTcpConnection {
    std::vector<Connection*> subflows;
    Time now;
    constexpr const auto& getSubflows() const { return subflows; }
};
struct MpOrbSemiCoupledAlpha {
    static constexpr int subflowRateSignal=20, connectionRateSignal=21, rateShareSignal=22;
    OrbtcpStateVariables storage;
    OrbtcpStateVariables* state = &storage;
    Connection connection{TCP_S_ESTABLISHED, this, state};
    Connection* conn = &connection;
    MpTcpConnection* meta = nullptr;
    Time now;
    bool firstRTT = false, hasPathDigest = false;
    uint32_t lastPathDigest = 0;
    double additiveIncreaseResidual = 0;
    int rtos = 0, losses = 0, alphaHookCalls = 0;
    constexpr MpTcpConnection* getMetaConnection() { return meta; }
    constexpr Time simTime() const { return meta ? meta->now : now; }
    virtual constexpr void adjustAdditiveIncrease() {
        ++alphaHookCalls;
        state->additiveIncrease *= 2; // Beta must never use this boost.
    }
    virtual constexpr double measureInflight(const IntDataVec& data) {
        if (data.empty()) return 0;
        auto record = data.front();
        if (!record.accepted || !std::isfinite(record.bandwidth) || record.bandwidth <= 0 ||
            !std::isfinite(record.u) || record.u <= 0) return 0;
        if (hasPathDigest && record.digest != lastPathDigest) {
            lastPathDigest = record.digest;
            return state->u; // PINT parent returns old U on route change.
        }
        hasPathDigest = true; lastPathDigest = record.digest;
        if (record.flows == 0) return 0;
        state->sharingFlows=record.flows;
        state->u = record.u; state->additiveIncrease = record.ai;
        adjustAdditiveIncrease();
        return state->u;
    }
    virtual constexpr void processRexmitTimer(TcpEventCode&) { rtos++; state->afterRto=true; }
    virtual constexpr void rackLossDetected() { losses++; state->lossRecovery=true; }
};
'''
checks = r'''
using Beta = MpOrbSemiCoupledBeta;
constexpr bool near(double a, double b) { return a-b < 1e-9 && b-a < 1e-9; }
constexpr double signal(const Beta& c, int id) { return c.connection.signals[id]; }
template<class A=Beta, class B=Beta>
struct Pair {
    A a;
    B b;
    MpTcpConnection meta{{&a.connection,&b.connection}};
    constexpr Pair() {
        a.meta=b.meta=&meta;
        a.connection.id=1; b.connection.id=2;
        a.state->snd_cwnd=99; b.state->snd_cwnd=1;
        b.state->sharingFlows=4;
    }
};
constexpr void feed(MpOrbSemiCoupledAlpha& c, double time=0, double u=.98, uint32_t ai=1000,
                    double bandwidth=1000, uint32_t digest=1, bool accepted=true) {
    if (c.meta) c.meta->now={time+.025};
    else c.now={time+.025};
    c.measureInflight({{{time},bandwidth,u,c.state->sharingFlows,ai,digest,accepted}});
}
constexpr void ready(Pair<>& p, double ua=.98, double ub=.1) {
    feed(p.a,0,ua); feed(p.b,0,ub); feed(p.a,0,ua);
}
constexpr bool redistribution() {
    Pair<> p;
    ready(p);
    if (!near(p.a.roundWeight,.79) || !near(p.b.roundWeight,.21)) return false;
    if (!near(signal(p.a,Beta::correctionSignal),-.2) ||
        !near(signal(p.b,Beta::correctionSignal),.2)) return false;
    if (!near(p.a.roundWeight+p.b.roundWeight,1) || !p.a.roundActive || !p.b.roundActive) return false;
    if (signal(p.a,Beta::alphaAiSignal)!=990 || signal(p.b,Beta::alphaAiSignal)!=10) return false;
    if (!near(p.a.roundError,0) || !near(p.b.roundError,-.8)) return false;
    if (p.a.roundMembers!=p.b.roundMembers || p.a.roundUntil!=p.b.roundUntil) return false;
    if (p.a.alphaHookCalls || p.b.alphaHookCalls) return false;
    // High U also redistributes, with no requirement for an underutilised path.
    Pair<> high;
    high.a.state->snd_cwnd=high.b.state->snd_cwnd=50;
    ready(high,.98,1.5);
    if (!near(high.a.roundWeight,.625) || !near(high.b.roundWeight,.375)) return false;
    // B/N does not enter the new coupling: very different B/N, same U and rates.
    feed(high.a,.11,.98,1000,1000000); feed(high.b,.11,1.5,1000,1);
    return near(high.a.roundWeight,.625) && near(high.b.roundWeight,.375);
}
static_assert(redistribution(), "Proposed example, both redistribution directions, and no B/N or Alpha boost");

constexpr bool plainAlpha() {
    for (double u : {.1,.95,.98,1.5}) {
        Pair<> p;
        ready(p,u,u);
        if (p.a.roundActive || p.b.roundActive) return false;
        p.a.state->snd_cwnd=25; p.b.state->snd_cwnd=75;
        feed(p.a,.01,u); feed(p.b,.01,u);
        if (!near(signal(p.a,Beta::weightSignal),.25) || !near(signal(p.b,Beta::weightSignal),.75)) return false;
    }
    Pair<> disabled;
    disabled.a.redistributionGain=disabled.b.redistributionGain=0;
    ready(disabled);
    if (disabled.a.roundActive || signal(disabled.a,Beta::weightSignal)!=.99) return false;
    Pair<> band;
    ready(band,.92,.99); // Different U, both within the deadband.
    if (band.a.roundActive || band.b.roundActive) return false;
    // Identical errors with three paths must not enable a roundoff-only correction.
    Pair<> triple;
    Beta c;
    c.connection.id=3; c.meta=&triple.meta;
    triple.meta.subflows.push_back(&c.connection);
    feed(triple.a,0,1.3); feed(triple.b,0,1.3); feed(c,0,1.3);
    return !triple.a.roundActive && !triple.b.roundActive && !c.roundActive;
}
static_assert(plainAlpha(), "Live plain Alpha in deadband, for equal errors and at gain zero");

constexpr bool sharedRounds() {
    Pair<> p;
    ready(p);
    const auto expiry=p.a.roundUntil;
    for (int ack=0;ack<100;++ack) { feed(p.a); feed(p.b,0,.1); }
    if (!near(p.a.roundWeight,.79) || !near(p.b.roundWeight,.21) || p.a.roundUntil!=expiry) return false;
    p.a.state->snd_cwnd=49; p.b.state->snd_cwnd=51;
    feed(p.a,.04); feed(p.b,.04,.1);
    if (!near(signal(p.a,Beta::weightSignal),.79) || !near(signal(p.a,Beta::baselineShareSignal),.99) ||
        !near(signal(p.a,Beta::currentRateShareSignal),.49)) return false;
    feed(p.b,.101,.1); // Any sibling can refresh the entire set at expiry.
    if (!near(p.a.roundWeight,.29) || !near(p.b.roundWeight,.71)) return false;
    if (p.a.roundUntil!=p.b.roundUntil || p.a.roundUntil<=expiry) return false;
    feed(p.a,.101);
    if (!near(signal(p.a,Beta::weightSignal),.29)) return false;
    // Maximum participating RTT determines the common control interval.
    Pair<> unequal;
    unequal.b.state->srtt={.2};
    ready(unequal);
    if (!near(unequal.a.roundUntil,.225)) return false;
    feed(unequal.a,.15);
    if (!near(unequal.a.roundUntil,.225)) return false;
    feed(unequal.b,.205,.1);
    return near(unequal.a.roundUntil,.430) && unequal.a.roundUntil==unequal.b.roundUntil;
}
static_assert(sharedRounds(), "Common cached weights, no per-ACK accumulation and correct RTT timing");

constexpr bool clippingAndRecovery() {
    Pair<> p;
    ready(p,.98,10);
    if (!near(p.a.roundWeight,1) || p.b.roundWeight!=0) return false;
    if (p.a.state->additiveIncrease>1000 || p.b.state->additiveIncrease!=0) return false;
    // The zero-AI path still gains weight when new probes report low U.
    feed(p.b,.101,.1);
    if (!near(p.b.roundWeight,.21)) return false;
    // Clear all differences at a later refresh: return to plain Alpha.
    feed(p.a,.202,.98); feed(p.b,.202,.98); feed(p.a,.303,.98);
    if (p.a.roundActive || signal(p.a,Beta::weightSignal)!=.99) return false;
    return true;
}
static_assert(clippingAndRecovery(), "Nonnegative normalized weights, zero-AI reentry and return to Alpha");

constexpr bool roundingAndStartup() {
    Pair<> p;
    ready(p);
    p.a.additiveIncreaseResidual=p.b.additiveIncreaseResidual=0;
    uint32_t total=0;
    for (int i=0;i<100;++i) {
        feed(p.a,0,.98,1); feed(p.b,0,.1,1);
        total+=p.a.state->additiveIncrease+p.b.state->additiveIncrease;
    }
    if (!near(total+p.a.additiveIncreaseResidual+p.b.additiveIncreaseResidual,100)) return false;
    for (uint32_t ai : {0U,1U,7U,1000U,UINT32_MAX}) {
        feed(p.a,0,.98,ai); feed(p.b,0,.1,ai);
        if (p.a.state->additiveIncrease>ai || p.b.state->additiveIncrease>ai) return false;
        if (p.a.additiveIncreaseResidual<0 || p.a.additiveIncreaseResidual>=1 ||
            p.b.additiveIncreaseResidual<0 || p.b.additiveIncreaseResidual>=1) return false;
    }
    p.meta.subflows.pop_back();
    p.a.additiveIncreaseResidual=.75;
    feed(p.a,0,.98,UINT32_MAX);
    if (p.a.state->additiveIncrease!=UINT32_MAX || signal(p.a,Beta::weightSignal)!=1) return false;
    p.a.state->initialPhase=true;
    p.a.additiveIncreaseResidual=.5;
    feed(p.a,0,.98,7);
    if (p.a.state->additiveIncrease!=7 || p.a.additiveIncreaseResidual!=.5) return false;
    p.a.state->initialPhase=false; p.a.firstRTT=true;
    feed(p.a,0,.98,9);
    return p.a.state->additiveIncrease==9 && p.a.alphaHookCalls==0;
}
static_assert(roundingAndStartup(), "Fractional credit, uncoupled cap, one path and unchanged startup");

constexpr bool fallbackAndMembership() {
    for (int guard=0;guard<12;++guard) {
        Pair<> p;
        ready(p);
        switch (guard) {
            case 0: p.b.firstRTT=true; break;
            case 1: p.b.state->initialPhase=true; break;
            case 2: p.b.state->lossRecovery=true; break;
            case 3: p.b.state->afterRto=true; break;
            case 4: p.b.hasFeedback=false; break;
            case 5: p.b.smoothedU=std::numeric_limits<double>::quiet_NaN(); break;
            case 6: p.b.smoothedU=std::numeric_limits<double>::infinity(); break;
            case 7: p.b.redistributionGain=.9; break;
            case 8: p.b.sampleTime={-1}; break;
            case 9: p.b.sampleTime={1}; break;
            case 10: p.b.utilizationDeadband=.1; break;
            case 11: p.b.smoothingRtts=2; break;
        }
        feed(p.a,.05);
        if (signal(p.a,Beta::activeSignal)!=0 || signal(p.a,Beta::correctionSignal)!=0 ||
            signal(p.a,Beta::weightSignal)!=.99 || !std::isnan(signal(p.a,Beta::errorSignal)) ||
            !p.b.roundMembers.empty()) return false;
    }
    Pair<Beta,MpOrbSemiCoupledAlpha> mixed;
    feed(mixed.a);
    if (mixed.a.state->additiveIncrease!=990 || mixed.a.alphaHookCalls) return false;
    Pair<> p;
    ready(p);
    p.b.connection.fsm=0;
    p.meta.subflows.push_back(nullptr);
    feed(p.a,.01);
    if (signal(p.a,Beta::weightSignal)!=1) return false;
    p.b.connection.fsm=TCP_S_CLOSE_WAIT;
    feed(p.a,.02);
    if (!near(p.a.roundWeight,.79)) return false;
    // An already-expired sibling must invalidate a still-cached correction.
    p.b.sampleTime={-.19};
    feed(p.a,.03);
    return signal(p.a,Beta::activeSignal)==0 && signal(p.a,Beta::weightSignal)==.99;
}
static_assert(fallbackAndMembership(), "Pool-wide freshness/settings/recovery fallback and membership changes");

constexpr bool telemetryAndInvalidation() {
    Pair<> p;
    ready(p);
    // Repeated sample timestamps cannot repeatedly smooth the same observation.
    feed(p.a,0,.5);
    if (!near(p.a.smoothedU,.98)) return false;
    feed(p.a,.05,.5);
    if (!near(p.a.smoothedU,.74)) return false;
    feed(p.a,.05,.5);
    if (!near(p.a.smoothedU,.74)) return false;
    p.meta.now={.1};
    if (p.a.measureInflight({{{0},1000,.1}})!=0 || p.a.sampleTime!=.05) return false;
    feed(p.a,.1,.1,1000,1000,1,false);
    feed(p.a,.1,.1,1000,0);
    feed(p.a,.1,0);
    p.a.measureInflight({{{.1},1000,.1,0}});
    if (p.a.sampleTime!=.05 || !near(p.a.smoothedU,.74)) return false;
    // Route changes discard prior-path U and invalidate the whole cached snapshot.
    p.meta.now={.11};
    auto epoch=p.a.feedbackEpoch;
    if (p.a.measureInflight({{{.08},1000,.1,1,1000,2}})!=0 || p.a.hasFeedback ||
        p.a.feedbackEpoch==epoch) return false;
    feed(p.b,.09,.1);
    if (signal(p.b,Beta::activeSignal)!=0) return false;
    feed(p.a,.10,.98,1000,1000,2);
    if (!near(p.a.smoothedU,.98) || !p.a.roundActive) return false;
    const auto expiry=p.a.roundUntil;
    p.b.rackLossDetected();
    feed(p.a,.11,.98,1000,1000,2);
    if (p.a.roundActive || p.b.hasFeedback || p.b.losses!=1) return false;
    p.b.state->lossRecovery=false;
    feed(p.b,.12,.1);
    if (!p.a.roundActive || p.a.roundUntil<=expiry) return false;
    p.meta.now={.5};
    if (p.a.measureInflight({{{.11},1000,.98,1,1000,2}})!=0 || p.a.hasFeedback) return false;
    feed(p.a,.5,.98,1000,1000,2);
    if (p.a.roundActive) return false; // Peer is stale according to shared simulation time.
    feed(p.b,.5,.1);
    if (!p.a.roundActive) return false;
    p.meta.now={.55};
    if (p.a.measureInflight({{{1},1000,.98,1,1000,2}})!=0 || p.a.hasFeedback) return false;
    TcpEventCode event=0;
    p.b.processRexmitTimer(event);
    return !p.b.hasFeedback && !p.b.roundActive && p.b.rtos==1;
}
static_assert(telemetryAndInvalidation(), "Measurement-time U averaging, rejected samples, epochs and loss invalidation");
'''

with tempfile.TemporaryDirectory(prefix='beta-policy-check-') as temp:
    path = Path(temp) / 'beta.cc'
    path.write_text(preamble + header + methods + checks)
    subprocess.run(['clang++', '-std=c++23', '-fsyntax-only', str(path)], check=True)
print('Beta U-redistribution compile-time checks passed (no executable or simulator built/run).')
