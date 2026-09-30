# OrbCC/PINT and MpORB Alpha: implementation-derived hybrid and fluid models

**Historical snapshot:** the equations, executable model and saved numerical
results below use the former fixed queue EWMA gain of 0.03. On 2026-09-30,
`PintQueue` changed to `gain = min(1, tau / avgRtt)` in average-RTT mode, keeping
the full inter-departure interval in the rate sample. The per-flow RTT ablation
uses elapsed time since that flow's last EWMA update for its gain. The queue
EWMA dynamics and numerical comparisons below have not been revalidated for
this change and should not be treated as the current implementation model.

This analysis describes the inspected local source, not an idealised HPCC algorithm.
MpORB Alpha follows `MpOrbSemiCoupledAlpha -> MpOrbSemiCoupledBase ->
MpOrbUncoupled -> OrbtcpPintFlavour -> OrbtcpFlavour`. Standalone OrbCC can
select either the PINT flavour or the older full-INT flavour; their telemetry
filters and startup rules differ. Beta, Pressure and U are not substituted for Alpha.

**Evidence labels.** **Exact** means a transcription of a control-relevant
source operation, conditional on its guards. **Hybrid model** retains packet,
ACK, timer, mode and quantisation events, while abstracting simulator plumbing.
**Fluid approximation** replaces some of those events by continuous rates.
**Numerical result** below means an independent Python model calculation,
not an OMNeT++ run or a measurement from the user's experiment results.

The executable model and saved results are
[model.py](/Users/av288/omnetpp-6.3.0/samples/mporb/analysis/orbcc-fluid-model/model.py)
and [summary.json](/Users/av288/omnetpp-6.3.0/samples/mporb/analysis/orbcc-fluid-model/results/summary.json).
The summary records SHA-256 hashes of the main source files used. No transport
implementation was changed for this analysis.

**1. Source map.** These are the principal implementation anchors.

| Operation | Source |
|---|---|
| Queue packet arrival, counters, RTT moments | [PintQueue.cc:392](/Users/av288/omnetpp-6.3.0/samples/orbtcp/src/queueing/queue/PintQueue.cc:392) |
| Queue service, U EWMA, bottleneck selection | [PintQueue.cc:278](/Users/av288/omnetpp-6.3.0/samples/orbtcp/src/queueing/queue/PintQueue.cc:278), [PintQueue.cc:471](/Users/av288/omnetpp-6.3.0/samples/orbtcp/src/queueing/queue/PintQueue.cc:471) |
| Flow-count and average-RTT epochs | [PintQueue.cc:156](/Users/av288/omnetpp-6.3.0/samples/orbtcp/src/queueing/queue/PintQueue.cc:156) |
| Sender accepts PINT and computes AI | [OrbtcpPintFlavour.cc:104](/Users/av288/omnetpp-6.3.0/samples/orbtcp/src/transportlayer/orbtcp/flavours/OrbtcpPintFlavour.cc:104) |
| PINT window update and committed window | [OrbtcpPintFlavour.cc:220](/Users/av288/omnetpp-6.3.0/samples/orbtcp/src/transportlayer/orbtcp/flavours/OrbtcpPintFlavour.cc:220) |
| RTT filters, ACK handling, pacing, timer | [OrbtcpFlavour.cc:128](/Users/av288/omnetpp-6.3.0/samples/orbtcp/src/transportlayer/orbtcp/flavours/OrbtcpFlavour.cc:128), [OrbtcpFlavour.cc:558](/Users/av288/omnetpp-6.3.0/samples/orbtcp/src/transportlayer/orbtcp/flavours/OrbtcpFlavour.cc:558) |
| Alpha AI multiplier and integer residual | [MpOrbSemiCoupledAlpha.cc:23](/Users/av288/omnetpp-6.3.0/samples/mporb/src/transportlayer/tcp/flavours/MpOrbSemiCoupledAlpha.cc:23) |
| Meta-connection identity used by N | [MpOrbSemiCoupledBase.cc:82](/Users/av288/omnetpp-6.3.0/samples/mporb/src/transportlayer/tcp/flavours/MpOrbSemiCoupledBase.cc:82) |
| Actual send eligibility and pace timer | [TcpPacedConnection.cc:691](/Users/av288/omnetpp-6.3.0/samples/tcpPaced/src/transportlayer/tcp/TcpPacedConnection.cc:691) |
| Sender tags and split queue-delay feedback | [MpOrbSubflowConnection.cc:129](/Users/av288/omnetpp-6.3.0/samples/mporb/src/transportlayer/tcp/MpOrbSubflowConnection.cc:129), [MpOrbSubflowConnection.cc:187](/Users/av288/omnetpp-6.3.0/samples/mporb/src/transportlayer/tcp/MpOrbSubflowConnection.cc:187) |
| Older full-INT telemetry/controller | [IntQueue.cc:168](/Users/av288/omnetpp-6.3.0/samples/orbtcp/src/queueing/queue/IntQueue.cc:168), [OrbtcpFlavour.cc:330](/Users/av288/omnetpp-6.3.0/samples/orbtcp/src/transportlayer/orbtcp/flavours/OrbtcpFlavour.cc:330) |

**2. Variables, units and state.** Let i denote a subflow, c its connection,
l a directed link, k a packet/ACK event and m a measurement epoch. A byte is
8 bits, but payload, queued-packet and on-wire byte counts must remain distinct.

| Symbol | Meaning / implementation | Units |
|---|---|---|
| W_i, V_i | Current `snd_cwnd`, committed `prevWnd` | TCP payload bytes |
| M_i, A_i, e_i | MSS, `additiveIncrease`, Alpha fractional AI residual | Payload bytes; e_i is a fraction of one byte |
| F_i | `m_bytesInFlight` / SACK `pipe` | Payload bytes |
| S_i, J_i, W_i^recv | Unsent assigned subflow queue, retained write queue, receive-window credit | Payload bytes |
| x_i, v_i, a_i^sched | First-transmission rate, ACKed payload rate, scheduler assignment rate | Payload bytes/s |
| X_c, rhat_i | Sum of W_i/s_i over siblings, W_i/s_i | Payload bytes/s |
| p_i, delta_i^pace | Nominal pacing rate, intersending interval | Payload bytes/s; s |
| R_i, s_i, v_i^RTT | Latest raw RTT, `srtt`, `rttvar` | s |
| E_i, Ebar_i | Queue-corrected RTT sample, its EWMA | s |
| D_i, d_i^f, d_i^r | Propagation/serialization baseline and directional delays in the model | s |
| Q_l | Actual queued packet bytes reported by the queue, after dequeue | Queue-domain bytes |
| Z_l | Fluid backlog expressed as remaining link-service work | Service-domain bytes |
| b_k, L_k, L_k^wire | TCP payload, queued packet size, serialized packet size | Bytes in their respective domains |
| B_l | Nominal datarate/8; packet telemetry `B` | Service-domain bytes/s |
| kappa_l, chi_l | b/L^wire, L/L^wire for a fixed-size mix | Dimensionless byte ratios |
| H_l | Queue's `avgRtt`; normalization interval | s |
| u_l, U_i | Local filtered utilisation, selected/decoded ACK utilisation | Dimensionless |
| N_l, N_i, S_l^init | Distinct recent IDs / decoded bottleneck count; initial-phase count | Dimensionless counts |
| alpha_q, g, a, eta, w_i | Queue EWMA gain, RTT EWMA gain, AI fraction, target, Alpha weight | Dimensionless |
| t_l^last, t_i^react, tau_l,k | Last local U update, next timer deadline, sample gap | s |
| lambda_l, nu_i | Tagged-data dequeue frequency, RTT-sample frequency | 1/s |
| K_l^1, K_l^2 | Queue's accumulated RTT/cwnd moments | s; s^2 |

Here `a = additiveIncreasePercent = 0.05`, `alpha_q = 0.03`, `g = 1/8`, and
`eta = 0.95` by the defaults examined. These are separate parameters: the
name **Alpha** does not mean the EWMA coefficient `alpha`.

The complete control-relevant state also contains discrete information:

- Sender: `firstRTT`, `initialPhase`, `endInitialPhase`, `updateWindow`,
  `ssthresh`, latest INT vector `L`, path digest/ID, bottleneck ID,
  last feedback time, TCP state, SACK/recovery flags and sequence boundaries,
  retransmission/ACK/pacing timer states and the retransmission scoreboard.
- Queue: ordered packets and arrival timestamps, packet/byte capacity and
  drop policy, cumulative payload `txBytes`, current service completion,
  flow-ID sets or bitmap banks, startup-ID sets/banks, two RTT sums,
  measurement-timer deadline/activity flag, `hasPintSample`, local U and its
  last-update time. The no-average-RTT variant has a per-ID U map instead.
- MpORB transport: scheduler-selected subflow and remaining burst, average
  pacing estimates, subflow write queues, meta send/window state, DSN mapping,
  receive reassembly and reinjection state. They determine whether W can
  actually be used. A cwnd-only ODE cannot determine application goodput.
- Static/configured data: routing, B, MSS, configured fallback base RTT,
  eta/AI gains, PINT codecs and feedback probability, packet capacities.

Several fields declared in `OrbtcpFamilyState.msg` do **not** drive this PINT/
Alpha control path: `normalisedInflight`, `normalisedInflightPrev`, `targetUtil`,
`incStage`, `maxStage`, `additiveIncreaseGainPerAck`, `R`, `rttCount`.
`B` is the configured initial bandwidth field; accepted feedback uses the
measured link B and stores it in `bottBW`. `subFlows` is not the Alpha divisor.
The delivery-rate EWMA helper in `MpOrbSemiCoupledBase` is not called by Alpha.
These dormant fields should not acquire invented differential equations.

**3. Exact queue events and what U measures.** On arrival of a TCP data packet,
PINT obtains payload b_k after removing IP/TCP headers, marks its connection
ID, marks its startup ID if applicable, and accumulates

\[
K_l^{1+}=K_l^1+\widetilde E_i\frac{b_k}{\widetilde W_i},\qquad
K_l^{2+}=K_l^2+\widetilde E_i^2\frac{b_k}{\widetilde W_i}.
\]

The tildes denote decoded sender metadata. The two increments have units
s and s^2 respectively: b/W is dimensionless. This happens **before queue
overflow handling**, so a packet dropped subsequently can still affect N
and the RTT moments. The packet is enqueued, then the configured dropper
removes packets until capacity constraints hold.

At a busy measurement epoch, the queue assigns

\[
H_l^+=\begin{cases}H_l^{fixed},&H_l^{fixed}>0,\\
K_l^2/K_l^1,&K_l^1,K_l^2>0,\\H_l,&\text{otherwise},\end{cases}
\qquad
N_l^+=\max(1,\operatorname{round}(\widehat{\#IDs}_l)).
\]

It resets the completed epoch's sums/sets or bitmap bank and schedules the
next epoch approximately H_l seconds later. On an idle expiry it returns
without updating/rescheduling; arrival restarts measurement. H and N are
sample-and-hold values, **not EWMAs**. N is not the instantaneous number of
backlogged senders and does not immediately fall when a flow stops.
If H is nonpositive after the epoch calculation, the configured initial RTT
is the fallback; the default initial RTT is 10 ms.

With exact sets, the cardinality is the number of distinct IDs seen in the
epoch. With the default bitmap size m=4096 and z empty bits, the estimate is
`-m log(z/m)`; an empty bitmap gives zero and a full bitmap uses `m log(m)`.
The total count is rounded, floored at one and ultimately limited to 65535.
The startup count may be zero. Count encoding is exact through 31 with default
8-bit/max-65535 settings; above this it rounds conservatively to logarithmic
levels. Hash collisions/epoch activity remain sources of count error.

Alpha's tag ID hashes the **meta connection**, so two Alpha subflows from one
connection crossing the same link count once there. MpOrbUncoupled uses its
subflow connection ID. These differ on shared links. On edge-disjoint paths,
each participating connection still contributes one ID per visited link.
PINT also creates a tag for previously untagged TCP data; such data can share
the default ID rather than representing distinct accurately counted senders.

On dequeue at t_k, the current packet is removed first. Let Q_l,k be the
remaining queue bytes, and b_k the dequeued payload. With average RTT enabled:

\[
\tau_{l,k}=\max(t_k-t_l^{last}, b_k/B_l),\qquad
z_{l,k}=\frac{Q_{l,k}}{B_lH_l}+\frac{b_k}{B_l\tau_{l,k}},
\]
\[
u_l^+=(1-\alpha_q)u_l+\alpha_q z_{l,k},\qquad t_l^{last+}=t_k.
\]

Both terms of z are dimensionless; tau has units s. This uses the interval
between **dequeued TCP data packets**, not between this sender's ACKs and not
the arrival rate. The first local sample is only Q/(BH), with no rate term.
The payload counter also increases by b_k. Pure ACKs do not update this U
filter or data-packet timestamp, although they consume link service and can
accumulate reverse queue delay.

The no-average-RTT variant replaces H by the packet's carried RTT (normally
sender srtt) and updates a per-ID U EWMA. Its inter-dequeue timestamp remains
global. Thus it is not simply the default model with H deleted.

Local U is encoded/decoded, and the packet retains the **largest decoded U**
along its forward path. Equal-U ties choose the smaller B/decoded-N. B, N,
startup count, hop ID, Q and timestamp belong to this same selected record.
Reverse ACK traversal preserves that bottleneck record.

For default legacy U coding, let f=512, b=1.05 and Pmax=255. First form
`u0=max(1,ceil(f*u))/f`. Adjacent levels are `b^p/f`; choose the upper level
with probability `(u0-u_low)/(u_high-u_low)`, saturating at Pmax. Its
conditional expectation is u0 between levels, **not necessarily u**, and
maximum selection across hops introduces another bias. `pintBits=0` carries
the exact simulation U. Auto-scaled coding uses a different logarithmic base
and no initial ceil(f*u), but still clips and selects a maximum.

**The bandwidth distinction matters.** B is channel bit/s divided by 8, not
a learned per-flow available bandwidth. The U rate numerator is payload;
Q includes queued packet headers. On-wire PPP overhead may occupy still
another byte domain. For constant packet sizes, Q=chi Z and payload service
capacity is kappa B. Therefore even at a fully busy link, the payload rate
term is about kappa, not necessarily 1. INT tags themselves are simulator
metadata; this analysis does not assign them an invented serialized size.

**4. Exact RTT and sender telemetry events.** MpORB installs queue-delay
telemetry before invoking the RTT measurement callback on a new ACK. With TCP
timestamps, the callback uses millisecond timestamp differences. For sample
R_i and total decoded forward-plus-reverse queue delay d_i^Q:

\[
E_i=R_i-d_i^Q,\quad
s_i^+=s_i+g(R_i-s_i),\quad
v_i^{RTT+}=v_i^{RTT}+g(|R_i-s_i|-v_i^{RTT}),
\]
\[
\bar E_i^+=\bar E_i+g(E_i-\bar E_i),\qquad g=1/8.
\]

All quantities and increments are seconds. The error in the variance update
uses the pre-update srtt. Initial srtt is set to the first raw sample; the
initial corrected-RTT filter starts from raw RTT before its correction.
If E or its smoother becomes nonpositive, the implementation falls back to
raw RTT / srtt. RTO is clipped from `srtt+4*rttvar` to [0.2,240] s in
`OrbtcpFlavour`. Queue-delay coding rounds each hop's residence to 64 us,
saturating each direction at 262.08 ms. Consequently Ebar is a queue-corrected
RTT estimate, **not an oracle propagation RTT**: serialization, ACK generation,
timestamp quantisation and unmeasured/saturated queueing can remain in it.

Packets carry Ebar and W through the sender metadata codecs: base RTT uses
24 bits in 1 us units; cwnd uses an exponent/mantissa code. The switch uses
their decoded values in its moment ratio. The separate forward-delay scheduler
uses `Ebar/2 + measured_forward_queue_delay`; the division by two is an explicit
symmetric-baseline assumption, not measured forward propagation.

Accepted PINT feedback sets `state->u = U_i` and `state->alpha = 1`.
There is **no second sender U EWMA** in this flavour. The U EWMA already ran
at the switch. `state->txRate = B_i U_i` has units bytes/s but includes the
queue term and filtering/encoding; it is not a measured subflow rate or a
pure aggregate payload service rate.

Feedback with empty records, nonfinite/nonpositive U or B, zero decoded N,
or a failed configured feedback-probability trial does not update cwnd.
Alpha itself has no general two-RTT freshness rejection. On a path-digest
change, the current PINT parent installs the new digest but returns **old U**
without recomputing AI; that ACK can therefore apply old U/AI to the saved
window. This behaviour is retained in the exact model, not silently repaired.

**5. Exact Alpha and window updates.** Outside startup, an accepted ACK
first produces the uncoupled budget

\[
A_i^0=\left\lfloor a\frac{B_i}{N_i}R_i\right\rfloor.
\]

Units: `[1] * [bytes/s] / [1] * [s] = [payload-byte budget]` under the
implementation's nominal-bandwidth convention. R_i here is the **latest raw
RTT**, not H_l, srtt, Ebar or the feedback interarrival time.

For Alpha, use the current *pre-update* window values:

\[
\widehat r_i=W_i/s_i,\quad
\widehat X_c=\sum_{j\in\mathcal E_c}W_j/s_j,\quad
w_i=\widehat r_i/\widehat X_c,
\]
\[
A_i=\lfloor A_i^0w_i+e_i\rfloor,\qquad
e_i^+=A_i^0w_i+e_i-A_i.
\]

Rates have units payload bytes/s; w is dimensionless; A and e are byte
increments. The eligible set comprises non-null established/close-wait Alpha
(including subclasses) siblings with positive finite W/srtt. It does not
filter on current backlog, recovery, telemetry age or actual delivery rate.
An idle established sibling can dilute the weight. Residual credit advances
on **every accepted AI-hook call**, not just once per window commit. If the
uncoupled budget has already truncated to zero, the Alpha hook cannot recover
that lost fraction. Startup is uncoupled. With no usable meta/rate sum, the
computed uncoupled budget is left alone.

Define the byte-valued committed anchor V as `prevWnd` when positive, otherwise
W; define `h(U)=[1-eta/U]_+` for U>0. In congestion avoidance the proposed
window is exactly the clipped integer form of

\[
T_i=V_i\min(1,\eta/U_i)+A_i
    =V_i+A_i-V_i h(U_i).
\]

This is a byte-valued expression. **The weight multiplies only A, not V or
the entire target window.** Repeated ACKs recompute T from V; they do not add
A to the previous ACK's cwnd repeatedly.

The actual update then applies the source's guards:

\[
W_i^{sendable}=M_i\max(1,\lfloor W_i/M_i\rfloor),\qquad
G_i=\mathbf1\{S_i>0\;\land\;F_i+M_i\ge W_i^{sendable}\},
\]
\[
W_i^+=\max\{M_i,\ G_i\,\operatorname{clip32}(T_i)
 +(1-G_i)\min(\operatorname{clip32}(T_i),W_i)\}.
\]

`clip32` truncates nonnegative finite byte values and bounds them by uint32
maximum; invalid/nonpositive targets return zero before the MSS floor.
This G is the instantaneous `isCwndLimited()` test. The separate remembered
`m_isCwndLimited` usage state is not what this OrbCC method reads.

The react timer sets `updateWindow=true` and schedules its next expiry using
the current srtt. It does **not** itself modify V or W. The first subsequent
accepted ACK executes the above update, then assigns V_i^+=W_i^+ and clears
the flag. If no feedback arrives for several timer periods, the bool does
not accumulate multiple AI credits. All intermediate accepted ACKs can still
change W using the old V. Thus there are two clocks: ACK target recomputation
and approximately-one-RTT commits.

**Startup and recovery are additional modes.** Establishment seeds W=7300
bytes, V=W and a 1 us bootstrap pacing interval. The first startup timer,
scheduled after accepted feedback, clears `firstRTT` and enters `initialPhase`.
With R_i^start chosen as positive Ebar, then raw RTT/srtt, then configured T:

\[
W_i^{fair}=\eta(B_i/N_i)R_i^{start},\quad
A_i^{start}=\operatorname{clip32}\!\left(\min\{[W_i^{fair}-V_i]_+,
a(B_i/\max(1,S_i^{init}))R_i^{start}\}\right).
\]

Each term is bytes. PINT startup targets `min(V+A_start, floor(W_fair))`;
it does not perform the usual U decrease in that branch. A commit reaching
the threshold exits startup. Duplicate ACK handling also clears initial phase.
The full-INT variant differs, as described below.
Setting `pintUseInitialPhase=false` skips these startup flags and starts the
PINT congestion-avoidance policy directly; it does not remove establishment
or TCP's RTT sampling requirements.

RACK/SACK loss, RTO, retransmission and connection closure are jump events,
not an invented Reno `-W/2` term. In this inheritance chain PRR is not enabled
by the model-based OrbCC flavour. On entering RACK recovery, the common code
sets the recovery boundary and a recovery window approximately
`F + max(lastAckedSackedBytes,M)`, capped to uint32; PINT aligns V afterwards.
RTO backs off its timer, changes the scoreboard/recovery flags, invokes the
transport retransmission path and realigns V. **The inspected RTO call chain
does not itself implement the comment's claimed cwnd=M reset**: that reduction
is left to algorithm subclasses and is not present here. Retain the actual
reset map if modelling losses. Loss/recovery is deliberately excluded from
the numerical experiments below.

**6. Sending, RTT and queue conservation.** The exact pacing update is

\[
p_i=1.2\,\frac{\max(W_i,F_i)}{s_i},\qquad
\delta_i^{pace}=M_i/p_i.
\]

p is payload bytes/s and delta is s. Existing scheduled pace events are not
rescheduled when this value changes; the new interval affects later sends.
The pacer cannot transmit merely because p is positive: it also needs queued
data, at least one MSS of send/recovery-window space, receiver credit and an
eligible sequence range. In the loss-free paced path, F+M must fit within W.
This is why **actual x is not generally W/srtt or 1.2 W/srtt**.

A general fluid conservation representation is

\[
\dot S_i=a_i^{sched}-x_i,\qquad
\dot F_i=x_i-v_i,\qquad
0\le x_i\le p_i,
\]

with extra reinjection, retransmission and SACK-loss terms in loss modes.
Every derivative is bytes/s. ACK rate v is delayed receiver delivery, not
instantaneously equal to x. Window shrink may leave F>W; sending must then
stop until flight drains. At a constant-window boundary F=W, sustainable
x is constrained by ACKs. For a steady, continuously backlogged, loss-free,
window-limited path, Little's-law closure gives

\[
F_i\simeq W_i,\qquad x_i\simeq W_i/R_i.
\]

This is a modelling approximation; it is not an assignment in the code.
For time-varying RTT, the corresponding derivative is

\[
\dot x_i=\dot W_i/R_i-(W_i/R_i^2)\dot R_i.
\]

Both terms have units payload bytes/s^2. Dropping the second term assumes
constant RTT, which is particularly restrictive during LEO route changes.

For directed link l, let A_l be arriving service-work bytes/s and let D_l^svc
be departing service-work bytes/s. In the constant-size abstraction:

\[
A_l=\sum_{i\ni l}x_{i\to l}/\kappa_{il}+A_l^{other},\quad
\dot Z_l=A_l-D_l^{svc}-D_l^{drop},\quad Q_l\simeq\chi_l Z_l,
\]
\[
D_l^{svc}=\begin{cases}B_l,&Z_l>0,\\\min(A_l,B_l),&Z_l=0.\end{cases}
\]

Z and Q are bytes; every rate in the conservation equation is bytes/s.
At a finite capacity, drop work removes overflow. Actual packet-count limits
cannot be converted to one constant byte limit with variable packet sizes.
FIFO class departures, propagation delays and downstream arrivals close a
multi-hop network; do not send the same instantaneous x to every hop during
a transient. Reverse ACK queues have their own work conservation even though
their zero-payload packets do not drive the forward PINT U filter.

A path RTT closure is

\[
R_i\simeq D_i+\sum_{l\in P_i^f}Z_l/B_l
                  +\sum_{l\in P_i^r}Z_l/B_l+d_i^{ACK}.
\]

D absorbs propagation and the chosen serialization approximation, dACK is
receiver ACK delay, and each term is seconds. Actual packet RTT is a sum of
residence times encountered at different times, not this instantaneous sum.

For a full MpORB transport model, a scheduler maps meta backlog into
`a_i^sched`. The current default ranks retained write bytes / pacing-rate
average; the INT scheduler ranks forward-delay estimate plus
`(unsent+referenceBurst)/min(averagePace,currentPace,windowRate)` and applies
admission/burst rules. Both are byte/s and second-valued operations, not an
extra term in the congestion controller. Its queue/credit state must be
retained to predict starvation or HoL. The numerical model assumes every
subflow remains fed and therefore predicts payload transport, **not DSN
in-order application goodput**.

**7. Fluid U, H, N and RTT approximations.** For regular local packet spacing
delta=1/lambda and a fixed input z, n EWMA updates give

\[
u_n-z=(1-\alpha_q)^n(u_0-z).
\]

An exact exponential interpolation at those regular sample times has

\[
\dot u_l=k_l\left(\frac{y_l^{payload,out}}{B_l}
                    +\frac{Q_l}{B_lH_l}-u_l\right),\qquad
k_l=-\lambda_l\log(1-\alpha_q),\quad
\tau_l^U=1/k_l.
\]

The derivative has units 1/s: k is 1/s and the bracket is dimensionless.
The often-used `k≈alpha_q*lambda` is the small-gain approximation; it is also
the mean drift for Poisson jump times under the appropriate independence
assumptions. At 100 Mbps payload service, M=1448 and alpha_q=.03, lambda is
about 8632.6/s and the regular-event time constant is about 3.80 ms—not one
RTT. At no data departures the code freezes U; the fluid lambda must fall to
zero, not continue decaying U on an arbitrary wall-clock timer.

**There is an additional sampling closure here.** Generally,

\[
\mathbb E_{packet}\left[\frac{b_k}{B_l\max(\Delta t_k,b_k/B_l)}\right]
\ne\frac{\text{time-average payload service}}{B_l}.
\]

For fixed b and no queue contribution, 100 equal intervals of `2b/B` have
time-average load .5 and event-average sample .5. Ninety-nine intervals of
`b/B` followed by one of `101b/B` have the **same** time-average load .5 but
event-average sample `(99+1/101)/100 = .990099`. Thus a faithful general
fluid U closure needs a departure-spacing distribution (and its correlation
with queues), not merely aggregate throughput. Gain-1.2 pacing plus a cwnd
gate can produce bursts and gaps. The packet model retains this; the basic
ODE assumes it away. Constant per-event EWMA also gives a different time
average from an event average on irregular sample times.

The queue average-RTT moment ratio has the epoch approximation

\[
H_l^{new}\simeq
\frac{\int_{epoch}\sum_{i\ni l}x_{i\to l}\bar E_i^2/W_i\,dt}
     {\int_{epoch}\sum_{i\ni l}x_{i\to l}\bar E_i/W_i\,dt}.
\]

Numerator: s^2; denominator: s; ratio: s. It becomes an ordinary arithmetic
average of RTTs only under extra assumptions: constant backlogged rates,
W_i=x_i R_i and Ebar_i=R_i. Otherwise it is this ratio of moments. Retaining
the epoch reset is the faithful hybrid option. A smooth approximation
`dot H=(Htarget-H)/T_H` with T_H≈H has units s/s but introduces a filter that
is **not literally present** in the source.

N is best retained as the discrete, delayed occupancy count. If arrivals of
each ID are approximated as Poisson, the expected number of IDs seen during
an epoch is `sum_c [1-exp(-integral lambda_c dt)]`, a dimensionless count.
That is an optional statistical closure, not the code's exact set/sketch.
For fixed backlogged cases, taking N equal to the known distinct-ID count is
the simplest justified approximation; taking N to be the number of subflows
is wrong for shared Alpha paths belonging to the same connection.

RTT EWMAs similarly admit

\[
\dot s_i=k_i^R(R_i-s_i),\quad
\dot{\bar E}_i=k_i^R(R_i-d_i^Q-\bar E_i),\quad
k_i^R=-\nu_i\log(1-g),
\]
\[
\dot v_i^{RTT}=k_i^R\bigl(|R_i-s_i|-v_i^{RTT}\bigr).
\]

These are RTT, corrected-RTT and deviation-filter approximations. Their derivatives have
units s/s. nu is the actual measurement frequency; timestamps, delayed ACKs,
Karn sampling and ACK thinning affect it. It is not automatically 1/R.

**8. Hybrid sender to continuous window dynamics.** The faithful hybrid
state keeps V, W, the commit flag and deadline separately and uses the jumps
in section 5. To obtain an ODE, assume fixed established paths, fresh feedback,
post-startup operation, large enough windows to ignore byte/MSS rounding,
continuous backlog, no receiver/DSN/loss constraints, and W≈V between commits.
Let T_i^c be the mean commit interval. Then

\[
f_i=\frac{a(B_i/N_i)R_iw_i-W_i h(U_i)}{T_i^c},\qquad
\dot W_i\simeq\min(f_i,0)+G_i\max(f_i,0).
\]

Both numerator terms are bytes; dividing by seconds gives bytes/s. For
uncoupled OrbCC set w_i=1. For Alpha keep the W/srtt weight; under the stated
quasi-steady closure it becomes x_i/X_c. With frequent accepted ACKs,
T_i^c≈s_i. More generally, periodic expiries and the following accepted ACKs
determine the commit intervals: with one commit per expiry, each interval is
the timer period plus the **difference** between successive ACK-wait offsets. Multiple
elapsed timer periods without accepted feedback collapse to one flag and
can lengthen the interval. The timer is not restarted at each commit.
Do **not** multiply the whole AI term by the ACK packet rate.

For a delayed model, B_i,N_i,U_i are the bottleneck record generated earlier
on the acknowledged data packet, while R_i and w_i are the sender's latest
values at the ACK event. Write, for example, U_i(t)=Q_U(u_b(t-d_i^feedback)),
with the selected hop b and its associated B/N retained. Selection can change
between ACKs. The feedback delay from that hop to the sender is not generally
the full RTT and does not include all forward hops before that hop. Sender
traffic reaches the hop after its own forward delay. Together these make a
delay/hybrid system. The implemented numerical ODE explicitly uses zero
feedback delay; the independent packet calculation has propagation and ACK
delays.

The conservation ODE with a reflected empty-queue boundary and h's kink is
continuous-time but piecewise smooth. A genuinely smooth **regularisation**
on U>0, positive windows and a fixed active set can use

\[
h_\epsilon(U)=\epsilon\log(1+\exp((1-\eta/U)/\epsilon)),\qquad
D_{l,\epsilon}^{svc}=B_l[1-\exp(-Z_l/\epsilon_Q)],
\]
\[
\dot Z_l=A_l-D_{l,\epsilon}^{svc},\qquad
\dot W_i=[a(B_i/N_i)R_iw_i-W_ih_\epsilon(U_i)]/T_i^c.
\]

epsilon is dimensionless, epsilon_Q is service bytes. These equations are
smooth within the positive-state domain and preserve nonnegative queue
trajectories. They approximate the threshold and work-conserving service;
they are not exact implementation behaviour. For a sub-capacity constant
arrival rate, the artificial equilibrium queue is
`-epsilon_Q log(1-A/B)`, tending to zero as epsilon_Q tends to zero. Thus
regularisation itself changes finite-epsilon equilibria. The main numerical
results use the physical reflected queue/threshold ODE rather than hiding
this extra bias.

**9. Equilibrium conditions and their limits.** Consider fixed routes and
capacities, exact deterministic U, fixed accurate N, constant RTT, all relevant
paths fed, no loss, cwnd floor or growth-gate binding, and no codec bias.
Stationarity of the congestion-avoidance controller requires

\[
W_i^*\left(1-\frac{\eta}{U_i^*}\right)=
a\frac{B_i}{N_i}R_i^*w_i^*,\qquad U_i^*>\eta,
\]
\[
x_i^*\left(1-\frac{\eta}{U_i^*}\right)=
a\frac{B_i}{N_i}w_i^*.
\]

The first equality is bytes=bytes; the second is bytes/s=bytes/s. Positive AI
cannot give a free interior fixed point at U=eta. Floors, integer-zero AI,
app/scheduler limitation and startup can produce other fixed points. In a
stochastic/oscillatory regime the relevant stationarity condition is instead
an expectation at commit events, approximately
`E[A - V h(U)] = 0` when the gate/floor are inactive; it cannot be replaced by
`E[A] - E[V] h(E[U]) = 0` without a further decorrelation/linearisation step.

Queues also obey complementary equilibrium conditions:

\[
Z_l^*\ge0,\quad A_l^*\le B_l,\quad
Z_l^*(B_l-A_l^*)=0,
\qquad
u_l^*\simeq y_l^{payload,out*}/B_l+Q_l^*/(B_lH_l^*).
\]

The last equality needs the regular-spacing closure. These conditions plus
route incidence, a consistent bottleneck choice and sender equations are
the network equilibrium system. They do not by themselves prove stability
or identify a proportional-fair utility function.

For Alpha, substituting w_i=x_i/X_c and cancelling a positive x_i gives

\[
1-\eta/U_i^*=\frac{aB_i}{N_i X_c^*}.
\]

All active subflows of c must be compatible with the same X_c. This is a
conditional balance relation, not a B/N rate target for each subflow.

**One link, n single-path connections, no header-domain discrepancy.** With
equal controller parameters and N=n, symmetry and Q=0 give

\[
\sum_i x_i^*=B(\eta+a),\qquad
x_i^*=B(\eta+a)/n,\qquad U^*=\eta+a.
\]

For .95+.05=1, this is 100 Mbps total on a 100 Mbps link. The zero-queue
solution requires eta+a<=1. If eta+a>1, stationary capacity-limited service
requires a positive queue and U above 1 (assuming a<1), not an impossible
payload rate greater than capacity.

**One Alpha connection, K equal edge-disjoint links, one ID per link.** With
w_i=1/K and zero queues:

\[
x_i^*=B(\eta+a/K),\quad X_c^*=B(K\eta+a),\quad U_i^*=\eta+a/K.
\]

For two 100 Mbps links this gives 97.5 Mbps per path and 195 Mbps total.
Uncoupled instead gives 100+100 Mbps in this ideal closure. Different fixed
RTTs do not change these equilibrium rates because raw RTT, srtt and actual
RTT coincide there; they change windows and transient dynamics. A 20/80 ms
pair therefore needs four times the cwnd on the longer path for equal rates.

**Two unequal disjoint capacities, one Alpha connection.** With N_1=N_2=1:

\[
x_i^*=\frac{\eta B_i X^*}{X^*-aB_i},\qquad
\sum_{i=1}^{2}\frac{\eta B_i}{X^*-aB_i}=1,
\]
\[
X^{*2}-(\eta+a)(B_1+B_2)X^*
 +(a^2+2a\eta)B_1B_2=0.
\]

Choose the root with X*>a max(B_i) and feasible rates. The polynomial terms
all have units (bytes/s)^2. For 100/50 Mbps links the rates are 98.352712 and
48.323644 Mbps, totalling 146.676356 Mbps; the split is not exactly 2:1.

**Two Alpha subflows sharing the same single bottleneck.** They have one
meta ID and hence N=1 for that sole connection. The ideal aggregate is
again B(eta+a). The controller does not uniquely determine the subflow split:
equal-RTT positive splits form a continuum. The symmetric reduced ODE
preserves its initial rate ratio. Packet ordering, scheduler choices, RTT
differences and rounding can move the actual system along that continuum.
There is no separate-path allocation fairness guarantee here.

**Accounting for fixed packet overhead.** For n identical single-path
connections saturating a link with payload fraction kappa, x_i=kappa B/n.
The regular-packet stationary U and queue are

\[
U^*=\frac{\eta}{1-a/\kappa},\qquad
Q^*=BH\left[\frac{\eta}{1-a/\kappa}-\kappa\right].
\]

This positive-queue branch requires a<kappa and the bracket nonnegative;
otherwise use the feasible zero-queue branch. Z=Q/chi gives the service-work
queue and delay is Z/B. B H is bytes, so Q has the correct units. With 1448
payload bytes charged as 1500 service/queue bytes, kappa=.9653333, B=12.5 MB/s,
H=.04 s, eta=.95 and a=.05, x*=96.533333 Mbps, U*=1.001893664 and
Q*=18,280.165 bytes. This is an explicitly chosen accounting example, not an
assertion that every simulated packet has a 1500-byte wire size.

**Local stability of the simplest closure.** On a single queue-free path,
instantaneous U=x/B and fixed RTT R give, above eta,
`dot x=[B(eta+a)-x]/R`. The local pole is -1/R (1/s). This demonstrates
stability of that reduced mode only. With the U lag retained, the linearised
single-flow equations have matrix

\[
\begin{pmatrix}
-a/((\eta+a)R) & -\eta B/((\eta+a)R)\\
k/B & -k
\end{pmatrix}
\]

on (delta x,delta u). The off-diagonal units are bytes/s^2 and 1/byte,
consistent with delta x in bytes/s and delta u dimensionless. Trace is
negative and determinant k/R>0, giving local stability for k,R>0 **without
feedback delay or queue-mode changes**. ACK delays, quantisation, pacing,
queue reflection, sparse sampling and route changes are not covered by this
argument. In the low-U mode Alpha's growth is proportional to the current
rate share, so a small path can still recover slowly despite spare capacity.

For example, with fixed R_i=s_i, U_i<eta, G_i=1 and an approximately constant
connection total X_c, the reduced equation becomes

\[
\dot x_i=\frac{a B_i}{N_i R_i X_c}x_i,
\qquad
T_i^{grow}=\frac{N_iR_iX_c}{aB_i}.
\]

The coefficient is 1/s and Tgrow is seconds. Recovery from x0 to x1 then
takes approximately `Tgrow * log(x1/x0)`. If B=100 Mbps, N=6, R=.04 s and
X_c=200 Mbps are held fixed, Tgrow=9.6 s and a tenfold increase takes about
22.1 s. This is an illustrative conditional calculation, not an experiment-5
prediction: N, U, X and scheduler eligibility change there. It nevertheless
shows why an underloaded but very small Alpha subflow need not recover in
one or two RTTs.

**10. Numerical checks and what they establish.** The saved run used 8 s,
RK4 step 0.2 ms, a=.05, eta=.95, queue gain .03, MSS 1448, exact U and exact
per-epoch ID counting. Paths have 40 ms propagation RTT unless noted. Initial
windows are explicitly prescribed congestion-avoidance states; startup,
loss, finite buffers, receiver blocking and the multipath scheduler are excluded.
The initial srtt and queue H are also prescribed from the path propagation
RTTs, rather than reproduced from the default 10 ms H and TCP handshake.
Payload/service bytes are equated in the main table to isolate controller
effects. The overhead example above is a separate check.

The event calculation independently includes FIFO packet service, code-form
per-departure U EWMA, ACK flight and queue delay, millisecond RTT timestamps,
sender metadata/delay quantisation, RTT smoothing, integer AI/residuals,
gain-1.2 pacing, the saved-window timer rule and the instantaneous growth gate.
It assumes every ACK acknowledges one data packet, unlimited unsent demand
on every subflow, and one measured link per path. Its timer tie ordering is
Python insertion order, not a claim about all OMNeT++ event priorities. These
are **small model projections**, not full simulations of the transport stack.

| Case | Analytical ideal / converged fluid ODE, Mbps | Packet/ACK projection, mean ACKed Mbps over second half |
|---|---:|---:|
| One path, one connection | 100.000 | 96.969 |
| One link, two single-path connections | 50.000 / 50.000 | 47.262 / 49.193 |
| Two disjoint 100 Mbps paths, Alpha | 97.500 / 97.500 | 93.720 / 93.717 |
| Two disjoint paths, RTT 20/80 ms, Alpha | 97.500 / 97.500 | 95.584 / 92.242 |
| Disjoint 100/50 Mbps paths, Alpha | 98.353 / 48.324 | 96.107 / 46.963 |
| Shared link, same Alpha connection, initial 30:70 rate split | 30.000 / 70.000; aggregate 100 | 12.771 / 83.596; aggregate 96.368 |
| Two disjoint paths, uncoupled | 100.000 / 100.000 | 96.969 / 96.969 |

The ODE's final rates match the analytical values to numerical precision.
Halving the equal-path integration step changes its final rates by about
1.06e-7 bytes/s. The positive-queue overhead calculation matches its analytical
queue to within 1e-8 bytes. These checks establish internal consistency and
numerical resolution of the chosen closure, **not runtime fidelity**.

The packet projection does not match all of these values:
retaining finite packets, burst/gap U sampling, delayed ACK information,
discrete window commits and the growth gate changes the trajectory and its
averages. In the shared-link case even the split changes strongly while the
aggregate stays near capacity. No single cause is isolated by that combined
comparison. The separate regular-versus-bursty spacing check proves that
the common U≈mean-load substitution can fail independently of the rest.
An 8 s run also cannot establish every long-run distribution or global
convergence property.
For the one-path projection, the recorded cwnd over 4–8 s ranges from
464,435 to 548,155 bytes, with U from .9075 to 1.0783. These are continuing
oscillations in that model over the measured interval, rather than convergence
to a single constant window.

To reproduce from the workspace root:

```sh
python3 samples/mporb/analysis/orbcc-fluid-model/model.py
```

Use `--skip-packets` for ODE/algebra checks only. Use a separate `--output`
directory and smaller `--dt` or larger `--duration` for further numerical
checks. Model CSV traces include windows, queues and U; packet traces report
cumulative acknowledged bytes, not application goodput. Real transport
validation would compare those quantities with instrumented OMNeT++ runs
under matched codecs, traffic, scheduler, packet accounting and timing.

**11. Full-INT OrbCC differs from PINT.** With `IntQueue` and `OrbtcpFlavour`,
the queue reports per-hop cumulative payload transmit counters, timestamps,
queue lengths, counts and H. On matched successive records the sender forms

\[
z_{il,k}=\frac{\min(Q_{l,k},Q_{l,k-1})}{B_lH_l}
 +\frac{T_{l,k}^{bytes}-T_{l,k-1}^{bytes}}{B_l(t_{l,k}-t_{l,k-1})}.
\]

Both terms are dimensionless. It chooses maximum z across hops, breaking ties
by tighter B/N, then smooths that selected value at the sender:

\[
U_i^+=(1-\alpha_s)U_i+\alpha_s\max_l z_{il,k}.
\]

The usual alpha_s is fixed .03 per accepted measurement; when configured
alpha<=0, it becomes `clamp(tau/H,0,1)`. That optional time-derived branch
does **not** govern the current PINT queue EWMA. Max-after-per-link-EWMA
(PINT) and EWMA-after-max (full INT) do not commute. The event clock and
the rate estimator also differ: the counter difference is a time-interval
aggregate, unlike one packet divided by its latest inter-dequeue gap.

For regular accepted measurement intervals, the corresponding full-INT
fluid approximation is at the sender:

\[
\dot U_i=k_i^{INT}(z_i^{max}-U_i),\qquad
k_i^{INT}=-\nu_i^{INT}\log(1-\alpha_s),\qquad
z_i^{max}=\max_{l\in P_i}\left[
\frac{y_l^{payload,out}}{B_l}+\frac{Q_l}{B_lH_l}\right].
\]

The derivative and kINT have units 1/s; both terms inside the maximum are
dimensionless. This additionally replaces the two-sample minimum queue by
a slowly varying Q and the counter difference by a local service rate.
With adaptive alpha_s=tau/H<<1, this gives kINT approximately 1/H only when
the accepted measurement spacing and tau coincide. Sparse/missing records
and different hop timestamps break that simplification. The sender window,
RTT, queue and epoch equations above then couple to this sender filter,
instead of to the PINT per-link EWMA and maximum-selection operator.

Full INT estimates queue delay with arrival queue bytes/B; PINT accumulates
actual queue residence and quantises it. Full INT startup sets its AI to
zero at U>=eta and applies the U-dependent window equation with a startup
cap; PINT's startup branch is the separate capped growth rule above. The
post-startup AI/window/pacing skeleton remains closely related, but equating
the two implementations without stating these differences is not faithful.

The most useful result is therefore a hierarchy: retain the hybrid model
for packet/ACK fidelity, use the fluid balance equations for conditional
equilibria and insight, and explicitly test the sampling, delay and scheduler
closures before using their equilibria as predictions for a LEO experiment.
