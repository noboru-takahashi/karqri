"""Windows USB CDC collector. No firmware writes, resets, or serial output."""

import argparse
import base64
from dataclasses import dataclass
from datetime import datetime, timezone
import json
import math
from pathlib import Path
import sys
import time
import uuid


START = "KARQRI_PICO_VALIDATION_START"
PASS = "KARQRI_PICO_VALIDATION_PASS"
FAIL = "KARQRI_PICO_VALIDATION_FAIL"
PHASES = (
    "single_task", "cooperative_three_task", "event_delayed_r0",
    "suspend_resume", "hardware_preemption",
)
EXIT_CODES = {"PASS": 0, "FAIL": 1, "TIMEOUT": 2}


@dataclass(frozen=True)
class Result:
    status: str
    reason: str


class Validation:
    """One uninterrupted connection/boot only; never combines attempts."""

    def __init__(self):
        self.started = False
        self.phases = []
        self.metric = False
        self.errors = []
        self.result = None

    def line(self, line):
        if self.result is not None:
            if line == START or line.startswith("PICO_FAIL ") or line.startswith(FAIL):
                self.result = Result("FAIL", "conflicting output after terminal result: " + line)
            return
        if line == START:
            if self.started:
                self.errors.append("duplicate START / reboot without disconnect")
            self.started = True
        elif line.startswith("PICO_FAIL ") or line.startswith(FAIL):
            self.errors.append(line)
            if line.startswith(FAIL):
                self.result = Result("FAIL", "; ".join(self.errors))
        elif line.startswith("PICO_PHASE "):
            phase = line[len("PICO_PHASE "):]
            if not self.started or len(self.phases) >= len(PHASES) or phase != PHASES[len(self.phases)]:
                self.errors.append("missing START or unexpected phase: " + phase)
            self.phases.append(phase)
        elif line.startswith("PICO_METRIC "):
            if not self.started or tuple(self.phases) != PHASES or self.metric:
                self.errors.append("missing phases or duplicate/out-of-order metric")
            fields = line.split()[1:]
            try:
                values = dict(field.split("=", 1) for field in fields)
                required = {"irq", "ticks", "switches", "worker0", "worker1", "overlap"}
                if set(values) != required or len(fields) != len(required):
                    raise ValueError("metric fields")
                if not all(value.isascii() and value.isdecimal() for value in values.values()):
                    raise ValueError("metric numbers")
                v = {key: int(value) for key, value in values.items()}
                if not (v["irq"] >= v["ticks"] >= 30 and v["switches"] >= 20
                        and v["worker0"] > 0 and v["worker1"] > 0 and v["overlap"] == 0):
                    raise ValueError("metric checks")
            except ValueError:
                self.errors.append("invalid PICO_METRIC")
            self.metric = True
        elif line == PASS:
            if not self.started or tuple(self.phases) != PHASES or not self.metric:
                self.errors.append("incomplete capture: START/phases/metric missing")
            self.result = (Result("FAIL", "; ".join(self.errors)) if self.errors
                           else Result("PASS", "complete validation sequence"))

    def expired(self):
        return Result("FAIL", "; ".join(self.errors)) if self.errors else Result(
            "TIMEOUT", "deadline reached without a complete terminal result; "
            f"START={self.started}, phases={self.phases}, metric={self.metric}")


class Lines:
    def __init__(self):
        self.pending = b""

    def feed(self, data):
        self.pending += data
        while b"\n" in self.pending:
            raw, self.pending = self.pending.split(b"\n", 1)
            if len(raw) > 8192:
                raise ValueError("oversized serial line")
            yield raw.rstrip(b"\r").decode("utf-8", errors="strict")
        if len(self.pending) > 8192:
            raise ValueError("oversized unterminated serial line")


class Journal:
    def __init__(self, path):
        path.parent.mkdir(parents=True, exist_ok=True)
        # Exclusive creation: never append old evidence or overwrite a previous run.
        self.file = path.open("x", encoding="utf-8", buffering=1)
        self.origin = time.monotonic()
        self.run_id = str(uuid.uuid4())

    def __call__(self, event, **fields):
        record = dict(utc=datetime.now(timezone.utc).isoformat(),
                      elapsed_s=round(time.monotonic() - self.origin, 6),
                      run_id=self.run_id, event=event, **fields)
        self.file.write(json.dumps(record, ensure_ascii=True) + "\n")
        if event == "line":
            print(record["utc"], fields["text"], flush=True)
        elif event != "bytes":
            print(record["utc"], event, fields, flush=True)

    def close(self):
        self.file.close()


def matches(port, args):
    if args.port:
        return port.device.upper() == args.port.upper()
    return (port.vid == args.vid and port.pid == args.pid
            and (not args.serial_number or port.serial_number == args.serial_number))


def identity(port):
    return (port.device.upper(), port.vid, port.pid, port.serial_number, port.location)


def monitor(args, enumerate_ports, open_port, emit, clock=time.monotonic, sleep=time.sleep):
    """Injectable I/O and clock permit device-free transport tests."""
    deadline = clock() + args.timeout
    # Existing devices are quarantined until observed absent. This avoids reading
    # buffered output from a run that predates this collector, even with --port.
    baseline = {identity(p) for p in enumerate_ports() if matches(p, args)}
    emit("armed", ignored_existing=[p[0] for p in baseline], timeout_s=args.timeout)
    connection = None
    selected = None
    pinned = None
    validation = Validation()
    lines = Lines()
    attempt = 0
    notice = None
    try:
        while clock() < deadline:
            ports = [p for p in enumerate_ports() if matches(p, args)]
            present = {identity(p) for p in ports}
            baseline.intersection_update(present)
            if connection is not None and identity(selected) not in present:
                emit("disconnect", attempt=attempt, port=selected.device,
                     partial_bytes_b64=base64.b64encode(lines.pending).decode(),
                     errors=validation.errors)
                if validation.errors:
                    return validation.expired()
                connection.close()
                connection = None
                validation, lines = Validation(), Lines()
            if connection is None:
                candidates = [p for p in ports if identity(p) not in baseline]
                if pinned:
                    candidates = [p for p in candidates if
                                  (p.serial_number == pinned[1] if pinned[1]
                                   else p.device.upper() == pinned[0])]
                if len(candidates) != 1:
                    state = tuple(sorted(p.device for p in candidates))
                    if state != notice:
                        emit("waiting", candidates=list(state),
                             reason="multiple candidates; use --port/--serial-number" if state
                             else "waiting for a fresh CDC appearance")
                        notice = state
                    sleep(min(0.1, max(0, deadline - clock())))
                    continue
                selected = candidates[0]
                try:
                    connection = open_port(selected.device)
                except OSError as exc:
                    if str(exc) != notice:
                        emit("open_retry", port=selected.device, error=str(exc))
                        notice = str(exc)
                    sleep(min(0.1, max(0, deadline - clock())))
                    continue
                pinned = (selected.device.upper(), selected.serial_number)
                attempt += 1
                validation, lines = Validation(), Lines()
                emit("connected", attempt=attempt, port=selected.device,
                     serial_number=selected.serial_number, dtr=True, baudrate=115200)
            try:
                data = connection.read(4096)
            except OSError as exc:
                emit("disconnect", attempt=attempt, port=selected.device, error=str(exc),
                     partial_bytes_b64=base64.b64encode(lines.pending).decode(),
                     errors=validation.errors)
                if validation.errors:
                    return validation.expired()
                connection.close()
                connection = None
                validation, lines = Validation(), Lines()
                # A read error without observed removal is not fresh evidence.
                baseline.add(identity(selected))
                continue
            if data:
                emit("bytes", attempt=attempt, data_b64=base64.b64encode(data).decode())
                try:
                    for line in lines.feed(data):
                        emit("line", attempt=attempt, text=line)
                        validation.line(line)
                except (ValueError, UnicodeDecodeError) as exc:
                    return Result("FAIL", "invalid serial stream: " + str(exc))
                if validation.result:
                    return validation.result
            else:
                sleep(min(0.02, max(0, deadline - clock())))
        emit("partial", attempt=attempt,
             data_b64=base64.b64encode(lines.pending).decode())
        return validation.expired()
    finally:
        if connection is not None:
            connection.close()


def serial_open(serial_module, device):
    port = serial_module.Serial(port=None, baudrate=115200, timeout=0,
                                write_timeout=0, rtscts=False, dsrdtr=False)
    try:
        port.port = device
        port.dtr = True
        port.rts = False
        port.open()
        port.dtr = True
        # No write, input reset, break, baud change or firmware reset.
        return port
    except BaseException:
        port.close()
        raise


def replay(path, emit):
    validation, lines = Validation(), Lines()
    emit("simulation", input=str(path), hardware_test=False)
    with path.open("rb") as source:
        while data := source.read(4096):
            emit("bytes", attempt=1, data_b64=base64.b64encode(data).decode())
            try:
                for line in lines.feed(data):
                    emit("line", attempt=1, text=line)
                    validation.line(line)
            except (ValueError, UnicodeDecodeError) as exc:
                return Result("FAIL", "invalid simulated stream: " + str(exc))
            if validation.result:
                return validation.result
    return validation.expired()


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="e.g. COM7; still requires fresh appearance")
    parser.add_argument("--serial-number", help="USB serial number for auto selection")
    parser.add_argument("--vid", type=lambda s: int(s, 16), default=0x2E8A)
    parser.add_argument("--pid", type=lambda s: int(s, 16), default=0x000A)
    parser.add_argument("--timeout", type=float, default=30.0,
                        help="total live deadline, including waiting/reconnects (default 30 s)")
    parser.add_argument("--log", type=Path, help="new JSONL path (must not exist)")
    parser.add_argument("--replay", type=Path, help="simulate from plain firmware text; no USB access")
    args = parser.parse_args(argv)
    if not math.isfinite(args.timeout) or args.timeout <= 0:
        parser.error("--timeout must be positive and finite")
    if args.port and args.serial_number:
        parser.error("use --port or --serial-number, not both")
    if not args.replay and sys.platform != "win32":
        parser.error("live capture must run in Windows Python, not WSL; --replay works anywhere")
    path = args.log or Path("logs") / (
        "pico-validation-" + datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S")
        + "-" + uuid.uuid4().hex[:8] + ".jsonl")
    journal = None
    try:
        journal = Journal(path)
        journal("run", source="simulation" if args.replay else "Windows CDC",
                python=sys.version, port=args.port, timeout_s=args.timeout)
        if args.replay:
            result = replay(args.replay, journal)
        else:
            import serial
            from serial.tools import list_ports
            journal("dependency", pyserial=serial.VERSION)
            result = monitor(args, list_ports.comports,
                             lambda device: serial_open(serial, device), journal)
        journal("result", status=result.status, reason=result.reason,
                hardware_test=not bool(args.replay))
        print(("SIMULATED " if args.replay else "") + result.status + ": " + result.reason)
        print("Log:", path.resolve())
        return EXIT_CODES[result.status]
    except KeyboardInterrupt:
        if journal:
            journal("aborted", reason="operator interrupted; not a validation result")
        print("ABORTED", file=sys.stderr)
        return 130
    except (OSError, ImportError) as exc:
        if journal:
            journal("error", reason=str(exc))
        print("ERROR:", exc, file=sys.stderr)
        return 3
    finally:
        if journal:
            journal.close()


if __name__ == "__main__":
    sys.exit(main())
