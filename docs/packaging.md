# Packaging the portable ZIP

From Git Bash or PowerShell at the repository root, with MSYS2 installed at
`C:\msys64`:

```sh
cmake --workflow --preset package
```

The preset sets the MSYS2 PATH itself. It configures a Release build in
`build-release/` with tests off, builds it, and writes
`build-release/POSMV-Sweep-<version>-win64.zip`. The version comes from
`project()` in `CMakeLists.txt`.

## Contents

| Path | Source |
|---|---|
| `posmv-sweep.exe`, `posmv-simulator.exe` | Release build, no console window |
| `*.dll` | Recursive import scan of both programs, from the MSYS2 UCRT64 bin directory only (`cmake/DeployRuntime.cmake`) |
| `README.txt` | `docs/delivery/README.txt`, the user guide in Traditional Chinese |
| `LICENSE.txt` | Repository `LICENSE` (MIT) |
| `THIRD-PARTY-NOTICES.txt`, `licenses/` | `cmake/ThirdPartyNotices.cmake`; fixed wording in `docs/delivery/THIRD-PARTY-NOTICES.txt.in` and `THIRD-PARTY-NOTICES-com0com.txt` |
| `drivers/`, `licenses/com0com/` | `cmake/Com0com.cmake`; turn off with `-DPOSMV_BUNDLE_COM0COM=OFF` |
| `BUILD-INFO.txt` | Version, git commit, date and build type |

How the third-party notices are made:
- `pacman` finds the MSYS2 package that owns each bundled DLL.
- That package's license files are copied into `licenses/<package>/`.
- The package's version, licenses, URL and source link are listed in
  `THIRD-PARTY-NOTICES.txt`.
- Packages without license files: LGPL-2.1 ones get the standard text in
  `licenses/common/`, and any other such package stops the packaging.

How the com0com files are bundled:
- The pinned 2.2.2.0 signed installer and its official source come from
  SourceForge, each checked by SHA-256.
- Downloads are cached in `build-release/third_party/`. To package offline, put
  the two files there.
- The Simulator looks for the installer in `<exe>\drivers\` first.

## Verification (2026-10-02, `a742877` plus the packaging commit)

- The ZIP is 18.8 MB, with 60 DLLs from 41 MSYS2 packages; every package has
  license text.
- It was extracted outside the repository and both programs were started with
  PATH reduced to `%SystemRoot%\system32;%SystemRoot%`. Every one of the 61
  MSYS2 DLLs each process loaded came from the package folder, none from
  `C:\msys64`.
- Both windows rendered normally. The Simulator's environment check and
  self-test passed from the packaged executable.
- Not covered:
  - A machine that has never had MSYS2 or com0com installed.
  - The UAC install path from the package.
