#!/usr/bin/env python3
"""Compile-time checks only; no executable or OMNeT++ simulation."""
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[1]
COMMON = ROOT.parent / 'orbtcp/src/common'
SOURCE = r'''
#include "DeltaShuffle.h"
using namespace inet::delta;
constexpr bool near(double a, double b) { return a-b < 1e-8 && b-a < 1e-8; }
constexpr double absval(double a) { return a < 0 ? -a : a; }
constexpr bool equalNeeds() {
    LinkShuffle link;
    link.issue(0, 5, 1, 2);
    link.issue(0, 5, 1, 2);
    return link.issue(1, 5, 1, 2).returned == 0 && link.issue(1, 5, 1, 2).returned == 0;
}
static_assert(equalNeeds(), "Equal connection need must produce no shuffle");
constexpr bool strongerSmallGap() {
    LinkShuffle old, revised;
    for (auto *link : {&old, &revised}) {
        link->issue(0, 5, 1, 1.1);
        link->issue(0, 5, 1, 1.0);
    }
    const double before = old.issue(1, 5, 1, 1.1, 1).returned;
    const double after = revised.issue(1, 5, 1, 1.1, 8).returned;
    const double donor = revised.issue(1, 5, 1, 1.0, 8).returned;
    return before > 0 && near(after, 8*before) && near(after+donor, 0) && revised.magnitude<=5;
}
static_assert(strongerSmallGap(), "Stronger correction near equal rates retains matching negative feedback");

constexpr bool stableBalance() {
    for (double gain : {1., 8., 1000.})
    for (double a : {.1, 1., 10.}) for (double b : {.2, 2., 20.}) {
        LinkShuffle link;
        link.issue(0, 5, 1, a, gain); link.issue(0, 5, 3, b, gain);
        auto first = link.issue(1, 5, 1, a, gain);
        auto second = link.issue(1, 5, 3, b, gain);
        if (!near(first.returned + second.returned, 0) || link.magnitude > 5+1e-8) return false;
        if (a < b && !(first.returned < 0 && second.returned > 0)) return false;
        if (a > b && !(first.returned > 0 && second.returned < 0)) return false;
    }
    return true;
}
static_assert(stableBalance(), "Stable weighted populations are centred with the correct signs");
constexpr bool boundDuringChanges() {
    for (int oldCount : {1, 5, 100}) for (int count : {1, 5, 100}) {
        LinkShuffle link;
        for (int i=0; i<oldCount; ++i) if (link.issue(0, 5, 1, 1).returned != 0) return false;
        double totalAbs=0;
        for (int i=0; i<count; ++i) totalAbs += absval(link.issue(1, 5, 1, i%2 ? .1 : 100).returned);
        if (totalAbs>5+1e-8 || !near(totalAbs, link.magnitude)) return false;
        if (link.issue(3, 5, 1, 1).returned != 0) return false;
    }
    return true;
}
static_assert(boundDuringChanges(), "Magnitude is capped even when the old average is stale");
constexpr bool changedPopulationIsNotZeroSum() {
    LinkShuffle link;
    link.issue(0, 5, 1, 1);
    link.issue(1, 5, 1, 2);
    return link.net > 0; // Explicitly NOT a strict zero-sum implementation.
}
static_assert(changedPopulationIsNotZeroSum());
constexpr bool segmentation() {
    LinkShuffle a,b;
    a.issue(0, 5, 10, 1); b.issue(0, 5, 10, 1);
    double single=a.issue(1, 5, 2, 2).returned;
    double split=b.issue(1, 5, 1, 2).returned + b.issue(1, 5, 1, 2).returned;
    return near(single, split) && near(a.mass,b.mass) && near(a.needMass,b.needMass);
}
static_assert(segmentation(), "Smaller packets do not earn more correction");
constexpr bool packetRateCompensation() {
    LinkShuffle link;
    // Equal payload: path at x=2 sends twice as often as path at x=1.
    link.issue(0, 5, 1./2, 1); link.issue(0, 5, 1./2, 1); link.issue(0, 5, 1., 3);
    return near(link.issue(1, 5, 1, 2).meanNeed, 2);
}
static_assert(packetRateCompensation(), "L/x compensates sampling frequency");
constexpr bool disabled() {
    LinkShuffle link;
    link.issue(0, 0, 1, 1);
    return link.issue(1, 0, 1, 100).returned == 0;
}
static_assert(disabled(), "Zero shuffle fraction disables the queue correction");
constexpr double feedbackForDuration(int milliseconds) {
    LinkShuffle link;
    // Same steady traffic observed over 20ms, then either 20ms or 40ms.
    for (int i=0;i<20;++i) {
        link.issue(0,5,1./20,1.1);
        link.issue(0,5,1./20,1.0);
    }
    link.beginRound(1); // Timer, not first packet, closes the prior interval.
    double positive=0, negative=0;
    for (int i=0;i<milliseconds;++i) {
        positive+=link.issue(1,5,1./milliseconds,1.1).returned;
        negative+=link.issue(1,5,1./milliseconds,1.0).returned;
    }
    return near(positive+negative,0) && link.magnitude<=5 ? positive : -1;
}
static_assert(feedbackForDuration(20)>0);
static_assert(near(feedbackForDuration(20),feedbackForDuration(40)),
    "Longer rounds must not multiply the per-round budget through larger weight sums");
constexpr bool idleRoundExpiresHistory() {
    LinkShuffle link;
    link.issue(0,5,1,1.1);link.issue(0,5,1,1.0);
    link.beginRound(1);
    if(link.mass!=0 || !near(link.previousMean,1.05))return false;
    link.beginRound(2); // The timer closes a completely empty round.
    return link.previousMass==0 && link.previousMean==0 && link.issue(2,5,1,2).returned==0;
}
static_assert(idleRoundExpiresHistory(),"Empty timer rounds discard stale normalisers");

'''
with tempfile.TemporaryDirectory(prefix='delta-shuffle-check-') as temp:
    source = Path(temp) / 'checks.cc'
    source.write_text(SOURCE)
    subprocess.run(['clang++', '-std=c++17', '-fsyntax-only', '-I', str(COMMON), str(source)], check=True)
print('Delta shuffle invariants passed at compile time; no executable or simulation run.')
