# POSMV-Sweep

POS MV TSS1 Motion Sweep Proxy. Windows 10/11 x64, C++20, CMake,
MSYS2 UCRT64. Offline development first; not commissioned for instrumentation use.

## Build (MSYS2 UCRT64 shell)

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

GTK4 is a separate optional build dependency. Install the UCRT64 GTK4 and
pkgconf packages, then configure with `-DPOSMV_BUILD_GUI=ON`.
Never mix MINGW64 libraries with the UCRT64 compiler.

## Offline replay

```sh
./build/posmv-sweep.exe --replay input.txt --output output.txt --angle 10 --cycle 4 --rate 25
./build/posmv-sweep.exe --replay input.txt --output passthrough.txt --off
```

`--invert` changes direction. Rate accepts 25, 50 or 100. Captures must contain
CRLF-delimited frames. Invalid records are discarded; incomplete trailing records
are counted at EOF. Summary reports valid output, invalid candidates and clipping.
Output paths must differ from input; existing output files are overwritten.
Replay is unpaced and assumes equally spaced valid input frames; it does not
reconstruct capture timestamps. See `docs/protocol.md` and `docs/architecture.md`.

Status: offline milestones 1–4 implemented; GTK shell requires GTK4 installation
and has not yet been built or launch-tested. Live scheduler, serial, and full
operator GUI are pending. No hardware validation has been performed.