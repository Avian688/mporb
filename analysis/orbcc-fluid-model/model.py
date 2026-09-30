#!/usr/bin/env python3
"""Reproducible OrbCC/PINT/Alpha model experiments; NOT an OMNeT++ simulation.

Units internally: TCP payload bytes, link-service bytes, seconds, bytes/second.
The ODE assumes continuously backlogged subflows and regular packet spacing.
The independent event projection includes packets, FIFO service, ACKs, pacing,
RTT/window/telemetry timers, integer AI and the instantaneous cwnd-limited gate.
It excludes startup, loss, receiver limits, DSN scheduling/reassembly and routes.
See ../../docs/orbcc-fluid-model.md for the derivation and exact scope.
Requires Python 3 and NumPy; writes only into this analysis directory by default.
"""
import argparse
import hashlib
import heapq
import itertools
import json
import math
from collections import deque
from dataclasses import dataclass
from pathlib import Path

import numpy as np

MSS = 1448
ETA = 0.95
AI_GAIN = 0.05
QUEUE_ALPHA = 0.03
RTT_GAIN = 0.125


@dataclass
class Case:
    name: str
    capacity: tuple  # link-service bytes/s
    path_link: tuple  # one bottleneck link per subflow in these small cases
    connection: tuple  # Alpha groups and link flow-count IDs
    propagation: tuple  # round-trip propagation seconds
    initial_fraction: tuple
    alpha: bool = True
    charged_packet_bytes: int = MSS  # Q-domain = service-domain in these tests

    @property
    def n(self):
        return len(self.path_link)

    @property
    def counts(self):
        return np.array([len({self.connection[i] for i, link in enumerate(self.path_link)
                              if link == j}) for j in range(len(self.capacity))], dtype=float)


def weights(window, srtt, case):
    rate = window / srtt
    if not case.alpha:
        return np.ones(case.n)
    return np.array([rate[i] / sum(rate[k] for k in range(case.n)
                                  if case.connection[k] == case.connection[i])
                     for i in range(case.n)])


def fluid(case, duration, dt, record_step=0.01):
    """RK4 integration of the regular-spacing, zero-feedback-delay closure.

    State: W_i, Q_l, u_l, smoothedRTT_i. RTT excludes serialization here;
    the packet projection below includes it. N is fixed at the exact active
    connection count; switch H is the quasi-static packet-moment ratio.
    """
    n, m = case.n, len(case.capacity)
    B = np.array(case.capacity)
    link = np.array(case.path_link)
    D = np.array(case.propagation)
    N = case.counts
    incidence = np.zeros((m, n))
    incidence[link, np.arange(n)] = 1
    kappa = MSS / case.charged_packet_bytes
    z = np.r_[np.array(case.initial_fraction) * B[link] * D,
              np.zeros(m), np.full(m, 0.5), D]

    def quantities(y):
        W = np.maximum(y[:n], MSS)
        Q = np.maximum(y[n:n+m], 0)
        R = D + Q[link] / B[link]
        x = W / R
        incoming = incidence @ x
        service = np.where(Q > 0, kappa * B, np.minimum(incoming, kappa * B))
        # Packet-weighted ratio of moments: integral x D^2/W / integral x D/W.
        H = (incidence @ (x * D * D / W)) / (incidence @ (x * D / W))
        return W, Q, R, x, incoming, service, H

    def rhs(y):
        W, Q, R, x, incoming, service, H = quantities(y)
        s = np.maximum(y[n+2*m:], 1e-9)
        U = np.maximum(y[n+m:n+2*m], 1e-12)
        w = weights(W, s, case)
        A = AI_GAIN * B[link] / N[link] * R * w
        decrease = W * np.maximum(0, 1 - ETA / U[link])
        dW = (A - decrease) / s
        dW = np.where((W <= MSS) & (dW < 0), 0, dW)
        dQ = (incoming - service) / kappa
        dQ = np.where((Q <= 0) & (dQ < 0), 0, dQ)
        sample = service / B + Q / (B * H)
        # Exact exponential interpolation of a fixed-gain EWMA for regular events.
        kU = -np.log1p(-QUEUE_ALPHA) * service / MSS
        dU = kU * (sample - U)
        kR = -math.log1p(-RTT_GAIN) * x / MSS
        return np.r_[dW, dQ, dU, kR * (R - s)]

    rows = []
    stride = max(1, round(record_step / dt))
    steps = round(duration / dt)
    for step in range(steps + 1):
        if step % stride == 0 or step == steps:
            W, Q, R, x, incoming, service, H = quantities(z)
            rows.append([step * dt, *x, *W, *Q, *z[n+m:n+2*m]])
        if step == steps:
            break
        k1 = rhs(z)
        k2 = rhs(z + dt / 2 * k1)
        k3 = rhs(z + dt / 2 * k2)
        k4 = rhs(z + dt * k3)
        z += dt / 6 * (k1 + 2*k2 + 2*k3 + k4)
        z[:n] = np.maximum(z[:n], MSS)
        z[n:n+m] = np.maximum(z[n:n+m], 0)
    return np.array(rows)


def encode_cwnd_roundtrip(w):
    """PintSenderTelemetry.h: 5 exponent + 11 fraction bits."""
    w = int(w)
    if w <= 2047:
        return w
    shift = max(0, w.bit_length() - 12)
    significand = (w + (1 << shift) // 2) >> shift
    if significand > 4095:
        significand >>= 1
        shift += 1
    return significand << shift


def packet_projection(case, duration, seed=7, quantized_u=False):
    """Loss-free event projection of the inspected CA update rules.

    All subflows always have unsent payload; receiver windows are unbounded.
    Uses millisecond TCP timestamps, 64 us delay codes, encoded sender RTT/cwnd,
    exact per-epoch N, optional legacy 8-bit U encoding, and gain-1.2 pacing.
    Each route contains one measured FIFO link. Other propagation is symmetric.
    """
    n, m = case.n, len(case.capacity)
    B, D = np.array(case.capacity), np.array(case.propagation)
    link = np.array(case.path_link)
    W = (np.array(case.initial_fraction) * B[link] * D).astype(int)
    V = W.copy()
    F = np.zeros(n, dtype=int)
    s, rawR, baseR = D.copy(), D.copy(), D.copy()
    residual = np.zeros(n)
    pace = np.zeros(n, dtype=bool)
    interval = MSS * s / (1.2 * W)
    commit = np.zeros(n, dtype=bool)
    react_scheduled = np.zeros(n, dtype=bool)
    queues = [deque() for _ in range(m)]
    busy = [False] * m
    U = np.zeros(m)
    last = np.full(m, np.nan)
    H = np.array([np.mean(D[link == j]) for j in range(m)])
    N = np.ones(m, dtype=int)
    ids = [set() for _ in range(m)]
    moments = np.zeros((m, 2))
    delivered = np.zeros(n, dtype=int)
    sample_sum = np.zeros(m)
    samples = np.zeros(m, dtype=int)
    events, counter = [], itertools.count()
    rng = np.random.default_rng(seed)
    rows = []

    def event(t, kind, index, data=None):
        heapq.heappush(events, (t, next(counter), kind, index, data))

    def send(t, i):
        if pace[i] or F[i] + MSS > W[i]:
            return
        F[i] += MSS
        tag_base = math.floor(baseR[i] * 1e6 + 0.5) * 1e-6
        event(t + D[i]/2, 'arrival', link[i],
              (i, t, tag_base, encode_cwnd_roundtrip(W[i])))
        pace[i] = True
        event(t + interval[i], 'pace', i)

    def start_service(t, j):
        if busy[j] or not queues[j]:
            return
        busy[j] = True
        i, sent, tagged_base, tagged_w, arrived = queues[j].popleft()
        q = len(queues[j]) * case.charged_packet_bytes
        qterm = q / (B[j] * H[j])
        if np.isnan(last[j]):
            U[j] = qterm
        else:
            tau = max(t - last[j], MSS / B[j])
            sample = qterm + MSS / (B[j] * tau)
            U[j] = (1 - QUEUE_ALPHA) * U[j] + QUEUE_ALPHA * sample
            if t >= duration / 2:
                sample_sum[j] += sample
                samples[j] += 1
        last[j] = t
        feedback = U[j]
        if quantized_u:
            minimum = 1/512
            clamped = max(1, math.ceil(feedback * 512)) / 512
            power = math.log(clamped / minimum, 1.05)
            lower, upper = min(255, math.floor(power)), min(255, math.ceil(power))
            if lower == upper:
                feedback = minimum * 1.05**lower
            else:
                lo, hi = minimum * 1.05**lower, minimum * 1.05**upper
                feedback = hi if rng.random() < (clamped-lo)/(hi-lo) else lo
        qdelay = min(4095, math.floor((t-arrived)/64e-6 + 0.5)) * 64e-6
        service_time = case.charged_packet_bytes / B[j]
        event(t + service_time, 'finish', j)
        event(t + service_time + D[i]/2, 'ack', i,
              (sent, feedback, int(N[j]), qdelay))

    for j in range(m):
        event(H[j], 'measurement', j)
    for i in range(n):
        send(0, i)
    event(0, 'record', 0)
    while events:
        t, _, kind, i, data = heapq.heappop(events)
        if t > duration:
            break
        if kind == 'arrival':
            f, sent, tagged_base, tagged_w = data
            ids[i].add(case.connection[f])
            moments[i, 0] += tagged_base * MSS / tagged_w
            moments[i, 1] += tagged_base**2 * MSS / tagged_w
            queues[i].append((*data, t))
            start_service(t, i)
        elif kind == 'finish':
            busy[i] = False
            start_service(t, i)
        elif kind == 'pace':
            pace[i] = False
            send(t, i)
        elif kind == 'measurement':
            if ids[i]:
                N[i] = max(1, len(ids[i]))
                if moments[i, 0] > 0 and moments[i, 1] > 0:
                    H[i] = moments[i, 1] / moments[i, 0]
                ids[i].clear()
                moments[i] = 0
            # Continuous cases keep this active; idle shutdown is not exercised.
            event(t + H[i], 'measurement', i)
        elif kind == 'react':
            commit[i] = True
            event(t + s[i], 'react', i)
        elif kind == 'ack':
            sent, u, count, qdelay = data
            F[i] -= MSS
            delivered[i] += MSS
            rawR[i] = (math.floor(t*1000) - math.floor(sent*1000)) / 1000
            estimate = rawR[i] - qdelay
            s[i] += RTT_GAIN * (rawR[i] - s[i])
            baseR[i] += RTT_GAIN * (estimate - baseR[i])
            if estimate <= 0 or baseR[i] <= 0:
                baseR[i] = s[i]
            if u > 0:
                j = link[i]
                a = int(AI_GAIN * B[j] / count * rawR[i])
                if case.alpha and a > 0:
                    exact = a * weights(W, s, case)[i] + residual[i]
                    a = int(exact)
                    residual[i] = exact - a
                target = int(V[i] * min(1, ETA/u) + a)
                sendable = max(W[i] // MSS, 1) * MSS
                limited = F[i] + MSS >= sendable
                if not limited:
                    target = min(target, W[i])
                W[i] = max(MSS, target)
                if commit[i]:
                    V[i] = W[i]
                    commit[i] = False
            interval[i] = MSS * s[i] / (1.2 * max(W[i], F[i]))
            send(t, i)
            if not react_scheduled[i]:
                react_scheduled[i] = True
                event(t + s[i], 'react', i)
        elif kind == 'record':
            rows.append([t, *delivered, *W,
                         *(len(queue)*case.charged_packet_bytes for queue in queues), *U])
            event(t + 0.01, 'record', 0)
    return np.array(rows), (sample_sum / np.maximum(samples, 1)).tolist()


def analytical(case):
    """Ideal, exact-U, constant RTT, no-header equilibrium for the listed cases."""
    B = np.array(case.capacity)
    if len(B) == 1:
        # Shared bottleneck: Alpha counts connections, not its subflows.
        total = B[0] * (ETA + AI_GAIN)
        if len(set(case.connection)) == 1:
            # A continuum: same-RTT initial rates set one representative split.
            shares = np.array(case.initial_fraction) / sum(case.initial_fraction)
            return total * shares
        return np.full(case.n, total / case.n)
    if not case.alpha:
        return B * (ETA + AI_GAIN)
    # Disjoint links, one connection, N=1: sum eta B/(X-aB) = 1.
    low = AI_GAIN * max(B) * (1 + 1e-10)
    high = 2 * sum(B)
    for _ in range(100):
        mid = (low + high) / 2
        if sum(ETA * B / (mid - AI_GAIN*B)) > 1:
            low = mid
        else:
            high = mid
    total = (low + high)/2
    return ETA * B * total / (total - AI_GAIN*B)


def sanity_checks():
    # Saved-window ACK updates cannot be treated as additive growth on every ACK.
    V, A = 10000, 100
    assert [int(V + A) for _ in range(20)] == [10100]*20
    assert int(V * ETA / 1.0 + A) == 9600
    # Fixed-gain EWMA: n events, not an RTT, determine the settling time.
    U = 0
    for _ in range(100):
        U = (1-QUEUE_ALPHA)*U + QUEUE_ALPHA
    assert abs(U - (1-(1-QUEUE_ALPHA)**100)) < 1e-12
    # Equal time-average rate does not imply equal event-sampled utilization.
    regular = [2.0] * 100
    bursty = [1.0]*99 + [101.0]
    assert np.mean(regular) == np.mean(bursty)
    assert abs(np.mean([1/x for x in regular]) - 0.5) < 1e-12
    assert np.mean([1/x for x in bursty]) > 0.99
    return {'regular_spacing_U': 0.5,
            'bursty_spacing_U_mean': float(np.mean([1/x for x in bursty])),
            'both_mean_payload_load': 0.5}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--duration', type=float, default=8)
    parser.add_argument('--dt', type=float, default=0.0002)
    parser.add_argument('--output', type=Path, default=Path(__file__).parent / 'results')
    parser.add_argument('--skip-packets', action='store_true')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    B = 100e6/8
    cases = [
        Case('one_path', (B,), (0,), (0,), (.04,), (.5,)),
        Case('one_link_two_connections', (B,), (0,0), (0,1), (.04,.04), (.25,.25)),
        Case('two_disjoint_equal', (B,B), (0,1), (0,0), (.04,.04), (.5,.5)),
        Case('two_disjoint_unequal_rtt', (B,B), (0,1), (0,0), (.02,.08), (.5,.5)),
        Case('two_disjoint_unequal_capacity', (B,B/2), (0,1), (0,0), (.04,.04), (.5,.5)),
        Case('two_shared_same_connection', (B,), (0,0), (0,0), (.04,.04), (.15,.35)),
        Case('two_disjoint_uncoupled', (B,B), (0,1), (0,0), (.04,.04), (.5,.5), alpha=False),
    ]
    summary = []
    equal_coarse_final = None
    for case in cases:
        ode = fluid(case, args.duration, args.dt)
        np.savetxt(args.output / f'{case.name}_fluid.csv', ode, delimiter=',',
                   header=','.join(['time_s']+[f'rate_{i}_Bps' for i in range(case.n)]+
                                   [f'cwnd_{i}_B' for i in range(case.n)]+
                                   [f'queue_{j}_B' for j in range(len(case.capacity))]+
                                   [f'U_{j}' for j in range(len(case.capacity))]), comments='')
        expected = analytical(case)
        actual = ode[-1, 1:1+case.n]
        if case.name == 'two_disjoint_equal':
            equal_coarse_final = actual.copy()
        err = float(np.max(np.abs(actual - expected) / expected))
        record = {'case': case.name, 'analytical_Mbps': (expected*8e-6).tolist(),
                  'fluid_Mbps': (actual*8e-6).tolist(), 'relative_error': err}
        assert err < 0.002, record
        if not args.skip_packets:
            packets, sample_mean = packet_projection(case, args.duration)
            header = (['time_s']+[f'acked_{i}_B' for i in range(case.n)]+
                     [f'cwnd_{i}_B' for i in range(case.n)]+
                     [f'queue_{j}_B' for j in range(len(case.capacity))]+
                     [f'U_{j}' for j in range(len(case.capacity))])
            np.savetxt(args.output / f'{case.name}_packets.csv', packets,
                       delimiter=',', header=','.join(header), comments='')
            tail = packets[packets[:,0] >= args.duration/2]
            rate = (tail[-1,1:1+case.n] - tail[0,1:1+case.n]) / (tail[-1,0] - tail[0,0])
            record['packet_projection_Mbps'] = (rate*8e-6).tolist()
            record['packet_sample_mean'] = sample_mean
            record['packet_queue_mean_B'] = np.mean(tail[:,1+2*case.n:1+2*case.n+len(case.capacity)], axis=0).tolist()
        summary.append(record)
        print(json.dumps(record), flush=True)
    checks = sanity_checks()
    # A nonzero equilibrium queue when service capacity includes packet overhead.
    kappa = 1448/1500
    Ustar = ETA / (1-AI_GAIN/kappa)
    qstar = B * .04 * (Ustar-kappa)
    overhead = Case('one_path_1500_charged_bytes', (B,), (0,), (0,), (.04,), (.5,), charged_packet_bytes=1500)
    ov = fluid(overhead, args.duration, args.dt)
    checks['overhead_equilibrium'] = {'analytical_Mbps': kappa*100, 'fluid_Mbps': ov[-1,1]*8e-6,
                                     'analytical_queue_B': qstar, 'fluid_queue_B': ov[-1,3], 'U': Ustar}
    assert abs(ov[-1,1] / B - kappa) < 1e-5
    assert abs(ov[-1,3] / qstar - 1) < 0.001
    # Resolution convergence is not validation against OMNeT++.
    fine = fluid(cases[2], args.duration, args.dt/2)
    checks['dt_halving_max_rate_difference_Bps'] = float(np.max(np.abs(fine[-1,1:3] - equal_coarse_final)))
    snapshot = {}
    workspace = Path(__file__).resolve().parents[4]
    for relative in ('samples/orbtcp/src/queueing/queue/PintQueue.cc',
                     'samples/orbtcp/src/transportlayer/orbtcp/flavours/OrbtcpPintFlavour.cc',
                     'samples/orbtcp/src/transportlayer/orbtcp/flavours/OrbtcpFlavour.cc',
                     'samples/mporb/src/transportlayer/tcp/flavours/MpOrbSemiCoupledAlpha.cc'):
        snapshot[relative] = hashlib.sha256((workspace / relative).read_bytes()).hexdigest()
    (args.output / 'summary.json').write_text(json.dumps({'cases':summary, 'checks':checks,
        'duration_s':args.duration, 'dt_s':args.dt, 'source_sha256':snapshot}, indent=2)+'\n')
    print(json.dumps(checks), flush=True)


if __name__ == '__main__':
    main()
