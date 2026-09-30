#!/usr/bin/env python3
"""Exercise MpOrbU's extracted numerical expressions without building/running OMNeT++.

The expressions are evaluated as Python arithmetic. This checks the policy's
numerical properties, not C++ execution, ACK handling or transport convergence.
Separately syntax-check MpOrbU.cc against the real project headers.
"""
import math
from pathlib import Path
import re
import unittest


SOURCE = (Path(__file__).resolve().parents[1] /
          'src/transportlayer/tcp/flavours/MpOrbU.cc').read_text()


def expression(pattern):
    match = re.search(pattern, SOURCE)
    if not match:
        raise AssertionError(f'Production expression not found: {pattern}')
    return compile(match.group(1).replace('std::', 'math.'), '<MpOrbU expression>', 'eval')


SCORE = expression(r'totalScore \+= (.*);')
# SimTime is represented by seconds in these numerical tests.
gain_text = re.search(r'const double gain = (.*);', SOURCE).group(1)
GAIN = compile(gain_text.replace('std::', 'math.').replace('state->srtt.dbl()', 'rtt'),
               '<MpOrbU smoothing>', 'eval')
EXACT_AI = expression(r'const double exactAi = (.*);')


def weights(loads, beta):
    minimumLoad = min(loads)
    scores = [eval(SCORE, {'math': math}, dict(beta=beta, load=load, minimumLoad=minimumLoad))
              for load in loads]
    return [score / sum(scores) for score in scores]


class PolicyChecks(unittest.TestCase):
    def test_equal_and_single_paths(self):
        for count in (1, 2, 4, 8):
            for load in (0, 1, 1e6):
                self.assertEqual(weights([load] * count, 20), [1 / count] * count)

    def test_preference_and_example(self):
        low, high = weights([0.94 / 0.95, 0.98 / 0.95], 20)
        self.assertAlmostEqual(low, 0.698904, places=5)
        self.assertGreater(low, high)
        self.assertAlmostEqual(low + high, 1)
        self.assertEqual(weights([0.1, 1, 10], 0), [1 / 3] * 3)

    def test_no_dependence_on_current_rate_or_path_order(self):
        # A tiny-window path with better U must receive the larger AI fraction.
        loads = [0.8, 1, 1.05, 0.97]
        expected = weights(loads, 20)
        self.assertGreater(expected[0], expected[1])
        self.assertEqual(weights(list(reversed(loads)), 20), list(reversed(expected)))
        method = SOURCE[SOURCE.index('void MpOrbU::adjustAdditiveIncrease()'):]
        for rate_field in ('snd_cwnd', 'prevWnd', 'getDeliveryRate', 'connectionRate'):
            self.assertNotIn(rate_field, method)

    def test_extreme_loads_and_sensitivity(self):
        for beta in (0, 1, 20, 1e300):
            for loads in ([1e6, 1e6 + 1], [0, 1e300], [1, 1, 1]):
                result = weights(loads, beta)
                self.assertTrue(all(math.isfinite(w) and 0 <= w <= 1 for w in result))
                self.assertAlmostEqual(sum(result), 1)

    def test_smoothing_uses_elapsed_time_not_ack_count(self):
        def after_one_rtt(samples):
            value = 0.5
            for _ in range(samples):
                gain = eval(GAIN, {'math': math}, dict(elapsed=0.04 / samples,
                                                     smoothingRtts=1, rtt=0.04))
                value += gain * (1 - value)
            return value
        self.assertAlmostEqual(after_one_rtt(1), after_one_rtt(1000))
        self.assertAlmostEqual(after_one_rtt(1), 1 - 0.5 / math.e)

    def test_fractional_ai_stays_bounded(self):
        for weight in (0, 0.001, 0.25, 0.7, 1):
            residual = 0
            total = 0
            for _ in range(1000):
                exact = eval(EXACT_AI, {}, dict(uncoupledAi=1, weight=weight,
                                              additiveIncreaseResidual=residual))
                allocated = int(exact)
                residual = exact - allocated
                self.assertTrue(0 <= allocated <= 1)
                self.assertTrue(0 <= residual < 1)
                total += allocated
            self.assertLessEqual(abs(total - 1000 * weight), 1)
        self.assertIn('if (updateWindow)\n        additiveIncreaseResidual =', SOURCE)


if __name__ == '__main__':
    unittest.main()
