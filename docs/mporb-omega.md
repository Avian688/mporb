# MpORB Omega

Omega replaces Alpha's per-subflow additive-increase coupling with **one
connection-level price controller**. Each subflow still supplies INT feedback,
RTT estimation and TCP recovery. The meta connection updates all its subflow
windows from the same snapshot every 5 ms.

**Cwnd controls sending. Normal OrbCC pacing is inherited unchanged.** Omega
does not set a separate pacing target, add the usual OrbCC AI, or apply OrbCC's
utilisation-based window reduction on top of the price controller.

## Objective and equations

The intended ideal allocation is **connection proportional fairness**, not
connection max-min fairness:

```
maximise  sum_c log(X_c / R0)
subject to  sum_(c,r using l) x_cr <= eta * C_l
            X_c = sum_r x_cr
```

`x_cr`, `X_c`, `R0`, and `C_l` are TCP payload rates/capacities in bytes/s.
`R0` is a common numerical rate unit (default 100 Mbps / 8), not a demand or
entitlement. Use the same `omegaRateScale` for all connections. `eta=0.95` is
the desired link load. Prices and gains are dimensionless; time is seconds.

Each enabled output queue has an arrival-byte counter and a persistent price
`z_l`. Every `h=5ms`:

```
y_l = incoming TCP payload bytes / h
T_l = max(20ms, queue average RTT)
e_l = y_l / (eta * C_l) - 1
z_l <- max(0, z_l + 0.2 * h/T_l * (1 + z_l) * e_l)
p_l = max(0, z_l + (1 + z_l) * e_l + q_l/(C_l*T_l))
```

`q_l` is the queue's stored bytes, including packet headers, as in the existing
queue accounting. The load counter counts payload **before queue admission or
drop**, including retransmissions; it does not confuse capacity-limited
departures with offered load. ACK-only traffic does not enter this counter.
The payload-capacity/wire-capacity distinction remains an approximation.

The integral `z_l` retains congestion history. The instantaneous error and
queue terms damp the delayed loop and help drain queues. They vanish at a
queue-free equilibrium. An idle queue continues updating so its old price
decays. The factor `1+z_l` gives relative price adaptation at high prices; a
link with many competing connections need not raise its price one fixed tiny
increment at a time. No new flow or connection identifiers/tables are needed
for pricing.

A data packet accumulates `P_r = sum_(l in r) p_l`. ACKs echo that **forward**
price unchanged. This is separate from PINT's maximum-U bottleneck record;
reverse-path prices and U are not added to it. Feedback also carries the
minimum observed forward link capacity and coverage counters. A zero price
is valid and distinct from missing pricing support.

At each meta update, take one snapshot of all active subflows:

```
X = sum_r target_r
K = number of active subflows
T_c = max(20ms, all active subflow RTTs)
residual_r = 1 - (X/R0)*P_r
target_r <- clamp(target_r + 0.1 * h/T_c * (X/K) * clamp(residual_r, -1, 1),
                  MSS_r/RTT_r, minimum_forward_capacity_r)
cwnd_r = max(MSS_r, floor(target_r * smoothed_RTT_r))
```

All new targets are computed before any are installed. Existing in-flight
bytes above a reduced cwnd drain normally. TCP applies its usual whole-segment
sendability, receive-window and recovery restrictions. There is no factor
`x_r/X` in the update: a nearly empty path gets the same rate correction as
another path with the same price. `X` still appears in the connection's
marginal utility, which is needed to control its **aggregate** rate.

Without the transient clamp, this is the log-utility gradient `R0/X-P_r`
multiplied by the **same positive factor `X^2/(K*R0)` on every path**. This
reduces oversized updates when many connections have very small rates.
It does not multiply a path's correction by its own current rate. When prices
are zero, aggregate growth is 10% of the connection rate per reference RTT:
it scales with `X`, rather than remaining one fixed path's AI budget. `K`
prevents merely adding subflows from multiplying that fractional growth rate.

Ignoring bounds, two paths in one connection obey:

```
change_r - change_s = 0.1 * h/T_c * X^2/(K*R0) * (P_s - P_r)
```

Thus the cheaper path gains relative to the expensive path even when both
links remain busy. Total rate grows when marginal benefit exceeds the mean
path price and falls when the reverse holds; no fixed bulk-transfer demand is
specified. Both paths can initially grow if both are cheap relative to the
connection's total rate. A path being relatively expensive does not imply
it must immediately decrease in absolute terms.

At an ideal interior equilibrium, `P_r = R0/X_c` for every used path, with
nonnegative link prices and complementary slackness at `eta*C_l`. Unused
paths have price at least `R0/X_c`. This is the log-utility optimality condition.
The implementation's nonzero minimum window, finite updates, transient price
terms, delayed feedback, integer windows and recovery make it an approximation.

## Transport details and safeguards

- **Pacing remains OrbCC's:** its nominal rate is
  `1.2 * max(cwnd, bytesInFlight) / smoothedRTT`, with interval `MSS/rate`.
  The initial pre-RTT pacing bootstrap is also retained. Consequently the
  controller's target is a nominal cwnd/RTT rate, not a guaranteed measured
  delivery rate. No changes were made to the shared OrbCC pacing method.
- Initial window/rate is five MSS; the first valid measurement seeds a rate
  of at least `0.1*(B/N)`. `B/N` never determines steady-state entitlement.
- A changed path digest reduces the old target conservatively before the next
  joint update. Older feedback is ignored.
- Feedback older than four reference RTTs reduces a measured path to its
  one-MSS/RTT floor until feedback returns. That floor permits data/telemetry
  refresh; it is not a dedicated probe packet or a scheduler bypass. A hard
  outage still depends on TCP recovery and the existing scheduler/path manager.
- Cwnd cannot be increased over the transport's recovery window during
  recovery. RTO backs the target down to one MSS/RTT. First RACK recovery halves
  the target with the same floor. No normal ACK-triggered retransmission path
  bypasses pacing.
- Without queued application demand, the meta controller does not increase
  targets. Achieved delivery is measured from the existing SACK-aware delivered
  byte counter and averaged over the reference RTT. It is recorded alongside
  `B/N`, **not** used to turn scheduler limitation/HoL into a false entitlement.
- The normal scheduler is retained. Use `defaultCwnd` initially to keep unsent
  assignments bounded. The CC does not cancel data already assigned to a path.
- Immediate INT ACKs are required (`delayedAcksEnabled=false`); Omega checks
  this and the pacing/SACK settings at initialisation. It rejects echoed
  telemetry with zero priced hops or a mixture of priced/unpriced PINT hops.
  Ordinary non-PINT queues are not visible to the coverage counter: the setup
  must enable pricing on **every possible congestion point**, including shared
  access links. Congestion on an uninstrumented queue is not accounted for.

## Configuration

Select the class and enable prices in the experiment's own configuration:

```ini
**.tcp.typename = "MpOrb"
**.tcp.tcpAlgorithmClass = "MpOrbOmega"
**.schedulerMode = "defaultCwnd"
**.tcp.pacingEnabled = true
**.tcp.updatedSackEnabled = true
**.tcp.delayedAcksEnabled = false

# All relevant output links, including any shared access bottleneck:
**.ppp[*].queue.typename = "PintQueue"
**.ppp[*].queue.omegaPriceEnabled = true
**.ppp[*].queue.omegaPriceTarget = 0.95
**.ppp[*].queue.pintUseAverageRtt = true
**.ppp[*].queue.flowCountSketchEnabled = true
```

Put these in the selected `[Config ...]` so they override inherited defaults.
Replace conflicting entries in that configuration; OMNeT++ uses the first
matching assignment. Preserve the experiment's existing queue capacities,
flow-count encoding choices and consistent sender/queue settings. The fixed-size
flow sketch and average-RTT mode avoid the optional exact-flow sets/per-flow U
estimator; the **price mechanism itself** is constant-size in either mode.

Price metadata is represented by `OmegaPriceTag`, a small copyable/serializable
INET region tag. It intentionally abstracts wire overhead and quantisation.
No changes to generated `IntTag_m.*` are necessary. Rebuild **orbtcp and mporb**
before running Omega. Existing algorithms retain their behaviour with the
default `omegaPriceEnabled=false`; their windows/schedulers were not changed.

Record `omegaPathPrice`, `omegaTargetRate`, `omegaConnectionTargetRate`,
`omegaDeliveryRate`, `omegaFairShareReference`, `omegaPriceFresh`,
`omegaLinkPrice`, `omegaPriceMemory`, and `omegaOfferedLoad`, together with
cwnd, RTT, goodput, subflow send queue and receiver HoL. Enable those statistics
before broad `statistic-recording=false`/`vector-recording=false` rules.

## Validation and limits

`python3 samples/mporb/tests/check_omega_policy.py` constant-evaluates the
**production** price/rate equations in the C++ compiler, without building an
executable or running OMNeT++. Tests cover idle price decay, step bounds, the
window conversion/MSS floor, recovery of a tiny rate, delayed one-path and
two-path equilibria, a shared access bottleneck, hotspot departure, a three-link
redistribution chain and the parking-lot proportional-fair allocation.
It also checks 6, 20 and 100 symmetric competing connections; a fixed absolute
rate step was unstable in the denser numerical cases, motivating the common
connection-dependent step above.

For ideal 100 Mbps payload-capacity links at 95% target:

| Case | Expected connection rates |
| --- | --- |
| One connection, one link | 95 Mbps |
| Two single-path connections, one link | 47.5 Mbps each |
| One connection, two independent links | 190 Mbps total |
| Those two paths share a 100 Mbps access link | 95 Mbps total |
| A uses links 1/2; B uses only 1 | Approximately 95 Mbps each; A moves to 2 |
| A uses 1/2; B uses 2/3; C uses only 1 | Approximately 95 Mbps each, via redistribution |
| Three-link parking lot, one lane | Spine 23.75 Mbps; each rib 71.25 Mbps |

These numerical tests assume the controller can realise its rate targets.
They do **not** prove packet-level stability, scheduler cooperation, HoL
behaviour or superiority to Alpha. Nor do a few fixed-delay cases prove
stability under arbitrary LEO RTTs, route changes or link heterogeneity.
The gains are initial engineering choices requiring packet-level validation.
The sum of log utilities is strictly concave in connection totals, not in all
individual path splits; multiple equally good splits may exist.

The design uses the network-utility/shadow-price interpretation of
[Kelly, Maulloo and Tan (1998)](https://web.stanford.edu/class/cs244/papers/ShadowPricesFairnessStability.pdf).
Omega's discrete PI-price loop, meta update, cwnd actuator and safeguards are
an implementation choice here, not that paper's algorithm or stability theorem.
The supplied Handley discussion motivates coordinated redistribution; it is
not evidence that this particular controller is stable or novel.

Main code: `orbtcp/src/common/OmegaPrice.h` contains the equations;
`orbtcp/src/queueing/queue/PintQueue.cc` measures/offers prices;
`mporb/src/transportlayer/tcp/MpOrbOmegaControl.cc` performs the joint update;
`mporb/src/transportlayer/tcp/flavours/MpOrbOmega.cc` handles feedback and cwnd.
