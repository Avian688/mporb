# MpOrbDelta

`MpOrbDelta` inherits `MpOrbSemiCoupledAlpha`. It is the consolidated former
Theta controller. Alpha's AI, local utilisation-based MD, startup and pacing
remain the baseline; Delta adds a connection-rate fairness correction.
`MpOrbOmega` inherits Delta and retains its underutilisation AI boost.

## Sender to switch

Each data packet carries nominal subflow rate x = cwnd / srtt and total
connection rate X (sum over active, non-stale Delta-derived subflows), in
payload bytes/s. Before an RTT sample, rate estimation uses 20 ms. X is at
least the transmitting subflow's rate. A per-subflow serial identifies fresh
feedback; it is incremented for each stamp, not for each switch interval.

## Switch measurement

Only aggregate state is stored; there is no connection table. In a control
interval of duration T, each packet with payload L contributes:

    m = L / (max(x, rateFloor) * T)
    M += m
    R += m * X

At the boundary, retain previous M and meanX = R / M, then reset the current
accumulators. T defaults to the queue's avgRTT, sampled and frozen at each
boundary; deltaShuffleInterval can override it. An idle interval expires the
previous population. The first observation interval supplies no correction.

A participating selected bottleneck returns:

    packetMass = m
    meanConnectionRate = previous meanX
    rateAllowance = beta * B / previous M

B is link capacity in bytes/s; beta = deltaShuffleFraction defaults to 0.05.
Only the highest-U selected bottleneck supplies the feedback (OrbCC's tighter
B/N tie break remains). A newly selected disabled queue clears earlier feedback.
A later ordinary PINT winner is handled by the sender's final hop-ID check.

## Sender correction

On fresh echoed feedback from the final selected bottleneck:

    delta_x = m * rateAllowance * (1 - X / meanX)
    pending_window += delta_x * srtt

Below-average total connection rate gives positive correction; above-average
rate gives negative correction. These are proportional corrections, not a fixed
step. At Alpha's normal RTT commit, Delta adds the accumulated whole bytes to
Alpha's result, clamps the window to [MSS, UINT32_MAX], updates prevWnd, and
consumes the adjustment. Fractional bytes remain for later rounding. Fast ACK
updates still run Alpha, without repeatedly spending the pending correction.

With a stationary interval population, mass-weighted corrections sum to zero.
Previous-interval feedback, changing load, feedback loss/reordering and window
clipping prevent exact instantaneous conservation. The 5% coefficient sets a
scale; it is not a hard interval transfer cap or a global max-min guarantee.
The correction compares nominal cwnd/RTT rates, not measured application goodput.

Signals include mpOrbDeltaSubflowRate, mpOrbDeltaConnectionRate,
mpOrbDeltaCorrection, mpOrbDeltaAppliedShuffle and the queue's
mpOrbDeltaMeanConnectionRate, mpOrbDeltaRateAllowance, mpOrbDeltaRoundInterval.

Use MpOrbDeltaQueue for Delta/Omega experiments; set deltaShuffleFraction to
0.05 only on intended forward bottlenecks and 0 elsewhere. Alpha and uncoupled
comparators continue using ordinary PintQueue. Rebuild mporb before running the
new names and tag layout. Static checks do not validate simulation behaviour.
