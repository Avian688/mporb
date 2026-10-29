# PINT queue variants

`orbtcp.queueing.queue.PintQueue` implements base OrbCC telemetry: RTT and flow
measurements, utilisation/encoding, highest-U bottleneck selection, and queueing
delay. It contains no Delta counters, timer, parameters or signal declarations.
The refactor preserves these base calculations, including the current RTT
estimator and EWMA; it is not a rollback of their numerical behaviour.

`mporb.queueing.queue.MpOrbDeltaQueue` extends that queue. Its only packet hook
runs after normal bottleneck selection. The class owns the Delta measurement
round and dispatches on the sender-carried mode:

- Switch Delta computes signed feedback and enforces its existing round budget.
- Sender Delta and Omega return the previous mean need and normalisation
  coefficient; the sender computes the correction.

Neither formula, gain, interval selection nor mode's budget semantics changed.
Alpha and uncoupled OrbCC use ordinary PintQueue. Experiment generators select
MpOrbDeltaQueue on the existing Delta/Omega fairness allowlists. The dedicated
sender-Delta case in experimentTest also selects it.

Feedback includes its originating bottleneck ID. Both Delta senders require it
to match the final INT winner. This preserves chosen-bottleneck-only feedback
when a later ordinary PINT queue supersedes a Delta queue, without teaching the
base queue about Delta tags. Both the mporb and orbtcp libraries must be rebuilt
together before using the updated NED/configuration and tag layout.

Validation uses C++ syntax checks, NED validation, the existing Delta policy and
bottleneck tests, and scratch-only generation of all experiment INIs. No runtime
simulation results are implied by those checks.
