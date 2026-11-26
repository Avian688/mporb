# MpOrbDelta

`MpOrbDelta` inherits `MpOrbSemiCoupledAlpha`. It is the consolidated former
Theta controller. Delta adds a connection-rate fairness correction to Alpha's
AI, local utilisation-based MD and pacing. The coupled base retains ordinary
transport startup and disables the PINT initial phase.
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
Each round starts with separate positive and negative feedback budgets of
beta * B, using the link capacity at the round boundary. The queue computes
the packet's correction, limits it to the remaining budget for that sign,
and debits the grant. If capped, it scales the packet's rateAllowance so the
sender reconstructs the granted correction. Unused budget expires at the
round boundary.
Each queue that becomes the current PINT winner can write feedback and debit
its local budget. The sender accepts feedback whose hop ID matches the final
winner, selected by highest `U` with OrbCC's tighter `B/N` tie break. A newly
selected Delta queue with shuffling disabled clears earlier shuffle feedback.

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

With a stationary interval population, uncapped mass-weighted corrections sum
to zero. The two budgets can spend different amounts. Previous-interval
feedback, changing load, feedback loss/reordering and window clipping also
prevent exact instantaneous conservation.
The correction compares nominal cwnd/RTT rates, not measured application goodput.

Signals include mpOrbDeltaSubflowRate, mpOrbDeltaConnectionRate,
mpOrbDeltaCorrection, mpOrbDeltaAppliedShuffle and the queue's
mpOrbDeltaMeanConnectionRate, mpOrbDeltaRateAllowance, mpOrbDeltaRoundInterval.

## Queue placement

Use `MpOrbDeltaQueue` on links that should supply shuffle feedback. The default
`deltaShuffleFraction` is `0.05`. Set it to `0` to keep PINT telemetry and
disable shuffling on that queue. The four numbered experiments enable shuffling
on their forward bottlenecks. ExperimentLeo and ExperimentWorkload configure
Omega's shuffle queues on satellite, ground-station and user-terminal PPP
interfaces, so a terminal access link can also supply feedback.
