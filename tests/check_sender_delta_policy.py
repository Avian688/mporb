#!/usr/bin/env python3
"""Check aggregate measurement mode against legacy Delta; compile only."""
from pathlib import Path
import subprocess
import tempfile
COMMON = Path(__file__).resolve().parents[2] / 'orbtcp/src/common'
SOURCE = r'''
#include "DeltaShuffle.h"
using namespace inet::delta;
constexpr bool near(double x,double y){return x-y<1e-9 && y-x<1e-9;}
constexpr bool equivalence() {
    for(double gain : {1.,2.,8.}) {
        LinkShuffle old, sender;
        for(int round=0;round<3;++round) {
            for(double need : {1.,2.,4.}) {
                const auto a=old.issue(round,5,.5,need,gain);
                const auto b=sender.measure(round,5,.5,need,gain);
                if(!near(a.raw,.5*b.coefficient*(need-b.meanNeed)))return false;
                if(!near(old.mass,sender.mass) || !near(old.needMass,sender.needMass))return false;
                if(sender.net!=0 || sender.magnitude!=0)return false;
            }
        }
    }
    return true;
}
static_assert(equivalence(),"New feedback reproduces legacy RAW correction without switch grants/budget");
constexpr bool balanceAndTiming() {
    LinkShuffle link;
    if(link.measure(0,5,1,1,2).coefficient!=0)return false;
    link.measure(0,5,1,3,2);
    const auto a=link.measure(1,5,1,1,2);
    const auto b=link.measure(1,5,1,3,2);
    if(a.meanNeed!=2 || a.coefficient!=1.25 || b.coefficient!=a.coefficient)return false;
    if(!near(a.coefficient*(1-a.meanNeed)+b.coefficient*(3-b.meanNeed),0))return false;
    // No previous observation after an idle interval.
    return link.measure(3,5,1,1,2).coefficient==0;
}
static_assert(balanceAndTiming(),"Previous population, common K, stable zero-sum and idle reset");
constexpr bool noHiddenBudget() {
    LinkShuffle link;
    link.measure(0,5,1,1,2);
    const auto a=link.measure(1,5,10,10,2);
    return 10*a.coefficient*(10-a.meanNeed)>5 && link.magnitude==0 && link.net==0;
}
static_assert(noHiddenBudget(),"Changed population is intentionally not switch-clipped");

constexpr bool nonSelectedMeasuresOnly() {
    LinkShuffle link;
    link.issue(0,5,1,1,2,false); link.issue(0,5,1,3,2,false);
    auto skipped=link.issue(1,5,1,1,2,false);
    return skipped.meanNeed==2 && skipped.raw==0 && skipped.returned==0 &&
        link.mass==1 && link.needMass==1 && link.net==0 && link.magnitude==0;
}
static_assert(nonSelectedMeasuresOnly(),"Unselected links measure need but do not issue signed grants");

'''
with tempfile.TemporaryDirectory(prefix='sender-delta-policy-') as temp:
    source=Path(temp)/'check.cc'
    source.write_text(SOURCE)
    subprocess.run(['clang++','-std=c++17','-fsyntax-only','-I'+str(COMMON),str(source)],check=True)
print('Sender Delta measurement checks passed; no executable or simulation run.')
