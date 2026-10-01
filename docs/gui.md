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

The default offline simulation produces valid TSS1 input with +2 degree real
Roll. Connect starts independent receiver and output workers. Start Sweep and
Stop Sweep toggle modulation only; Stop Sweep does not stop communication.
Angle, cycle and direction require Apply and restart at CENTER (offset may jump).
Enabling Sweep also restarts at CENTER. Connection settings and output
rate apply on the next Connect (disconnect before changing them).

For Virtual COM testing uncheck offline mode, enter COM11 input and COM12
output, and use matching simulator/monitor pairs. Serial settings are explicit
8N1 without flow control, not a claim about actual POS MV settings. Baud is
validated against the selected output rate.

The output worker resamples the latest valid input at its configured rate,
using absolute monotonic deadlines. It emits nothing until the first valid
input arrives. OFF preserves the selected frame bytes, but resampling means
it does not guarantee one output per input record. GUI refresh is 10 Hz.

Input freshness timeout is configurable, with a development default of 500 ms.
Stale input suspends output and disables Sweep. Fresh input resumes output
with Sweep OFF. This policy requires hardware commissioning. Physical Roll sign remains
unconfirmed. Virtual COM integration and interactive GUI operations have not
yet been end-to-end tested. Automated tests exercise background simulation,
pass-through, modulation, rates and worker shutdown.

Simulator GTK4 controls, file selection/replay controls in the main GUI, COM
enumeration and automated Virtual COM installation are still separate pending
work; the main GUI currently accepts manually entered COM names.