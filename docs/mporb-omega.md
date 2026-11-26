# Omega: Delta with underutilisation AI

`MpOrbOmega` inherits Delta's connection-rate shuffle and adds an
underutilisation boost to Alpha's additive increase.

At a committed RTT window update, the boost requires finite `0 <= U < 0.75`,
a positive bottleneck sharing count `N`, a positive `RTT` and inactive PINT
startup flags. `B/N` is the maximum-path-U bottleneck's fair share in payload
bytes/s. Omega computes the following values.

    gain = min(0.2 * (0.95 - U), additiveIncreasePercent)
    fastWindowIncrease = gain * (B/N) * RTT
    appliedAI = max(AlphaAI, fastWindowIncrease)

The strict `0.75` gate leaves a `0.20` margin below the target `0.95`. The
`additiveIncreasePercent` cap limits the fast candidate to the normal OrbCC
additive increase for the same fair share and RTT. The default cap is `0.05`.

RTT is the same `rtt` value OrbCC uses to calculate its additive-increase bytes.
During that commit the parent sees max(AlphaAI, fastWindowIncrease). The original
Alpha AI is immediately restored afterward, so fast ACKs never apply the extra.
There is no accumulated boost credit, probing timer or new pacing rule. Integer
byte rounding follows the existing window representation.

The coupled base disables the PINT initial phase. Transport startup, Alpha's
coupling, local MD and cwnd growth gate, Delta's shuffle, loss handling and
ordinary OrbCC pacing are inherited. The growth gate can restrict the resulting
window. `omegaAdditionalIncrease` records the extra AI bytes presented to the
parent at a commit.

Omega uses the same [shuffle queues and budgets as Delta](mporb-delta.md#queue-placement).
The `0.75` gate applies to the AI boost. An enabled queue can grant shuffle
feedback whenever its hop becomes the PINT winner and prior-round measurements
and budget are available. Equal `U` uses OrbCC's tighter `B/N` tie break.

Run static regression with `python3 tests/check_omega_policy.py`. It checks the
actual Omega method with a stubbed parent. It covers the `U = 0.75` boundary,
the gain cap, startup guards, fast ACKs, Alpha-dominant AI and preservation of
parent corrections. Runtime stability and performance require simulation.
