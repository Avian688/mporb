# MpOrbPressure: aggregate-rate coupling and bounded withdrawal

Select the revised controller with the same class name:

```ini
**.tcp.typename = "MpOrb"
**.tcp.tcpAlgorithmClass = "MpOrbPressure"
```

Replace the existing algorithm assignment; more specific per-host assignments
can take precedence. Existing experiment matrices and scheduler code are unchanged.

## Control rule

For each eligible subflow i of connection c:

```text
q_i = B_i / N_i
x_i = committed_window_i / RTT_i
X_c = sum(x_j) across eligible subflows of this connection
s_i = x_i / X_c
AI_uncoupled_i = gamma * q_i * RTT_i
AI_i = AI_uncoupled_i * s_i
```

The meta connection computes X_c and the shares from current committed
windows. RTT is the current RTT used by OrbCC, with smoothed RTT as a fallback.
This is a window-based rate estimate, not measured application goodput or a
reservation. Using application goodput directly would make a DSN stall appear
to remove the connection's network load. Scheduler/application-limited windows
can still make the estimate inaccurate; the existing cwnd-limited growth gate
is retained.

The normalized shares sum to one for the same eligible meta snapshot. Unlike
the previous B/N-budget rule, the actual AI multiplier now differs by subflow
and depends on the connection's aggregate rate. B/N still sets the inherited
uncoupled growth magnitude; U still controls congestion reduction.

This rate-share term is mathematically related to the existing Alpha variant.
The additional mechanisms are committed-window estimates, fresh/active
membership, bounded faster withdrawal, weak-path re-entry and a probing window
floor. This is not a claim that the underlying rate-share idea is new.

## Withdrawal and re-entry

Let W be the last committed window. OrbCC's ordinary coupled candidate is:

```text
d = max(0, 1 - eta/U)
W_candidate = W * (1-d) + AI_i
```

When that candidate is below W, the controller multiplies the negative change
by `mpOrbPressureDecreaseGain` (default 4). The additional acceleration is
bounded so it does not take the window below 75% of W in one committed update
with the default `mpOrbPressureMaxDecreaseFraction = 0.25`. An ordinary OrbCC
reduction already larger than that remains allowed.

All fast ACK reactions use the same committed W. `prevWnd` is updated only
when the inherited react interval permits a commit, so repeated ACKs do not
compound the acceleration. The returned target becomes snd_cwnd and the
inherited ACK handler updates pacing. Existing in-flight packets still need
to drain; pacing retains OrbCC's in-flight-aware calculation.

A two-MSS window floor leaves room for transport probes. Both that floor and
the final target are capped by the same-state uncoupled OrbCC target. The floor
therefore cannot override a more severe uncoupled congestion response. It is
not a separate packet-generation or reinjection mechanism and cannot force the
Linux-style scheduler to select an idle subflow.

A weak subflow (rate share below one quarter of the equal-path share) can get
up to one current RTT of uncoupled AI when it is cwnd-limited and fresh feedback
shows either U below 0.9*eta or a greater-than-25% increase in B/N. Boosts start
at most once per `mpOrbPressureProbeIntervalRtts` RTTs (default 4). A B/N
improvement is remembered for one such interval. This avoids multiplying an
almost-empty path's tiny share indefinitely after conditions improve. The
boost stays within its uncoupled AI budget and does not persist at a stationary
congested bottleneck without a new improvement.

Optional parameters:

```ini
**.tcp.mpOrbPressureDecreaseGain = 4
**.tcp.mpOrbPressureMaxDecreaseFraction = 0.25
**.tcp.mpOrbPressureProbeIntervalRtts = 4
```

Gain 1 disables withdrawal acceleration. Startup, active loss recovery and a
single eligible path use the uncoupled window rule. RTO/RACK/path-change
handling clears coupling and re-entry state. Fractional AI bytes are carried
without a one-byte minimum, with every integer increase capped at uncoupled AI.

## Why the equilibrium differs from the previous iteration

With x_i = W_i/RTT_i, fixed feedback and no floor/boost/rounding, the rate change
per control update is proportional to:

```text
gamma * q_i * x_i * (1/X_c - p_i)
p_i = d_i / (gamma * q_i)
```

At a positive stationary rate, 1/X_c = p_i. A subflow withdraws when its path
cost exceeds its connection's marginal utility, 1/X_c. For one common limiting
hop per route, equal controller parameters and comparable telemetry, users of
that hop see the same effective cost. This matches the stationarity structure
of maximizing sum(log X_c). Accelerating only negative changes preserves these
interior stationary points.

The utility and path-price interpretation follows the network-utility
formulation in [Kelly, Charging and rate control for elastic traffic](https://www.statslab.cam.ac.uk/~frank/elastic.pdf).
The mapping to OrbCC's effective bottleneck cost above is an analytical model
of this implementation, not a general convergence theorem.

For the supplied eight-path ABC topology, the ideal capacity-level stationary
targets are:

| Phase | A | B | C | Each background connection |
|---|---:|---:|---:|---:|
| No background | 266.7 | 266.7 | 266.7 | - |
| Background on 5-6 | 200 | 200 | 200 | 20 |
| Background on 1-2 | 200 | 200 | 200 | 20 |

Values are Mbps before utilization targets, overhead and the finite probing
floor. They are fixed-point checks, not predicted exact application goodput.
Unlike the earlier rule, 240/280/280 is not stationary in the idealized shared
link model: A has greater marginal utility and B/C can yield shared capacity.

## Scope and limits

PINT reports one maximum-U bottleneck record, not the sum of prices of all
limiting hops. Therefore this controller is **PF-oriented for the current
single-bottleneck-per-route experiments**, not a general implementation of
network-wide proportional fairness. For example, with X traversing two busy
100 Mbps links and Y/Z traversing one each, the global PF point 33.3/66.7/66.7
is not a stationary point of this bottleneck-only rule. General multipath PF
would require a suitable aggregate path-cost signal and corresponding control
analysis.

The finite window floor, boosts, quantized/aged telemetry, changing membership,
delay, recovery and application-limited windows perturb the ideal model.
Closed-loop convergence speed and stability require experiments. Faster
withdrawal can also delay already-assigned DSNs on the withdrawing path: this
is not a proven HoL remedy, and goodput/HoL must be checked alongside allocation.

There is no new route discovery, packet scheduler, data reinjection policy,
router telemetry format or flow-count identifier. Per-subflow identifiers are
retained for the same flow-count inputs as Uncoupled. Shared bottlenecks within
a connection are not deduplicated.

## Feedback membership and diagnostics

The meta includes established/close-wait subflows with completed startup,
known RTT, fresh feedback, no active loss recovery and no scheduler-stale flag.
Sample time and reception must each be within two smoothed RTTs. Invalid and
out-of-order samples are rejected. The inherited PINT path-change ACK is skipped
rather than controlling with its old U. Timestamp ordering is not a complete
route-epoch protocol.

Per-subflow vectors:

- `mpOrbPressureSubflowRate`, `mpOrbPressureConnectionRate`: x_i and X_c, bytes/s.
- `mpOrbPressureWeight`: normalized x_i/X_c for the current meta snapshot.
- `mpOrbPressureAiScale`: applied AI multiplier; 1 during re-entry/fallback.
- `mpOrbPressureReentryBoost`: whether a temporary boost is active.
- `mpOrbPressureWindowDelta`: candidate window minus its committed anchor, bytes.
- `mpOrbPressureExtraWithdrawalBytes`: extra reduction below the ordinary coupled target.
- `mpOrbPressureUncoupledAi`, `mpOrbPressureWeightedAi`: pre/post-control AI, bytes.
- `mpOrbPressureFreshSubflows`: eligible subflows at the update.
- `mpOrbPressureFairRate`: B/N, bytes/s; `mpOrbPressure`: N/B, seconds/byte.
- `mpOrbPressureConnectionAiRate`: gamma*sum(q_i*s_i), before any re-entry boosts.

`mpOrbPressureWeight` and `mpOrbPressureConnectionAiRate` have changed meaning
from the previous B/N-budget iteration. Signals arrive on separate subflow ACKs,
so summing sample-held weights from different snapshots need not give one.
Fallback subflows emit weight/scale 1 and are outside the eligible sum.

## Static checks

```sh
clang++ -std=c++17 -fsyntax-only samples/mporb/tests/MpOrbPressurePolicyTest.cc
```

Compile-time checks cover normalized aggregate-rate coupling, the ABC interior
stationarity/directional conditions, the multi-bottleneck limitation, weak-path
re-entry conditions, freshness, withdrawal bounds and the uncoupled AI/target
caps. They do not exercise the OMNeT++ event loop, scheduler or ACK timing.
Build and simulation validation remain necessary before runtime claims.
