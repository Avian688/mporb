# Delta: Alpha plus signed connection-need shuffle

Select `MpOrbSemiCoupledDelta`. It inherits `MpOrbSemiCoupledAlpha` directly:
Alpha's steady-state x_r/X additive-increase coupling, fractional-byte carry,
startup, local U-based MD, telemetry identity, loss recovery and pacing.
Delta adds the existing signed rate correction at RTT commits. It does not
modify Alpha itself or replace its coupling. The earlier uncoupled-baseline
Delta is superseded; its allocation predictions do not describe this version.

With shuffle disabled, Delta uses Alpha's baseline window calculation. Single-path
connections have x_r/X=1; multipath subflows share the additive increase as in Alpha.

## Packet fields

`DeltaShuffleTag` is TCP-header telemetry alongside the existing PINT record.
Header size is abstracted, as for Omega. Every data transmission carries:

- x_r = cwnd / SRTT, in payload bytes/s (20 ms bootstrap before SRTT is known).
- X = sum of nominal rates of active, non-stale Delta subflows, including sender.
- A transmission serial, echoed on ACKs, to avoid applying a shuffle twice.
- The signed sum of forward-link shuffle feedback, also echoed on ACKs.

These rates are nominal window rates, not measured delivery rates. Scheduler or
receive-window limitations can make them overestimate actual traffic.

## Aggregate link calculation

Only forward Delta data participates. PINT queues keep aggregate sums, with no
new flow/connection table. Existing PINT U/flow-count state remains as before.
For packet payload L bytes and frozen round duration D seconds, form:

    need = 1 / max(X, rateFloor)                 [s/byte]
    mass = L / (max(x_r, rateFloor) * D)         [dimensionless]

Over an interval, accumulate M = sum(mass), H = sum(mass * need). The average
need for use in the next interval is mean = H/M. Weighting by L/(x_r*D) compensates
packet frequency and segmentation: a subflow sending twice as fast should not
count twice merely because it sends twice as many packets. Dividing by D makes
previous and current totals comparable when their round durations differ.

This estimates a subflow-weighted average connection need; without identities it
cannot deduplicate multiple subflows of the same connection on a shared link.
For the intended edge-disjoint bottlenecks, each connection ordinarily has one
subflow at a given bottleneck. Accuracy also depends on x_r matching actual rate.

Also retain the minimum and maximum need observed in each interval (two
aggregate counters, not flow records). For link capacity B in payload bytes/s,
shuffle fraction beta and need gain g:

    range      = maxNeed_previous - minNeed_previous
    scale      = max(2 * mean_previous / g, range)
    rawShuffle = beta * B * mass/M_previous * (need - mean_previous) / scale
    remaining  = max(0, beta * B - sum_abs_shuffle_so_far)
    shuffle    = clamp(rawShuffle, -remaining, +remaining)

Shuffle is a **rate increment in bytes/s**, not a cwnd-byte grant or traffic
volume. Larger need than the previous average gives positive shuffle; smaller
need gives negative shuffle. Equal need gives zero. Each returned value debits
the magnitude budget by its absolute value.

The gain strengthens the response to modest rate gaps. Using one common scale
for every packet preserves centring: for a stable population the signed sum is
zero, and sum(mass * abs(need - mean)) <= M * range bounds the absolute total.
The hard magnitude cap still protects changing populations. This avoids simply
multiplying feedback until early packets exhaust the budget and destroy balance.
Gain 1 recovers the earlier formula (with conservative range normalisation for
highly skewed populations). Gain 0 disables shuffle. There is no integral
controller, per-connection switch state, debt, rollback or retry mechanism.

Defaults on each `PintQueue`:

    deltaShuffleInterval = 0s   # automatic: queue average RTT
    deltaShuffleFraction = 0.05
    deltaNeedGain = 8
    deltaRateFloor = 8kbps       # 1000 payload bytes/s internally

For a 100 Mbps link, the combined absolute adjustments total at most 5 Mbps per
round. In a perfectly balanced round this is at most +2.5 Mbps and -2.5 Mbps.
The budget is per round. By default D is the queue's current average RTT,
sampled once when the round starts and held until its timer expires. An average
RTT update never moves the existing deadline. Startup uses pintInitialRtt until
an average is available; fixedAvgRTTVal is honoured through the queue estimate.
If pintUseAverageRtt is disabled, the fallback is positive fixedAvgRTTVal or
pintInitialRtt. Set deltaShuffleInterval to a positive value (e.g. 20ms) only for
an explicit fixed-interval comparison. Gain 8 and the 5% cap are unchanged.
For the approximately 40ms ABC paths, steady rounds are now about 40ms, so there
is roughly half as much shuffle per sender RTT as with the old 20ms interval.
Different sender RTTs on one link still give different R_sender / D ratios;
using a common average RTT does not guarantee RTT fairness.
Set deltaShuffleFraction=0 to disable shuffle and retain plain Alpha.
The previous `deltaGrantInterval` and `deltaAiFraction` parameters are removed.

Enable the 5% shuffle budget only on links intended to participate as bottlenecks.
PINT instrumentation alone does not imply that a link should issue shuffle.
The ABC experiment's currently edited `experiment4_mporb_semicoupled_alpha.ini`
(selecting Delta) keeps 0.05 on the eight router1 bottleneck outputs and sets 0
elsewhere, with specific entries before the wildcard. Access-link PINT remains
available. Its current topology has 20 Mbps bottlenecks and 10 Gbps access links:
5% would otherwise allow 1 Mbps versus 500 Mbps per round respectively. A
single-connection access link can issue nonzero feedback when that connection's
rate changes relative to the previous interval, even without competing flows.
The experiment 4 generator now preserves this participation scope as well.


The first active interval collects the mean and returns zero shuffle. After a
whole idle interval, stale averages are discarded and observation restarts.
A queue timer advances round boundaries even without a data departure. After a
whole empty round it discards stale averages and stops. The next Delta data
packet starts a fresh observation round. Queues with deltaShuffleFraction=0 do
not start shuffle timers or add feedback; their PINT telemetry still operates.

## Approximate balance, not strict zero-sum

For an unchanged interval population and weights, signed adjustments sum to zero
(up to floating-point rounding). They need **not** sum to zero during arrivals,
departures, changing rates or budget exhaustion, because the normaliser is from
the previous interval. This is the simple approximation selected for this test.
Earlier packets can consume the remaining budget during a sudden change.

Forward links add their signed feedback. Do not take the minimum: that would
systematically retain negative adjustments and discard balancing positives.
The 5% bound is per link, not per entire path. A multi-bottleneck path can receive
corrections from more than one link; this is an experimental limitation.

## Sender application

Alpha first computes its window, including coupled AI and inherited startup/MD.
On feedback reception, accumulate shuffle * SRTT in signed cwnd bytes. Then:

    newWindow = clamp(alphaWindow + wholePendingShuffleBytes,
                      MSS, UINT32_MAX)

Shuffle is applied **only on OrbCC's normal RTT-paced commit**. Fast ACK
updates use Alpha unchanged and do not apply pending shuffle. On commit,
install the resulting window as prevWnd and clear whole-byte shuffle, retaining
only the signed sub-byte remainder. This matters because OrbCC's cwnd growth gate
can return the current window: reapplying pending negative shuffle on fast ACKs
would subtract the same adjustment repeatedly. No bank of corrections blocked by
window limits is retained. Pacing follows the normal OrbCC cwnd/RTT calculation.
The shuffle has no additional cwnd-limited gate; Alpha's inherited OrbCC AI gate remains
in its unchanged computation.

The serial high-water mark drops duplicate and older reordered feedback rather
than adding a per-packet deduplication set. Normal delayed ACK settings remain
inherited; delayed ACKs may echo only the latest packet's shuffle. Thus packet or
ACK loss, ACK thinning/reordering, window floors/ceilings and changing RTTs can
unbalance the adjustments actually applied by senders. Immediate ACKs, as used
by the existing experiments, avoid delayed-ACK thinning. No shuffle is applied
before a valid SRTT is available. Alpha works even with missing shuffle
feedback or uninstrumented links.

## Baseline and gain interpretation

In steady state, Alpha sets AI_r = (x_r/X) * AI_r_OrbCC, with its existing
fractional-byte carry. Delta then adds the queue-issued shuffle at a committed
window update. Shuffle gain 8, the 5% per-link cap, average-RTT queue rounds,
participating bottlenecks and packet feedback accounting are unchanged.

Because the baseline AI is now coupled, a given shuffle has a larger magnitude
relative to baseline AI on low-share subflows than under uncoupled OrbCC. Results
and equilibrium calculations for the previous baseline must be reassessed; this
change alone is not a claim of max-min fairness or measured stability.

## Signals

All rates below are displayed in bps; internal arithmetic uses bytes/s.

| Signal | Location | Meaning |
|---|---|---|
| mpOrbDeltaSubflowRate | sender/queue | x_r |
| mpOrbDeltaConnectionRate | sender/queue | X |
| mpOrbDeltaNeed | sender/queue | 1/X after floor, in s/byte |
| mpOrbDeltaMeanNeed | queue | previous interval mean, in s/byte; zero while warming up |
| mpOrbDeltaRawShuffle | sender/queue | signed correction before magnitude cap |
| mpOrbDeltaReturnedShuffle | sender/queue | signed correction after cap |
| mpOrbDeltaRoundInterval | queue | frozen interval for this round, in seconds |
| mpOrbDeltaRoundShuffle | queue | running signed total for current round |
| mpOrbDeltaRoundShuffleMagnitude | queue | running absolute total for current round |
| mpOrbDeltaAppliedShuffle | sender | actual shuffle contribution at committed window update |

Sender raw/returned values sum participating forward links; queue values describe
that queue only. Sender need uses the last participating link's floor. The round
counters reset at each timer boundary; their last emitted sample in a round gives
the final total. mpOrbDeltaRoundInterval marks round starts, including empty rounds. Returned and applied shuffle are event increments;
do not integrate them as sample-held rates. Existing additiveIncrease records
**Alpha-coupled AI**, not the shuffle. To enable only Delta diagnostics, place
these before catch-all recording overrides:

    **.mpOrbDelta*.statistic-recording = true
    **.mpOrbDelta*.vector-recording = true

## Validation

`python3 tests/check_delta_policy.py` performs compile-time checks of the actual
link calculation: signs, stable-population balance, magnitude bound under load
changes, segmentation, packet-rate compensation, idle restart and disabled
shuffle. `tests/check_delta_sender.py` additionally checks the actual sender
computeWnd body with a minimal OrbCC growth-gate test stub: fast ACKs cannot spend
pending shuffle; only a committed update applies it. C++ syntax and NED validation cover the integration. No library build or
OMNeT++ simulation was run; convergence/goodput remain to be measured. This is
not a claim of connection max-min or proportional fairness.
