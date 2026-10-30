//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see http://www.gnu.org/licenses/.
//

package mporb.transportlayer.tcp;

import mptcp.transportlayer.tcp.SubflowConnection;

simple MpOrbSubflowConnection extends SubflowConnection
{
    parameters:
        @signal[mpOrbDeltaCorrection](type=double; unit=bps);
        @statistic[mpOrbDeltaCorrection](title="Delta total-rate fairness correction"; source=mpOrbDeltaCorrection; unit=bps; record=vector; interpolationmode=none);
        @signal[omegaAdditionalIncrease](type=double; unit=B);
        @statistic[omegaAdditionalIncrease](record=vector; unit=B; interpolationmode=none);
        @signal[mpOrbDeltaSubflowRate](type=double; unit=bps);
        @statistic[mpOrbDeltaSubflowRate](record=vector; unit=bps; interpolationmode=sample-hold);
        @signal[mpOrbDeltaConnectionRate](type=double; unit=bps);
        @statistic[mpOrbDeltaConnectionRate](record=vector; unit=bps; interpolationmode=sample-hold);
        @signal[mpOrbDeltaAppliedShuffle](type=double; unit=bps);
        @statistic[mpOrbDeltaAppliedShuffle](record=vector; unit=bps; interpolationmode=none);
        @signal[intSchedulerScore](type=double; unit=s);
        @signal[intSchedulerBurstBytes](type=long);
        @signal[intSchedulerFreshFeedback](type=bool);
        @signal[intSchedulerProbe](type=bool);
        @statistic[intSchedulerScore](record=vector; unit=s; interpolationmode=sample-hold);
        @statistic[intSchedulerBurstBytes](record=vector; interpolationmode=sample-hold);
        @statistic[intSchedulerFreshFeedback](record=vector; interpolationmode=sample-hold);
        @statistic[intSchedulerProbe](record=vector; interpolationmode=sample-hold);
        @class("inet::tcp::MpOrbSubflowConnection");

        @signal[txRate];
        @signal[tau];
        @signal[u];
        @signal[U];
        @signal[sharingFlows];
        @signal[bottleneckBandwidth];
        @signal[queueingDelay];
        @signal[mpOrbForwardQueueingDelay](type=double; unit=s);
        @signal[mpOrbReverseQueueingDelay](type=double; unit=s);
        @signal[estimatedRtt];
        @signal[smoothedEstimatedRtt];
        @signal[alpha];
        @signal[testRtt];
        @signal[sndMax];
        @signal[semiCoupledAlphaSubflowRate];
        @signal[semiCoupledAlphaConnectionRate];
        @signal[semiCoupledAlphaRateShare];
        @signal[mpOrbOliaBestPath];
        @signal[mpOrbOliaMaxWindowPath];
        @signal[mpOrbOliaCorrection];
        @signal[mpOrbOliaPathPrice];
        @signal[mpOrbOliaPathOpportunity];
        @signal[mpOrbOliaNormalizedWindow];

        @statistic[txRate](record=vector; interpolationmode=sample-hold);
        @statistic[tau](record=vector; interpolationmode=sample-hold);
        @statistic[u](record=vector; interpolationmode=sample-hold);
        @statistic[U](record=vector; interpolationmode=sample-hold);
        @statistic[sharingFlows](record=vector; interpolationmode=sample-hold);
        @statistic[bottleneckBandwidth](record=vector; interpolationmode=sample-hold);
        @statistic[queueingDelay](record=vector; interpolationmode=sample-hold);
        @statistic[mpOrbForwardQueueingDelay](record=vector; unit=s; interpolationmode=sample-hold);
        @statistic[mpOrbReverseQueueingDelay](record=vector; unit=s; interpolationmode=sample-hold);
        @statistic[estimatedRtt](record=vector; interpolationmode=sample-hold);
        @statistic[smoothedEstimatedRtt](record=vector; interpolationmode=sample-hold);
        @statistic[alpha](record=vector; interpolationmode=sample-hold);
        @statistic[testRtt](record=vector; interpolationmode=sample-hold);
        @statistic[sndMax](record=vector; interpolationmode=sample-hold);
        @statistic[semiCoupledAlphaSubflowRate](record=vector; interpolationmode=sample-hold);
        @statistic[semiCoupledAlphaConnectionRate](record=vector; interpolationmode=sample-hold);
        @statistic[semiCoupledAlphaRateShare](record=vector; interpolationmode=sample-hold);
        @statistic[mpOrbOliaBestPath](record=vector; interpolationmode=sample-hold);
        @statistic[mpOrbOliaMaxWindowPath](record=vector; interpolationmode=sample-hold);
        @statistic[mpOrbOliaCorrection](record=vector; interpolationmode=sample-hold);
        @statistic[mpOrbOliaPathPrice](record=vector; interpolationmode=sample-hold);
        @statistic[mpOrbOliaPathOpportunity](record=vector; interpolationmode=sample-hold);
        @statistic[mpOrbOliaNormalizedWindow](record=vector; interpolationmode=sample-hold);
}
