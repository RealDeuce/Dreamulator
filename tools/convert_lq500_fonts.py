#!/usr/bin/env python3
# license:BSD-3-Clause
# copyright-holders:Stephen Hurd
"""Convert LQ-500 CG ROM font TSV tables into C++ source for dreamulator.

Reads the extracted font TSV files from the lq500 repository and outputs
fontlq500.cpp with glyph data, per-glyph metrics, secondary metrics,
and accessor functions.

The CG ROM stores glyph columns as 3 bytes with MSB = top pin in each
byte group.  render_glyph_24pin() in dotrender.cpp expects bit 0 = top
pin, so each byte is bit-reversed during conversion.
"""

import csv
import os
import sys
from pathlib import Path

# Font directory: (index, tsv_filename, first_char, last_char, description)
FONT_DIR = [
    (0,  "lq500_4c_font_00_Roman_Draft.tsv",                            0x20, 0xEF, "Roman Draft"),
    (1,  "lq500_4c_font_01_Roman_Draft_Elite.tsv",                      0x20, 0xEF, "Roman Draft Elite"),
    (2,  "lq500_4c_font_02_Roman_Draft_Elite_Proportional_Condensed.tsv", 0x20, 0xEF, "Roman Draft Elite Prop Condensed"),
    (3,  "lq500_4c_font_03_Roman_LQ.tsv",                               0x20, 0xEF, "Roman LQ"),
    (4,  "lq500_4c_font_04_Roman_LQ_Elite.tsv",                         0x20, 0xEF, "Roman LQ Elite"),
    (5,  "lq500_4c_font_05_Roman_LQ_Elite_Proportional_Condensed.tsv",  0x20, 0xEF, "Roman LQ Elite Prop Condensed"),
    (6,  "lq500_4c_font_06_Roman_LQ_Proportional.tsv",                  0x20, 0xEF, "Roman LQ Proportional"),
    (7,  "lq500_4c_font_07_Block_LQ.tsv",                               0x00, 0xFF, "Block LQ"),
    (8,  "lq500_4c_font_08_Block_Draft.tsv",                            0x00, 0xFF, "Block Draft"),
    (9,  "lq500_4c_font_09_SansSerif_LQ.tsv",                           0x20, 0xEF, "Sans Serif LQ"),
    (10, "lq500_4c_font_10_SansSerif_LQ_Elite.tsv",                     0x20, 0xEF, "Sans Serif LQ Elite"),
    (11, "lq500_4c_font_11_SansSerif_LQ_Elite_Proportional_Condensed.tsv", 0x20, 0xEF, "Sans Serif LQ Elite Prop Condensed"),
    (12, "lq500_4c_font_12_SansSerif_LQ_Proportional.tsv",              0x20, 0xEF, "Sans Serif LQ Proportional"),
]

# Bit-reversal table for a single byte.
REV8 = bytes(int(f"{i:08b}"[::-1], 2) for i in range(256))


def reverse_byte(b: int) -> int:
    return REV8[b & 0xFF]


def parse_font_tsv(path: Path):
    """Parse a font TSV file and return a dict mapping char_code -> record."""
    glyphs = {}
    with open(path, newline="") as f:
        reader = csv.DictReader(f, delimiter="\t", quoting=csv.QUOTE_NONE)
        for row in reader:
            code = int(row["char_code"], 16)
            width = int(row["width"])
            start = int(row["start"])
            advance = int(row["advance"])
            half_res = int(row.get("half_res", "0"))
            hex_data = row["glyph_data_hex"].strip()

            # Parse hex columns.  Normal fonts: 6 hex chars = 3 bytes
            # per column.  Super/subscript fonts (dim1=$10): 4 hex chars
            # = 2 bytes per column.  Store them upper-aligned; runtime
            # rendering shifts the same two bytes down for subscript,
            # matching firmware effect #1 at $4AA8.
            col_bytes = []
            if hex_data:
                for chunk in hex_data.split():
                    if len(chunk) == 4:
                        # 2-byte column (16 dots): expand to 3 bytes.
                        # Default alignment: upper 16 pins [data, data, 0]
                        b0 = int(chunk[0:2], 16)
                        b1 = int(chunk[2:4], 16)
                        rb = [reverse_byte(b0),
                              reverse_byte(b1),
                              0]
                    else:
                        b0 = int(chunk[0:2], 16)
                        b1 = int(chunk[2:4], 16)
                        b2 = int(chunk[4:6], 16)
                        rb = [reverse_byte(b0),
                              reverse_byte(b1),
                              reverse_byte(b2)]
                    col_bytes.extend(rb)
                    # Half-res glyphs: firmware interleaves blank
                    # columns between stored columns to reconstruct
                    # the full-width sparse dot pattern.
                    if half_res:
                        col_bytes.extend([0, 0, 0])

            # For half-res, trim to the full glyph width.
            if half_res and col_bytes:
                target = width * 3
                col_bytes = col_bytes[:target]

            glyphs[code] = {
                "start": start,
                "width": width,
                "advance": advance,
                "data": col_bytes,
            }
    return glyphs


def parse_secondary_metrics(path: Path):
    """Parse the secondary metrics TSV.  Returns dict[base_name][char_code] -> bytes."""
    metrics = {}
    with open(path, newline="") as f:
        reader = csv.DictReader(f, delimiter="\t", quoting=csv.QUOTE_NONE)
        for row in reader:
            base = row["base"]
            code = int(row["char_code"], 16)
            vals = [int(row[f"byte{i}"], 16) for i in range(6)]
            if base not in metrics:
                metrics[base] = {}
            metrics[base][code] = vals
    return metrics


def emit_font_array(out, font_idx, desc, glyphs, first_char, last_char, max_w):
    """Write a single font's glyph data and metrics arrays."""
    count = last_char - first_char + 1

    # Metrics array: { start, width, advance }
    out.write(f"// Font {font_idx}: {desc}\n")
    out.write(f"// Characters 0x{first_char:02X}-0x{last_char:02X}, "
              f"max glyph width {max_w} columns\n")
    out.write(f"static constexpr Lq500GlyphMetrics "
              f"lq500_metrics_{font_idx:02d}[{count}] = {{\n")
    for ch in range(first_char, last_char + 1):
        g = glyphs.get(ch)
        if g:
            out.write(f"\t{{ {g['start']:3d}, {g['width']:3d}, "
                      f"{g['advance']:3d} }}, /* 0x{ch:02X} */\n")
        else:
            out.write(f"\t{{   0,   0,   0 }}, /* 0x{ch:02X} (missing) */\n")
    out.write("};\n\n")

    # Glyph data array: max_w * 3 bytes per character, zero-padded
    bytes_per_glyph = max_w * 3
    out.write(f"static constexpr uint8_t "
              f"lq500_glyphs_{font_idx:02d}[{count}][{bytes_per_glyph}] = {{\n")
    for ch in range(first_char, last_char + 1):
        g = glyphs.get(ch)
        data = g["data"] if g else []
        # Pad to max width
        padded = data + [0] * (bytes_per_glyph - len(data))
        padded = padded[:bytes_per_glyph]

        hex_str = ",".join(f"0x{b:02x}" for b in padded)
        out.write(f"\t{{ {hex_str} }}, /* 0x{ch:02X} */\n")
    out.write("};\n\n")


def main():
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <lq500_data_dir> <output.cpp>",
              file=sys.stderr)
        sys.exit(1)

    data_dir = Path(sys.argv[1])
    out_path = Path(sys.argv[2])

    # Parse all font TSV files
    all_fonts = []
    for font_idx, tsv_name, first_char, last_char, desc in FONT_DIR:
        tsv_path = data_dir / tsv_name
        if not tsv_path.exists():
            print(f"WARNING: missing {tsv_path}", file=sys.stderr)
            all_fonts.append((font_idx, {}, first_char, last_char, desc, 0))
            continue
        glyphs = parse_font_tsv(tsv_path)
        max_w = max((g["width"] for g in glyphs.values()), default=0)
        print(f"Font {font_idx:2d}: {desc:40s}  "
              f"chars={len(glyphs):3d}  max_width={max_w:2d}")
        all_fonts.append((font_idx, glyphs, first_char, last_char, desc, max_w))

    # Parse secondary metrics
    sec_path = data_dir / "lq500_4c_secondary_metrics.tsv"
    sec_metrics = parse_secondary_metrics(sec_path) if sec_path.exists() else {}

    # Generate C++ source
    with open(out_path, "w") as out:
        out.write("// license:BSD-3-Clause\n")
        out.write("// copyright-holders:Stephen Hurd\n")
        out.write("// LQ-500 font data extracted from 4C CG ROM (M10A10LA)\n")
        out.write("// Generated by tools/convert_lq500_fonts.py -- do not edit\n")
        out.write("#include \"dotrender.h\"\n")
        out.write("#include <cstdint>\n\n")

        out.write("struct Lq500GlyphMetrics {\n")
        out.write("\tuint8_t start;\n")
        out.write("\tuint8_t width;\n")
        out.write("\tuint8_t advance;\n")
        out.write("};\n\n")

        # Emit each font
        for font_idx, glyphs, first_char, last_char, desc, max_w in all_fonts:
            if not glyphs:
                continue
            emit_font_array(out, font_idx, desc, glyphs,
                            first_char, last_char, max_w)

        # Font directory table
        out.write("// Font directory: maps font index to data pointers\n")
        out.write("struct Lq500FontEntry {\n")
        out.write("\tconst Lq500GlyphMetrics *metrics;\n")
        out.write("\tconst uint8_t (*glyphs)[1];  // row size varies, cast needed\n")
        out.write("\tint first_char;\n")
        out.write("\tint last_char;\n")
        out.write("\tint max_width;\n")
        out.write("\tint bytes_per_glyph;\n")
        out.write("};\n\n")

        # LQ500_FONT_COUNT omitted -- font_index accessor uses switch/case

        # We need to use reinterpret_cast at runtime because array sizes vary.
        # Instead, generate accessor functions that know the correct sizes.

        # Generate get_lq500_glyph()
        out.write("Lq500GlyphInfo get_lq500_glyph(int font_index, uint8_t ch)\n")
        out.write("{\n")
        out.write("\tLq500GlyphInfo info = { nullptr, 0, 0, 0 };\n")
        out.write("\tswitch (font_index) {\n")
        for font_idx, glyphs, first_char, last_char, desc, max_w in all_fonts:
            if not glyphs:
                continue
            bpg = max_w * 3
            out.write(f"\tcase {font_idx}:\n")
            out.write(f"\t\tif (ch >= 0x{first_char:02X} "
                      f"&& ch <= 0x{last_char:02X}) {{\n")
            out.write(f"\t\t\tint idx = ch - 0x{first_char:02X};\n")
            out.write(f"\t\t\tinfo.start = "
                      f"lq500_metrics_{font_idx:02d}[idx].start;\n")
            out.write(f"\t\t\tinfo.width = "
                      f"lq500_metrics_{font_idx:02d}[idx].width;\n")
            out.write(f"\t\t\tinfo.advance = "
                      f"lq500_metrics_{font_idx:02d}[idx].advance;\n")
            out.write(f"\t\t\tif (info.width > 0)\n")
            out.write(f"\t\t\t\tinfo.data = "
                      f"lq500_glyphs_{font_idx:02d}[idx];\n")
            out.write(f"\t\t}}\n")
            out.write(f"\t\tbreak;\n")
        out.write("\tdefault: break;\n")
        out.write("\t}\n")
        out.write("\treturn info;\n")
        out.write("}\n\n")

        # Generate lq500_font_index()
        # Font selection logic based on family, lq/draft, elite, proportional, condensed
        # Maps from (family, quality/pitch flags) -> font directory index
        #
        # From the font directory config byte:
        #   bit 0: elite (12 CPI)
        #   bit 1: proportional
        #   bit 2: LQ mode
        #   bit 4: condensed
        # Family: 0x00=Roman, 0x01=SansSerif, 0xFF=Block
        out.write("int lq500_font_index(uint8_t family, bool lq, bool elite,\n")
        out.write("                     bool proportional, bool condensed)\n")
        out.write("{\n")
        out.write("\t// Build config byte matching CG ROM directory format\n")
        out.write("\tuint8_t config = 0;\n")
        out.write("\tif (elite) config |= 0x01;\n")
        out.write("\tif (proportional) config |= 0x02;\n")
        out.write("\tif (lq) config |= 0x04;\n")
        out.write("\tif (condensed) config |= 0x10;\n\n")

        # Build lookup table from the font directory
        out.write("\t// Search font directory for matching family + config\n")
        out.write("\t// Directory from CG ROM:\n")

        # Map: (family_code, config_byte) -> font_index
        # Family 0x00=Roman, 0x01=SansSerif, 0xFF=Block
        # We need to handle fallbacks: if exact match not found, try
        # without condensed, then without proportional, etc.
        out.write("\tstruct Entry { uint8_t family; uint8_t config; int index; };\n")
        out.write("\tstatic constexpr Entry dir[] = {\n")

        # Read the actual config bytes from the directory TSV
        dir_path = data_dir / "lq500_4c_font_directory.tsv"
        dir_entries = []
        if dir_path.exists():
            with open(dir_path, newline="") as f:
                reader = csv.DictReader(f, delimiter="\t")
                for row in reader:
                    idx = int(row["index"])
                    fam_raw = row["family"].strip()
                    fam = int(fam_raw, 16)
                    cfg = int(row["config"].strip(), 16)
                    dir_entries.append((fam, cfg, idx))
                    out.write(f"\t\t{{ 0x{fam:02X}, 0x{cfg:02X}, {idx:2d} }},\n")
        out.write("\t};\n\n")

        out.write("\t// Map Sans Serif family code 1 to match directory\n")
        out.write("\tuint8_t fam = family;\n")
        out.write("\tif (fam > 1) fam = 0;  // Unknown family -> Roman\n\n")

        out.write("\t// Firmware fallback chain per $154E documentation:\n")
        out.write("\tauto search = [&](uint8_t f, uint8_t c) -> int {\n")
        out.write("\t\tfor (const auto &e : dir)\n")
        out.write("\t\t\tif (e.family == f && e.config == c)\n")
        out.write("\t\t\t\treturn e.index;\n")
        out.write("\t\treturn -1;\n")
        out.write("\t};\n\n")

        out.write("\t// Pitch alternatives: cycle through pitch options\n")
        out.write("\tauto try_pitches = [&](uint8_t f, uint8_t base) -> int {\n")
        out.write("\t\tuint8_t pitch = static_cast<uint8_t>(base & 0x03);\n")
        out.write("\t\tuint8_t rest = static_cast<uint8_t>(base & ~0x03u);\n")
        out.write("\t\tint r = search(f, base);\n")
        out.write("\t\tif (r >= 0) return r;\n")
        out.write("\t\tstatic const uint8_t alt[][2] = {\n")
        out.write("\t\t\t{0x01, 0x02},\n")
        out.write("\t\t\t{0x00, 0x02},\n")
        out.write("\t\t\t{0x00, 0x01},\n")
        out.write("\t\t\t{0x01, 0x02},\n")
        out.write("\t\t};\n")
        out.write("\t\tint idx = (pitch < 4) ? pitch : 0;\n")
        out.write("\t\tfor (int i = 0; i < 2; i++) {\n")
        out.write("\t\t\tr = search(f, static_cast<uint8_t>(rest | alt[idx][i]));\n")
        out.write("\t\t\tif (r >= 0) return r;\n")
        out.write("\t\t}\n")
        out.write("\t\treturn -1;\n")
        out.write("\t};\n\n")

        out.write("\tauto full_scan = [&](uint8_t f, uint8_t cfg) -> int {\n")
        out.write("\t\tint r = search(f, cfg);\n")
        out.write("\t\tif (r >= 0) return r;\n")
        out.write("\t\tif (cfg & 0x40) {\n")
        out.write("\t\t\tuint8_t cfg2 = static_cast<uint8_t>(cfg & ~0x40u);\n")
        out.write("\t\t\tr = search(f, cfg2);\n")
        out.write("\t\t\tif (r >= 0) return r;\n")
        out.write("\t\t}\n")
        out.write("\t\tuint8_t cfg_no_italic = static_cast<uint8_t>(cfg & ~0x40u);\n")
        out.write("\t\tuint8_t cfg_first = cfg_no_italic;\n")
        out.write("\t\tuint8_t cfg_second = static_cast<uint8_t>(cfg_no_italic ^ 0x10u);\n")
        out.write("\t\tif ((cfg_no_italic & 0x10u) == 0) {\n")
        out.write("\t\t\tcfg_first = static_cast<uint8_t>(cfg_no_italic & ~0x10u);\n")
        out.write("\t\t\tcfg_second = static_cast<uint8_t>(cfg_no_italic | 0x10u);\n")
        out.write("\t\t}\n")
        out.write("\t\tr = try_pitches(f, cfg_first);\n")
        out.write("\t\tif (r >= 0) return r;\n")
        out.write("\t\tr = try_pitches(f, cfg_second);\n")
        out.write("\t\tif (r >= 0) return r;\n")
        out.write("\t\treturn -1;\n")
        out.write("\t};\n\n")

        out.write("\tint r = full_scan(fam, config);\n")
        out.write("\tif (r >= 0) return r;\n")
        out.write("\tif (fam != 0x00) {\n")
        out.write("\t\tr = full_scan(0x00, config);\n")
        out.write("\t\tif (r >= 0) return r;\n")
        out.write("\t}\n")
        out.write("\treturn 0;\n")
        out.write("}\n\n")

        # Secondary metrics
        out.write("// Secondary metrics from CG ROM (6 bytes per character)\n")
        out.write("// Used for proportional advance width calculation\n")
        for base_name, chars in sorted(sec_metrics.items()):
            arr_name = base_name.replace("-", "_")
            # Find range
            codes = sorted(chars.keys())
            if not codes:
                continue
            first = codes[0]
            last = codes[-1]
            count = last - first + 1
            out.write(f"static constexpr uint8_t "
                      f"lq500_sec_metrics_{arr_name}"
                      f"[{count}][6] = {{\n")
            for ch in range(first, last + 1):
                vals = chars.get(ch, [0, 0, 0, 0, 0, 0])
                hex_str = ",".join(f"0x{v:02x}" for v in vals)
                out.write(f"\t{{ {hex_str} }}, /* 0x{ch:02X} */\n")
            out.write("};\n\n")

        out.write("const uint8_t *get_lq500_sec_metrics(int base, uint8_t ch)\n")
        out.write("{\n")
        for i, (base_name, chars) in enumerate(sorted(sec_metrics.items())):
            arr_name = base_name.replace("-", "_")
            codes = sorted(chars.keys())
            if not codes:
                continue
            first = codes[0]
            last = codes[-1]
            out.write(f"\tif (base == {i} && ch >= 0x{first:02X} "
                      f"&& ch <= 0x{last:02X})\n")
            out.write(f"\t\treturn lq500_sec_metrics_{arr_name}"
                      f"[ch - 0x{first:02X}];\n")
        out.write("\treturn nullptr;\n")
        out.write("}\n")

    print(f"\nGenerated {out_path} "
          f"({os.path.getsize(out_path):,d} bytes)")


if __name__ == "__main__":
    main()
