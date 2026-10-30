# PINT queue variants

`orbtcp.queueing.queue.PintQueue` provides base OrbCC telemetry: RTT and flow
measurements, utilisation/encoding, highest-U bottleneck selection and queueing
delay. It contains no Delta counters, timer, parameters or signals.

`mporb.queueing.queue.MpOrbDeltaQueue` extends the normal telemetry hook with
aggregate packet-mass and connection-rate measurements. The previous interval's
mean connection rate and common rate allowance are returned for the chosen
bottleneck. The switch does not calculate signed grants. Both `MpOrbDelta` and
its boosted child `MpOrbOmega` calculate the same correction at the sender.
See [mporb-delta.md](mporb-delta.md) for equations and approximation limits.

The feedback's bottleneck ID must match the final INT winner, including when a
later ordinary PintQueue replaces the selected bottleneck. Experiment generators
enable fairness measurement only on their explicit bottleneck allowlists.
Alpha and uncoupled OrbCC use ordinary PintQueue. Base queue RTT estimation and
EWMA behaviour are unchanged by the variant consolidation.
