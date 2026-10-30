package mporb.queueing.queue;
import orbtcp.queueing.queue.PintQueue;
// Base OrbCC telemetry plus aggregate total-connection-rate measurements.
simple MpOrbDeltaQueue extends PintQueue
{
    parameters:
        @class("inet::queueing::MpOrbDeltaQueue");
        @signal[mpOrbDeltaMeanConnectionRate](type=double; unit=bps);
        @statistic[mpOrbDeltaMeanConnectionRate](record=vector; unit=bps; interpolationmode=sample-hold);
        @signal[mpOrbDeltaRateAllowance](type=double; unit=bps);
        @statistic[mpOrbDeltaRateAllowance](record=vector; unit=bps; interpolationmode=sample-hold);
        @signal[mpOrbDeltaRoundInterval](type=simtime_t; unit=s);
        @statistic[mpOrbDeltaRoundInterval](record=vector; unit=s; interpolationmode=sample-hold);
        // 0 selects avgRTT, sampled and frozen at each round boundary.
        double deltaShuffleInterval @unit(s) = default(0s);
        double deltaShuffleFraction = default(0.05); // coefficient scale, not a hard budget
        double deltaRateFloor @unit(bps) = default(8kbps); // floor for packet mass L/(x*T)
}
