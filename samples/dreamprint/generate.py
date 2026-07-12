#!/usr/bin/env python3
"""Generate dreamprint sample input streams."""

from pathlib import Path
from typing import Iterable


OUT_DIR = Path(__file__).resolve().parent
ESC = b"\x1b"
FF = b"\x0c"


def label(parts: Iterable[str]) -> bytes:
    names = list(parts)
    if not names:
        return b"normal"
    return " + ".join(names).encode("ascii")


def sample_text() -> bytes:
    return b"The quick brown fox jumps 0123456789"


def fx80_master_select(
    bold: bool,
    double_strike: bool,
    expanded: bool,
) -> bytes:
    value = 0
    if bold:
        value |= 0x08
    if double_strike:
        value |= 0x10
    if expanded:
        value |= 0x20
    return ESC + b"!" + bytes([value])


def fx80_text_style(
    bold: bool,
    double_strike: bool,
    italic: bool,
    underline: bool,
    expanded: bool,
) -> bytes:
    return (
        fx80_master_select(bold, double_strike, expanded)
        + (ESC + b"4" if italic else ESC + b"5")
        + ESC + b"-" + (b"\x01" if underline else b"\x00")
    )


def fx80_script(script: str) -> bytes:
    if script == "super":
        return ESC + b"S\x00"
    if script == "sub":
        return ESC + b"S\x01"
    return ESC + b"T"


def fx80_pitch(mode: str) -> bytes:
    if mode == "12cpi":
        return ESC + b"p\x00" + b"\x12" + ESC + b"M"
    if mode == "15cpi":
        return ESC + b"p\x00" + b"\x12" + ESC + b"g"
    if mode == "condensed":
        return ESC + b"p\x00" + ESC + b"P" + b"\x0f"
    if mode == "proportional":
        return ESC + b"p\x01"
    return ESC + b"p\x00" + b"\x12" + ESC + b"P"


def fx80_reset() -> bytes:
    return ESC + b"@"


def fx80_plain() -> bytes:
    return (
        fx80_text_style(False, False, False, False, False)
        + fx80_pitch("10cpi")
        + fx80_script("normal")
    )


def build_fx80() -> bytes:
    out = bytearray()
    out += fx80_reset()
    out += b"FX-80 text attribute matrix\r\n\r\n"
    fonts = [("draft", b"")]
    pitches = [
        ("10cpi", "10cpi"),
        ("12cpi", "12cpi"),
        ("15cpi", "15cpi"),
        ("condensed", "condensed"),
        ("proportional", "proportional"),
    ]
    scripts = [("normal", ""), ("super", "superscript"), ("sub", "subscript")]
    for font_name, font_cmd in fonts:
        for pitch_name, pitch_mode in pitches:
            for script, script_name in scripts:
                for expanded in (False, True):
                    for underline in (False, True):
                        for italic in (False, True):
                            for double_strike in (False, True):
                                for bold in (False, True):
                                    if pitch_mode == "proportional" and (
                                        script != "normal" or double_strike
                                    ):
                                        continue
                                    parts = [font_name]
                                    parts.append(pitch_name)
                                    if bold:
                                        parts.append("bold")
                                    if double_strike:
                                        parts.append("double-strike")
                                    if italic:
                                        parts.append("italic")
                                    if underline:
                                        parts.append("underline")
                                    if expanded:
                                        parts.append("expanded")
                                    if script_name:
                                        parts.append(script_name)

                                    out += fx80_plain() + font_cmd
                                    out += label(parts) + b"\r\n"
                                    out += font_cmd
                                    out += fx80_text_style(
                                        bold, double_strike, italic, underline, expanded
                                    )
                                    out += fx80_script(script)
                                    out += fx80_pitch(pitch_mode)
                                    out += sample_text() + b"\r\n"
                                    out += fx80_plain()
                                    out += b"\r\n"
    out += fx80_plain()
    out += FF
    return bytes(out)


def bj10e_reset() -> bytes:
    return ESC + b"[K" + b"\x01\x00\x00"


def bj10e_text_style(
    bold: bool,
    underline: bool,
    overline: bool,
    double_strike: bool,
    expanded: bool,
    double_high: bool,
) -> bytes:
    out = bytearray()
    out += ESC + (b"E" if bold else b"F")
    out += ESC + (b"G" if double_strike else b"H")
    out += ESC + b"-" + (b"\x01" if underline else b"\x00")
    out += ESC + b"_" + (b"\x01" if overline else b"\x00")
    out += ESC + b"W" + (b"\x01" if expanded else b"\x00")
    height = 0x22 if double_high else 0x11
    width = 0x02 if expanded else 0x01
    out += ESC + b"[@" + b"\x04\x00" + bytes([0x00, 0x00, height, width])
    return bytes(out)


def bj10e_script(script: str) -> bytes:
    if script == "super":
        return ESC + b"S\x00"
    if script == "sub":
        return ESC + b"S\x01"
    return ESC + b"T"


def bj10e_plain() -> bytes:
    return bj10e_text_style(False, False, False, False, False, False) + bj10e_script("normal")


def bj10e_font_pitch(font_mode: str, pitch_mode: str) -> bytes:
    modes = {
        ("economy", "10cpi"): 0x00,
        ("economy", "12cpi"): 0x08,
        ("economy", "condensed"): 0x10,
        ("high-quality", "10cpi"): 0x02,
        ("high-quality", "12cpi"): 0x0A,
        ("high-quality", "condensed"): 0x12,
        ("high-quality", "proportional"): 0x03,
    }
    return ESC + b"I" + bytes([modes[(font_mode, pitch_mode)]])


def build_bj10e() -> bytes:
    out = bytearray()
    out += bj10e_reset()
    out += b"Canon BJ-10e text attribute matrix\r\n\r\n"
    fonts = [
        ("high-quality", "high-quality",
         [("10cpi", "10cpi"), ("12cpi", "12cpi"), ("condensed", "condensed"),
          ("proportional", "proportional")]),
        ("economy", "economy",
         [("10cpi", "10cpi"), ("12cpi", "12cpi"), ("condensed", "condensed")]),
    ]
    for font_name, font_mode, pitches in fonts:
        for pitch_name, pitch_mode in pitches:
            for script, script_name in [("normal", ""), ("super", "superscript"), ("sub", "subscript")]:
                for double_high in (False, True):
                    for expanded in (False, True):
                        for double_strike in (False, True):
                            for overline in (False, True):
                                for underline in (False, True):
                                    for bold in (False, True):
                                        parts = [font_name, pitch_name]
                                        if bold:
                                            parts.append("bold")
                                        if underline:
                                            parts.append("underline")
                                        if overline:
                                            parts.append("overline")
                                        if double_strike:
                                            parts.append("double-strike")
                                        if expanded:
                                            parts.append("double-width")
                                        if double_high:
                                            parts.append("double-height")
                                        if script_name:
                                            parts.append(script_name)

                                        out += bj10e_font_pitch("high-quality", "10cpi") + bj10e_plain()
                                        out += label(parts) + b"\r\n"
                                        out += bj10e_font_pitch(font_mode, pitch_mode)
                                        out += bj10e_text_style(
                                            bold,
                                            underline,
                                            overline,
                                            double_strike,
                                            expanded,
                                            double_high,
                                        )
                                        out += bj10e_script(script)
                                        out += sample_text() + b"\r\n"
                                        out += bj10e_font_pitch("high-quality", "10cpi") + bj10e_plain()
                                        out += b"\r\n"
    out += bj10e_plain()
    out += FF
    return bytes(out)


def iw_reset() -> bytes:
    return ESC + b"c"


def iw_font(mode: str) -> bytes:
    if mode == "nlq":
        return ESC + b"M"
    if mode == "draft":
        return ESC + b"a1"
    return ESC + b"m"


def iw_pitch(mode: str) -> bytes:
    commands = {
        "9cpi": b"n",
        "10cpi": b"N",
        "12cpi": b"E",
        "13.4cpi": b"e",
        "15cpi": b"q",
        "17cpi": b"Q",
        "prop144": b"p",
        "prop160": b"P",
    }
    return ESC + commands[mode]


def iw_text_style(
    bold: bool,
    underline: bool,
    expanded: bool,
    half_height: bool,
    script: str,
) -> bytes:
    out = bytearray()
    out += ESC + (b"!" if bold else b'"')
    out += ESC + (b"X" if underline else b"Y")
    out += b"\x0e" if expanded else b"\x0f"
    out += ESC + (b"w" if half_height else b"W")
    if script == "super":
        out += ESC + b"x"
    elif script == "sub":
        out += ESC + b"y"
    else:
        out += ESC + b"z"
    return bytes(out)


def iw_color(index: int) -> bytes:
    return ESC + b"K" + str(index).encode("ascii")


def iw_lf_when_full(enabled: bool) -> bytes:
    return ESC + (b"D" if enabled else b"Z") + b" \x00"


def build_imagewriter() -> bytes:
    out = bytearray()
    out += iw_reset()
    out += iw_lf_when_full(True)
    out += b"ImageWriter II text attribute matrix\r\n\r\n"
    fixed_pitches = [
        ("9cpi", "9cpi"),
        ("10cpi", "10cpi"),
        ("12cpi", "12cpi"),
        ("13.4cpi", "13.4cpi"),
        ("15cpi", "15cpi"),
        ("17cpi", "17cpi"),
    ]
    all_pitches = fixed_pitches + [
        ("prop144", "prop144"),
        ("prop160", "prop160"),
    ]
    fonts = [
        ("standard", "standard", (False, True), (False, True), (False, True),
         [("normal", ""), ("super", "superscript"), ("sub", "subscript")],
         all_pitches),
        ("draft", "draft", (False,), (False,), (False,), [("normal", "")],
         fixed_pitches),
        ("nlq", "nlq", (False, True), (False,), (False, True), [("normal", "")],
         all_pitches),
    ]
    for (font_name, font_mode, expanded_values, half_height_values,
         bold_values, scripts, pitches) in fonts:
        for pitch_name, pitch_mode in pitches:
            for script, script_name in scripts:
                for half_height in half_height_values:
                    for expanded in expanded_values:
                        for underline in (False, True):
                            for bold in bold_values:
                                parts = [font_name, pitch_name]
                                if bold:
                                    parts.append("bold")
                                if underline:
                                    parts.append("underline")
                                if expanded:
                                    parts.append("expanded")
                                if half_height:
                                    parts.append("half-height")
                                if script_name:
                                    parts.append(script_name)

                                out += iw_text_style(False, False, False, False, "normal")
                                out += iw_font("nlq")
                                out += iw_pitch("10cpi")
                                out += label(parts) + b"\r\n"
                                out += iw_font(font_mode)
                                out += iw_pitch(pitch_mode)
                                out += iw_text_style(bold, underline, expanded, half_height, script)
                                out += sample_text() + b"\r\n"
                                out += iw_text_style(False, False, False, False, "normal")
                                out += iw_font("nlq")
                                out += iw_pitch("10cpi")
                                out += b"\r\n"

    colors = [
        (1, "yellow"),
        (2, "magenta"),
        (3, "cyan"),
        (4, "orange"),
        (5, "green"),
        (6, "violet"),
    ]
    out += iw_text_style(False, False, False, False, "normal")
    out += iw_font("nlq")
    out += iw_pitch("10cpi")
    out += b"\r\nImageWriter II ribbon colour samples\r\n\r\n"
    for index, name in colors:
        out += iw_text_style(False, False, False, False, "normal")
        out += iw_font("standard")
        out += iw_pitch("10cpi")
        out += iw_color(index)
        out += f"colour {index} {name} ".encode("ascii")
        out += sample_text() + b"\r\n"
    out += iw_text_style(False, False, False, False, "normal")
    out += iw_font("standard")
    out += iw_pitch("10cpi")
    out += iw_color(0)
    out += FF
    return bytes(out)


def lq500_reset() -> bytes:
    return ESC + b"@"


def lq500_quality(mode: str) -> bytes:
    return ESC + b"x" + (b"\x01" if mode == "lq" else b"\x00")


def lq500_typestyle(family: str) -> bytes:
    families = {"roman": 0, "sans-serif": 1}
    return ESC + b"k" + bytes([families.get(family, 0)])


def lq500_pitch(mode: str) -> bytes:
    if mode == "12cpi":
        return ESC + b"p\x00" + b"\x12" + ESC + b"M"
    if mode == "15cpi":
        return ESC + b"p\x00" + b"\x12" + ESC + b"g"
    if mode == "condensed":
        return ESC + b"p\x00" + ESC + b"P" + b"\x0f"
    if mode == "proportional":
        return ESC + b"p\x01"
    return ESC + b"p\x00" + b"\x12" + ESC + b"P"


def lq500_text_style(
    bold: bool,
    double_strike: bool,
    italic: bool,
    underline: bool,
    expanded: bool,
) -> bytes:
    value = 0
    if bold:
        value |= 0x08
    if double_strike:
        value |= 0x10
    if expanded:
        value |= 0x20
    if italic:
        value |= 0x40
    if underline:
        value |= 0x80
    return (
        ESC + b"!" + bytes([value])
    )


def lq500_script(script: str) -> bytes:
    if script == "super":
        return ESC + b"S\x00"
    if script == "sub":
        return ESC + b"S\x01"
    return ESC + b"T"


def lq500_plain() -> bytes:
    return (
        lq500_text_style(False, False, False, False, False)
        + lq500_pitch("10cpi")
        + lq500_script("normal")
    )


def build_lq500() -> bytes:
    out = bytearray()
    out += lq500_reset()
    out += b"Epson LQ-500 text attribute matrix\r\n\r\n"

    # Fonts: Draft Sans Serif falls back to Draft Roman (no Draft SS
    # fonts in CG directory) — identical output, omitted.
    fonts = [
        ("draft-roman", "draft", "roman"),
        ("lq-roman", "lq", "roman"),
        ("lq-sans-serif", "lq", "sans-serif"),
    ]

    # Pitches per quality.  Documented collapses applied:
    # - LQ omits "condensed" (SI/DC2 condensed has no visible effect
    #   in LQ — effect #2 only fires under condensed-Draft composite).
    # - 15 cpi omitted everywhere (falls back to elite font = same as
    #   12 cpi, and advance comes from font metrics → identical output).
    draft_pitches = ["10cpi", "12cpi", "condensed", "proportional"]
    lq_pitches = ["10cpi", "12cpi", "proportional"]

    scripts = [("normal", ""), ("super", "superscript"), ("sub", "subscript")]

    # Outline/shadow: 4 mutually exclusive states (VV:2A bits 5+6)
    os_states = [(0, ""), (1, "outline"), (2, "shadow"),
                 (3, "outline+shadow")]

    count = 0
    for font_name, quality, family in fonts:
        pitches = draft_pitches if quality == "draft" else lq_pitches
        for pitch_mode in pitches:
            for script, script_name in scripts:
                # Super/subscript pitch collapse: all non-condensed
                # pitches select the same CG font entry and secondary
                # metrics provide fixed per-character advance, so
                # 10cpi/12cpi/proportional with super/sub are identical.
                # Keep only 10cpi as the representative.
                if script != "normal" and pitch_mode not in ("10cpi", "condensed"):
                    continue
                # Condensed + super/sub only meaningful in Draft
                # (condensed effect #2 doesn't fire in LQ)
                if script != "normal" and pitch_mode == "condensed" and quality != "draft":
                    continue

                for dh in (False, True):
                    for os_val, os_name in os_states:
                        for expanded in (False, True):
                            for underline in (False, True):
                                for italic in (False, True):
                                    for double_strike in (False, True):
                                        for bold in (False, True):
                                            parts = [font_name, pitch_mode]
                                            if bold:
                                                parts.append("bold")
                                            if double_strike:
                                                parts.append("dbl-strike")
                                            if italic:
                                                parts.append("italic")
                                            if underline:
                                                parts.append("underline")
                                            if expanded:
                                                parts.append("expanded")
                                            if script_name:
                                                parts.append(script_name)
                                            if os_name:
                                                parts.append(os_name)
                                            if dh:
                                                parts.append("dbl-height")

                                            setup = (
                                                lq500_quality(quality)
                                                + lq500_typestyle(family)
                                                + lq500_pitch(pitch_mode)
                                                + lq500_script(script)
                                                + lq500_text_style(
                                                    bold, double_strike,
                                                    italic, underline,
                                                    expanded)
                                            )
                                            if os_val:
                                                setup += (ESC + b"q"
                                                          + bytes([os_val]))
                                            if dh:
                                                setup += ESC + b"w\x01"

                                            out += (lq500_quality("lq")
                                                    + lq500_typestyle("roman")
                                                    + lq500_plain())
                                            out += label(parts) + b"\r\n"
                                            out += setup
                                            out += sample_text() + b"\r\n"
                                            if os_val:
                                                out += ESC + b"q\x00"
                                            if dh:
                                                out += ESC + b"w\x00"
                                            out += (lq500_quality("lq")
                                                    + lq500_typestyle("roman")
                                                    + lq500_plain()
                                                    + b"\r\n")
                                            count += 1
    print(f"  LQ-500 unique renderings: {count}")

    out += lq500_plain()
    out += FF
    return bytes(out)


def pcl_param(prefix: bytes, body: bytes) -> bytes:
    return ESC + prefix + body


def ljii_font(pitch: str, italic: bool, bold: bool) -> bytes:
    if pitch == "line-printer":
        return pcl_param(b"&k", b"2S")
    if pitch == "12cpi":
        pitch_body = b"12h"
    else:
        pitch_body = b"10h"
    style = b"1s" if italic else b"0s"
    weight = b"3b" if bold else b"0b"
    return pcl_param(b"(s", b"0p" + pitch_body + style + weight + b"3T")


def ljii_plain() -> bytes:
    return ljii_font("10cpi", False, False) + pcl_param(b"&d", b"@")


def build_ljii_text_attributes() -> bytes:
    out = bytearray()
    out += ESC + b"E"
    out += b"LaserJet II text attribute matrix\r\n\r\n"
    pitches = [
        ("10cpi", "10cpi"),
        ("12cpi", "12cpi"),
        ("line-printer", "line-printer"),
    ]
    for pitch_name, pitch_mode in pitches:
        for underline in (False, True):
            for italic in (False, True):
                for bold in (False, True):
                    if pitch_mode == "line-printer" and (italic or bold):
                        continue
                    parts = [pitch_name]
                    if bold:
                        parts.append("bold")
                    if italic:
                        parts.append("italic")
                    if underline:
                        parts.append("underline")

                    out += ljii_plain()
                    out += label(parts) + b"\r\n"
                    out += ljii_font(pitch_mode, italic, bold)
                    if underline:
                        out += pcl_param(b"&d", b"0D")
                    out += sample_text() + b"\r\n"
                    out += ljii_plain()
                    out += b"\r\n"

    out += ljii_plain()
    out += FF
    return bytes(out)


def build_ljii() -> bytes:
    out = bytearray()
    out += ESC + b"E"
    out += b"LaserJet II PCL4 smoke sample\r\n\r\n"

    out += pcl_param(b"(s", b"0p10h0s0b3T")
    out += b"Courier 10 cpi bold stroke\r\n"
    out += pcl_param(b"(s", b"0p12h0s0b3T")
    out += b"Courier 12 cpi\r\n"
    out += pcl_param(b"(", b"1E") + b"UK symbol set: #\\^~"
    out += pcl_param(b"(", b"8U") + b"\r\n"
    out += pcl_param(b"&d", b"0D")
    out += b"Underlined text should copy from the invisible layer"
    out += pcl_param(b"&d", b"@")
    out += b"\r\n\r\n"

    out += pcl_param(b"*p", b"300x900Y")
    out += pcl_param(b"*c", b"900a90b0P")
    out += pcl_param(b"*p", b"330x1080Y")
    out += b"Black rule above was emitted by ESC *c.\r\n"

    out += pcl_param(b"*t", b"300R")
    out += pcl_param(b"*p", b"330x1200Y")
    out += pcl_param(b"*r", b"1A")
    for row in (
        b"\xFF\x00\xFF\x00",
        b"\x81\x00\x81\x00",
        b"\xBD\x00\xBD\x00",
        b"\x81\x00\x81\x00",
        b"\xFF\x00\xFF\x00",
    ):
        out += pcl_param(b"*b", str(len(row)).encode("ascii") + b"W") + row
    out += pcl_param(b"*r", b"0B")

    out += pcl_param(b"*p", b"330x1320Y")
    out += pcl_param(b"&p", b"20X") + b"Literal PCL payload"
    out += b"\r\n"
    out += pcl_param(b"&l", b"26A")
    out += b"A4 page after page-size command\r\n"
    out += FF
    out += pcl_param(b"&l", b"1O")
    out += b"Landscape page after orientation command\r\n"
    out += FF
    return bytes(out)


def main() -> None:
    samples = {
        "fx80-text-attributes.bin": build_fx80(),
        "bj10e-text-attributes.bin": build_bj10e(),
        "imagewriter-ii-text-attributes.bin": build_imagewriter(),
        "lq500-text-attributes.bin": build_lq500(),
        "laserjet-ii-text-attributes.bin": build_ljii_text_attributes(),
        "laserjet-ii-pcl4-smoke.bin": build_ljii(),
    }
    for name, data in samples.items():
        path = OUT_DIR / name
        path.write_bytes(data)
        print(f"{path.relative_to(OUT_DIR.parent.parent)}: {len(data)} bytes")


if __name__ == "__main__":
    main()
