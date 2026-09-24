# MpORB split-delay telemetry and intInformed scheduler

Enable the scheduler on the meta connections:

```ini
**.tcp.*.schedulerMode = "intInformed"
**.tcp.pintSeparateQueueingDelay = true
```

Replace any earlier matching `schedulerMode` assignment: OMNeT++ uses the first
matching assignment. Split-delay feedback is enabled by default for MpORB;
the scheduler default remains `default`. Alpha, Pressure and Uncoupled can all
use `intInformed`. No congestion-control equation is changed by this scheduler.

## Where the algorithms live

`mptcp/MpTcpPacketScheduler.cc` owns the existing schedulers and shared burst
dispatch. `mporb/MpOrbIntScheduler.cc` owns the INT selector, arrival score,
write-memory admission and reinjection ranking. `MpOrbConnection::createPacketScheduler`
selects that class only for `intInformed`; ordinary MPTCP has no INT scheduler mode.
INT parameters and diagnostic signals also live in MpORB. The shared PINT
message and queue support remain in orbtcp because the queues process both formats.

| Mode | Main selection rule | Assignment limit |
| --- | --- | --- |
| `default` | Lowest queued bytes / average pacing rate | Default write-memory admission, approximately 64 KiB bursts |
| `defaultCwnd` | Same score among paths with unsent allowance | Unsent cwnd allowance and remaining write memory; one-segment starvation turn |
| `intInformed` (MpORB) | Lowest forward delay + prospective burst drain time | Same unsent allowance and write memory as defaultCwnd; one-segment starvation turn |
| `lowestRtt` | Lowest RTT among eligible paths | Existing per-segment admission |
| `directPull` | Subflows request data when able to send | Existing pull admission; no central path ranking |

Start with `MpOrbIntScheduler::selectDefaultSubflow` to read the new algorithm.
The service rate, available write-memory space and arrival score are calculated
in `evaluatePath` in the same source file. Each candidate is evaluated once;
selection and diagnostics reuse that result. Meta recovery uses the inherited
write-memory admission check.
`tests/check_int_scheduler.py` constant-evaluates the production calculation
with transport stubs; it does not run packet scheduling or the simulator.

## Telemetry

The existing `queueingDelayCode` accumulates forward queue residence on data
packets. A MpORB PINT receiver echoes that value unchanged and starts the new
`reverseQueueingDelayCode` at zero. On these ACKs, PINT queues update only the
reverse accumulator. Both fields use the existing 12-bit, 64 us encoding and
saturate individually at 262.08 ms. The format discriminator is simulation
metadata. Header sizes and bandwidth overhead are abstracted away.

OrbCC's `queueingDelay` remains the decoded sum of both directions, so RTT
queue correction still uses the complete queueing delay. Legacy OrbCC ACKs,
and MpORB with `pintSeparateQueueingDelay=false`, retain their original single
round-trip accumulator. Forward U/B/N and path-digest selection are unchanged.

The scheduler caches valid split feedback on advancing TCP ACKs. It rejects
future/out-of-order timestamps, requires both sample and reception age to be
at most two current smoothed RTTs, and falls back when either field saturates.
The first feedback on a changed forward path digest invalidates the old cache;
a subsequent fresh sample can qualify the new path. Recovery/RTO also disables
use of the cached directional estimate while active.

## Ranking and admission

The scheduler reuses modern-default burst dispatch, weighted average pacing,
transport activity checks and default write-memory admission.
For each writable subflow:

```text
rate = min(average pacing rate, current pacing rate, cwnd / smoothed RTT)
forwardDelay = queue-corrected smoothed RTT / 2 + forward queueing delay
score = forwardDelay + (unsent bytes + 65428 reference bytes) / rate
```

The reference size is identical across candidates. The score uses unsent
bytes, not all retained TCP-unacknowledged bytes: acknowledged-at-receiver
data need not travel forward again. Outstanding bytes remain charged to the
write-memory admission check. The score is a prospective ranking proxy, not an
exact arrival time or a guarantee of in-order delivery.

Only queueing is measured directionally. Halving the queue-corrected RTT
assumes symmetric non-queueing delay; it does not discover asymmetric route
propagation or remove receiver ACK-generation delay. The baseline is smoothed
and can lag route changes. Without usable INT, ranking uses smoothed RTT/2
instead, retaining the prospective score and bounded admission. No global
topology/route oracle or receiver HoL counter is read by the scheduler.

Both `intInformed` and `defaultCwnd` share the original INT unsent allowance:
`min(max(0, max(cwnd, MSS) - unsentBytes), available write memory)`, additionally
limited by the 65,428-byte burst target and rounded down to complete segments.
Cached bursts and reinjection recheck this allowance. Repeated selections cannot
bypass the unsent bound. A cwnd reduction pauses assignment until the existing
backlog drains; queued data is not discarded. Bytes in flight consume write
memory but are not subtracted from the unsent allowance. TCP enforces cwnd/rwnd
on actual transmission. The INT score is unchanged.

The burst-only rollback removed this allowance and allowed a preferred path to
absorb the meta send buffer. A tiny-window path can then lose every ranking
comparison and receive no ACK-driven recovery opportunities. Restoring admission
allows another path to be considered when the preferred path's unsent allowance
is exhausted. This is not a wall-clock recovery guarantee; the existing starvation
threshold still applies if a path remains eligible but consistently loses ranking.

An eligible path passed over for `CWND_MAX_SKIPPED_BURSTS` selections gets a one-segment turn,
as in `defaultCwnd`. This prevents persistent ranking starvation when data,
credit and scheduling opportunities exist. It does not force unavailable paths
or provide a wall-clock service guarantee. Small new-DSN probes can still cause
reordering. Timer reinjection keeps the existing idle-transport eligibility,
but ranks eligible targets using this score and caps the fragment by the same
write-memory budget. It does not add a proactive DSN rescue algorithm.

## Diagnostics and validation

Subflow vectors:

- `mpOrbForwardQueueingDelay`, `mpOrbReverseQueueingDelay`: decoded seconds.
- `intSchedulerScore`: selected-path score in seconds at burst creation.
- `intSchedulerBurstBytes`: admitted burst size.
- `intSchedulerFreshFeedback`: whether the selected score used split INT.
- `intSchedulerProbe`: whether starvation protection selected this burst.

Static policy checks:

```sh
python3 samples/mporb/tests/check_int_scheduler.py
clang++ -std=c++17 -fsyntax-only samples/orbtcp/tests/PintSplitDelayTest.cc
```

Rebuild orbtcp (including generated messages), mptcp and mporb before running.
The changed message schema is not ABI-compatible with old binaries. The
implementation needs runtime validation for delay asymmetry, background-flow
transitions, tiny windows, stale/saturated telemetry and route changes. Compare
`defaultCwnd` against `intInformed`, and `intInformed` with split feedback disabled,
using the same CCA and seeds. The last comparison isolates the directional
telemetry contribution from prospective ranking.
