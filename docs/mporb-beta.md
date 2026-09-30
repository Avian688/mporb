# MpOrb Beta: U-based redistribution around plain Alpha

Select `tcpAlgorithmClass = "MpOrbSemiCoupledBeta"`. This replaces Beta's former
B/N opportunity-share policy. Alpha itself is unchanged, and Beta never calls
Alpha's experimental boost hook.

```ini
**.tcp.mpOrbBetaRedistributionGain = 0.5
**.tcp.mpOrbBetaUtilizationDeadband = 0.05
**.tcp.mpOrbBetaSmoothingRtts = 1
```

These are the defaults. Gain must be finite and in [0,1], deadband finite and
nonnegative, and smoothing RTTs finite and positive. The deadband is in absolute
U units: eta=0.95 and deadband=0.05 give the band [0.90,1.00].

## Main algorithm

For each eligible subflow i:

```
x_i = cwnd_i / srtt_i
s_i = x_i / sum(x_j)
e_i = sign(smoothedU_i - eta) * max(abs(smoothedU_i - eta) - deadband, 0)
meanError = sum(e_j) / numberOfSubflows
z_i = max(0, s_i + gain * (meanError - e_i))
w_i = z_i / sum(z_j)
AI_i = OrbCC_AI_i * w_i
```

Rates x have units bytes/s. U, errors, shares and weights are dimensionless;
AI has units bytes. Fractional-byte AI credit is retained. Integer AI never
exceeds the parent's uncoupled AI for the current update.

A high-U path relinquishes AI weight relative to lower-U siblings; a low-U
path receives more. The correction is added to the share, not multiplied by
it, so a nearly dormant path can receive a useful increment. Negative proposed
weights are clipped and the complete vector is renormalised. The resulting
weights sum to one; the local byte increments need not have a fixed total
because OrbCC's base AI depends on B/N and RTT.

Example: shares (0.99,0.01), U=(0.98,0.10), eta=0.95, deadband=0.05 and gain=0.5
produce errors (0,-0.8) and weights (0.79,0.21), before byte rounding.

When all errors are identical (including all paths inside the deadband), or
gain=0, Beta uses live plain Alpha shares. One path gets weight 1. This is not
an allocation target based on B/N: B/N remains only in OrbCC's original AI.

## Feedback and timing

The original reported U still drives OrbCC's ordinary window response. Beta
maintains a separate light U average for coupling to reduce post-encoding
sample noise:

```
h = min(1, elapsedMeasurementTime / (smoothingRtts * srtt_i))
smoothedU_i += h * (acceptedU_i - smoothedU_i)
```

It uses accepted PINT timestamps, not ACK counts. Repeated timestamps do not
advance the average. The first sample, or a sample after a stale-feedback gap,
initialises it directly. Future/expired samples are rejected; older samples
cannot overwrite a newer accepted observation. Route changes skip the old-U
control update; route changes, RTO and RACK invalidate feedback/cache epochs.

The pool follows Alpha: established/close-wait Alpha-family siblings with a
finite positive cwnd/srtt. Redistribution requires every member to be Beta with
matching settings/eta, past startup, outside recovery, and to have accepted U
feedback no older than twice its own smoothed RTT. Otherwise the entire pool's
cached correction is discarded and updates use live plain Alpha shares.

Any eligible ACK can refresh a shared weight snapshot. It writes every
participating sibling's weight and a common expiry of now + maximum pool RTT.
ACKs before expiry use that snapshot; they do not accumulate corrections.
Membership or feedback-epoch changes invalidate it immediately. Freshness and
recovery eligibility are checked on every update, including between refreshes.
Returning inside the deadband takes effect at the next snapshot refresh.
No extra timer or scheduler modification is introduced.

Startup, the utilisation-based window reduction, committed-window logic,
cwnd-limited growth gate, minimum cwnd, pacing and scheduler probing remain
inherited. Relinquishing AI is not a direct cwnd transfer and does not move
already-assigned bytes. A scheduler-starved path still needs fresh feedback
before it can participate in redistribution.

## Diagnostics

- `mpOrbBetaSmoothedU`: the separate coupling U average, on accepted samples.
- `mpOrbBetaUtilizationError`: deadband error used at the last snapshot; NaN on
  invalid-pool fallback.
- `mpOrbBetaRateShare`: live plain Alpha rate share.
- `mpOrbBetaBaselineShare`: share used for the applied weight (snapshot share
  during redistribution, live share otherwise).
- `mpOrbBetaWeight`: effective AI weight.
- `mpOrbBetaWeightCorrection`: effective weight minus baseline share, **after**
  clipping and renormalisation; not necessarily the unclipped proposed correction.
- `mpOrbBetaRedistributionActive`: valid cached nonzero U-error correction.
- `mpOrbBetaUncoupledAi`: parent's integer AI before coupling, bytes.
- `mpOrbBetaAlphaAi`: counterfactual live plain Alpha AI using the same incoming
  fractional credit, bytes; excludes Alpha's boost.
- `mpOrbBetaWeightedAi`: applied integer AI, bytes.

Existing Alpha rate signals also remain available. Experiment 2/3/4 Beta
recorders and extractors include the new signals; startup only emits the
coupling U average. Signals from separate subflows arrive asynchronously:
compare coherent weight snapshots rather than assuming arbitrarily sampled
last-value traces sum to one across a refresh.

`python3 tests/check_beta_policy.py` constant-evaluates production Beta methods,
with OMNeT++ plumbing and PINT acceptance stubbed. It does not run a simulator.
The policy is intended to improve clear congestion/underutilisation transients;
when all paths are similarly busy it deliberately falls back to Alpha. Faster
convergence, stability and connection fairness are not yet runtime-validated.
Rebuild mporb before rerunning simulations.
