import sys
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import lamp_test_cycle as cycle


class FakePort:
    def __init__(self, confirms=True):
        self.now = 0
        self.commands = []
        self.returns = {}
        self.pending = []
        self.confirms = confirms

    @staticmethod
    def sample(device, active):
        return f'I RX short=0x1234 node={device} AC={1-active} sensor_valid=1 self_test={active} uptime=1_s'.encode()

    def write(self, data):
        device = data.decode().split()[-1]
        self.commands.append((self.now, device))
        if self.confirms:
            end = self.now + 30
            self.returns[device] = end
            self.pending = [(self.now + 1, self.sample(device, 1)),
                            (end, self.sample(device, 0)),
                            (end, f'I TEST short=0x1234 node={device} id=0 result=PASS duration=30_s samples=6'.encode())]

    def readline(self, _):
        if self.pending:
            when, line = self.pending.pop(0)
            self.now = max(when, self.now)
            return line
        self.now += 1
        return self.sample(cycle.LAMPS[self.now % 2], 0)


class CycleTests(unittest.TestCase):
    def run_with_port(self, port, cycles):
        with patch.object(cycle, 'synchronize', return_value=b'@CSV STATUS recording=1'), \
             patch.object(cycle, 'wait_repeat_ready', return_value='@REPEAT STATUS node=00000000 phase=off remaining_s=0'), \
             patch.object(cycle.time, 'monotonic', side_effect=lambda: port.now), patch('builtins.print'):
            cycle.run(port, cycles)

    def test_alternates_without_overlap_and_keeps_each_rest(self):
        port = FakePort()
        self.run_with_port(port, 2)
        self.assertEqual([device for _, device in port.commands], list(cycle.LAMPS) * 2)
        times = [when for when, _ in port.commands]
        self.assertTrue(all(b - a >= 30 for a, b in zip(times, times[1:])))
        self.assertGreaterEqual(times[2] - times[0], 330)
        self.assertGreaterEqual(times[3] - times[1], 330)
        self.assertGreaterEqual(port.now - max(port.returns.values()), 300)

    def test_missing_confirmation_never_resends_or_tests_other_lamp(self):
        port = FakePort(confirms=False)
        with self.assertRaisesRegex(cycle.TransferError, 'no confirmó el inicio'):
            self.run_with_port(port, 2)
        self.assertEqual([device for _, device in port.commands], [cycle.LAMPS[0]])
