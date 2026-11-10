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
#include <cmath>
#include <stdexcept>
#include "DeltaShuffle.h"
using cRuntimeError = std::runtime_error;
namespace delta = inet::delta;
struct Module {constexpr const Module* getParentModule()const{return this;} constexpr int getId()const{return 7;}};
constexpr Module module{};
constexpr const Module* getParentModule(){return &module;}
struct Record {double u;constexpr double getPintUtilization()const{return u;}};
struct Tag {
    double meanConnectionRate=0, rateAllowance=0, packetMass=0, connectionRate=20;
    int bottleneckId=-1;
    unsigned hops=0;
};
'''
source+='constexpr bool selected(bool hasBottleneckRecord,double decodedUtilization,double localFairShare,double currentFairShare,Record intData){return '+condition+';}\n'
source+=f'''
void apply(Tag& t, delta::LinkShuffle& deltaShuffle, bool chosen, bool enabled, double mass) {{
    auto tag = &t;
    const double deltaShuffleFraction = .05, bandwidthBytesPerSecond = 100;
    if (chosen) {{{clear}}}
    if (enabled && chosen) {{{assign}}}
}}
'''
source+=r'''
bool checks(){
 Tag t;
 delta::LinkShuffle link;
 link.beginRound(0,5);
 link.observe(2,40);
 link.beginRound(1,5);
 if(!selected(false,.2,100,0,{0}))return false;
 apply(t,link,true,true,1);
 if(t.hops!=1 || t.packetMass!=1 || t.meanConnectionRate!=40 || t.rateAllowance!=2.5 || t.bottleneckId!=7)return false;
 if(selected(true,.1,10,100,{.2}))return false;
 apply(t,link,false,true,2);
 if(t.packetMass!=1)return false;
 if(!selected(true,.8,100,100,{.2}))return false;
 apply(t,link,true,true,6);
 if(t.packetMass!=6 || t.hops!=1 || t.rateAllowance!=1.25)return false;
 if(!selected(true,.8,50,100,{.8}) || selected(true,.8,200,100,{.8}))return false;
 apply(t,link,true,true,1);
 if(t.rateAllowance!=0)return false;
 t.connectionRate=80;
 apply(t,link,true,true,4);
 if(delta::rateCorrection(t.packetMass,t.rateAllowance,t.connectionRate,t.meanConnectionRate)!=-5)return false;
 apply(t,link,true,true,1);
 if(t.rateAllowance!=0)return false;
 apply(t,link,true,false,4);
 return t.hops==0 && t.packetMass==0 && t.rateAllowance==0 && t.meanConnectionRate==0;
}
int main(){return checks() ? 0 : 1;}
'''
# A later ordinary PintQueue can win without touching the Delta tag: the sender
# must reject that tag by comparing its origin with the final INT hop.
sender=(root/'src/transportlayer/tcp/flavours/MpOrbDelta.cc').read_text()
assert 'tag->bottleneckId == data.front().getHopId()' in sender
assert 'tag->serial > lastShuffleSerial' in sender
with tempfile.TemporaryDirectory(prefix='delta-bottleneck-check-') as tmp:
 p=Path(tmp)/'check.cc';p.write_text(source)
 executable=p.with_suffix('')
 subprocess.run(['clang++','-std=c++17','-I'+str(root/'src/common'),str(p),'-o',str(executable)],check=True)
 subprocess.run([str(executable)],check=True)
print('Actual bottleneck selection/assignment checks passed; no simulation.')
