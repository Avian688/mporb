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

import mptcp.transportlayer.tcp.MpTcp;

simple MpOrb extends MpTcp
{
    parameters:
        @class("inet::tcp::MpOrb");
        int bandwidth = default(18750000);
        double basePropagationRTT @unit(s) = default(0.01s);
        int subFlows = default(1);
        int sharingFlows = default(2);
        double additiveIncreasePercent = default(0.05);
        double eta = default(0.95);
        double alpha = default(0.03);
        // Common rate unit for log-utility prices, NOT a requested connection rate.
        double omegaRateScale @unit(bps) = default(100Mbps);
        double omegaRateGain = default(0.1);
        double omegaControlInterval @unit(s) = default(5ms);
        double omegaMinRtt @unit(s) = default(20ms);
        double pintFeedbackProbability = default(1);
        int pintFlowCountBits = default(8);
        int pintMaxFlowCount = default(65535);
        bool pintUseAverageRtt = default(true);
        bool pintUseInitialPhase = default(true);
        bool pintUseInitialPhaseFlowCount = default(true);
        bool pintSeparateQueueingDelay = default(true); // MpORB ACKs echo forward delay and accumulate reverse delay separately
        double mpOrbBetaRedistributionGain = default(0.5); // [0,1]: strength of the centred U-error AI correction; 0 uses plain Alpha
        double mpOrbBetaUtilizationDeadband = default(0.05); // Absolute U tolerance around eta
        double mpOrbBetaSmoothingRtts = default(1); // Measurement-time U averaging for coupling, in subflow RTTs
        double mpOrbUBeta = default(20); // Sensitivity of normalised exp(-beta * U / eta) AI weights
        double mpOrbUSmoothingRtts = default(1); // Time constant for MpOrbU's utilisation EWMA
        double mpOrbPressureDecreaseGain = default(1);
        double mpOrbPressureMaxDecreaseFraction = default(0.25);
        int mpOrbPressureProbeIntervalRtts = default(4);
}
