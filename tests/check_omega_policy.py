#!/usr/bin/env python3
"""Constant-evaluate the actual Omega computeWnd with a minimal sender-Delta stub.
No executable or OMNeT++ simulation. Tests the boost boundary and parent contract.
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
struct State {uint32_t additiveIncrease=500,bottBW=1000000;int sharingFlows=2;bool initialPhase=false;};
struct Connection {uint32_t lastExtra=0;constexpr void emit(int,uint32_t x){lastExtra=x;}};
struct MpOrbSenderDelta {
 State *state=nullptr;Connection *conn=nullptr;Time rtt;bool firstRTT=false;int calls=0;int fairness=400;
 constexpr uint32_t computeWnd(double u,bool commit){
  ++calls;
  // Parent stub exposes which AI Omega presents, including fairness at commit.
  if(!commit)return 8000;
  const double base=u>=.95 ? 10000*.95/u : 10000;
  return static_cast<uint32_t>(base)+state->additiveIncrease+fairness;
 }
};
struct MpOrbOmega:MpOrbSenderDelta {constexpr uint32_t computeWnd(double,bool);};
'''
checks=r'''
constexpr bool boostAndRestore(){
 State s;Connection c;MpOrbOmega a; a.state=&s;a.conn=&c;
 // fastAI=.2*(1e6/2)*(.95-.5)*.04=1800 bytes, vs Alpha's 500.
 auto w=a.computeWnd(.5,true);
 if(w<12199 || w>12200 || c.lastExtra<1299 || c.lastExtra>1300 || s.additiveIncrease!=500)return false;
 for(int i=0;i<100;++i)if(a.computeWnd(.5,false)!=8000 || s.additiveIncrease!=500)return false;
 return a.calls==101 && a.fairness==400;
}
static_assert(boostAndRestore(),"Extra AI at commit only, no leak to fast ACKs, fairness preserved");
constexpr bool boundaryAndStartup(){
 State s;Connection c;MpOrbOmega a; a.state=&s;a.conn=&c;
 for(double u:{.9,.95,1.2}) {
  auto expected=static_cast<uint32_t>(u>=.95 ? 10000*.95/u : 10000)+900;
  if(a.computeWnd(u,true)!=expected || c.lastExtra!=0)return false;
 }
 a.firstRTT=true;if(a.computeWnd(.2,true)!=10900 || c.lastExtra!=0)return false;
 a.firstRTT=false;s.initialPhase=true;if(a.computeWnd(.2,true)!=10900)return false;
 s.initialPhase=false;s.additiveIncrease=3000;
 return a.computeWnd(.5,true)==13400 && c.lastExtra==0 && s.additiveIncrease==3000;
}
static_assert(boundaryAndStartup(),"Strict U threshold, unchanged startup/MD and max rather than sum AI");
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
