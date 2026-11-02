# Windows USB CDC capture for Pico validation

This collector observes the existing RP2040 validation firmware. It never writes
to the serial port, flashes firmware, sends a break, requests a reset, or selects
1200 baud. Firmware and Kernel/port code are unchanged.

Use **Windows Python 3.10 or newer**, not WSL Python, for live COM capture.
Dependency: `pyserial==3.5`. Mock tests and file replay use only Python's standard
library and also run in WSL. Windows COM/USB behavior and actual firmware execution
must be checked on hardware; mock PASS is not hardware PASS.

## Copy tools and UF2 from WSL

Create `C:\KarqriValidation` in Windows. From the repository root in WSL:

```bash
cp tools/pico_validation/{capture.py,test_capture.py,requirements.txt,README.md} /mnt/c/KarqriValidation/
cp build/pico-rp2040/karqri_pico_validation.uf2 /mnt/c/KarqriValidation/
sha256sum build/pico-rp2040/karqri_pico_validation.uf2
```

This copies an existing UF2; it does not prove its source revision. Record HEAD,
build configuration, SDK/toolchain versions, and firmware hash alongside the log.
Use the validation UF2, not the track demo UF2. Do not attach the target USB device
to WSL/usbipd: Windows handles both the BOOTSEL drive and CDC port for this test.

## Install and check on Windows (PowerShell)

```powershell
cd C:\KarqriValidation
py -3 -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r .\requirements.txt
.\.venv\Scripts\python.exe -B -m unittest discover -s . -p test_capture.py -v
Get-FileHash .\karqri_pico_validation.uf2 -Algorithm SHA256
.\.venv\Scripts\python.exe -m serial.tools.list_ports -v
```

No virtualenv activation or PowerShell execution-policy change is needed. Compare
the UF2 hash with WSL. Close IDE serial monitors and other COM clients.

## BOOTSEL to capture

1. Connect the Pico while holding BOOTSEL. Release it after the `RPI-RP2` drive
   appears. Confirm its drive letter. A CDC port is not expected in BOOTSEL mode.
2. Start the collector **before** copying the UF2:

   ```powershell
   .\.venv\Scripts\python.exe -u .\capture.py
   ```

   Wait for `armed`. In a second PowerShell window, promptly copy the UF2 to
   the actual BOOTSEL drive (replace `E:`):

   ```powershell
   Copy-Item C:\KarqriValidation\karqri_pico_validation.uf2 E:\
   ```

3. The Pico boots and exposes CDC. The collector polls ports, opens the matching
   port at 115200, and asserts DTR. Default USB filter is VID:PID `2e8a:000a`
   (the SDK 2.3.0 RP2040 CDC defaults). If more than one matches, it waits instead
   of guessing. Only one target should be connected unless explicitly selected.
4. Observe START, the five phases, metric, and the final result. Save the JSONL
   file printed by the collector. Repeat with a fresh boot and a new log if needed.

Explicit COM selection (the COM number must match the newly appearing CDC port):

```powershell
.\.venv\Scripts\python.exe -u .\capture.py --port COM7
```

Alternatively use a known USB serial number, which follows COM renumbering:

```powershell
.\.venv\Scripts\python.exe -u .\capture.py --serial-number YOUR_PICO_USB_SERIAL
```

`--port` overrides VID/PID filtering and fixes the COM number. Auto mode can use
custom `--vid`/`--pid` hexadecimal values. `--port` and `--serial-number` cannot be
combined. List ports on a separate preliminary boot if the number/serial is
unknown, then return to BOOTSEL before starting the real capture.

The total default deadline is **30 seconds from monitor arming**, including USB
appearance, open retries, and reconnects. START/reconnect does not reset it.
`--timeout 60` can explicitly change this test policy; record the override.
Do not wait at BOOTSEL for longer than the selected deadline after arming.

## Freshness and reconnect policy

- Ports already present at arming are ignored until observed absent and then
  present again, even with `--port`. Start while disconnected or in BOOTSEL.
- Fresh appearance means a newly observed attachment, not proof of a new boot
  nonce. Firmware emits neither nonce nor commit hash. Replug/reflash deliberately
  after arming; do not replay firmware text through another serial application.
- Once connected, auto mode pins the USB serial number. Without one it pins COM;
  keep a single target attached. Explicit COM mode does not follow COM renumbering.
- Disconnect discards partial bytes and the unfinished validation state. The next
  attempt needs its own START, all phases, metric, and terminal result. Captured
  protocol/check errors cause FAIL on disconnect and are not erased by reconnect.
- If a read fails but removal was not observed, the collector waits for an observed
  disappearance before reopening. A very fast unobserved unplug/replug may produce
  TIMEOUT; it cannot authorize a PASS from an uncertain session.
- Busy/open failures are retried within the same deadline. Multiple candidates
  remain unselected. Log output explains why it is waiting.
- Logs are exclusively created, never appended or overwritten. Default filenames
  contain UTC time plus a random suffix. Each record has a run UUID and UTC and
  monotonic timestamps. Connection attempts are numbered. Raw received chunks are
  preserved as base64 `bytes` records; decoded complete lines are `line` records.
- Timing reflects host receipt, not firmware event timestamps. START/terminal must
  be newline-terminated. Input buffers are not explicitly flushed after DTR, to
  avoid discarding the first live START. Driver behavior still requires testing.

## Result rules

Expected phase order:

```text
KARQRI_PICO_VALIDATION_START
PICO_PHASE single_task
PICO_PHASE cooperative_three_task
PICO_PHASE event_delayed_r0
PICO_PHASE suspend_resume
PICO_PHASE hardware_preemption
PICO_METRIC irq=... ticks=... switches=... worker0=... worker1=... overlap=...
KARQRI_PICO_VALIDATION_PASS
```

| Result | Rule | Exit code |
| --- | --- | --- |
| PASS | Complete ordered sequence in one connection, exactly one metric, no errors | 0 |
| FAIL | Firmware FAIL, captured PICO_FAIL, malformed sequence/metric, or incomplete terminal PASS | 1 |
| TIMEOUT | Deadline without complete result and without an already detected error | 2 |
| ERROR | Missing dependency, file/USB enumeration error, or collector failure | 3 |
| ABORTED | Ctrl+C; no validation result | 130 |

PICO_FAIL is logged immediately; the collector continues to capture the terminal
FAIL, or reports FAIL on deadline/disconnect. PASS must include START and all five
phases in order. Metrics mirror current validation checks: irq >= ticks >= 30,
switches >= 20, both workers nonzero, overlap == 0. A missing terminal newline is
not accepted as completion. A PASS result ends collection; contradictory lines
already in the same received chunk are still checked, but subsequent output is
not observed. The normal firmware prints no further result after its terminal.

An outputless run is TIMEOUT, **not** proof that the firmware failed: SDK CDC may
discard output before host DTR/connection. The firmware waits only 1.5 seconds,
not for an acknowledged collector. No host-only tool can guarantee receiving
START if Windows enumeration/open takes longer. Re-run; do not relax the parser
to accept a late PASS. No output is stored on the Pico for later retrieval.

For a timeout/fault, do not reset or unplug before any intended SWD diagnosis.
This script cannot read `karqri_pico_fault_reason`; that requires a SWD probe and
the matching ELF/debugger. It does not claim that zero fault reason or a successful
build is a hardware PASS.

## Offline tests and replay

From the repository root in WSL (no pyserial required):

```bash
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tools/pico_validation -v
```

Create a plain UTF-8 `mock.txt` containing the expected sequence above, replacing
the metric line with, for example:

```text
PICO_METRIC irq=33 ticks=30 switches=24 worker0=100 worker1=200 overlap=0
```

End the last line with a newline. Then on Windows:

```powershell
.\.venv\Scripts\python.exe .\capture.py --replay .\mock.txt
```

Replay opens no COM port and prints **SIMULATED PASS/FAIL/TIMEOUT**. The JSONL
marks `hardware_test=false`. EOF without a complete result is simulated TIMEOUT;
replay does not wait 30 seconds. Supply plain firmware text, not a saved JSONL.
Unit tests exercise the parser, chunk framing, fresh-device selection, busy open,
disconnect/reconnect, deadline, DTR adapter, and exclusive log creation using fake
I/O and clocks. They do not test Windows drivers or RP2040 exceptions.

## Reference and next stage

pyserial APIs used here: [Serial/DTR/read](https://pyserial.readthedocs.io/en/latest/pyserial_api.html)
and [list_ports metadata](https://pyserial.readthedocs.io/en/latest/tools.html).
OS/driver DTR transitions and metadata availability can vary; validate on Windows.

Stage 1 records only the existing five scenarios. It does not certify early
Event wakeup, self-PendSV, alarm missed recovery, boundary IRQ races, or MSP margin.
Stage 2 can reuse the journal/transport with a separate protocol/parser for the
test firmware; keep the Stage 1 phase checks strict rather than weakening them.

## Offline cooperative Trace Viewer

`trace_viewer.py` reads the collector JSONL and writes a single standalone HTML
file. It needs Python 3.10+ and `capture.py` in the same folder, but **no pyserial,
browser package, CDN, web server, or network connection**. Copy the new viewer
and tests/fixtures from WSL to the existing Windows folder, for example:

```bash
cp tools/pico_validation/{trace_viewer.py,test_trace_viewer.py,README.md} /mnt/c/KarqriValidation/
cp -r tools/pico_validation/fixtures /mnt/c/KarqriValidation/
```

Run in Windows PowerShell, replacing `your-validation.jsonl` with a collector
JSONL from your own run. Hardware captures are not bundled; a synthetic fixture
is available in the example below.

```powershell
cd C:\KarqriValidation
.\.venv\Scripts\python.exe .\trace_viewer.py `
  .\logs\your-validation.jsonl `
  -o .\cooperative-trace.html
Start-Process .\cooperative-trace.html
```

Or use any Windows Python 3.10+ with `py -3 trace_viewer.py ...`. Output must be a
new file; existing reports and input logs are never overwritten. Exit 0 means
Trace COMPLETE, 1 means an INCOMPLETE report was still written, 2 means a file/CLI
error. **Viewer exit codes do not change or represent validation PASS/FAIL.**

The HTML displays Validation PASS/FAIL/TIMEOUT/UNKNOWN independently from Trace
COMPLETE/INCOMPLETE, phase, counts, Task mappings, marks and completeness reasons.
It has inline CSS/JavaScript, an SVG event timeline, per-event checkboxes, horizontal
zoom and a full record table. Click/keyboard-select a marker or click a table row
to inspect the record. B=bootstrap, Y=yield, R=request, H=handoff, T=return, S=stop;
TASK_MARK uses its ASCII A..I. Dashed links join request to HANDOFF_COMMIT using
the latter's value=request seq. There are no inferred CPU-occupancy bars.

Time comes solely from PICO_TRACE `us`, not the JSONL receipt timestamp. Equal
microseconds retain sequence order. Current uses Task pointers; active/pending/
subject use context pointers. HANDOFF_COMMIT observes readiness to exception-return,
not execution of the next Task's instructions. Pointers without mappings are shown
as unknown. Records with invalid time remain in the table but not the plot.

Checks include BEGIN/END phase/count, actual count, unique consecutive sequence
starting at 1, overwritten=0, seq_overflow=0, time_valid=1, nondecreasing/ranged
times, mappings, and request references (including replacement/cancellation and
destination/source consistency). Missing/malformed data does not hide parseable
records; reasons appear as INCOMPLETE. A trace can be COMPLETE with Validation
FAIL, or INCOMPLETE with Validation PASS.

Only one uninterrupted collector attempt is analyzed at a time. Multiple trace
sessions mark the report INCOMPLETE and display only the first; use `--attempt 2`
to select a connection attempt. Separate different run IDs into separate files if
the attempt numbers overlap. `bytes` records are not decoded a second time; only
collector `line` records are input, so chunks and lines are never double-counted.
The viewer reuses the existing validation-line parser without changing it. A final
collector status alone cannot supply missing START/phases/metric lines. A broken
JSONL may hide validation output and suppresses a derived PASS to UNKNOWN.

`fixtures/cooperative_38.jsonl` is **synthetic, not a hardware capture**. It has 38
records, eight handoffs, marks ADGBEHCFI, overwritten=0 and constant receipt times
to test that the hardware-relative `us` values drive the plot. To preview it:

```powershell
.\.venv\Scripts\python.exe .\trace_viewer.py .\fixtures\cooperative_38.jsonl -o .\fixture-trace.html
.\.venv\Scripts\python.exe -B -m unittest discover -s . -p "test_*.py" -v
```

The fixture report is labelled SIMULATION. Tests cover missing/duplicate records,
counts, phases, request references, timestamps, overflow, validation independence,
session isolation, HTML escaping and inline-only resources. Log strings are HTML-
escaped or embedded as escaped JSON and inserted into the DOM via textContent.
The HTML also denies network connections through its Content Security Policy.
