# MpOrbPressure: equilibrium calculation and its limits

This analysis concerns the current rate-share controller, not the previous
B/N-budget controller. It does not change the controller or run OMNeT++.

## Correction to the previous fixed-point argument

The existing `netChange` checks in `MpOrbPressurePolicyTest.cc` choose
`U = eta / (1 - gamma*q*price)`. They establish consistency between a supplied
price and the window equation. They do **not** establish that the queues
produce that U at the supplied rate. In particular, 266.7/266.7/266.7 and
200/200/200 Mbps are capacity-level fairness benchmarks, not predictions of
this implementation's actual operating point.

Matching the marginal-utility condition is insufficient to prove the
hard-capacity proportional-fair optimum: link prices must also satisfy
capacity complementary slackness. The current rule can exert a positive
effective price below full capacity.

## Model and assumptions

- Each core link has capacity C = 100 Mbps, eta = 0.95, gamma = 0.05.
- Fixed RTT T = 0.04 s; window/RTT equals delivered payload rate.
- All subflows remain eligible, backlogged and window-limited.
- No recovery, loss, HoL, re-entry boost or changing routes.
- Negligible queue occupancy and sufficiently smooth packet spacing that
  the unencoded PINT utilization is approximated by U_l = y_l/C.
- Exact flow counts, no U encoding or byte rounding in the equilibrium table.
- All floor-level subflows continue sending and remain counted in N.
- Packet/header overhead is omitted. Values are model rates, not exact
  application goodput predictions for a 100 Mbps physical link.

PintQueue actually smooths per-packet samples of
`queueBytes/(B*avgRTT) + payloadBytes/(B*tau)`, with tau bounded below by
payload service time. The smooth-load assumption is an approximation, not
an identity for bursty traffic or for packet-weighted averaging.

## Interior stationarity

Use consistent rate units. Let q_i = C/N_i, x_i = W_i/T and
X_c = sum of eligible subflow rates in connection c. With no boost:

    AI_i/T = gamma*q_i*x_i/X_c
    d_i = max(0, 1 - eta/U_i)
    delta_x_i = x_i * (gamma*q_i/X_c - d_i)

A positive interior stationary subflow therefore satisfies:

    1/X_c = d_i/(gamma*q_i)
    U_i = eta/(1 - gamma*q_i/X_c)

Users of the same limiting link see the same q and U in this model. If both
have interior subflows on that link, their connection totals must be equal.
This equality need not hold when a subflow is at its lower bound.

For fixed N and U = y/C, the effective link price is

    p_l(y) = N_l/(gamma*C) * max(0, 1 - eta*C/y).

It corresponds to a congestion-penalized log-utility model, rather than
automatically to log utility maximization with only hard capacity constraints.
For y >= eta*C its integrated penalty is

    N_l/(gamma*C) * [y - eta*C - eta*C*log(y/(eta*C))].

That interpretation itself assumes fixed N and one selected bottleneck per
route; it is not a global claim for the switched packet-level implementation.

## ABC without background

Symmetry and shared-link interior stationarity give X_A = X_B = X_C = X.
The four shared links have N = 2; the four private links have N = 1.

    y_shared = 95*X/(X - 2.5)
    y_private = 95*X/(X - 5)
    3*X = 4*y_shared + 4*y_private

The feasible root is X = 257.0895009463 Mbps. Shared links carry 95.9328742902
Mbps and private links 96.8842514195 Mbps. A sends 64.2723752366 Mbps per path;
B and C each send 31.6604990537 Mbps on each shared path, plus their two
private-path rates. The aggregate is 771.2685028390 Mbps.

## Background phases and the window floor

The two-MSS floor corresponds to

    f = 2*1448*8/(0.04*1e6) = 0.5792 Mbps.

For a connection whose two strong paths still each count one weak competitor,
the relevant total is X_s = 2*eta*C + gamma*C/2 = 192.5 Mbps.
For two private strong paths plus two floor paths, the total is the larger root

    X_p^2 - (2*eta*C + gamma*C + 2*f)*X_p + 2*f*gamma*C = 0,

giving X_p = 196.1288683964 Mbps.

| Phase | A | B | C | Each background connection |
| --- | ---: | ---: | ---: | ---: |
| No background | 257.09 | 257.09 | 257.09 | - |
| Five background flows on each of 5 and 6 | 192.50 | 192.50 | 196.13 | 19.72 |
| Five background flows on each of 1 and 2 | 192.50 | 196.13 | 196.13 | 19.49 |

With background on 5/6, A's paths 1/2, B's paths 5/6 and C's paths 3/4 can
sit at f. A and B each put the rest of their total on their other two paths;
C uses its private paths. With background on 1/2, A and B put their 1/2
subflows at f and C puts its 3/4 subflows at f.

For five background flows of rate g sharing a link with h Mbps of floor
traffic and total counted flows N, their rate solves

    (5*g + h) * (1 - gamma*C/(N*g)) = eta*C.

Use (N,h) = (6,f) for phase 5/6 and (7,2*f) for phase 1/2.
Substitution into every subflow's ordinary target, floor and uncoupled cap
gave maximum residual below 2e-14 Mbps in a numerical algebra check.
This verifies these candidate fixed points, not their attraction or uniqueness.
After background admission stops, these phase calculations apply only after
the background connections' outstanding traffic has drained.

## Why gain 4 changes the noisy operating point

Ignoring bounds, the applied change is F(delta) = delta for delta >= 0 and
G*delta otherwise. Thus

    E[F(delta)] = E[delta] + (G-1)*E[min(delta,0)].

If E[delta] = 0 but fluctuations are nonzero, G > 1 produces negative mean
drift. It preserves a deterministic zero, not necessarily the stochastic
operating point. Bounds, growth gating and correlated feedback add further
effects.

Concrete example at the private-path equilibrium above: U = 0.9688425142,
q = 100, X = 257.0895009463. With the default legacy PINT base 1.05 and
512-flow scaling, the adjacent decoded U values are 0.9588949268 and
1.0068396731. Including the legacy upward grid rounding, the higher value
has probability approximately 0.2463 for this constant local U.

The ordinary coupled candidate changes by +1.0172% or -3.7005% of W.
Gain 4 changes those to +1.0172% or -14.8020%. Neither the floor nor the
25% bound is active in this example. The expected candidate change at this
fixed state is approximately -0.1447% with gain 1 and -2.8789% with gain 4.
These are conditional candidate-update calculations, not time-average
throughput or a complete stability analysis of the ACK/RTT feedback loop.

## Interpretation

The calculation predicts the smooth operating point and identifies a
quantization/withdrawal interaction capable of explaining sharp window cuts.
It does not establish that the new iteration outperforms the previous one.
Full convergence and mean-goodput predictions require the delayed, quantized,
membership-changing feedback loop and transport constraints to be included.

For the distinction between path-price stationarity and the full constrained
fairness conditions, see Kelly, *Charging and rate control for elastic traffic*:
https://www.statslab.cam.ac.uk/~frank/elastic.pdf
