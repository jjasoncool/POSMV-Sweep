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
worker. The Sweep switch toggles modulation only; turning it off does not stop
communication. Angle, cycle, direction and mode require Apply and restart at
CENTER (offset may jump unless finish-at-CENTER is selected). Enabling Sweep
also restarts at CENTER. Connection settings apply on the next Connect
(disconnect before changing them).

For Virtual COM testing pick the input and output ports the Simulator shows
from the dropdowns (重新掃描 COM refreshes them). Input and output must differ:
the form starts with two different ports, and Connect stays disabled, with the
reason shown, while they are the same or no port exists. Serial settings are explicit
8N1 without flow control, not a claim about actual POS MV settings. Baud is
not checked against a rate: output follows the input.

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