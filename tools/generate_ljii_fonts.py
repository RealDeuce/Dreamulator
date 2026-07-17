#!/usr/bin/env python3
"""Generate LaserJet II resident and font-cartridge glyph tables."""

from __future__ import annotations

import hashlib
import json
import sys
from pathlib import Path


CARTRIDGES = (
    {
        "id": 1,
        "key": "c2053a-c06",
        "label": "HP C2053A #C06 Bar Codes & More",
        "source": "../ljII/generated/roms/cartridges/c2053a-c06/fonts.json",
        "sha256": "276c76721fb5967014393b1f95bb81cdfdbc4c570367c4005bf509728e6405fa",
        "aliases": (
            "c2053a",
            "c2053a#c06",
            "c2053a #c06",
            "bar codes & more",
            "bar-codes-and-more",
        ),
    },
    {
        "id": 2,
        "key": "92286pc",
        "label": "HP 92286PC ProCollection",
        "source": "../ljII/generated/roms/cartridges/92286pc/fonts.json",
        "sha256": "3cb93d9b474f2fc96d307521248c119603dddd5220f34c4ea57f7546079861f6",
        "aliases": (
            "92286pc procollection",
            "procollection",
            "pro-collection",
        ),
    },
)


def u16(data: bytes, offset: int) -> int:
    return (data[offset] << 8) | data[offset + 1]


def u32(data: bytes, offset: int) -> int:
    return (u16(data, offset) << 16) | u16(data, offset + 2)


def s8(value: int) -> int:
    value &= 0xFF
    return value - 0x100 if value & 0x80 else value


def s16(data: bytes, offset: int) -> int:
    value = u16(data, offset)
    return value - 0x10000 if value & 0x8000 else value


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def packed_font_metric(word_value: int, byte_value: int) -> int:
    return (((word_value & 0xFFFF) << 8) | (byte_value & 0xFF)) & 0xFFFFFFFF


def builtin_pitch(word_0x24: int, byte_0x26: int) -> int:
    packed = packed_font_metric(word_0x24, byte_0x26)
    if packed < 2:
        return 0xFFFF
    return min(0xFFFF, 0x01D4C000 // packed)


def builtin_height(word_0x28: int, byte_0x2a: int) -> int:
    packed = packed_font_metric(word_0x28, byte_0x2a)
    return (packed * 0x00E1) // 0x2580


def source_name(doc: dict) -> str | None:
    source = doc.get("source")
    if isinstance(source, str):
        return source
    if isinstance(source, dict) and isinstance(source.get("path"), str):
        return source["path"]
    return None


def rom_path_for_doc(doc_path: Path, doc: dict) -> Path | None:
    name = source_name(doc)
    if name is None:
        return None
    relative_names = (name, name.removeprefix("generated/"))
    for parent in (doc_path.parent, *doc_path.parents):
        for relative in relative_names:
            candidate = parent / relative
            if candidate.is_file():
                return candidate
    return None


def verify_document_source(doc_path: Path, doc: dict) -> tuple[Path, bytes]:
    rom_path = rom_path_for_doc(doc_path, doc)
    if rom_path is None:
        raise ValueError(f"cannot resolve source ROM for {doc_path}")
    rom = rom_path.read_bytes()
    source = doc.get("source")
    if isinstance(source, dict):
        expected_size = source.get("size_bytes")
        if expected_size is not None and len(rom) != int(expected_size):
            raise ValueError(f"source ROM size mismatch for {doc_path}")
        expected_hash = source.get("sha256")
        if expected_hash is not None and sha256(rom) != expected_hash:
            raise ValueError(f"source ROM hash mismatch for {doc_path}")
    return rom_path, rom


def record_metadata(rom: bytes, base: int) -> tuple[int, ...]:
    return (
        rom[base + 0x20],
        u16(rom, base + 0x22),
        builtin_pitch(u16(rom, base + 0x24), rom[base + 0x26]),
        builtin_height(u16(rom, base + 0x28), rom[base + 0x2A]),
        rom[base + 0x21],
        s8(rom[base + 0x0D]),
        s8(rom[base + 0x30]),
        rom[base + 0x31],
        s16(rom, base + 0x1A),
        rom[base + 0x2F],
        s8(rom[base + 0x30]),
        rom[base + 0x31],
    )


def firmware_render_span(width: int, mode: int) -> int:
    span = (width + 7) // 8 if width else 0
    # IC30/IC13 0x1f38c..0x1f39e pads every odd non-mode-2 row.
    if span & 1 and mode != 2:
        span += 1
    return span


def verified_glyph_payload(rom: bytes, glyph: dict) -> tuple[bytes, int]:
    width = int(glyph["width"])
    rows = int(glyph["rows"])
    mode = int(glyph.get("mode", 0))
    render_span = firmware_render_span(width, mode)
    entry = int(glyph["entry_offset"])
    offset = int(glyph["bitmap_offset"])
    if entry < 0 or entry + 10 > len(rom):
        raise ValueError(f"glyph entry outside resource ROM: 0x{entry:x}")
    if offset != entry + rom[entry + 4]:
        raise ValueError(f"glyph bitmap offset mismatch at 0x{entry:x}")
    rom_metadata = (
        s16(rom, entry),
        s16(rom, entry + 2),
        rom[entry + 5],
        u16(rom, entry + 6),
        u16(rom, entry + 8),
    )
    metadata = (
        int(glyph["x_offset"]),
        int(glyph["y_offset"]),
        mode,
        rows,
        width,
    )
    if metadata != rom_metadata:
        raise ValueError(
            f"glyph metadata mismatch at 0x{entry:x}: {metadata} != {rom_metadata}"
        )
    length = rows * max(render_span, 1)
    if offset < 0 or offset + length > len(rom):
        raise ValueError(f"glyph bitmap outside resource ROM: 0x{offset:x}")
    payload = rom[offset : offset + length]
    documented_payload = glyph.get("payload_hex")
    documented_span = glyph.get("render_span")
    if documented_payload is not None:
        extracted = bytes.fromhex(documented_payload)
        expected_hash = glyph.get("payload_sha256")
        if expected_hash is not None and sha256(extracted) != expected_hash:
            raise ValueError(f"glyph payload hash mismatch at 0x{entry:x}")
        if rom[offset : offset + len(extracted)] != extracted:
            raise ValueError(f"glyph extracted bytes mismatch at 0x{entry:x}")
        if documented_span is not None and int(documented_span) == render_span:
            if extracted != payload:
                raise ValueError(f"glyph payload mismatch at 0x{entry:x}")
        else:
            raw_span = (width + 7) // 8 if width else 0
            if documented_span is None or int(documented_span) != raw_span or \
               render_span != raw_span + 1 or len(extracted) != rows * raw_span:
                raise ValueError(f"unsupported glyph span correction at 0x{entry:x}")
    return payload, render_span


def append_glyph(data: bytearray, glyphs: list[tuple[int, ...]],
                 record_index: int, host_byte: int, width: int, advance: int,
                 x_offset: int, y_offset: int, rows: int, render_span: int,
                 payload: bytes) -> None:
    offset = len(data)
    data.extend(payload)
    glyphs.append(
        (
            record_index,
            host_byte,
            width,
            advance,
            x_offset,
            y_offset,
            rows,
            render_span,
            offset,
            len(payload),
        )
    )


def append_resident_fonts(source: Path, data: bytearray,
                          records: list[tuple[int, ...]],
                          glyphs: list[tuple[int, ...]]) -> None:
    doc = json.loads(source.read_text(encoding="utf-8"))
    _, rom = verify_document_source(source, doc)
    for record in doc["records"]:
        record_index = len(records)
        first_glyph = len(glyphs)
        for glyph in record["glyphs"]:
            payload, render_span = verified_glyph_payload(rom, glyph)
            append_glyph(
                data, glyphs, record_index,
                int(glyph["host_byte"]), int(glyph["width"]),
                int(glyph["width"]), int(glyph["x_offset"]),
                int(glyph["y_offset"]), int(glyph["rows"]), render_span,
                payload,
            )
        base = int(record["record_start"])
        records.append(
            (
                0,
                int(record["context_longword"]),
                1,
                first_glyph,
                len(glyphs) - first_glyph,
                *record_metadata(rom, base),
            )
        )


def verify_offset_selection_fields(record: dict, header: bytes) -> None:
    fields = record["selection_fields"]
    expected = {
        "byte_0x0c": header[0x0C],
        "byte_0x0d": s8(header[0x0D]),
        "first_char_0x0e": u16(header, 0x0E),
        "last_char_0x10": u16(header, 0x10),
        "word_0x12": u16(header, 0x12),
        "word_0x14": u16(header, 0x14),
        "word_0x1c": u16(header, 0x1C),
        "word_0x1e": u16(header, 0x1E),
        "class_0x20": header[0x20],
        "spacing_0x21": header[0x21],
        "symbol_0x22": u16(header, 0x22),
        "pitch_word_0x24": u16(header, 0x24),
        "pitch_byte_0x26": header[0x26],
        "height_word_0x28": u16(header, 0x28),
        "height_byte_0x2a": header[0x2A],
        "byte_0x2f": header[0x2F],
        "byte_0x30": s8(header[0x30]),
        "byte_0x31": header[0x31],
        "byte_0x3c": header[0x3C],
    }
    for name, value in expected.items():
        if int(fields[name]) != value:
            raise ValueError(f"selection field {name} does not match header")


def append_offset_cartridge_fonts(spec: dict, doc: dict, rom: bytes,
                                  data: bytearray,
                                  records: list[tuple[int, ...]],
                                  glyphs: list[tuple[int, ...]],
                                  default_fonts: list[tuple]) -> None:
    for record in doc["records"]:
        base = int(record["record_offset"])
        length = int(record["record_length"])
        if base < 0 or base + length > len(rom):
            raise ValueError(f"cartridge record outside ROM at 0x{base:x}")
        if sha256(rom[base : base + length]) != record["record_sha256"]:
            raise ValueError(f"cartridge record hash mismatch at 0x{base:x}")
        header = bytes.fromhex(record["header_hex"])
        if len(header) <= 0x3C or rom[base : base + len(header)] != header:
            raise ValueError(f"cartridge record header mismatch at 0x{base:x}")
        verify_offset_selection_fields(record, header)
        decoded = record["decoded_metrics"]
        metadata = record_metadata(rom, base)
        if metadata[2] != int(decoded["pitch_13b76"]) or \
           metadata[3] != int(decoded["height_13bca"]):
            raise ValueError(f"cartridge decoded metrics mismatch at 0x{base:x}")

        record_index = len(records)
        first_glyph = len(glyphs)
        for glyph in record["glyph_slots"]:
            status = glyph["status"]
            if status == "absent":
                continue
            if status != "extracted":
                raise ValueError(
                    f"unsupported cartridge glyph status {status!r} at 0x{base:x}"
                )
            if int(glyph["entry_offset"]) != base + int(glyph["relative_offset"]):
                raise ValueError(f"cartridge glyph relative offset mismatch at 0x{base:x}")
            payload, render_span = verified_glyph_payload(rom, glyph)
            append_glyph(
                data, glyphs, record_index,
                int(glyph["host_byte"]), int(glyph["width"]),
                int(glyph["width"]), int(glyph["x_offset"]),
                int(glyph["y_offset"]), int(glyph["rows"]), render_span,
                payload,
            )
        records.append(
            (
                int(spec["id"]),
                base,
                1,
                first_glyph,
                len(glyphs) - first_glyph,
                *metadata,
            )
        )
        append_default_font(default_fonts, spec, record, base, True, metadata)


FIXED_SYMBOL_TABLE = {
    0x85: 0x0001, 0x8D: 0x0002, 0x8B: 0x0003, 0x45: 0x0004,
    0x8E: 0x0005, 0x41: 0x0025, 0x52: 0x0006, 0x4B: 0x0007,
    0x87: 0x0008, 0x59: 0x0009, 0x4A: 0x000B, 0x49: 0x002B,
    0x8C: 0x002C, 0x88: 0x000F, 0x89: 0x002F, 0x86: 0x0010,
    0x8A: 0x0012, 0x4E: 0x0032, 0x43: 0x0013, 0x5A: 0x0033,
    0x42: 0x0015, 0x8F: 0x00B3, 0x5F: 0x0016, 0x84: 0x001A,
    0x80: 0x010B, 0x83: 0x010C, 0x82: 0x010D, 0x81: 0x0115,
}


def fixed_symbol_set(encoded: int) -> int:
    table_value = FIXED_SYMBOL_TABLE.get(encoded)
    if table_value is not None:
        return table_value
    high = encoded & 0xF0
    suffix = {0xD0: 0x11, 0xE0: 0x15, 0xC0: 0x18}.get(high)
    if suffix is None:
        raise ValueError(f"unsupported fixed-record symbol byte 0x{encoded:02x}")
    return ((encoded & 0x0F) << 5) + suffix


def verify_fixed_selection_fields(record: dict, header: bytes) -> None:
    fields = record["selection_fields"]
    expected = {
        "extension_flag_0x0e": header[0x0E],
        "byte_0x0f": header[0x0F],
        "byte_0x10": header[0x10],
        "byte_0x11": header[0x11],
        "byte_0x16": header[0x16],
        "symbol_set_0x17": header[0x17],
        "typeface_0x18": header[0x18],
        "spacing_0x19": header[0x19],
        "pitch_0x1a": u16(header, 0x1A),
        "word_0x1c": u16(header, 0x1C),
        "height_0x20": u16(header, 0x20),
        "byte_0x26": header[0x26],
        "byte_0x27_signed": s8(header[0x27]),
        "next_record_delta_0x2e": u32(header, 0x2E),
    }
    for name, value in expected.items():
        if int(fields[name]) != value:
            raise ValueError(f"fixed selection field {name} does not match header")


def fixed_glyph_entry(rom: bytes, glyph: dict) -> tuple[int, ...]:
    entry = int(glyph["entry_offset"])
    entry_bytes = bytes.fromhex(glyph["entry_hex"])
    if len(entry_bytes) != 8 or rom[entry : entry + 8] != entry_bytes:
        raise ValueError(f"fixed glyph entry mismatch at 0x{entry:x}")
    span = entry_bytes[0]
    rows = entry_bytes[1]
    vertical = s8(entry_bytes[2])
    advance = entry_bytes[3]
    raw_relative = u32(entry_bytes, 4)
    relative = raw_relative & 0x00FFFFFF
    metadata = (
        span, rows, vertical, advance, raw_relative, relative,
        int(glyph["span_bytes"]), int(glyph["rows"]),
        int(glyph["byte_0x02_signed"]), int(glyph["byte_0x03"]),
        int(glyph["bitmap_relative_raw"]),
        int(glyph["bitmap_relative_24"]),
    )
    if metadata[:6] != metadata[6:]:
        raise ValueError(f"fixed glyph metadata mismatch at 0x{entry:x}")
    return span, rows, vertical, advance, relative


def fixed_glyph_payload(rom: bytes, record_base: int, bank_start: int,
                        glyph: dict) -> tuple[bytes, int, int, int, int, int]:
    span, rows, vertical, advance, relative = fixed_glyph_entry(rom, glyph)
    entry = int(glyph["entry_offset"])
    bitmap = record_base + relative
    length = span * rows
    bank_end = bank_start + 0x40000
    if bitmap < bank_start or bitmap + length > bank_end:
        raise ValueError(f"fixed glyph bitmap outside bank at 0x{entry:x}")
    payload = rom[bitmap : bitmap + length]
    extracted = bytes.fromhex(glyph["payload_hex"])
    if extracted != payload or int(glyph["payload_length"]) != length or \
       sha256(extracted) != glyph["payload_sha256"]:
        raise ValueError(f"fixed glyph payload mismatch at 0x{entry:x}")
    layout = glyph["payload_layout"]
    expected_layout = "split-last-byte-plane" if span > 1 and span & 1 \
        else "linear-rows"
    if layout != expected_layout:
        raise ValueError(f"fixed glyph layout mismatch at 0x{entry:x}")
    if layout == "split-last-byte-plane":
        prefix_span = span - 1
        trailing = prefix_span * rows
        payload = b"".join(
            payload[row * prefix_span : (row + 1) * prefix_span] +
            payload[trailing + row : trailing + row + 1]
            for row in range(rows)
        )
    return payload, span, rows, vertical, advance, span * 8


def append_fixed_cartridge_fonts(spec: dict, doc: dict, rom: bytes,
                                 data: bytearray,
                                 records: list[tuple[int, ...]],
                                 glyphs: list[tuple[int, ...]],
                                 default_fonts: list[tuple]) -> None:
    if doc.get("resource_format") != "fixed-FONT-chain":
        raise ValueError(f"unsupported fixed cartridge format for {spec['key']}")
    records_in = doc["records"]
    if len(records_in) != int(doc["summary"]["records"]):
        raise ValueError(f"fixed cartridge record count mismatch for {spec['key']}")
    for record_position, record in enumerate(records_in):
        base = int(record["record_offset"])
        length = int(record["record_length"])
        bank_index = int(record["bank_index"])
        bank_start = bank_index * 0x40000
        if bank_start < 0 or bank_start + 0x40000 > len(rom) or \
           base < bank_start or base + length > bank_start + 0x40000:
            raise ValueError(f"fixed cartridge record outside bank at 0x{base:x}")
        if sha256(rom[base : base + length]) != record["record_sha256"]:
            raise ValueError(f"fixed cartridge record hash mismatch at 0x{base:x}")
        header = bytes.fromhex(record["header_hex"])
        if len(header) != 0x40 or header[:4] != b"FONT" or \
           rom[base : base + len(header)] != header:
            raise ValueError(f"fixed cartridge record header mismatch at 0x{base:x}")
        if record.get("record_type_ascii") != "FONT" or u32(header, 0x2E) != length:
            raise ValueError(f"fixed cartridge record type/length mismatch at 0x{base:x}")
        name = record["name"]
        if bytes.fromhex(name["bytes_hex"]) != header[4:14]:
            raise ValueError(f"fixed cartridge record name mismatch at 0x{base:x}")
        verify_fixed_selection_fields(record, header)

        fields = record["selection_fields"]
        class_id = int(fields["byte_0x16"])
        if class_id not in (0, 1):
            raise ValueError(f"unsupported fixed orientation at 0x{base:x}")
        if int(fields["extension_flag_0x0e"]) != 0 or \
           len(record["glyph_slots"]) != 96:
            raise ValueError(f"unsupported fixed glyph table at 0x{base:x}")
        if record_position + 1 < len(records_in):
            next_record = records_in[record_position + 1]
            if int(next_record["bank_index"]) == bank_index and \
               base + length != int(next_record["record_offset"]):
                raise ValueError(f"broken fixed record chain at 0x{base:x}")
        record_index = len(records)
        first_glyph = len(glyphs)
        for glyph_index, glyph in enumerate(record["glyph_slots"]):
            entry = base + 0x40 + glyph_index * 8
            if int(glyph["entry_offset"]) != entry:
                raise ValueError(f"fixed glyph table offset mismatch at 0x{entry:x}")
            expected_host = 0x20 + glyph_index if glyph_index < 96 \
                else 0xA0 + glyph_index - 96
            if int(glyph["host_byte"]) != expected_host or \
               int(glyph["glyph_index"]) != glyph_index:
                raise ValueError(f"fixed glyph mapping mismatch at 0x{entry:x}")
            status = glyph["status"]
            fixed_glyph_entry(rom, glyph)
            if status == "absent":
                if "payload_hex" in glyph or "payload_length" in glyph:
                    raise ValueError(f"absent fixed glyph has payload at 0x{entry:x}")
                continue
            if status != "extracted":
                raise ValueError(f"unsupported fixed glyph status {status!r}")
            payload, span, rows, vertical, advance, width = fixed_glyph_payload(
                rom, base, bank_start, glyph)
            if class_id == 0:
                x_offset = 0
                y_offset = rows - vertical - 1
            else:
                x_offset = vertical + 1 - width
                y_offset = rows - 1
            append_glyph(
                data, glyphs, record_index, expected_host, width, advance,
                x_offset, y_offset, rows, span, payload,
            )

        metadata = (
            class_id,
            fixed_symbol_set(int(fields["symbol_set_0x17"])),
            int(fields["pitch_0x1a"]),
            int(fields["height_0x20"]),
            int(fields["spacing_0x19"]),
            int(fields["byte_0x26"]),
            int(fields["byte_0x27_signed"]),
            int(fields["typeface_0x18"]),
            -5,
            int(fields["byte_0x26"]),
            int(fields["byte_0x27_signed"]),
            int(fields["typeface_0x18"]),
        )
        records.append(
            (
                int(spec["id"]), base, 0, first_glyph,
                len(glyphs) - first_glyph, *metadata,
            )
        )
        append_default_font(default_fonts, spec, record, base, False, metadata)


def append_cartridge_fonts(root: Path, spec: dict, data: bytearray,
                           records: list[tuple[int, ...]],
                           glyphs: list[tuple[int, ...]],
                           default_fonts: list[tuple]) -> None:
    source = (root / spec["source"]).resolve()
    source_bytes = source.read_bytes()
    if sha256(source_bytes) != spec["sha256"]:
        raise ValueError(f"cartridge extraction hash mismatch for {spec['key']}")
    doc = json.loads(source_bytes.decode("utf-8"))
    if doc.get("schema") != "hp-laserjet-resource-fonts-v1":
        raise ValueError(f"unsupported cartridge schema for {spec['key']}")
    _, rom = verify_document_source(source, doc)
    resource_format = doc.get("resource_format")
    if resource_format == "fixed-FONT-chain":
        append_fixed_cartridge_fonts(
            spec, doc, rom, data, records, glyphs, default_fonts)
    elif resource_format is None:
        append_offset_cartridge_fonts(
            spec, doc, rom, data, records, glyphs, default_fonts)
    else:
        raise ValueError(
            f"unsupported cartridge format {resource_format!r} for {spec['key']}"
        )


FONT_NAMES = {
    "TmsRmn": "Times Roman",
    "LtrGothic": "Letter Gothic",
    "Pres Elite": "Prestige Elite",
    "Helv": "Helvetica",
    "Line Print": "Line Printer",
}

PCL_SYMBOL_NAMES = {
    0x0002: "Line Drawing",
    0x000F: "OCR-A",
    0x0015: "US-ASCII",
    0x0019: "Code 39",
    0x002F: "OCR-B",
    0x0035: "US Legal",
    0x0115: "Roman-8",
    0x0119: "UPC/EAN",
    0x01F9: "USPS ZIP",
}


def pcl_symbol_name(value: int) -> str:
    suffix = value % 32
    if suffix > 30:
        raise ValueError(f"invalid PCL symbol-set word 0x{value:04x}")
    return f"{value // 32}{chr(ord('@') + suffix)}"


def decimal_metric(value: int) -> str:
    return f"{value / 100:.2f}".rstrip("0").rstrip(".")


def cartridge_font_family(record: dict) -> str:
    raw_name = record.get("name", {}).get("text", "Font")
    return FONT_NAMES.get(raw_name, raw_name)


def cartridge_font_label(record: dict, metadata: tuple[int, ...]) -> str:
    class_id, symbol, pitch, height, spacing, style, stroke, _typeface = metadata[:8]
    del class_id
    name = cartridge_font_family(record)
    size = f"{decimal_metric(height)} pt"
    if spacing == 0:
        size = f"{decimal_metric(pitch)} CPI / {size}"
    style_name = "Upright" if style == 0 else "Italic" if style == 1 \
        else f"Style {style}"
    weight_name = "Medium" if stroke == 0 else "Bold" if stroke >= 3 \
        else "Light" if stroke < 0 else f"Weight {stroke}"
    symbol_name = PCL_SYMBOL_NAMES.get(symbol)
    if symbol_name is None:
        raise ValueError(
            f"no display name for cartridge symbol set {pcl_symbol_name(symbol)}"
        )
    return f"{name} {size} {style_name} {weight_name}, {symbol_name}"


def append_default_font(default_fonts: list[tuple], spec: dict, record: dict,
                        context: int, offset_table: bool,
                        metadata: tuple[int, ...]) -> None:
    cartridge = int(spec["id"])
    if cartridge <= 0 or cartridge > 0x3ff or \
       context < 0 or context >= 0x200000:
        raise ValueError(f"default font ID cannot encode {spec['key']} 0x{context:x}")
    font_id = cartridge << 21 | context
    class_id, symbol, pitch, height, spacing, style, stroke, typeface = metadata[:8]
    default_fonts.append(
        (
            font_id, f"{spec['key']}:{context:06x}",
            cartridge_font_label(record, metadata),
            cartridge_font_family(record), class_id,
            symbol, pitch, height, spacing, style, stroke, typeface,
            cartridge, context, offset_table,
        )
    )


def c_array(name: str, data: bytes) -> str:
    lines = [f"static constexpr uint8_t {name}[] = {{"]
    for i in range(0, len(data), 16):
        chunk = data[i : i + 16]
        lines.append("\t" + ", ".join(f"0x{byte:02x}" for byte in chunk) + ",")
    lines.append("};")
    return "\n".join(lines)


def c_string(value: str) -> str:
    return json.dumps(value, ensure_ascii=True)


def generated_logic() -> str:
    return r'''
uint32_t default_ljii_context_for_pitch(float pitch_cpi, int symbol_set, int class_id)
{
	if (pitch_cpi > 14.0f) {
		if (class_id) {
			if (symbol_set == 0x0155) return 0x440ad87au;
			if (symbol_set == 0x0175) return 0x440adcceu;
			if (symbol_set == 0x000e) return 0x400ae122u;
			return 0x400ad4aau;
		}
		if (symbol_set == 0x0155) return 0x440946b4u;
		if (symbol_set == 0x0175) return 0x44094b08u;
		if (symbol_set == 0x000e) return 0x40094f5cu;
		return 0x400942e4u;
	}
	if (class_id) {
		if (symbol_set == 0x0155) return 0x440a3850u;
		if (symbol_set == 0x0175) return 0x440a3ca0u;
		if (symbol_set == 0x000e) return 0x400a40f0u;
		return 0x400a3484u;
	}
	if (symbol_set == 0x0155) return 0x4408a37cu;
	if (symbol_set == 0x0175) return 0x4408a7ccu;
	if (symbol_set == 0x000e) return 0x4008ac1cu;
	return 0x40089fb0u;
}

static int ljii_abs(int value)
{
	return value < 0 ? -value : value;
}

static bool ljii_case_equal(const char *left, const char *right)
{
	if (!left || !right) return false;
	while (*left && *right) {
		unsigned char a = (unsigned char)*left++;
		unsigned char b = (unsigned char)*right++;
		if (std::tolower(a) != std::tolower(b)) return false;
	}
	return *left == *right;
}

size_t ljii_cartridge_count()
{
	return sizeof(ljii_cartridges) / sizeof(ljii_cartridges[0]);
}

const LjiiCartridgeInfo *ljii_cartridge_info(size_t index)
{
	return index < ljii_cartridge_count() ? &ljii_cartridges[index] : nullptr;
}

const LjiiCartridgeInfo *find_ljii_cartridge(int id)
{
	for (const auto &cartridge : ljii_cartridges)
		if (cartridge.id == id) return &cartridge;
	return nullptr;
}

bool ljii_valid_cartridge(int id)
{
	return find_ljii_cartridge(id) != nullptr;
}

int parse_ljii_cartridge(const char *value)
{
	if (!value || !*value || ljii_case_equal(value, "none") ||
	    ljii_case_equal(value, "empty"))
		return 0;
	for (const auto &cartridge : ljii_cartridges)
		if (ljii_case_equal(value, cartridge.key) ||
		    ljii_case_equal(value, cartridge.label))
			return cartridge.id;
'''


def generated_logic_tail() -> str:
    return r'''
	return -1;
}

size_t ljii_default_font_count()
{
	return sizeof(ljii_default_fonts) / sizeof(ljii_default_fonts[0]);
}

const LjiiDefaultFontInfo *ljii_default_font_info(size_t index)
{
	return index < ljii_default_font_count() ? &ljii_default_fonts[index] : nullptr;
}

const LjiiDefaultFontInfo *find_ljii_default_font(int id)
{
	for (const auto &font : ljii_default_fonts)
		if (font.id == id) return &font;
	return nullptr;
}

int parse_ljii_default_font(const char *value)
{
	if (!value) return -1;
	if (ljii_case_equal(value, "courier") ||
	    ljii_case_equal(value, "courier-medium")) return 0;
	if (ljii_case_equal(value, "courier-bold") ||
	    ljii_case_equal(value, "courier_bold") ||
	    ljii_case_equal(value, "bold")) return 1;
	if (ljii_case_equal(value, "line-printer") ||
	    ljii_case_equal(value, "line_printer") ||
	    ljii_case_equal(value, "lineprinter")) return 2;
	for (const auto &font : ljii_default_fonts)
		if (ljii_case_equal(value, font.key)) return font.id;
	return -1;
}

bool ljii_default_font_available(int id, LjiiCartridgeSlots cartridges,
	                              int orientation)
{
	const auto *font = find_ljii_default_font(id);
	if (!font || (font->orientation >= 0 && font->orientation != (orientation & 1)))
		return false;
	int cartridge = font->request.exact_cartridge;
	return cartridge == 0 || cartridges.slot[0] == cartridge ||
	       cartridges.slot[1] == cartridge;
}

struct LjiiCandidate {
	const LjiiRecordEntry *record;
	uint32_t context;
	uint8_t resource_class;
};

static uint32_t cartridge_context(int slot, const LjiiRecordEntry &record)
{
	uint32_t form = record.offset_table ? 0x40000000u : 0;
	return form | (slot == 0 ? 0x00200000u : 0x00400000u) | record.context;
}

static void append_ljii_candidates(std::vector<LjiiCandidate> &out,
	                               uint8_t class_id,
	                               LjiiCartridgeSlots cartridges)
{
	for (const auto &record : ljii_records)
		if (record.cartridge == 0 && record.class_id == class_id)
			out.push_back({ &record, record.context, 0 });
	for (int slot = 0; slot < 2; slot++) {
		int id = cartridges.slot[slot];
		if (id == 0) continue;
		for (const auto &record : ljii_records)
			if (record.cartridge == id && record.class_id == class_id)
				out.push_back({ &record, cartridge_context(slot, record), 1 });
	}
}

template <typename Predicate>
static std::vector<LjiiCandidate> filter_ljii_candidates(
	const std::vector<LjiiCandidate> &candidates, Predicate predicate)
{
	std::vector<LjiiCandidate> filtered;
	for (const auto &candidate : candidates)
		if (predicate(*candidate.record)) filtered.push_back(candidate);
	return filtered;
}

static void nearest_pitch(std::vector<LjiiCandidate> &candidates, int requested)
{
	auto exact = filter_ljii_candidates(candidates, [requested](const auto &record) {
		return ljii_abs((int)record.pitch - requested) <= 5;
	});
	if (!exact.empty()) {
		candidates = std::move(exact);
		return;
	}
	int best_above = 0x7fffffff;
	int best_below = -1;
	for (const auto &candidate : candidates) {
		int pitch = candidate.record->pitch;
		if (pitch >= requested) best_above = std::min(best_above, pitch);
		else best_below = std::max(best_below, pitch);
	}
	int chosen = best_above != 0x7fffffff ? best_above : best_below;
	candidates = filter_ljii_candidates(candidates, [chosen](const auto &record) {
		return (int)record.pitch == chosen;
	});
}

static void nearest_height(std::vector<LjiiCandidate> &candidates, int requested)
{
	auto exact = filter_ljii_candidates(candidates, [requested](const auto &record) {
		return ljii_abs((int)record.height - requested) <= 25;
	});
	if (!exact.empty()) {
		candidates = std::move(exact);
		return;
	}
	int best_diff = 0x7fffffff;
	for (const auto &candidate : candidates)
		best_diff = std::min(best_diff,
		                     ljii_abs((int)candidate.record->height - requested));
	candidates = filter_ljii_candidates(
		candidates, [requested, best_diff](const auto &record) {
			return ljii_abs((int)record.height - requested) == best_diff;
		});
}

static bool ljii_better_candidate(const LjiiCandidate &candidate,
	                              const LjiiCandidate &best)
{
	if (candidate.resource_class != best.resource_class)
		return candidate.resource_class > best.resource_class;
	const auto &left = *candidate.record;
	const auto &right = *best.record;
	if (left.height != right.height) return left.height > right.height;
	if (left.tie_a != right.tie_a) return left.tie_a > right.tie_a;
	if (left.tie_b != right.tie_b) return left.tie_b > right.tie_b;
	return left.tie_c > right.tie_c;
}

uint32_t select_ljii_context(const LjiiFontRequest &request, int orientation,
	                         LjiiCartridgeSlots cartridges)
{
	uint8_t class_id = (uint8_t)(orientation & 1);
	if (request.exact_cartridge != 0) {
		for (int slot = 0; slot < 2; slot++) {
			if (cartridges.slot[slot] != request.exact_cartridge) continue;
			for (const auto &record : ljii_records)
				if (record.cartridge == request.exact_cartridge &&
				    record.context == request.exact_context &&
				    record.offset_table == request.exact_offset_table &&
				    record.class_id == class_id)
					return cartridge_context(slot, record);
		}
	}
	std::vector<LjiiCandidate> candidates;
	append_ljii_candidates(candidates, class_id, cartridges);
	auto symbol = filter_ljii_candidates(candidates, [&request](const auto &record) {
		return record.symbol == request.symbol_set;
	});
	if (symbol.empty())
		symbol = filter_ljii_candidates(candidates, [&request](const auto &record) {
			return record.symbol == (request.secondary ? 0x000e : 0x0115);
		});
	if (!symbol.empty()) candidates = std::move(symbol);

	auto spacing = filter_ljii_candidates(candidates, [&request](const auto &record) {
		return record.spacing == request.spacing;
	});
	if (request.spacing == 1) {
		if (!spacing.empty()) candidates = std::move(spacing);
	} else if (!spacing.empty()) {
		candidates = std::move(spacing);
		nearest_pitch(candidates, request.pitch);
	}
	nearest_height(candidates, request.height);

	auto style = filter_ljii_candidates(candidates, [&request](const auto &record) {
		return record.style == request.style;
	});
	if (!style.empty()) candidates = std::move(style);
	auto stroke = filter_ljii_candidates(candidates, [&request](const auto &record) {
		return request.stroke >= 3 ? record.stroke >= 3 :
		                             record.stroke == request.stroke;
	});
	if (!stroke.empty()) candidates = std::move(stroke);
	auto typeface = filter_ljii_candidates(candidates, [&request](const auto &record) {
		return record.typeface == request.typeface;
	});
	if (!typeface.empty()) candidates = std::move(typeface);

	const LjiiCandidate *best = nullptr;
	for (const auto &candidate : candidates)
		if (!best || ljii_better_candidate(candidate, *best)) best = &candidate;
	return best ? best->context : default_ljii_context_for_pitch(
		(float)request.pitch / 100.0f, request.symbol_set, class_id);
}

static const LjiiRecordEntry *find_ljii_record(uint32_t context,
	                                          LjiiCartridgeSlots cartridges)
{
	for (const auto &record : ljii_records)
		if (record.cartridge == 0 && record.context == context) return &record;
	uint32_t address = context & 0x00ffffffu;
	for (int slot = 0; slot < 2; slot++) {
		uint32_t base = slot == 0 ? 0x00200000u : 0x00400000u;
		if (address < base || address >= base + 0x00200000u) continue;
		uint32_t relative = address - base;
		for (const auto &record : ljii_records)
			if (record.cartridge == cartridges.slot[slot] &&
			    record.context == relative &&
			    record.offset_table == ((context & 0x40000000u) != 0))
				return &record;
	}
	return nullptr;
}

int ljii_context_pitch(uint32_t context, LjiiCartridgeSlots cartridges)
{
	const auto *record = find_ljii_record(context, cartridges);
	return record ? record->pitch : 0;
}

LjiiFontMetrics get_ljii_font_metrics(uint32_t context,
	                                  LjiiCartridgeSlots cartridges)
{
	const auto *record = find_ljii_record(context, cartridges);
	if (!record) return {};
	return { true, record->class_id, record->symbol, record->pitch,
	         record->height, record->spacing, record->style, record->stroke,
	         record->typeface, record->underline_distance };
}

LjiiGlyphInfo get_ljii_glyph(uint32_t context, uint8_t host_byte,
	                         LjiiCartridgeSlots cartridges)
{
	const auto *record = find_ljii_record(context, cartridges);
	if (!record) return {};
	for (uint32_t i = 0; i < record->count; i++) {
		const auto &glyph = ljii_glyphs[record->first + i];
		if (glyph.host != host_byte) continue;
		return { true, glyph.width, glyph.advance, glyph.x_offset, glyph.y_offset,
		         glyph.rows, glyph.span, ljii_glyph_data + glyph.offset,
		         glyph.len };
	}
	return {};
}
'''


def main() -> int:
    if len(sys.argv) != 3:
        print(
            "Usage: tools/generate_ljii_fonts.py "
            "<../ljII/generated/analysis/ic32_ic15_builtin_glyph_payloads.json> "
            "<src/print/fontljii.cpp>",
            file=sys.stderr,
        )
        return 2

    source = Path(sys.argv[1]).resolve()
    output = Path(sys.argv[2])
    root = Path(__file__).resolve().parents[1]
    data = bytearray()
    records: list[tuple[int, ...]] = []
    glyphs: list[tuple[int, ...]] = []
    default_fonts: list[tuple] = []
    append_resident_fonts(source, data, records, glyphs)
    for cartridge in CARTRIDGES:
        append_cartridge_fonts(
            root, cartridge, data, records, glyphs, default_fonts)
    if len({font[0] for font in default_fonts}) != len(default_fonts):
        raise ValueError("duplicate cartridge default-font ID")
    if len({font[1] for font in default_fonts}) != len(default_fonts):
        raise ValueError("duplicate cartridge default-font key")
    labels = [(font[12], font[4], font[2]) for font in default_fonts]
    if len(set(labels)) != len(labels):
        raise ValueError("duplicate cartridge default-font menu label")

    out = [
        "// Generated by tools/generate_ljii_fonts.py -- do not edit",
        "// Resident source: " + sys.argv[1],
    ]
    for cartridge in CARTRIDGES:
        out.append(f"// Cartridge source: {cartridge['source']}")
    out.extend(
        [
            '#include "fontljii.h"',
            "#include <algorithm>",
            "#include <cctype>",
            "#include <utility>",
            "#include <vector>",
            "",
            "struct LjiiRecordEntry {",
            "\tuint16_t cartridge; uint32_t context; uint8_t offset_table;",
            "\tuint32_t first; uint16_t count;",
            "\tuint8_t class_id; uint16_t symbol; uint16_t pitch; uint16_t height;",
            "\tuint8_t spacing; int8_t style; int8_t stroke; uint8_t typeface;",
            "\tint16_t underline_distance;",
            "\tuint8_t tie_a; int8_t tie_b; uint8_t tie_c;",
            "};",
            "struct LjiiGlyphEntry {",
            "\tuint16_t record; uint8_t host; uint16_t width; uint16_t advance;",
            "\tint16_t x_offset;",
            "\tint16_t y_offset; uint16_t rows; uint16_t span; uint32_t offset; uint16_t len;",
            "};",
            "",
            c_array("ljii_glyph_data", bytes(data)),
            "",
            "static constexpr LjiiRecordEntry ljii_records[] = {",
        ]
    )
    for (cartridge, context, offset_table, first, count, class_id, symbol,
         pitch, height,
         spacing, style, stroke, typeface, underline_distance, tie_a, tie_b,
         tie_c) in records:
        out.append(
            f"\t{{ {cartridge}u, 0x{context:08x}u, {offset_table}u, "
            f"{first}u, {count}u, "
            f"{class_id}u, 0x{symbol:04x}u, {pitch}u, {height}u, {spacing}u, "
            f"{style}, {stroke}, {typeface}u, {underline_distance}, "
            f"{tie_a}u, {tie_b}, {tie_c}u }},"
        )
    out.extend(["};", "", "static constexpr LjiiGlyphEntry ljii_glyphs[] = {"])
    for (record, host, width, advance, xoff, yoff, rows, span, offset,
         length) in glyphs:
        out.append(
            f"\t{{ {record}u, 0x{host:02x}u, {width}u, {advance}u, "
            f"{xoff}, {yoff}, "
            f"{rows}u, {span}u, {offset}u, {length}u }},"
        )
    out.extend(["};", "", "static constexpr LjiiCartridgeInfo ljii_cartridges[] = {",
                '\t{ 0, "none", "Empty" },'])
    for cartridge in CARTRIDGES:
        out.append(
            f"\t{{ {cartridge['id']}, {c_string(cartridge['key'])}, "
            f"{c_string(cartridge['label'])} }},"
        )
    out.extend(
        [
            "};",
            "",
            "static constexpr LjiiDefaultFontInfo ljii_default_fonts[] = {",
            '\t{ 0, "courier", "Courier 10 CPI Medium", "Courier", -1, '
            '{ 0x0115, 1000, 1200, 0, 0, 0, 3, false, 0, 0, false } },',
            '\t{ 1, "courier-bold", "Courier 10 CPI Bold", "Courier", -1, '
            '{ 0x0115, 1000, 1200, 0, 0, 3, 3, false, 0, 0, false } },',
            '\t{ 2, "line-printer", "Line Printer 16.66 CPI", "Line Printer", -1, '
            '{ 0x0115, 1666, 850, 0, 0, 0, 0, false, 0, 0, false } },',
        ]
    )
    for (font_id, key, label, family, orientation, symbol, pitch, height,
         spacing, style, stroke, typeface, cartridge, context,
         offset_table) in default_fonts:
        out.append(
            f"\t{{ {font_id}, {c_string(key)}, {c_string(label)}, "
            f"{c_string(family)}, {orientation}, "
            f"{{ 0x{symbol:04x}, {pitch}, {height}, {spacing}, {style}, "
            f"{stroke}, {typeface}, false, {cartridge}, 0x{context:08x}u, "
            f"{'true' if offset_table else 'false'} }} }},"
        )
    out.extend(["};", "", generated_logic().strip("\n")])
    for cartridge in CARTRIDGES:
        for alias in cartridge["aliases"]:
            out.append(
                f"\tif (ljii_case_equal(value, {c_string(alias)})) "
                f"return {cartridge['id']};"
            )
    out.append(generated_logic_tail().strip("\n"))

    output.write_text("\n".join(out) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
