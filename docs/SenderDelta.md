# MpOrbSenderDelta

Select `tcpAlgorithmClass = "MpOrbSenderDelta"` in the same places that select
`MpOrbSemiCoupledDelta`. Retain the experiment's bottleneck-only
`deltaShuffleFraction = 0.05` allowlist and zero elsewhere. The new variant uses
the existing Delta queue parameters, interval timer, rate floor and ACK echo.
Existing switch-side Delta remains `MpOrbSemiCoupledDelta`.

The class inherits Alpha directly. Alpha source is unchanged. Normal coupled AI,
local utilisation MD, startup, pacing and RTT commits run through the same parent
methods. The fairness correction is added once at a normal committed window;
fast feedback does not repeatedly spend it. The MSS floor still applies.

For packet payload L, stamped nominal subflow rate x and total connection rate X:

    mass = L / (max(x, rateFloor) * interval)
    need = 1 / max(X, rateFloor)
    M += mass; H += mass * need

Only the chosen maximum-U bottleneck returns an exact measurement record. Its mean and K
are common across the interval and come from the previous completed interval:

    meanNeed = H_prev / M_prev
    scale = max(2 * meanNeed / gain, maxNeed_prev - minNeed_prev)
    K = fraction * B / (M_prev * scale)

B is payload bytes/s, mass is dimensionless, need is seconds/byte and K has
units (bytes/s)^2. No previous population means K=0. The queue also returns the
packet mass and its need so the sender can use the identical sample and floor;
this avoids reconstructing a past packet's rates or link interval at ACK time.
There is no per-connection switch table, signed switch calculation, per-packet
clipping or switch budget spending for this variant.

On fresh echoed feedback the sender calculates:

    correction = mass * K * (need - meanNeed)
    pendingWindow += correction * subflowRTT

At Alpha's next normal window commit it adds pendingWindow, retaining fractional
bytes for the next commit. Existing Delta's serial high-water duplicate handling
is retained. Corrections come only from the selected bottleneck, as in switch-side Delta.
The returned measurements are exact, not PINT-approximated.

For a stable observed population the raw corrections sum to zero on each link.
With changed traffic, delayed/lost feedback, window floors or inactive subflows,
exact applied zero-sum is not guaranteed. The 5% parameter scales K; without the
legacy switch clip it is not a hard transient budget. No new limiter is added.

Signals: existing mpOrbDeltaSubflowRate, mpOrbDeltaConnectionRate,
mpOrbDeltaNeed and mpOrbDeltaAppliedShuffle; new mpOrbSenderDeltaCorrection
(bytes/s converted to bits/s for recording), and queue mpOrbDeltaNeedCoefficient.
Queue mpOrbDeltaMeanNeed remains available. Alpha's other signals are unchanged.

Static checks (no simulation):

    python3 tests/check_delta_policy.py
    python3 tests/check_sender_delta_policy.py
    python3 tests/check_delta_sender.py
    python3 tests/check_delta_sender.py --variant MpOrbSenderDelta

These check legacy behaviour, measurement/raw-correction equivalence, stable
balance, startup/idle telemetry, intentional lack of a switch budget and window
commit timing. They do not establish runtime fairness or stability.

## Bottleneck scope

Feedback follows the same selected record as OrbCC U/B/N: maximum decoded U,
then tighter B/N for ties. A later winner replaces earlier feedback; a disabled
winner clears it. Other participating queues still measure M/H/range. No signed
sender correction from those other links is applied.

Selecting only some packets' link feedback can break the link-wide zero-sum
property even with stable traffic. In switch-side Delta an upstream provisional
grant may also be replaced downstream; its already-spent budget is not refunded.
The returned correction is always from the final selected hop. No extra state
or refund protocol is introduced to compensate for that limitation.
