# Reserved Linux backend

Not implemented or verified. Windows remains the delivery target.

Future implementations:
- SerialTransport PIMPL using POSIX descriptors and raw termios settings.
- available_ports(): enumerate serial devices, plus manually selected PTY paths.
- EnvironmentManager: own two PTY masters, expose slave paths to Sweep, retain
  lifetime until explicit teardown, support bidirectional connectivity testing.
- No PowerShell, com0com, driver installation or Windows handles in this backend.

Shared protocol, sweep, replay/validation and scheduling must remain unchanged.
Before enabling Linux application builds, add PTY integration, permissions,
disconnect/recovery, shutdown and GTK lifecycle tests on Linux.
The current environment-manager reporting still uses Windows script result files;
replace that with typed common results when the Linux backend is implemented.