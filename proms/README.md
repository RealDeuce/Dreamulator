# Printer Character Generator ROM Format

## Epson LX-800 (9-pin NLQ)

**ROM:** `lx800.ic3c` (32,768 bytes, UPD7810 firmware + embedded chargen)

### Pointer Table

Located at offset **0x3236** in the ROM. Each entry is a 16-bit
little-endian address pointing to the character's data block within the
ROM. There are **180 entries** covering characters 0x00–0xB3.

```
0x3236: [ptr_char_0x00 lo] [ptr_char_0x00 hi]
0x3238: [ptr_char_0x01 lo] [ptr_char_0x01 hi]
...
0x339C: [ptr_char_0xB3 lo] [ptr_char_0xB3 hi]
```

Character data spans approximately 0x339E–0x411F.

### Character Data Block

Each character's data starts with a header, followed by two-pass NLQ
column data. The polarity is **inverted**: a 0 bit means "fire pin"
(ink on paper), a 1 bit means "no fire."

#### Normal characters (attr != 0xFF)

```
[attr] [pass1 data] [pass2 data]
```

- **attr** (1 byte):
  - Bits 0–3: body column count per pass
  - Bits 4–6: proportional spacing info (not fully decoded)
  - Bit 7: descender flag (character has pin 9 data)

#### Wide characters (attr == 0xFF)

```
[0xFF] [sub_attr] [body_cols] [pass1 data] [pass2 data]
```

- **sub_attr** (1 byte): same layout as normal attr (bit 7 = descender)
- **body_cols** (1 byte): body column count per pass (typically 0x13 = 19)

### Pass Data Layout

Each pass contains the column data for one NLQ printing pass. The two
passes are printed at the same horizontal position but offset vertically
by half a pin pitch (1/144"), producing 16 effective vertical positions
from 9 physical pins.

#### Without descender (attr bit 7 = 0)

Each pass is exactly `body_cols` bytes:

```
[col0] [col1] ... [colN-1]
```

Each byte is one column. Bits 0–7 map to pins 0–7 (MSB = top pin,
LSB = bottom pin). A 0 bit fires the pin.

Total data bytes = body_cols × 2.

#### With descender (attr bit 7 = 1)

Each pass has `pin9_bytes` packed bytes **before** the column data:

```
[pin9 packed bytes] [col0] [col1] ... [colN-1]
```

Where `pin9_bytes = (total_data / 2) - body_cols`.

The pin 9 packed bytes encode one bit per column for the 9th
(descender) pin, MSB-first:

```
byte 0: bit 7 = col 0, bit 6 = col 1, ..., bit 0 = col 7
byte 1: bit 7 = col 8, bit 6 = col 9, ...
```

A 0 bit fires pin 9 for that column (same inverted polarity as the
body data).

To combine pass 1 and pass 2 pin 9 data: **AND** the raw bytes from
both passes. Since 0 = fire, AND means the pin fires only where
**both** passes agree to fire. This produces correct symmetry for
characters like `[`, `]`, `(`, `)` whose top and bottom rows must
match.

Total data bytes = (body_cols + pin9_bytes) × 2.

### Combining the Two Passes

For pins 0–7: **invert** each byte (XOR 0xFF) to get 1 = ink, then
**OR** the corresponding columns from pass 1 and pass 2. This fills
in horizontal strokes that only appear in one pass (e.g., the crossbar
of 'H' is entirely in pass 2).

For pin 9: **AND** the raw (non-inverted) bytes from both passes, then
interpret 0 bits as ink.

Finally, flip the bit order of each body column byte (reverse bits 0–7)
so that bit 0 = top pin in the output format.

### Worked Example: 'H' (0x48)

```
Pointer: ptrs[0x48] → addr in ROM
attr = 0x09 → body_cols = 9, no descender
Data: 18 bytes (9 per pass)

Pass 1 raw:  7E 00 7E FF FF FF 7E 00 7E
Pass 2 raw:  FF 01 EF EF EF EF EF 01 FF

Invert:
Pass 1 inv:  81 FF 81 00 00 00 81 FF 81
Pass 2 inv:  00 FE 10 10 10 10 10 FE 00

OR combined: 81 FF 91 10 10 10 91 FF 81

Flip bits (MSB→LSB swap):
             81→81  FF→FF  91→89  10→08 ...

Rendered (bit 0 = top pin):
  ###...###
  .#.....#.
  .#.....#.
  .#######.    ← crossbar from pass 2
  .#.....#.
  .#.....#.
  .#.....#.
  ###...###
```

### Worked Example: 'p' (0x70)

```
attr = 0x89 → body_cols = 9, descender = true
Data: 22 bytes, half = 11, pin9_bytes = 11 - 9 = 2

Pass 1 (11 bytes): [pin9_0 pin9_1] [col0..col8]
Pass 2 (11 bytes): [pin9_0 pin9_1] [col0..col8]

Pin 9: AND raw pass1[0..1] with raw pass2[0..1]
       Then 0 bits = fire, MSB = col 0
```

## Epson FX-80 (9-pin Draft)

**ROMs:**
- `epson_8426k9_m1206ba029_read_as_27c128.bin` (16,384 bytes) — UPD7810
  firmware for FX-80+
- `epson_fx_c42040kb_8042ah.bin` (2,048 bytes) — 8042AH slave CPU
  firmware (print head controller). Not character data.

### Font Tables

Two 128-character font tables are embedded in the firmware ROM:

| Table  | Offset   | Size  |
|--------|----------|-------|
| Roman  | 0x17A3   | 1,536 |
| Italic | 0x1DA3   | 1,536 |

Each table contains 128 characters (0x00–0x7F). Characters 0x00–0x1F
are international characters (Code Page 437 variants); 0x20–0x7F are
standard ASCII.

### Character Block (12 bytes)

```
[prefix] [col0] [col1] ... [col8] [pin_hi] [pin_lo]
```

**Fixed size:** every character is exactly 12 bytes.

- **prefix** (1 byte): bit 7 = descender flag (see below), remaining
  bits encode proportional spacing information
- **col0–col8** (9 bytes): column data, one byte per half-dot column.
  Polarity is **inverted** (0 = fire pin, 1 = no fire). Bit order is
  MSB = topmost pin in the column
- **pin_hi, pin_lo** (2 bytes): packed extra pin data, 1 bit per
  column, MSB-first (bit 15 of the 16-bit word = column 0). Polarity
  is inverted (0 = fire)

### Pin Mapping (Descender Flag)

The meaning of the column byte bits depends on bit 7 of the prefix:

**Non-descender (prefix bit 7 = 1):**
```
Column byte: bit 7 = pin 1 (row 0, top)
             bit 0 = pin 8 (row 7)
Packed data: pin 9 (row 8, descender) — usually 0xFFFF (empty)
```

**Descender (prefix bit 7 = 0):**
```
Column byte: bit 7 = pin 2 (row 1)
             bit 0 = pin 9 (row 8, descender)
Packed data: pin 1 (row 0, top) — usually 0xFFFF (empty)
```

The descender flag shifts the column byte one pin downward, allowing
the character to extend into the descender area (pin 9). Characters
flagged as descenders include: g, p, q, y, comma, semicolon,
underscore, and some international characters.

### Half-Dot Grid

The FX-80 draft font uses a 12-position × 9-row grid at 120 DPI
horizontal. Even-numbered columns (0, 2, 4, 6, 8) are main dot
positions; odd-numbered columns (1, 3, 5, 7) are interleaved half-dot
positions that overlap adjacent main dots. Only 9 columns are stored
in ROM; positions 9–11 are implicit trailing whitespace.

Character widths are proportional, ranging from 5 to 12 half-dot
positions.

### Worked Example: 'A' (0x41, Roman)

```
Offset: 0x17A3 + 0x41 × 12 = 0x1AAF
prefix = 0x8B → non-descender, 12-wide
Column data (inverted): E1 DF B7 7F F7 7F B7 DF E1
Pin data: FF FF (no descender)

Invert columns → 1E 20 48 80 08 80 48 20 1E

Rendered (9 cols × 9 rows, bit 0 = top):
  ...#.#...
  ..#...#..
  .#.....#.
  #.......#
  #.#.#.#.#
  #.......#
  #.......#
  .........    ← pin 8 (empty)
  .........    ← pin 9 (empty)
```
