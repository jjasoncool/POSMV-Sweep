# Virtual COM manager implementation status

Simulator now launches Manage-VirtualCom.ps1 for Inspect/Create/Remove.
Create and Remove explicitly request Windows elevation. A compatible installed
com0com setupc.exe is required; no driver installer is bundled or downloaded.
Default detection searches Program Files/com0com and Program Files (x86)/com0com.

The script records owned pairs and checks their current endpoint names before
removal. It requests COM# allocation rather than guessing COM numbers. The GUI
only applies the saved mapping after a successful operation; enumeration is NOT
a connectivity test. Uninstallation, connectivity self-test and installer
integration are still missing.

Validation performed: C++ build, five CTest entries, PowerShell syntax parsing,
missing-tool error result. Creation/removal with an actual driver has NOT been
tested. In particular the chosen installed setupc version must be verified for
command/output compatibility before this manager is considered usable.

Replay preflight supports strict/tolerant parsing and cancellation. Combined
Start waits for RX-open confirmation before starting TX. GUI lifecycle and
serial failure/recovery tests remain outstanding; this is not full acceptance.