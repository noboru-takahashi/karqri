"""Device-free tests: python -m unittest discover -s tools/pico_validation -v."""

from argparse import Namespace
import io
import json
from pathlib import Path
from tempfile import TemporaryDirectory
from types import SimpleNamespace
import unittest
from contextlib import redirect_stdout

import capture


METRIC = "PICO_METRIC irq=33 ticks=30 switches=24 worker0=100 worker1=200 overlap=0"
GOOD = [capture.START, *("PICO_PHASE " + p for p in capture.PHASES), METRIC, capture.PASS]


def port(name="COM7", serial="pico-one"):
    return SimpleNamespace(device=name, vid=0x2E8A, pid=0x000A,
                           serial_number=serial, location="1-2")


class Clock:
    def __init__(self):
        self.value = 0.0

    def now(self):
        return self.value

    def sleep(self, seconds):
        self.value += seconds


class Connection:
    def __init__(self, chunks):
        self.chunks = list(chunks)
        self.closed = False

    def read(self, size):
        if self.chunks:
            value = self.chunks.pop(0)
            if isinstance(value, Exception):
                raise value
            return value
        return b""

    def close(self):
        self.closed = True


class ProtocolTests(unittest.TestCase):
    def evaluate(self, lines):
        v = capture.Validation()
        for line in lines:
            v.line(line)
        return v.result or v.expired()

    def test_complete_sequence(self):
        self.assertEqual(self.evaluate(GOOD).status, "PASS")

    def test_missing_start_phase_metric_or_terminal(self):
        for index in range(len(GOOD)):
            with self.subTest(index=index):
                self.assertNotEqual(self.evaluate(GOOD[:index] + GOOD[index + 1:]).status, "PASS")

    def test_phase_order_duplicates_and_reboot(self):
        self.assertEqual(self.evaluate([capture.START] + GOOD).status, "FAIL")
        swapped = GOOD.copy()
        swapped[1], swapped[2] = swapped[2], swapped[1]
        self.assertEqual(self.evaluate(swapped).status, "FAIL")
        self.assertEqual(self.evaluate(GOOD + [capture.START]).status, "FAIL")

    def test_fail_before_start_and_after_pass(self):
        for lines in (["PICO_FAIL example"] + GOOD, GOOD + ["PICO_FAIL late"],
                      [capture.FAIL + " count=1"], ["PICO_FAIL no terminal"]):
            self.assertEqual(self.evaluate(lines).status, "FAIL")

    def test_bad_metrics(self):
        for metric in ("PICO_METRIC irq=1", METRIC.replace("overlap=0", "overlap=1"),
                       METRIC.replace("ticks=30", "ticks=29"),
                       METRIC.replace("worker0=100", "worker0=0"),
                       METRIC + " irq=33", METRIC.replace("irq=33", "irq=-1")):
            self.assertEqual(self.evaluate(GOOD[:-2] + [metric, capture.PASS]).status, "FAIL")

    def test_empty_timeout(self):
        self.assertEqual(self.evaluate([]).status, "TIMEOUT")

    def test_split_crlf_and_partial_terminal(self):
        f = capture.Lines()
        self.assertEqual(list(f.feed(b"PICO_PHA")), [])
        self.assertEqual(list(f.feed(b"SE single_task\r\nnext")), ["PICO_PHASE single_task"])
        self.assertEqual(f.pending, b"next")
        with self.assertRaises(ValueError):
            list(capture.Lines().feed(b"x" * 8193))
        with self.assertRaises(UnicodeDecodeError):
            list(capture.Lines().feed(b"\xff\n"))


class MonitorTests(unittest.TestCase):
    def run_monitor(self, lists, connections, explicit=None, serial_number=None, timeout=0.5):
        clock, events, opened = Clock(), [], []
        snapshots = iter(lists)
        latest = []

        def enumerate_ports():
            nonlocal latest
            latest = next(snapshots, latest)
            clock.value += 0.01
            return latest

        streams = iter(connections)

        def open_port(device):
            opened.append(device)
            stream = next(streams)
            if isinstance(stream, Exception):
                raise stream
            return stream

        args = Namespace(port=explicit, serial_number=serial_number, vid=0x2E8A,
                         pid=0x000A, timeout=timeout)
        result = capture.monitor(args, enumerate_ports, open_port,
                                 lambda event, **fields: events.append((event, fields)),
                                 clock.now, clock.sleep)
        self.assertLess(clock.value, timeout + 0.1)
        return result, events, opened

    def test_new_device_pass_and_close(self):
        c = Connection([(line + "\r\n").encode() for line in GOOD])
        result, _, opened = self.run_monitor([[], [port()]], [c])
        self.assertEqual(result.status, "PASS")
        self.assertEqual(opened, ["COM7"])
        self.assertTrue(c.closed)

    def test_existing_port_is_never_read_even_explicit(self):
        for explicit in (None, "COM7"):
            result, _, opened = self.run_monitor([[port()]], [], explicit=explicit)
            self.assertEqual(result.status, "TIMEOUT")
            self.assertEqual(opened, [])

    def test_existing_disappears_then_reappears(self):
        c = Connection([("\n".join(GOOD) + "\n").encode()])
        result, _, _ = self.run_monitor([[port()], [], [port()]], [c], explicit="com7")
        self.assertEqual(result.status, "PASS")

    def test_partial_attempt_not_combined_across_disconnect(self):
        first = Connection([("\n".join(GOOD[:4]) + "\n").encode()])
        second = Connection([("\n".join(GOOD[4:]) + "\n").encode()])
        result, events, _ = self.run_monitor([[], [port()], [], [port()]], [first, second])
        self.assertEqual(result.status, "FAIL")
        self.assertTrue(any(e == "disconnect" for e, _ in events))

    def test_reconnect_same_serial_changed_com_full_new_attempt(self):
        first = Connection([b"KARQRI_PICO_VALIDATION_STA"])
        second = Connection([("\n".join(GOOD) + "\n").encode()])
        result, events, opened = self.run_monitor([[], [port()], [], [port("COM9")]], [first, second])
        self.assertEqual(result.status, "PASS")
        self.assertEqual(opened, ["COM7", "COM9"])
        self.assertEqual(sum(e == "connected" for e, _ in events), 2)

    def test_read_error_requires_observed_removal(self):
        c = Connection([OSError("unplug")])
        result, _, opened = self.run_monitor([[], [port()]], [c])
        self.assertEqual(result.status, "TIMEOUT")
        self.assertEqual(len(opened), 1)
        self.assertTrue(c.closed)

    def test_read_error_then_observed_reconnect(self):
        first = Connection([OSError("unplug")])
        second = Connection([("\n".join(GOOD) + "\n").encode()])
        result, _, opened = self.run_monitor([[], [port()], [], [port()]], [first, second])
        self.assertEqual(result.status, "PASS")
        self.assertEqual(len(opened), 2)

    def test_known_failure_cannot_be_erased_by_disconnect(self):
        for ending in (b"", OSError("unplug")):
            first = Connection([b"PICO_FAIL example\n", ending])
            snapshots = ([[], [port()], [], [port()]] if ending == b""
                         else [[], [port()]])
            result, _, opened = self.run_monitor(snapshots, [first])
            self.assertEqual(result.status, "FAIL")
            self.assertEqual(len(opened), 1)

    def test_explicit_port_selects_among_multiple(self):
        c = Connection([("\n".join(GOOD) + "\n").encode()])
        result, _, opened = self.run_monitor([[], [port(), port("COM8", "other")]],
                                             [c], explicit="COM7")
        self.assertEqual(result.status, "PASS")
        self.assertEqual(opened, ["COM7"])

    def test_busy_port_retried(self):
        c = Connection([("\n".join(GOOD) + "\n").encode()])
        result, events, opened = self.run_monitor([[], [port()]], [OSError("busy"), c])
        self.assertEqual(result.status, "PASS")
        self.assertEqual(len(opened), 2)
        self.assertTrue(any(e == "open_retry" for e, _ in events))

    def test_multiple_candidates_and_wrong_device(self):
        for devices in ([port(), port("COM8", "pico-two")],
                        [SimpleNamespace(device="COM5", vid=1, pid=2, serial_number=None, location=None)]):
            result, _, opened = self.run_monitor([[], devices], [])
            self.assertEqual(result.status, "TIMEOUT")
            self.assertFalse(opened)

    def test_silence_no_deadline_reset(self):
        streams = [Connection([]), Connection([])]
        result, _, _ = self.run_monitor([[], [port()], [], [port()]], streams)
        self.assertEqual(result.status, "TIMEOUT")

    def test_serial_filter(self):
        result, _, opened = self.run_monitor([[], [port()]], [], serial_number="other")
        self.assertEqual(result.status, "TIMEOUT")
        self.assertFalse(opened)


class FileAndAdapterTests(unittest.TestCase):
    def test_journal_exclusive_timestamped_and_replay_labelled(self):
        with TemporaryDirectory() as directory, redirect_stdout(io.StringIO()) as stdout:
            folder = Path(directory)
            source = folder / "mock.txt"
            source.write_text("\n".join(GOOD) + "\n", encoding="utf-8")
            log = folder / "run.jsonl"
            self.assertEqual(capture.main(["--replay", str(source), "--log", str(log)]), 0)
            self.assertIn("SIMULATED PASS", stdout.getvalue())
            records = [json.loads(line) for line in log.read_text().splitlines()]
            self.assertTrue(all("utc" in row and "run_id" in row for row in records))
            self.assertFalse(records[-1]["hardware_test"])
            self.assertEqual(records[-1]["status"], "PASS")
            with self.assertRaises(FileExistsError):
                capture.Journal(log)

    def test_serial_adapter_115200_dtr_no_write_or_reset(self):
        class FakeSerial:
            def __init__(self, **kwargs):
                self.kwargs = kwargs
                self.opened = False

            def open(self):
                self.opened = True
                self.assert_dtr_at_open = self.dtr

            def close(self):
                self.opened = False

        c = capture.serial_open(SimpleNamespace(Serial=FakeSerial), "COM7")
        self.assertTrue(c.opened and c.dtr and c.assert_dtr_at_open)
        self.assertEqual(c.kwargs["baudrate"], 115200)
        self.assertEqual(c.kwargs["timeout"], 0)
        self.assertFalse(c.rts)
        self.assertEqual(c.port, "COM7")


if __name__ == "__main__":
    unittest.main()
