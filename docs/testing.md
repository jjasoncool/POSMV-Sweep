# Testing utilities

## Level 1: files

Validator is integrated into `posmv-simulator.exe`; no separate validator
executable is built. Available CLI commands:

```sh
./build/posmv-simulator.exe validate --input source.txt
./build/posmv-simulator.exe send --replay source.txt --output loop.txt --loop --count 100 --unpaced
```

Validation returns 0 for a nonempty valid-only stream, 2 for invalid/empty
streams, and 1 for argument/I/O errors. Unlike replay, validation reports garbage
outside frames instead of silently ignoring it. Error messages currently group
field/status/range failures; detailed per-field diagnostics remain pending.
Loop CLI currently requires a finite run: default 100 frames, or explicit count.
Continuous until Stop, strict replay preflight, live monitor, OFF comparison and
Simulator GTK4 controls remain pending. The GTK4 package installation attempt
timed out; neither complete GUI is currently delivered.

From MSYS2 UCRT64 at the repository root:

```sh
./build/posmv-simulator.exe --output source.txt --sine-roll --angle 10 --cycle 4 --rate 25 --count 101 --unpaced
./build/posmv-sweep.exe --replay source.txt --output transformed.txt --off
./build/posmv-simulator.exe --replay source.txt --output replayed.txt --rate 50
ctest --test-dir build --output-on-failure
```

Generated frames use zero accelerations/heave/pitch and status G as test fixtures,
not measured POS MV behavior. `--roll` selects a fixed Roll; `--sine-roll` selects
elapsed-time negative-first sine Roll. Default generated run is 100 frames.
Replay stops at EOF, unless limited by explicit `--count`. Invalid frames are
discarded/counted. `--unpaced` is deterministic file-only output. Default output
is paced with steady_clock absolute deadlines; missed slots are skipped, not burst.

## Level 2: paired virtual COM ports

Prerequisite: a connected pair, for example COM10 <-> COM11. On explicit operator
request (UAC), the Simulator's Virtual COM controls install the pinned com0com
2.2.2.0 driver and create two owned pairs; see virtual-com-status.md.
Close other software holding these ports. Start receiver first in one terminal:

```sh
./build/posmv-sweep.exe --input-port COM11 --baud 38400 --output received.txt --seconds 10 --rate 25 --off
```

Then start transmitter in another terminal within the capture window:

```sh
./build/posmv-simulator.exe --port COM10 --baud 38400 --rate 25 --count 100 --roll 1.29
```

Expected receiver: 100 valid frames, zero invalid frames if no capture timeout
or driver errors occurred. Validate captured records through offline replay.
Repeat at 50/100 Hz with adequate baud and for sine/replay sources. Serial test
settings are explicitly 8N1 with software/hardware flow control disabled; these
are test defaults, NOT confirmed instrument settings. Baud must meet theoretical
27-byte 8N1 demand (270 * rate); no settings are silently increased.

Capture is receive-driven serial-to-file processing, not an independent-rate
serial-to-serial proxy. Capture phase uses actual monotonic elapsed time. Serial
write errors and zero-byte write timeouts fail the CLI; reads have a finite wait.
This level passed on 2026-10-01 with com0com 2.2.2.0 pairs; see
verification/2026-10-01-com0com-2.2.2.0.md. The full serial-to-serial proxy chain
and full timing metrics are not yet verified end to end.

## Level 3

Physical loopback/instrument tests are pending verified wiring, electrical
interface and actual COM settings. Do not connect to live sonar equipment based
only on virtual/offline results.