# Architecture and implementation status

Implemented: independent protocol library, bounded stream parser, preserving
encoder, elapsed-time sine generator, clipping composer, offline replay CLI,
CTest harness with no third-party test dependency. GTK4 shell is optional.

Replay uses synthetic timestamps `valid_frame_index / selected_rate` because
plain TSS1 captures have no timestamps. This is deterministic offline conversion,
NOT live output scheduling or recovery of actual capture timing. Processing is
unpaced and output rate only describes the assumed input timeline.

Added: simulator, reusable steady_clock deadline pacing and Windows serial
transport, plus bounded serial-to-file capture. See testing.md for limitations.
Pending: independent main-proxy output worker, full timing diagnostics,
live motion state and thread-safe
configuration, full GTK operator controls, logging and hardware commissioning.
GTK must never perform protocol work or precise scheduling. The current shell
does not claim to be the first usable full operator version.

Open commissioning questions: physical interface; baud/parity/data/stop bits;
MBES model and TSS1 acceptance; Roll sign and actual beam steering response;
latency tolerance; whether live output resamples or matches source rate.
No automatic water-depth control or sonar ping synchronization is implemented.

Negative-first is the configurable default, not a claim about real port direction.
Enabling/disabling phase transitions for a live system remain to be specified.