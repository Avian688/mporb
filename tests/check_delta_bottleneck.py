#!/usr/bin/env python3
"""Compile the actual bottleneck selection and feedback assignment blocks."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
base=(root.parent/'orbtcp/src/queueing/queue/PintQueue.cc').read_text()
queue=(root/'src/queueing/queue/MpOrbDeltaQueue.cc').read_text()
def body_at(s,start):
 opening=s.index('{',start);i=opening+1;depth=1
 while depth:
  depth+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[opening+1:i-1]
start=base.index('if (!hasBottleneckRecord ||')
condition=base[start+3:base.index('{',start)].strip()
clear=body_at(queue,queue.index('if (selectedBottleneck)'))
assign=body_at(queue,queue.index('if (selectedBottleneck)',queue.index('deltaShuffle.observe(')))
assign=assign[:assign.index('static const simsignal_t')]
source=r'''
#include <cstdint>
struct Module {constexpr const Module* getParentModule()const{return this;} constexpr int getId()const{return 7;}};
constexpr Module module{};
constexpr const Module* getParentModule(){return &module;}
struct Record {double u;constexpr double getPintUtilization()const{return u;}};
struct Tag {double meanConnectionRate=0,rateAllowance=0,packetMass=0;int bottleneckId=-1;unsigned hops=0;};
struct Link {double previousMass=2,previousMeanRate=40;};
'''
source+='constexpr bool selected(bool hasBottleneckRecord,double decodedUtilization,double localFairShare,double currentFairShare,Record intData){return '+condition+';}\n'
source+='constexpr void apply(Tag& t,bool chosen,bool enabled,double mass){auto tag=&t;Link deltaShuffle;const double deltaShuffleFraction=.05,bandwidthBytesPerSecond=100; if(chosen){'+clear+'}if(enabled && chosen){'+assign+'}}\n'
source+=r'''
constexpr bool checks(){
 Tag t;
 if(!selected(false,.2,100,0,{0}))return false;
 apply(t,true,true,1);
 if(t.hops!=1 || t.packetMass!=1 || t.meanConnectionRate!=40 || t.rateAllowance!=2.5 || t.bottleneckId!=7)return false;
 if(selected(true,.1,10,100,{.2}))return false;
 apply(t,false,true,2);
 if(t.packetMass!=1)return false;
 if(!selected(true,.8,100,100,{.2}))return false;
 apply(t,true,true,3);
 if(t.packetMass!=3 || t.hops!=1)return false;
 if(!selected(true,.8,50,100,{.8}) || selected(true,.8,200,100,{.8}))return false;
 apply(t,true,false,4);
 return t.hops==0 && t.packetMass==0 && t.rateAllowance==0 && t.meanConnectionRate==0;
}
static_assert(checks(),"Only chosen bottleneck returns feedback; disabled winner clears earlier feedback");
'''
# A later ordinary PintQueue can win without touching the Delta tag: the sender
# must reject that tag by comparing its origin with the final INT hop.
sender=(root/'src/transportlayer/tcp/flavours/MpOrbDelta.cc').read_text()
assert 'tag->bottleneckId == data.front().getHopId()' in sender
assert 'tag->serial > lastShuffleSerial' in sender
with tempfile.TemporaryDirectory(prefix='delta-bottleneck-check-') as tmp:
 p=Path(tmp)/'check.cc';p.write_text(source)
 subprocess.run(['clang++','-std=c++17','-fsyntax-only',str(p)],check=True)
print('Actual bottleneck selection/assignment checks passed; no simulation.')
