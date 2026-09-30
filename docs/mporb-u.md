# MpOrbU

MpOrbU replaces Alpha's rate-share additive-increase multiplier with a
continuous INT utilisation weight. It uses the existing MpORB/PINT transport
and scheduler. No rate-share term, temporary boost, extra withdrawal, or new
INT field is introduced.

```ini
**.tcp.tcpAlgorithmClass = "MpOrbU"
**.tcp.mpOrbUBeta = 20
**.tcp.mpOrbUSmoothingRtts = 1
```

These are module parameters on `tcp`, unlike `schedulerMode`, which belongs
on `tcp.conn-*`. Replace an earlier matching algorithm assignment; OMNeT++
uses the first matching INI assignment. Existing experiment generators and
their Alpha configurations are not changed by adding this variant.

## Controller

For each eligible subflow i, the algorithm uses its existing PINT bottleneck U:

```text
gain = 1 - exp(-elapsed_sample_time / (smoothingRtts * srtt))
smoothedU_i += gain * (U_i - smoothedU_i)
z_i = smoothedU_i / eta_i
weight_i = exp(-beta * z_i) / sum_j exp(-beta * z_j)
AI_i = weight_i * uncoupledOrbccAI_i
```

The implementation subtracts the minimum z before exponentiation to avoid
all scores underflowing when U is high. This leaves the normalised weights
unchanged. Lower U gets more growth; equal U/eta gets equal weights; beta=0
gives equal weights regardless of U. A single eligible subflow has weight 1.
The default beta=20 is an experimental starting value, not a tuned or validated
choice. The smoothing time constant defaults to one RTT and depends on elapsed
measurement time rather than the number of ACKs.

Each AI remains between zero and its uncoupled OrbCC increment. Fractional
bytes are carried between committed window updates so a small positive weight
is not permanently rounded away. Fast ACK updates do not repeatedly spend or
accumulate that rounding credit. Startup, congestion decreases, the existing
cwnd-limited growth gate, minimum cwnd, pacing and loss handling remain inherited
from OrbCC/PINT. Only the U used for coupling is smoothed here; the underlying
OrbCC congestion response still uses the original accepted U.

## Feedback and eligibility

Only established/close-wait MpOrbU siblings past startup, outside recovery,
and with a valid sample no older than two of their own RTTs enter the sum.
Future, out-of-order and older-than-two-RTT feedback is rejected. Sparse PINT
sampling still follows the parent controller's configured feedback probability.
The first accepted U initializes the smoother. It is reset after a telemetry
gap or path-digest change; a path-change ACK cannot apply the previous path's U
to the new path. The next accepted record initializes the new path's smoother.
With no usable peer feedback, normal uncoupled AI is the fallback.

This does not arrange probes on scheduler-starved paths. Fresh telemetry still
requires transmissions. Large beta can amplify noise and yield near-zero
weights; no connection fairness or convergence guarantee is claimed.

## Signals

The subflow exposes `mpOrbUSmoothedU`, `mpOrbUWeight`, `mpOrbUFreshSubflows`,
`mpOrbUUncoupledAi` and `mpOrbUWeightedAi`. Startup/recovery fallback has weight
1 and fresh-subflow count 0. Peer weight samples arrive at different times;
normalisation applies to the eligible snapshot used for each decision, not to
arbitrarily aligned points from different subflow vectors.

If an experiment disables recordings, place any desired overrides before its
catch-all rules. For example:

```ini
**.mpOrbUWeight.statistic-recording = true
**.mpOrbUWeight.result-recording-modes = vector(removeRepeats)
**.mpOrbUWeight.vector-recording = true
```

## Validation and build

`src/Makefile` includes the new object, and regenerated deep makefiles discover
it automatically. Rebuild MpORB before selecting `MpOrbU`.

Numerical checks use expressions extracted from the production source:

```sh
python3 samples/mporb/tests/check_u_policy.py
```

These validate weights, sensitivity, numerical extremes, smoothing and AI
rounding as Python arithmetic. The production C++ is separately syntax-checked
against the real project headers. They do not establish transport behaviour or
runtime performance; no build or simulation was run for this addition.
