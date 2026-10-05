# POSMV-Sweep

POS MV TSS1 Motion Sweep Proxy. It sits on the serial line between a POS MV and
an MBES, forwards each TSS1 frame on arrival and, when Sweep is on, adds a
synthetic Roll offset (sine or fixed) so the beam sweeps. All other fields are
passed through unchanged.

Not commissioned for instrumentation use: no hardware or live-sonar validation
has been performed. Terms are defined in [GLOSSARY.md](GLOSSARY.md).

## Programs

| Executable | Purpose |
| --- | --- |
| `posmv-sweep.exe` | Operator Proxy (GTK4): choose Input/Output COM and baud, Connect, Sweep ON/OFF, Apply, trend chart, beam sketch with MBES limit. CLI offers offline replay and serial-to-file capture. |
| `posmv-simulator.exe` | Test console (GTK4 + CLI): virtual COM environment (com0com), generated/replay TX, RX validation by frame matching, delay and Offset statistics. |

Details: [docs/gui.md](docs/gui.md), [docs/testing.md](docs/testing.md),
[docs/virtual-com-status.md](docs/virtual-com-status.md),
[docs/protocol.md](docs/protocol.md), [docs/architecture.md](docs/architecture.md).
Requirements and decisions: [docs/acceptance.md](docs/acceptance.md),
[docs/adr/](docs/adr/); test records: [docs/verification/](docs/verification/).

## Platform

- Windows 10/11 x64 only. C++20, CMake ≥ 3.20 (≥ 3.25 for the packaging
  workflow), Ninja, MSYS2 UCRT64 GCC, GTK4.
- Linux is **not supported yet**: CMake stops with a fatal error on non-Windows
  hosts. Shared code (protocol, sweep, validation) is platform-neutral, but the
  serial, clock and virtual COM backends exist only for Windows. See
  [src/platform/linux/README.md](src/platform/linux/README.md).

## Build (MSYS2 UCRT64 shell)

Install [MSYS2](https://www.msys2.org/) at the default `C:\msys64`, open the
**MSYS2 UCRT64** shell (not MSYS or MINGW64), and from the repository root run:

```sh
pacman -S --needed mingw-w64-ucrt-x86_64-{gcc,cmake,ninja,pkgconf,gtk4}
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

- The programs are `build/posmv-sweep.exe` and `build/posmv-simulator.exe`.
- GUI is on by default; `-DPOSMV_BUILD_GUI=OFF` builds CLI only.
- After linking, the required UCRT64/GTK DLLs are copied beside both
  executables, so they start from Explorer without MSYS2 on `PATH`.
- Never mix MINGW64 libraries with the UCRT64 compiler.

## Package for delivery

```sh
cmake --workflow --preset package
```

This produces `build-release/POSMV-Sweep-<version>-win64.zip`: both programs,
their DLLs, a Traditional Chinese user guide, third-party notices and licenses,
and the pinned com0com 2.2.2.0 installer with its source. See
[docs/packaging.md](docs/packaging.md).

- Run it from the repository root in any shell (PowerShell, Git Bash or UCRT64);
  the preset puts `C:\msys64\ucrt64\bin` on `PATH` itself. It needs the MSYS2
  packages from the Build section.
- The first run downloads the com0com files (SHA-256 checked) into
  `build-release/third_party/`. Offline, put the two files there first.
- Close any program started from `build-release/`; a running exe cannot be
  relinked.

## License

MIT, see [LICENSE](LICENSE). Bundled third-party components keep their own
licenses, listed in the package's `THIRD-PARTY-NOTICES.txt`.

## Offline replay

```sh
./build/posmv-sweep.exe --replay input.txt --output output.txt --angle 10 --cycle 4 --rate 25
./build/posmv-sweep.exe --replay input.txt --output passthrough.txt --off
```

`--invert` changes direction. Captures must contain CRLF-delimited frames.
Invalid records are discarded and counted; output must differ from input.
Replay is unpaced and does not reconstruct capture timestamps.
