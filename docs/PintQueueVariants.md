# PINT queue variants

`orbtcp.queueing.queue.PintQueue` provides base OrbCC telemetry: RTT and flow
measurements, utilisation/encoding, highest-U bottleneck selection and queueing
delay. It contains no Delta counters, timer, parameters or signals.

`mporb.queueing.queue.MpOrbDeltaQueue` extends the normal telemetry hook with
aggregate packet-mass and connection-rate measurements. When a queue becomes
the current PINT winner, it calculates a signed correction from the previous
round's mean rate and caps it against the remaining budget for that sign.
The queue encodes the grant in the rate allowance. `MpOrbDelta` and `MpOrbOmega`
reconstruct and apply it at the sender. See [Delta's equations and queue
placement](mporb-delta.md) for details.

The feedback's bottleneck ID must match the final PINT winner. A later ordinary
`PintQueue` can win the bottleneck selection, in which case the sender ignores
the earlier shuffle feedback. Queue placement varies by experiment. Alpha and
uncoupled OrbCC use `PintQueue` wherever those experiments request OrbCC
telemetry. Both queue variants use the same base RTT estimation and EWMA.
