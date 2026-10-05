# GTK4 main application

Build in MSYS2 UCRT64:

```sh
cmake -S . -B build -G Ninja -DPOSMV_BUILD_GUI=ON
cmake --build build
ctest --test-dir build --output-on-failure
./build/posmv-sweep.exe
```

No arguments opens the GTK4 operator interface. GTK4 is required by default;
headless builds can explicitly select `-DPOSMV_BUILD_GUI=OFF`.

Windows/MinGW builds deploy recursively imported GTK and compiler runtime DLLs
beside both executables after linking. You can launch the main executable
directly from Explorer without adding MSYS2 to the system PATH. Keep the DLLs
alongside the executable; copying just the EXE is not a portable deployment.
This fixes DLL loading for local development; it is not yet a release installer
with GTK assets and third-party license notices. A missing session D-Bus warning
may still appear in stderr; the tested GUI stays running without that service.

Connect opens the selected input and output ports and starts one forwarding
worker; connection errors show beside it, in the state label.

Apply and a blue Sweep toggle sit at the bottom right of the Sweep block.

The toggle shows the state: `Sweep：OFF`, `Sweep：ON` (pressed) or
`Sweep：收尾中` during a soft stop.
- OFF forwards the input unchanged (Output = Input).
- One click ON takes the settings on screen and starts at CENTER.
- One click OFF stops, at the next CENTER if finish-at-CENTER is selected.
- Turning Sweep off does not stop communication.

Apply saves mode, angle, cycle, fixed Offset and direction:
- It turns blue while settings on screen are not saved.
- Its feedback appears right above it.
- With Sweep ON, Apply restarts at CENTER at once (the offset may jump), or at
  the next CENTER when finish-at-CENTER is selected.
- With Sweep OFF, it only saves.
- Finish-at-CENTER and both limits take effect without Apply.

Connection settings apply on the next Connect (disconnect before changing them).

For Virtual COM testing pick the input and output ports the Simulator shows
from the dropdowns (重新掃描 COM refreshes them). Input and output must differ:
the form starts with two different ports, and Connect stays disabled, with the
reason shown, while they are the same or no port exists. Baud is a list of
standard values (1200 to 230400), defaulting to 9600, the POS MV manual's
minimum for 25 Hz; set it to match the POS MV and MBES ports. Serial settings are explicit
8N1 without flow control, not a claim about actual POS MV settings. Baud is
not checked against a rate: output follows the input.

Left of the trend chart, a beam sketch shows the ship from the stern. Its parts:
- The ship leans with Real Roll, labelled above it.
- A grey sector is the MBES range (±limit, default 10°), around the hull's
  down axis, so it leans with the ship.
- Both lines are angles from straight down, as in the trend chart; positive is
  to the right. The port/starboard side still needs commissioning.
- A red solid line, labelled `波束`, is the expected beam. The model assumes the
  MBES steers relative to the hull by the Roll it receives (Output), up to its
  limit: beam = clamp(Output, ±limit) − Real. It is not read from the MBES.
- Within the limit the beam equals the Offset. Beyond it, an orange dashed line,
  labelled `目標`, shows the Offset the beam could not reach.
- Bottom left: how far the MBES turns relative to the hull (`MBES 轉`), and
  either the limit or `已達上限` in orange.
- Lines are grey while nothing is sent.

The trend chart draws the same expected beam as a red line over the Offset. The
two lines part only where the limit clips the beam.

There is no separate message area, so warnings never shift the layout:
- The Output value turns orange while it exceeds the MBES limit, and the
  diagnostics count those frames.
- The peak sweep rate turns orange above its limit.

Each valid input frame is forwarded as soon as it arrives (adr/0001): unchanged
with Sweep OFF, with only the Roll field rewritten when ON. Invalid input is
dropped and counted. The GUI shows the forwarding delay, warns when |Output
Roll| exceeds the operator-set MBES stabilisation limit or the peak Offset rate
exceeds its limit, and offers a fixed-Offset commissioning mode and an optional
finish-at-CENTER for OFF and Apply. GUI refresh is 10 Hz.

Input freshness timeout is configurable, with a development default of 500 ms.
Stale input suspends output and disables Sweep. Fresh input resumes output
with Sweep OFF and no settling hold. This policy requires hardware
commissioning. Physical Roll sign remains unconfirmed; the fixed-Offset mode is
for commissioning it. GUI end-to-end runs over Virtual COM pairs are recorded
in `verification/`. Automated tests drive the engine through in-memory
transports: pass-through, modulation, invalid input, stale handling, the MBES
limit and finish-at-CENTER.

The Simulator (GTK4) manages the Virtual COM pairs, sends test frames or a
capture, and validates the proxy output; see `testing.md`.