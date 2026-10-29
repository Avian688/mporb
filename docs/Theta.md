# MpOrbSemiCoupledTheta

Alpha plus sender-side, XCP-inspired total-connection-rate shuffling. This is
not the complete XCP congestion controller. Existing Alpha, Delta and Omega
remain selectable.

At each MpOrbDeltaQueue, packet mass m=L/(max(x, floor)*T) weights the reported
nominal connection rate X. The queue remembers the previous interval's M=sum(m)
and meanX=sum(m*X)/M. A packet carries mass, meanX and allowance=beta*B/M from
its selected highest-U bottleneck. The sender calculates:

    delta_x = mass * allowance * (1 - X / meanX)

All rates in the calculation are payload bytes/s. The default beta is 0.05.
The sender adds delta_x*srtt to its pending window adjustment and consumes it
at the inherited normal RTT commit, after Alpha's AI/MD. Duplicate/outdated
serial filtering, bottleneck-ID matching, cwnd floor and pacing are inherited.
Theta adds no underutilisation boost. First/idle intervals have zero feedback.

The shared queue also retains inverse-need aggregate statistics for coexistence
with Delta, but Theta's correction does not depend on inverse need, need range
or deltaNeedGain. Theta rate measurements include all Delta-tagged traffic.
The existing parameter name deltaShuffleFraction sets its rate scale. There is
no hard sender-side shuffle cap. Previous-interval delay, loss of feedback and
window clipping mean exact zero-sum cancellation is not guaranteed in transients.
The average is subflow-weighted, not connection-deduplicated if several subflows
share the same bottleneck. X is cwnd/srtt, not application delivery rate.

Use tcpAlgorithmClass="MpOrbSemiCoupledTheta" with MpOrbDeltaQueue on the
fairness-enabled bottlenecks. experimentTest includes a dedicated Theta case;
other experiment matrices are unchanged. Rebuild orbtcp and mporb together
before running, because the shared telemetry tag layout changed.

Signals: mpOrbThetaMeanConnectionRate and mpOrbThetaRateAllowance at the queue;
mpOrbThetaCorrection at the sender. Applied corrections still use the inherited
mpOrbDeltaAppliedShuffle signal. All these rate signals use bits/s.
