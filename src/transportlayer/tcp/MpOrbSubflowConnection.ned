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
        @signal[initialPhaseSharingFlows];
        @signal[bottleneckBandwidth];
        @signal[avgRtt];
        @signal[queueingDelay];
        @signal[mpOrbForwardQueueingDelay](type=double; unit=s);
        @signal[mpOrbReverseQueueingDelay](type=double; unit=s);
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
        @signal[mpOrbUSmoothedU](type=double);
        @signal[mpOrbUWeight](type=double);
        @signal[mpOrbUFreshSubflows](type=long);
        @signal[mpOrbUUncoupledAi](type=long);
        @signal[mpOrbUWeightedAi](type=long);
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
        @signal[semiCoupledEpsilonPathCost];
        @signal[semiCoupledEpsilonDesiredShare];
        @signal[semiCoupledEpsilonRateShare];
        @signal[semiCoupledEpsilonRedistribution];
        @signal[semiCoupledThetaFairRate];
        @signal[semiCoupledThetaHeadroomRate];
        @signal[semiCoupledThetaAiShare];
        @signal[semiCoupledThetaConnectionAiRate];
        @signal[mpOrbOliaBestPath];
        @signal[mpOrbOliaMaxWindowPath];
        @signal[mpOrbOliaCorrection];
        @signal[mpOrbOliaPathPrice];
        @signal[mpOrbOliaPathOpportunity];
        @signal[mpOrbOliaNormalizedWindow];
        @signal[mpOrbPressure];
        @signal[mpOrbPressureWeight];
        @signal[mpOrbPressureFairRate];
        @signal[mpOrbPressureAiScale];
        @signal[mpOrbPressureConnectionAiRate];
        @signal[mpOrbPressureFreshSubflows];
        @signal[mpOrbPressureUncoupledAi];
        @signal[mpOrbPressureWeightedAi];
        @signal[mpOrbPressureSubflowRate];
        @signal[mpOrbPressureConnectionRate];
        @signal[mpOrbPressureReentryBoost];
        @signal[mpOrbPressureWindowDelta];
        @signal[mpOrbPressureExtraWithdrawalBytes];

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
        @statistic[mpOrbForwardQueueingDelay](record=vector; unit=s; interpolationmode=sample-hold);
        @statistic[mpOrbReverseQueueingDelay](record=vector; unit=s; interpolationmode=sample-hold);
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
        @statistic[mpOrbUSmoothedU](record=vector; interpolationmode=sample-hold);
        @statistic[mpOrbUWeight](record=vector; interpolationmode=sample-hold);
        @statistic[mpOrbUFreshSubflows](record=vector; interpolationmode=sample-hold);
        @statistic[mpOrbUUncoupledAi](record=vector; unit=B; interpolationmode=sample-hold);
        @statistic[mpOrbUWeightedAi](record=vector; unit=B; interpolationmode=sample-hold);
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
        @statistic[semiCoupledEpsilonPathCost](record=vector; interpolationmode=sample-hold);
        @statistic[semiCoupledEpsilonDesiredShare](record=vector; interpolationmode=sample-hold);
        @statistic[semiCoupledEpsilonRateShare](record=vector; interpolationmode=sample-hold);
        @statistic[semiCoupledEpsilonRedistribution](record=vector; interpolationmode=sample-hold);
        @statistic[semiCoupledThetaFairRate](record=vector; interpolationmode=sample-hold);
        @statistic[semiCoupledThetaHeadroomRate](record=vector; interpolationmode=sample-hold);
        @statistic[semiCoupledThetaAiShare](record=vector; interpolationmode=sample-hold);
        @statistic[semiCoupledThetaConnectionAiRate](record=vector; interpolationmode=sample-hold);
        @statistic[mpOrbOliaBestPath](record=vector; interpolationmode=sample-hold);
        @statistic[mpOrbOliaMaxWindowPath](record=vector; interpolationmode=sample-hold);
        @statistic[mpOrbOliaCorrection](record=vector; interpolationmode=sample-hold);
        @statistic[mpOrbOliaPathPrice](record=vector; interpolationmode=sample-hold);
        @statistic[mpOrbOliaPathOpportunity](record=vector; interpolationmode=sample-hold);
        @statistic[mpOrbOliaNormalizedWindow](record=vector; interpolationmode=sample-hold);
        @statistic[mpOrbPressure](record=vector; interpolationmode=sample-hold);
        @statistic[mpOrbPressureWeight](record=vector; interpolationmode=sample-hold);
        @statistic[mpOrbPressureFairRate](record=vector; interpolationmode=sample-hold);
        @statistic[mpOrbPressureAiScale](record=vector; interpolationmode=sample-hold);
        @statistic[mpOrbPressureConnectionAiRate](record=vector; interpolationmode=sample-hold);
        @statistic[mpOrbPressureFreshSubflows](record=vector; interpolationmode=sample-hold);
        @statistic[mpOrbPressureUncoupledAi](record=vector; interpolationmode=sample-hold);
        @statistic[mpOrbPressureWeightedAi](record=vector; interpolationmode=sample-hold);
        @statistic[mpOrbPressureSubflowRate](record=vector; interpolationmode=sample-hold);
        @statistic[mpOrbPressureConnectionRate](record=vector; interpolationmode=sample-hold);
        @statistic[mpOrbPressureReentryBoost](record=vector; interpolationmode=sample-hold);
        @statistic[mpOrbPressureWindowDelta](record=vector; interpolationmode=sample-hold);
        @statistic[mpOrbPressureExtraWithdrawalBytes](record=vector; interpolationmode=sample-hold);
}
