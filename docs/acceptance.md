# Confirmed delivery contract

This checklist records confirmed requirements, not claims of implementation.
Unchecked items require implementation and/or recorded verification. Building
successfully and passing the current CTest entries is not full acceptance.

## Application boundary

- POSMV-Sweep is the production serial proxy. Its GUI connects real existing
  ports, virtual or physical, through the same engine. No GUI simulation mode.
- POSMV-Simulator is the separate GTK4 test console and CLI. It manages virtual
  pairs, transmits fixtures/replay, and receives and validates proxy output.
- No third validator executable. Protocol and validation logic remain reusable.

## 1. Virtual COM environment — first implementation priority

- [ ] Select and verify a free-first driver distribution and management interface.
- [ ] Record source/version, license and redistribution requirements; a submodule
  alone is not a driver installer or redistribution compliance evidence.
- [ ] Detect installation, offer explicit installation/uninstallation flow.
- [ ] Never silently disable Windows security settings.
- [ ] Create two pairs without overwriting occupied COM assignments.
- [ ] Persist ownership; remove only pairs created by this application.
- [ ] Perform pair connectivity self-test and report actual success/failure.
- [ ] Automatically assign Simulator TX/RX endpoints.
- [ ] Show prominently the two endpoints Sweep must select, plus baud.
- [ ] Keep internal endpoints in advanced controls; never imply typed names exist.

Topology: Simulator TX <-> Sweep Input -> proxy -> Sweep Output <-> Simulator RX.
Four endpoints, two pairs. The applications do not open the same endpoint.

## 2. Simulator operation

- [ ] TX/RX side-by-side; virtual environment management on a separate page.
- [ ] Start Test waits for successful RX opening before TX; abort if RX fails.
- [ ] Independent advanced TX/RX controls and reliable cancellable shutdown.
- [ ] Fixed configurable Roll, sine Roll and file replay modes in GUI.
- [ ] File selection and preflight counts/errors before playback.
- [ ] Default tolerant replay plus selectable strict mode; reject zero valid frames.
- [ ] Loop until Stop by default; optional once; restart from beginning after Stop.
- [ ] Lock source/rate during TX; show position, loop count and sent count.
- [ ] Absolute monotonic paced output, no catch-up bursts across loop boundaries.
- [ ] Protocol, timing and behavior verdicts displayed independently.
- [ ] Detailed errors, receive interval min/mean/max and current rate.
- [ ] Last-ten-second trends, bounded raw-frame view and event log.
- [ ] Optional sequence tag in Heave for generated sources (off for OFF
  comparison): show delay, missing frames and the measured Offset.
- [ ] Deterministic malformed/split/garbage/drop injection; off by default.
- [ ] Later stage: seeded random delay and disconnect/reconnect injection.

### Confirmed OFF validation semantics

Use a fixed known frame for automatic serial OFF tests. Compare every received
frame byte-for-byte to the expected frame. Require actual received data; an empty
capture is not a pass. Do not require equal TX/RX counts: invalid input frames are
dropped (each valid frame is forwarded once, adr/0001).

For varying capture replay, report protocol/timing and traceable content; mark
behavior indeterminate when alignment cannot support an exact verdict. Do not
invent sequence fields, infer definite packet loss solely from gaps, or label
protocol validity as proof of correct sweep behavior.

## 3. Production GUI

- [ ] Existing-port dropdowns and refresh, rather than manual-only entry.
- [ ] Single-page Traditional Chinese controls retaining instrument terminology.
- [ ] Distinct disconnected/waiting/active/stale/error states.
- [ ] Single Sweep switch; OFF continues fresh motion output.
- [ ] Forward each valid frame on arrival: unchanged with Sweep OFF, only Roll
  rewritten when ON; no output-rate setting (adr/0001). Show forwarding delay.
- [ ] Angle means +/- synthetic amplitude; cycle means full center-left-center-right-center.
- [ ] Apply angle/cycle/direction explicitly; reset phase at Apply and OFF->ON,
  with a warning about potential offset discontinuity.
- [ ] Optional finish at CENTER, default off: OFF follows the sine to the next
  CENTER before stopping, and Apply waits for it.
- [ ] Fixed Offset mode for commissioning the Roll sign and direction.
- [ ] Warn when |Output Roll| exceeds an operator-set MBES stabilisation limit
  (default 15°, leaving Angle ±10° room for vessel roll), and count those frames.
- [ ] Show the peak sweep rate 2π·Angle/Cycle; warn above an operator-set
  limit (default 10°/s).
- [ ] Lock connection settings while connected.
- [ ] Default configurable 500 ms stale timeout; suspend stale output and disable
  sweep; fresh input resumes output with sweep remaining OFF and no settling hold.
- [ ] Real/offset/output values, ten-second trends, diagnostics and event log.
- [ ] Distinguish Current Rate from Average Rate and reset stats coherently.

## 4. Verification gates

- [ ] Actual two-pair serial integration using the supplied capture.
- [ ] Fixed-frame OFF full-byte comparison and ON amplitude/direction/cycle tests.
- [ ] Sustained runs with 25/50/100 Hz input: forwarding delay and intervals
  measured, no accumulated phase drift.
- [ ] Tagged bench test: measured Offset matches the configured Sweep regardless
  of simulated vessel roll; delay and missing frames reported.
- [ ] Stale suspension/recovery, occupied ports, output failure and shutdown tests.
- [ ] Failure injection and serial stream recovery tests.
- [ ] GUI/controller lifetime tests including closing with file dialog open.
- [ ] Test worker transitions, not only the freshness helper or simulation branch.
- [ ] Record environment, results, limitations and commits for each stage.

## 5. Distribution — confirmed portable ZIP first

- [ ] Two executables, dependent DLLs, GTK resources, instructions and licenses.
- [ ] Verify on an environment without development PATH dependencies.
- [ ] Normal GUI has no console; debug console and CLI redirection tested.
- [ ] Driver setup remains an explicit Simulator-guided operation; copying the
  portable ZIP alone does not install virtual ports.
- [ ] Update outdated architecture/testing instructions with each delivery.

Physical interface, instrument COM framing, sonar behavior, roll sign and latency
remain commissioning questions. No automatic depth/ping-driven sweep control.
Linux serial support and a full application installer are not this delivery gate.