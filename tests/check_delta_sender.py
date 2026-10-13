#!/usr/bin/env python3
"""Compile-time regression of the actual Delta computeWnd body; no simulation.

Uses a minimal Alpha parent stub reproducing its inherited OrbCC current-cwnd growth gate.
Only signal registration and trunc() are replaced for C++17 constant evaluation.
"""
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[1]
code = (ROOT / 'src/transportlayer/tcp/flavours/MpOrbSemiCoupledDelta.cc').read_text()
start = code.index('uint32_t MpOrbSemiCoupledDelta::computeWnd(')
opening = code.index('{', start)
depth = 1
end = opening + 1
while depth:
    depth += (code[end] == '{') - (code[end] == '}')
    end += 1
body = 'constexpr ' + code[start:end]
body = body.replace('std::trunc(', 'truncate(')
body = body.replace('static const simsignal_t appliedSignal = cComponent::registerSignal("mpOrbDeltaAppliedShuffle");',
                    'constexpr int appliedSignal = 0;')
STUB = r'''
#include <algorithm>
#include <cstdint>
#include <limits>
constexpr double SIMTIME_ZERO=0;
struct Rtt { constexpr double dbl() const {return .04;} constexpr operator double() const{return .04;} };
struct State { uint32_t snd_cwnd=10000, prevWnd=10000, snd_mss=1000; Rtt srtt; };
struct Connection { template<class T> constexpr void emit(int,T) {} };
struct MpOrbSemiCoupledAlpha {
    State *state;
    uint32_t baseTarget=10000;
    int baseCalls=0;
    constexpr explicit MpOrbSemiCoupledAlpha(State *s):state(s){}
    constexpr uint32_t computeWnd(double, bool commit) {
        ++baseCalls;
        // This is the real growth-gate behaviour that exposed repeated spending.
        const auto window=std::min(baseTarget,state->snd_cwnd);
        if(commit) state->prevWnd=window;
        return window;
    }
};
struct MpOrbSemiCoupledDelta:MpOrbSemiCoupledAlpha {
    double pendingShuffleBytes=0;
    Connection *conn;
    constexpr MpOrbSemiCoupledDelta(State *s,Connection *c):MpOrbSemiCoupledAlpha(s),conn(c){}
    constexpr uint32_t computeWnd(double,bool);
};
// Same truncation semantics for the finite test inputs; std::trunc is not C++17 constexpr.
constexpr double truncate(double x){return static_cast<int64_t>(x);}
'''
CHECKS = r'''
constexpr bool onceOnly(double pending,uint32_t expected) {
    State s; Connection sink; MpOrbSemiCoupledDelta a(&s,&sink);
    a.pendingShuffleBytes=pending;
    for(int i=0;i<100;++i){
        s.snd_cwnd=a.computeWnd(.95,false);
        if(s.snd_cwnd!=10000 || a.pendingShuffleBytes!=pending)return false;
    }
    s.snd_cwnd=a.computeWnd(.95,true);
    if(s.snd_cwnd!=expected || s.prevWnd!=expected)return false;
    if(a.pendingShuffleBytes!=pending-truncate(pending))return false;
    a.baseTarget=expected;
    for(int i=0;i<100;++i){
        s.snd_cwnd=a.computeWnd(.95,false);
        if(s.snd_cwnd!=expected)return false;
    }
    return a.baseCalls==201;
}
static_assert(onceOnly(-1000.75,9000),"Negative shuffle must not repeatedly collapse cwnd on fast ACKs");
static_assert(onceOnly(1000.75,11000),"Positive shuffle is also applied only at commit");
static_assert(onceOnly(-50000,1000),"Window floor is retained and clipped shuffle is consumed");
static_assert(onceOnly(0,10000),"Disabled shuffle keeps the Alpha baseline result");
constexpr bool fastMdStillRuns(){
    State s;Connection sink;MpOrbSemiCoupledDelta a(&s,&sink);
    a.pendingShuffleBytes=-1000;
    a.baseTarget=8000;
    s.snd_cwnd=a.computeWnd(1.2,false);
    return s.snd_cwnd==8000 && a.pendingShuffleBytes==-1000 && s.prevWnd==10000;
}
static_assert(fastMdStillRuns(),"Deferring shuffle must not defer OrbCC fast MD");
'''
with tempfile.TemporaryDirectory(prefix='delta-sender-check-') as temp:
    source=Path(temp)/'check.cc'
    source.write_text(STUB+'\n'+body+'\n'+CHECKS)
    subprocess.run(['clang++','-std=c++17','-fsyntax-only',str(source)],check=True)
print('Delta sender commit regression passed (actual method body, stubbed OrbCC parent; no simulation).')
