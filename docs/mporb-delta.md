# MpORB Delta: relax Alpha's additive-increase suppression

Select after rebuilding mporb:

```ini
**.tcp.typename = "MpOrb"
**.tcp.tcpAlgorithmClass = "MpOrbSemiCoupledDelta"
```

Delta inherits the Alpha/PINT transport and replaces only the steady-state AI
hook. It starts from plain Alpha's rate share; it does not call Alpha's
experimental boost. Existing Alpha and Beta implementations are unchanged.

## Rule

For each eligible subflow r, with window W in bytes and smoothed RTT R in seconds:

```text
x_r = W_r / R_r                         [bytes/s]
X   = sum(x_j)                          [bytes/s]
K   = number of eligible subflows       [dimensionless]
s_r = x_r / X                          [dimensionless]
w_r = min(1, K * s_r)                   [dimensionless]
AI_r = OrbCC_AI_r * w_r                 [bytes]
```

OrbCC supplies its existing local AI, including its B/N and RTT dependence.
Delta retains fractional-byte credit when rounding, as Alpha does. AI is never
multiplied repeatedly by calling Alpha first. Startup, U-based window reduction,
committed-window updates, growth eligibility, cwnd floors and scheduling remain
inherited. This is an AI change, not a multiplier on the full window target.

The pool matches Alpha: established/close-wait Alpha-family subflows with a
positive RTT and finite positive cwnd/RTT. K counts exactly the same subflows
whose rates enter X, rather than the configured path count. An invalid own
membership/rate or total rate leaves the incoming OrbCC AI unchanged. Ordinary
experiments should use Delta on all subflows of the connection.

## Effect

Ignoring byte rounding, the weight lies between plain Alpha's s_r and 1.
For K equal-rate subflows each weight is 1, so each gets its full local OrbCC AI.
For one subflow this is uncoupled OrbCC. Weights are deliberately **not**
renormalised to sum to one.

| Rate shares | Plain Alpha weights | Delta weights |
| --- | --- | --- |
| (0.5, 0.5) | (0.5, 0.5) | (1, 1) |
| (0.8, 0.2) | (0.8, 0.2) | (1, 0.4) |
| (0.25, 0.25, 0.25, 0.25) | Same as shares | (1, 1, 1, 1) |
| (0.99, 0.01) | (0.99, 0.01) | (1, 0.02) |

If all local uncoupled AI values equal A, plain Alpha's summed AI is A, while
Delta's is A * sum(min(1, K*s_r)), between A and K*A. With unequal local AI,
the corresponding sum is sum(A_r * w_r); neither rule has a universal common
byte budget. Different RTTs also mean these increments are applied on different
control timescales, so summed window increments are not a wall-clock rate gain.

## Limits

The cap ensures each local AI stays at or below its own uncoupled value. It
also means unequal paths do **not** recover the entire uncoupled aggregate AI
budget. Restoring sum(A_r) while keeping every AI_r <= A_r would require every
path to be uncoupled, leaving no AI suppression for redistribution.

Delta changes the allocation dynamics and may change equilibrium. It does not
establish connection max-min fairness, and physical core-path disjointness
does not remove competition on shared access links. Low-rate paths still get
small AI: a 1% share becomes a 2% weight for two paths. Thus this alleviates
the budget reduction but does not eliminate slow recovery or scheduler starvation.
As in Alpha, pool membership has no additional telemetry-freshness or
application-demand filter; an established idle path may still participate.

## Diagnostics and validation

- `semiCoupledAlphaSubflowRate`, `semiCoupledAlphaConnectionRate`, and
  `semiCoupledAlphaRateShare` retain their original meanings.
- `mpOrbDeltaEligibleSubflows`: K used for this update.
- `mpOrbDeltaWeight`: applied min(1, K*s_r).
- `mpOrbDeltaUncoupledAi` / `mpOrbDeltaWeightedAi`: input/output AI in bytes.

Enable selected diagnostic vectors explicitly when an experiment's broad
vector recording is off. No existing experiment matrices are changed.

`python3 tests/check_delta_policy.py` constant-evaluates the production AI hook
with stubbed transport state using a syntax-only compiler invocation. It checks
equal/asymmetric paths, unequal RTT/local AI, membership changes, startup,
invalid inputs, and fractional-byte credit. It does not simulate packets or
establish convergence, fairness, or goodput improvements.
