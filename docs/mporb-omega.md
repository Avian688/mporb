# Omega: sender Delta with underutilisation AI

`MpOrbOmega` now inherits `MpOrbSenderDelta`. The old price controller, its
meta timer, switch price state, telemetry and parameters have been removed.

For the maximum-path-U bottleneck, take B/N in payload bytes/s. Only when
U < 0.9, after startup, at a normal committed RTT window update:

    fastRateIncrease = 0.2 * (B/N) * (0.95-U)
    fastWindowIncrease = fastRateIncrease * RTT
    additionalAI = max(0, fastWindowIncrease - AlphaAI)

RTT is the same `rtt` value OrbCC uses to calculate its additive-increase bytes.
During that commit the parent sees max(AlphaAI, fastWindowIncrease). The original
Alpha AI is immediately restored afterward, so fast ACKs never apply the extra.
There is no accumulated boost credit, probing timer or new pacing rule. Integer
byte rounding follows the existing window representation.

Startup, Alpha's coupling and local MD, its cwnd growth gate, sender Delta's
fairness correction/commit timing, loss handling and ordinary OrbCC pacing are
inherited. If the growth gate restricts the window, the requested boost need not
be fully realised. `omegaAdditionalIncrease` records the additional AI bytes
presented to the parent at a commit, not the final cwnd delta.

Use the same bottleneck queue allowlist and `deltaShuffleFraction` settings as
sender Delta. The selected highest-U hop alone supplies fairness feedback;
normal OrbCC U and B/N retain the existing PINT representation. Equal U uses
OrbCC's tighter B/N tie break. No selected participating hop means no shuffle.

Run static regression with `python3 tests/check_omega_policy.py`. It checks the
actual Omega method with a stubbed parent, including U=0.9, startup, no boost
on fast feedback, Alpha-dominant AI and preservation of parent corrections.
No runtime stability or performance is established by that check.
