#!/usr/bin/env python3
"""Constant-evaluate the actual Omega computeWnd with a minimal Delta stub.
No executable or OMNeT++ simulation. Tests the boost cap, boundary and parent contract.
"""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'src/transportlayer/tcp/flavours/MpOrbOmega.cc').read_text()
a=s.index('uint32_t MpOrbOmega::computeWnd');b=s.index('\n}\n',a)+2
body='constexpr '+s[a:b]
body=body.replace('static const simsignal_t signal = cComponent::registerSignal("omegaAdditionalIncrease");','constexpr int signal = 0;')
body=body.replace('std::isfinite(u)','(u == u && u < 1e300 && u > -1e300)')
stub=r'''
#include <algorithm>
#include <cstdint>
#include <limits>
constexpr double SIMTIME_ZERO=0;
struct Time {double value=.04; constexpr double dbl()const{return value;} constexpr operator double()const{return value;}};
struct State {
 uint32_t additiveIncrease=500,bottBW=1000000;
 double additiveIncreasePercent=.05;
 int sharingFlows=2;
 bool initialPhase=false;
};
struct Connection {uint32_t lastExtra=0;constexpr void emit(int,uint32_t x){lastExtra=x;}};
struct MpOrbDelta {
 State *state=nullptr;Connection *conn=nullptr;Time rtt;bool firstRTT=false;int calls=0;int fairness=400;
 uint32_t parentAi=0;
 constexpr uint32_t computeWnd(double u,bool commit){
  ++calls;
  // Parent stub exposes which AI Omega presents, including fairness at commit.
  parentAi=state->additiveIncrease;
  if(!commit)return 8000;
  const double base=u>=.95 ? 10000*.95/u : 10000;
  return static_cast<uint32_t>(base)+state->additiveIncrease+fairness;
 }
};
struct MpOrbOmega:MpOrbDelta {constexpr uint32_t computeWnd(double,bool);};
'''
checks=r'''
constexpr bool boostAndRestore(){
 State s;Connection c;MpOrbOmega a; a.state=&s;a.conn=&c;
 // The uncapped 1800-byte increase falls to 1000 at the normal OrbCC gain of .05.
 if(a.computeWnd(.5,true)!=11400 || c.lastExtra!=500 || s.additiveIncrease!=500)return false;
 for(int i=0;i<100;++i) {
  if(a.computeWnd(.5,false)!=8000 || a.parentAi!=500 || s.additiveIncrease!=500 || c.lastExtra!=500)
   return false;
 }
 return a.calls==101 && a.fairness==400;
}
static_assert(boostAndRestore(),"Extra AI at commit only, no leak to fast ACKs, fairness preserved");
constexpr bool configurableCap(){
 State s;Connection c;MpOrbOmega a; a.state=&s;a.conn=&c;
 s.additiveIncreasePercent=.03125;
 // The configured gain times the 20,000-byte fair window gives 625 bytes.
 return a.computeWnd(.5,true)==11025 && c.lastExtra==125 && s.additiveIncrease==500;
}
static_assert(configurableCap(),"Omega follows the configured normal OrbCC AI gain");
constexpr bool boundaryAndStartup(){
 State s;Connection c;MpOrbOmega a; a.state=&s;a.conn=&c;
 for(double u:{.75,.750001,.8,.85,.9,.95,1.2}) {
  auto expected=static_cast<uint32_t>(u>=.95 ? 10000*.95/u : 10000)+900;
  if(a.computeWnd(u,true)!=expected || c.lastExtra!=0)return false;
 }
 a.firstRTT=true;if(a.computeWnd(.2,true)!=10900 || c.lastExtra!=0)return false;
 a.firstRTT=false;s.initialPhase=true;if(a.computeWnd(.2,true)!=10900)return false;
 s.initialPhase=false;s.additiveIncrease=3000;
 return a.computeWnd(.5,true)==13400 && c.lastExtra==0 && s.additiveIncrease==3000;
}
static_assert(boundaryAndStartup(),"Strict U threshold, startup guards, parent MD and max AI");
constexpr bool fluctuatingFeedback(){
 State s;Connection c;MpOrbOmega a; a.state=&s;a.conn=&c;
 // A near-target dip that formerly qualified must not request extra AI.
 for(double u:{.84,.79,.88,.76,.9}) {
  if(a.computeWnd(u,true)!=10900 || c.lastExtra!=0 || s.additiveIncrease!=500)return false;
 }
 // Just below the gate, the uncapped gain is about .0400002, giving 800 AI bytes.
 if(a.computeWnd(.749999,true)!=11200 || c.lastExtra!=300 || s.additiveIncrease!=500)return false;
 for(double u:{.75,.750001,.82}) {
  if(a.computeWnd(u,true)!=10900 || c.lastExtra!=0 || s.additiveIncrease!=500)return false;
 }
 return true;
}
static_assert(fluctuatingFeedback(),"Only U strictly below 0.75 admits the boost; no boost persists across feedback");
constexpr bool rttScaling() {
 State s;Connection c;MpOrbOmega a; a.state=&s;a.conn=&c;
 const double shortIncrease=a.computeWnd(.5,true)-10000-a.fairness;
 a.rtt.value=.08;s.additiveIncrease=1000; // Alpha AI also scales with its RTT.
 const double longIncrease=a.computeWnd(.5,true)-10000-a.fairness;
 const double error=longIncrease-2*shortIncrease;
 return error>=-1 && error<=1 && s.additiveIncrease==1000;
}
static_assert(rttScaling(),"Double RTT doubles fast AI bytes: the same nominal rate increment per RTT");

'''
with tempfile.TemporaryDirectory(prefix='omega-check-') as temp:
 p=Path(temp)/'check.cc';p.write_text(stub+body+checks)
 subprocess.run(['clang++','-std=c++17','-fsyntax-only',str(p)],check=True)
print('Omega actual-method checks passed; parent stub only, no simulation.')
