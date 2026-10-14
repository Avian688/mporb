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
        @signal[omegaAdditionalIncrease](type=double; unit=B);
        @statistic[omegaAdditionalIncrease](record=vector; unit=B; interpolationmode=none);
        @signal[mpOrbSenderDeltaCorrection](type=double; unit=bps);
        @statistic[mpOrbSenderDeltaCorrection](title="Sender-computed fairness rate correction"; source=mpOrbSenderDeltaCorrection; unit=bps; record=vector; interpolationmode=none);
        @signal[mpOrbDeltaSubflowRate](type=double; unit=bps);
        @statistic[mpOrbDeltaSubflowRate](record=vector; unit=bps; interpolationmode=sample-hold);
        @signal[mpOrbDeltaConnectionRate](type=double; unit=bps);
        @statistic[mpOrbDeltaConnectionRate](record=vector; unit=bps; interpolationmode=sample-hold);
        @signal[mpOrbDeltaNeed](type=double);
        @statistic[mpOrbDeltaNeed](record=vector; interpolationmode=sample-hold);
        @signal[mpOrbDeltaRawShuffle](type=double; unit=bps);
        @statistic[mpOrbDeltaRawShuffle](record=vector; unit=bps; interpolationmode=none);
        @signal[mpOrbDeltaReturnedShuffle](type=double; unit=bps);
        @statistic[mpOrbDeltaReturnedShuffle](record=vector; unit=bps; interpolationmode=none);
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
        @signal[mpOrbBetaWeight](type=double);
        @signal[mpOrbBetaWeightCorrection](type=double);
        @signal[mpOrbBetaSmoothedU](type=double);
        @signal[mpOrbBetaUtilizationError](type=double);
        @signal[mpOrbBetaBaselineShare](type=double);
        @signal[mpOrbBetaRateShare](type=double);
        @signal[mpOrbBetaRedistributionActive](type=bool);
        @signal[mpOrbBetaAlphaAi](type=long);
        @signal[mpOrbBetaUncoupledAi](type=long);
        @signal[mpOrbBetaWeightedAi](type=long);
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
        @statistic[mpOrbBetaWeight](record=vector; interpolationmode=sample-hold);
        @statistic[mpOrbBetaWeightCorrection](record=vector; interpolationmode=sample-hold);
        @statistic[mpOrbBetaSmoothedU](record=vector; interpolationmode=sample-hold);
        @statistic[mpOrbBetaUtilizationError](record=vector; interpolationmode=sample-hold);
        @statistic[mpOrbBetaBaselineShare](record=vector; interpolationmode=sample-hold);
        @statistic[mpOrbBetaRateShare](record=vector; interpolationmode=sample-hold);
        @statistic[mpOrbBetaRedistributionActive](record=vector; interpolationmode=sample-hold);
        @statistic[mpOrbBetaAlphaAi](record=vector; unit=B; interpolationmode=sample-hold);
        @statistic[mpOrbBetaUncoupledAi](record=vector; unit=B; interpolationmode=sample-hold);
        @statistic[mpOrbBetaWeightedAi](record=vector; unit=B; interpolationmode=sample-hold);
        @statistic[mpOrbOliaBestPath](record=vector; interpolationmode=sample-hold);
        @statistic[mpOrbOliaMaxWindowPath](record=vector; interpolationmode=sample-hold);
        @statistic[mpOrbOliaCorrection](record=vector; interpolationmode=sample-hold);
        @statistic[mpOrbOliaPathPrice](record=vector; interpolationmode=sample-hold);
        @statistic[mpOrbOliaPathOpportunity](record=vector; interpolationmode=sample-hold);
        @statistic[mpOrbOliaNormalizedWindow](record=vector; interpolationmode=sample-hold);
}
