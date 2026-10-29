package mporb.queueing.queue;
import orbtcp.queueing.queue.PintQueue;
// Base OrbCC telemetry plus switch-side or sender-side Delta feedback.
simple MpOrbDeltaQueue extends PintQueue
{
    parameters:
        @signal[mpOrbThetaMeanConnectionRate](type=double; unit=bps);
        @statistic[mpOrbThetaMeanConnectionRate](record=vector; unit=bps; interpolationmode=sample-hold);
        @signal[mpOrbThetaRateAllowance](type=double; unit=bps);
        @statistic[mpOrbThetaRateAllowance](record=vector; unit=bps; interpolationmode=sample-hold);
        @class("inet::queueing::MpOrbDeltaQueue");
        @signal[mpOrbDeltaNeedCoefficient](type=double);
        @statistic[mpOrbDeltaNeedCoefficient](title="Previous interval fairness normalisation coefficient"; source=mpOrbDeltaNeedCoefficient; record=vector; interpolationmode=none);
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
        @signal[mpOrbDeltaMeanNeed](type=double);
        @statistic[mpOrbDeltaMeanNeed](record=vector; interpolationmode=sample-hold);
        @signal[mpOrbDeltaRoundShuffle](type=double; unit=bps);
        @statistic[mpOrbDeltaRoundShuffle](record=vector; unit=bps; interpolationmode=sample-hold);
        @signal[mpOrbDeltaRoundShuffleMagnitude](type=double; unit=bps);
        @statistic[mpOrbDeltaRoundShuffleMagnitude](record=vector; unit=bps; interpolationmode=sample-hold);
        // Delta-tagged DATA only; no additional per-flow/connection state.
        // 0 uses avgRTT, sampled and frozen at each round boundary.
        // Positive values retain an explicit fixed-interval comparison mode.
        double deltaShuffleInterval @unit(s) = default(0s);
        @signal[mpOrbDeltaRoundInterval](type=simtime_t; unit=s);
        @statistic[mpOrbDeltaRoundInterval](record=vector; unit=s; interpolationmode=sample-hold);
        double deltaShuffleFraction = default(0.05); // Legacy Delta: hard round budget; SenderDelta: coefficient scale only
        double deltaNeedGain = default(2); // Common need-range normalisation gain
        double deltaRateFloor @unit(bps) = default(8kbps); // Inverse-weight regularisation


}
