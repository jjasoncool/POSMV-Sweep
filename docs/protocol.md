# TSS1 contract

Source of truth: the supplied project handoff, not an inferred hardware protocol.
Body: `:XXAAAASMHHHHQMRRRRSMPPPP`, 25 ASCII bytes; CRLF adds 2 bytes.
Zero-based positions: separator 7, heave sign 8, status 13, Roll sign 14,
Roll digits 15–18, separator 19, pitch sign 20, pitch digits 21–24.
Positive sign is a space; negative sign is `-`. Heave is centimetres;
Roll and pitch are hundredths of a degree. Known accepted status flags:
`UuGgHhFf`. Other statuses are rejected pending authoritative clarification.

Parser accepts arbitrary chunks and retains at most 27 bytes. A new colon
resynchronizes an incomplete candidate. Garbage outside candidates is ignored.
Incomplete candidates count as invalid only on resynchronization or `finish()`.
Encoder preserves all bytes except Roll. Sweep OFF preserves even negative zero.
Round to nearest hundredth; composer clamps at +/-90 degrees and reports clipping.

ASCII 8N1 theoretical wire demand: 27 * 10 * rate bits/s, excluding any margin.
25 Hz requires 6750 bit/s; 50 Hz 13500; 100 Hz 27000. No serial defaults
or operator throughput validation are implemented in this offline version.