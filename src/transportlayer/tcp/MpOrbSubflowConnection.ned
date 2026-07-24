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
        @signal[semiCoupledBetaFairRate];
        @signal[semiCoupledBetaTotalFairRate];
        @signal[semiCoupledBetaFairRateShare];
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
        @statistic[semiCoupledBetaFairRate](record=vector; interpolationmode=sample-hold);
        @statistic[semiCoupledBetaTotalFairRate](record=vector; interpolationmode=sample-hold);
        @statistic[semiCoupledBetaFairRateShare](record=vector; interpolationmode=sample-hold);
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
}
