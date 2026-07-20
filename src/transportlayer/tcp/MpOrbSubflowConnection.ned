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
        @class("inet::tcp::MpOrbSubflowConnection");

        @signal[txRate];
        @signal[tau];
        @signal[u];
        @signal[U];
        @signal[sharingFlows];
        @signal[initialPhaseSharingFlows];
        @signal[bottleneckBandwidth];
        @signal[avgRtt];
        @signal[queueingDelay];
        @signal[estimatedRtt];
        @signal[smoothedEstimatedRtt];
        @signal[avgEstimatedRtt];
        @signal[alpha];
        @signal[measuringInflight];
        @signal[testRtt];
        @signal[sndMax];
        @signal[txBytes];
        @signal[semiCoupledAlphaSubflowRate];
        @signal[semiCoupledAlphaConnectionRate];
        @signal[semiCoupledAlphaRateShare];
        @signal[semiCoupledDeltaBaseAiRate];
        @signal[semiCoupledDeltaAlphaAiRate];
        @signal[semiCoupledDeltaTargetShare];
        @signal[semiCoupledDeltaRateShare];
        @signal[semiCoupledDeltaResponsiveness];
        @signal[semiCoupledDeltaAiShare];
        @signal[semiCoupledEpsilonPathPrice];
        @signal[semiCoupledEpsilonOpportunity];
        @signal[semiCoupledEpsilonTargetShare];
        @signal[semiCoupledEpsilonRateShare];
        @signal[semiCoupledEpsilonResponsiveness];
        @signal[semiCoupledEpsilonAiShare];
        @signal[semiCoupledEpsilonAiRateBudget];
        @signal[semiCoupledEpsilonAdjustedAi];

        @statistic[txRate](record=vector; interpolationmode=sample-hold);
        @statistic[txBytes](record=vector; interpolationmode=sample-hold);
        @statistic[tau](record=vector; interpolationmode=sample-hold);
        @statistic[u](record=vector; interpolationmode=sample-hold);
        @statistic[U](record=vector; interpolationmode=sample-hold);
        @statistic[sharingFlows](record=vector; interpolationmode=sample-hold);
        @statistic[initialPhaseSharingFlows](record=vector; interpolationmode=sample-hold);
        @statistic[bottleneckBandwidth](record=vector; interpolationmode=sample-hold);
        @statistic[avgRtt](record=vector; interpolationmode=sample-hold);
        @statistic[queueingDelay](record=vector; interpolationmode=sample-hold);
        @statistic[estimatedRtt](record=vector; interpolationmode=sample-hold);
        @statistic[smoothedEstimatedRtt](record=vector; interpolationmode=sample-hold);
        @statistic[avgEstimatedRtt](record=vector; interpolationmode=sample-hold);
        @statistic[alpha](record=vector; interpolationmode=sample-hold);
        @statistic[measuringInflight](record=vector; interpolationmode=sample-hold);
        @statistic[testRtt](record=vector; interpolationmode=sample-hold);
        @statistic[sndMax](record=vector; interpolationmode=sample-hold);
        @statistic[semiCoupledAlphaSubflowRate](record=vector; interpolationmode=sample-hold);
        @statistic[semiCoupledAlphaConnectionRate](record=vector; interpolationmode=sample-hold);
        @statistic[semiCoupledAlphaRateShare](record=vector; interpolationmode=sample-hold);
        @statistic[semiCoupledDeltaBaseAiRate](record=vector; interpolationmode=sample-hold);
        @statistic[semiCoupledDeltaAlphaAiRate](record=vector; interpolationmode=sample-hold);
        @statistic[semiCoupledDeltaTargetShare](record=vector; interpolationmode=sample-hold);
        @statistic[semiCoupledDeltaRateShare](record=vector; interpolationmode=sample-hold);
        @statistic[semiCoupledDeltaResponsiveness](record=vector; interpolationmode=sample-hold);
        @statistic[semiCoupledDeltaAiShare](record=vector; interpolationmode=sample-hold);
        @statistic[semiCoupledEpsilonPathPrice](record=vector; interpolationmode=sample-hold);
        @statistic[semiCoupledEpsilonOpportunity](record=vector; interpolationmode=sample-hold);
        @statistic[semiCoupledEpsilonTargetShare](record=vector; interpolationmode=sample-hold);
        @statistic[semiCoupledEpsilonRateShare](record=vector; interpolationmode=sample-hold);
        @statistic[semiCoupledEpsilonResponsiveness](record=vector; interpolationmode=sample-hold);
        @statistic[semiCoupledEpsilonAiShare](record=vector; interpolationmode=sample-hold);
        @statistic[semiCoupledEpsilonAiRateBudget](record=vector; interpolationmode=sample-hold);
        @statistic[semiCoupledEpsilonAdjustedAi](record=vector; interpolationmode=sample-hold);
}
