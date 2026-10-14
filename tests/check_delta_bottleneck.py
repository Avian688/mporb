#!/usr/bin/env python3
"""Compile-time checks of the queue's actual selection/feedback assignment blocks."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
s=(root.parent/'orbtcp/src/queueing/queue/PintQueue.cc').read_text()
def body_at(start):
 opening=s.index('{',start);i=opening+1;depth=1
 while depth:
  depth+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[opening+1:i-1],i
start=s.index('if (!hasBottleneckRecord ||')
condition=s[start+3:s.index('{',start)].strip()
clear,_=body_at(s.index('if (!tag->echoed && selectedBottleneck)'))
sender,end=body_at(s.index('if (selectedBottleneck)',s.index('deltaShuffle.measure(')))
legacy,_=body_at(s.index('if (selectedBottleneck)',s.index('deltaShuffle.issue(')))
source=r'''
#include <cstdint>
struct Record {double u;constexpr double getPintUtilization()const{return u;}};
struct Measurement {double mass,need,meanNeed,coefficient;};
struct Samples {Measurement value{};int count=0;constexpr void clear(){count=0;} constexpr void push_back(Measurement v){value=v;++count;}};
struct Tag {double need=0,rawShuffle=0,shuffle=0;unsigned hops=0;Samples measurements;};
struct Feedback {double meanNeed=2,coefficient=3,raw=4,returned=5;};
'''
source+='constexpr bool selected(bool hasBottleneckRecord,double decodedUtilization,double localFairShare,double currentFairShare,Record intData){return '+condition+';}\n'
source+='constexpr void apply(Tag& t,bool chosen,bool enabled,bool senderMode,double need){auto tag=&t;const double mass=1;Feedback feedback; if(chosen){'+clear+'}if(enabled && chosen){if(senderMode){'+sender+'}else{'+legacy+'}}}\n'
source+=r'''
constexpr bool checks(bool sender){
 Tag t;
 if(!selected(false,.2,100,0,{0}))return false;
 apply(t,true,true,sender,1);
 if(t.hops!=1)return false;
 // A lower-U downstream hop must leave the earlier winner untouched.
 if(selected(true,.1,10,100,{.2}))return false;
 apply(t,false,true,sender,2);
 if(t.need!=1)return false;
 // A higher-U downstream hop replaces, never sums, feedback.
 if(!selected(true,.8,100,100,{.2}))return false;
 apply(t,true,true,sender,3);
 if(t.need!=3 || t.hops!=1)return false;
 if(sender && (t.measurements.count!=1 || t.measurements.value.need!=3))return false;
 if(!sender && (t.rawShuffle!=4 || t.shuffle!=5))return false;
 // Preserve OrbCC's equal-U tighter-share tie break.
 if(!selected(true,.8,50,100,{.8}) || selected(true,.8,200,100,{.8}))return false;
 // A disabled final winner removes earlier fairness correction entirely.
 apply(t,true,false,sender,4);
 return t.need==0 && t.hops==0 && t.measurements.count==0 && t.rawShuffle==0 && t.shuffle==0;
}
static_assert(checks(false),"Switch Delta only returns chosen bottleneck feedback");
static_assert(checks(true),"Sender Delta only returns chosen bottleneck measurements");
'''
with tempfile.TemporaryDirectory(prefix='delta-bottleneck-check-') as tmp:
 p=Path(tmp)/'check.cc';p.write_text(source)
 subprocess.run(['clang++','-std=c++17','-fsyntax-only',str(p)],check=True)
print('Actual bottleneck selection/assignment checks passed for both modes; no simulation.')
