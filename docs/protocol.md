# TSS1 contract

Source of truth: POS MV manual, Interfaces and Data Formats, Table 26 "TSS1
Output Format" (supplied by the user on 2026-10-02).

Body: `:XXAAAASMHHHHQMRRRRSMPPPP`, 25 ASCII bytes; CRLF adds 2 bytes.

| Zero-based positions | Field | Encoding |
|---|---|---|
| 0 | `:` | start of packet |
| 1–2 | XX, horizontal acceleration | hexadecimal, 3.83 cm/s² per count, 0 to 9.81 m/s² |
| 3–6 | AAAA, vertical acceleration | hexadecimal, 0.0625 cm/s² per count, 0 to 40.96 m/s² |
| 7 | space | |
| 8–12 | Heave | sign and 4 digits, centimetres, ±99 m |
| 13 | status | `UuGgHhFf`: unaided, GNSS, heading, full aided; lowercase means settling |
| 14–18 | Roll | sign and 4 digits, hundredths of a degree, ±90° |
| 19 | space | |
| 20–24 | Pitch | sign and 4 digits, hundredths of a degree, ±90° |

The table prints the acceleration ranges in cm/s², but its own units reach them
only in m/s², and only as hexadecimal: FF × 3.83 cm/s² ≈ 9.77 m/s² and
FFFF × 0.0625 cm/s² = 40.96 m/s². Decimal digits would stop at 3.79 and 6.25
m/s². The supplied capture has A–F in these fields in 2605 of 4574 frames.

Sign: a space for positive, `-` for negative. POS MV can invert the sense of
roll, pitch and heave (MV-POSView Controller), so the Roll sign the MBES sees
is a commissioning item; check it with the fixed Offset mode.

POS MV sends TSS1 at 1–200 Hz and recommends at least 9600 baud at 25 Hz. ASCII
8N1 needs 27 × 10 × rate bit/s: 6750 at 25 Hz, 13500 at 50 Hz, 27000 at 100 Hz
and 54000 at 200 Hz (so 57,600 baud or more). The Simulator sends at 25, 50,
100 or 200 Hz and offers only the standard bauds that carry the chosen rate,
selecting the lowest: 9600, 19200, 38400 and 57600. The Proxy uses one baud for
both ports and forwards at the input rate, so its output keeps up with any
input that arrives.

Parser accepts arbitrary chunks and retains at most 27 bytes. A new colon
resynchronizes an incomplete candidate. Garbage outside candidates is ignored.
Incomplete candidates count as invalid only on resynchronization or `finish()`.
Encoder preserves all bytes except Roll. Sweep OFF preserves even negative zero.
Round to nearest hundredth; composer clamps at +/-90 degrees and reports clipping.
