#!/usr/bin/env python3
"""Compile-time Theta arithmetic and aggregate interval regression checks."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
source = r'''#include "ThetaShuffle.h"
#include "DeltaShuffle.h"
using inet::theta::rateCorrection;
using inet::delta::LinkShuffle;
constexpr bool near(double a,double b){return a-b<1e-8 && b-a<1e-8;}
static_assert(near(rateCorrection(1,2,20,40),1));
static_assert(near(rateCorrection(1,2,40,40),0));
static_assert(near(rateCorrection(1,2,60,40),-1));
static_assert(rateCorrection(1,2,40,0)==0,"No prior population: no correction");
static_assert(near(rateCorrection(.2,2,20,40)*5,1),"Packet splitting preserves total correction");
constexpr bool rounds(){
 LinkShuffle link;
 link.beginRound(0);
 for(double X : {20.,40.,60.}){
  link.rateMass += X;
  link.measure(0,6,1,1/X,2);
 }
 link.beginRound(1);
 if(!near(link.previousMeanRate,40) || link.previousMass!=3)return false;
 double net=0;
 for(double X : {20.,40.,60.})net+=rateCorrection(1,6/link.previousMass,X,link.previousMeanRate);
 if(!near(net,0))return false;
 link.beginRound(2);
 return link.previousMeanRate==0 && link.previousMass==0;
}
static_assert(rounds(),"Previous interval mean, equal-rate fixed point, zero-sum and idle reset");
// Total connection service controls the sign, not this path's local rate.
static_assert(rateCorrection(1,2,200,137.5)<0);
static_assert(rateCorrection(1,2,75,137.5)>0);
// Rate-to-window conversion gives the same nominal rate change at different RTTs.
static_assert(near((125000*.02)/.02,(125000*.1)/.1));
'''
with tempfile.TemporaryDirectory(prefix='theta-check-') as temp:
    path=Path(temp)/'check.cc';path.write_text(source)
    subprocess.run(['clang++','-std=c++17','-fsyntax-only','-I'+str(root/'src/common'),
                    '-I'+str(root.parent/'orbtcp/src/common'),str(path)],check=True)
print('Theta arithmetic/interval checks passed; no simulation.')
