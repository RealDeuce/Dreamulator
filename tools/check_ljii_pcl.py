#!/usr/bin/env python3
import argparse
import hashlib
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


ESC = b"\x1b"
FF = b"\x0c"


def run(cmd):
    return subprocess.run(cmd, check=True, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE)


def require_tool(name):
    path = shutil.which(name)
    if not path:
        raise SystemExit(f"missing required tool: {name}")
    return path


def render(dreamprint, source, pdf):
    run([str(dreamprint), "--model", "JET", str(source), str(pdf)])


def pdftotext(pdf):
    return run(["pdftotext", str(pdf), "-"]).stdout.decode("utf-8",
                                                          errors="replace")


def pdf_pages(pdf):
    info = run(["pdfinfo", str(pdf)]).stdout.decode("utf-8", errors="replace")
    for line in info.splitlines():
        if line.startswith("Pages:"):
            return int(line.split(":", 1)[1].strip())
    raise AssertionError("pdfinfo did not report page count")


def ppm_image(pdf, stem, dpi=72):
    run(["pdftoppm", "-r", str(dpi), "-singlefile", str(pdf), str(stem)])
    data = Path(f"{stem}.ppm").read_bytes()
    if not data.startswith(b"P6"):
        raise AssertionError("pdftoppm did not emit binary PPM")
    cursor = 2

    def token():
        nonlocal cursor
        while cursor < len(data) and data[cursor] in b" \t\r\n":
            cursor += 1
        if cursor < len(data) and data[cursor] == ord("#"):
            while cursor < len(data) and data[cursor] not in b"\r\n":
                cursor += 1
            return token()
        start = cursor
        while cursor < len(data) and data[cursor] not in b" \t\r\n":
            cursor += 1
        return data[start:cursor]

    width = int(token())
    height = int(token())
    maxval = int(token())
    if maxval != 255:
        raise AssertionError("expected 8-bit PPM")
    while cursor < len(data) and data[cursor] in b" \t\r\n":
        cursor += 1
    pixels = data[cursor:]
    if len(pixels) < width * height * 3:
        raise AssertionError("truncated PPM payload")
    return width, height, pixels[:width * height * 3]


def ppm_pixels(pdf, stem, dpi=72):
    return ppm_image(pdf, stem, dpi)[2]


def ppm_nonwhite(pdf, stem, dpi=72):
    pixels = ppm_pixels(pdf, stem, dpi)
    return sum(1 for i in range(0, len(pixels), 3)
               if pixels[i:i + 3] != b"\xff\xff\xff")


def ppm_sha256(pdf, stem, dpi=72):
    return hashlib.sha256(ppm_pixels(pdf, stem, dpi)).hexdigest()


def ppm_bbox(pdf, stem, dpi=72, min_x_filter=0):
    width, height, pixels = ppm_image(pdf, stem, dpi)
    min_x = width
    min_y = height
    max_x = -1
    max_y = -1
    for y in range(height):
        row = y * width * 3
        for x in range(min_x_filter, width):
            off = row + x * 3
            if (pixels[off] == 0xff and pixels[off + 1] == 0xff and
                    pixels[off + 2] == 0xff):
                continue
            if x < min_x:
                min_x = x
            if y < min_y:
                min_y = y
            if x > max_x:
                max_x = x
            max_y = y
    if max_x < 0:
        return None
    return min_x, min_y, max_x, max_y


def ppm_pixel(pdf, stem, x, y, dpi=72):
    width, height, pixels = ppm_image(pdf, stem, dpi)
    if x < 0 or y < 0 or x >= width or y >= height:
        raise AssertionError("pixel sample outside rendered image")
    off = (y * width + x) * 3
    return pixels[off:off + 3]


def ppm_pixel_dark(pdf, stem, x, y, dpi=72):
    pixel = ppm_pixel(pdf, stem, x, y, dpi=dpi)
    return sum(pixel) < 384


def ppm_rect_nonwhite(pdf, stem, x0, y0, x1, y1, dpi=72):
    width, height, pixels = ppm_image(pdf, stem, dpi)
    x0 = max(0, min(width, x0))
    x1 = max(0, min(width, x1))
    y0 = max(0, min(height, y0))
    y1 = max(0, min(height, y1))
    count = 0
    for y in range(y0, y1):
        row = y * width * 3
        for x in range(x0, x1):
            off = row + x * 3
            if pixels[off:off + 3] != b"\xff\xff\xff":
                count += 1
    return count


def write(path, data):
    path.write_bytes(data)
    return path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--dreamprint", default="build/dreamprint")
    args = parser.parse_args()

    require_tool("pdftotext")
    require_tool("pdfinfo")
    require_tool("pdftoppm")

    root = Path(__file__).resolve().parents[1]
    dreamprint = (root / args.dreamprint).resolve()
    if not dreamprint.exists():
        raise SystemExit(f"dreamprint not found: {dreamprint}")

    with tempfile.TemporaryDirectory(prefix="ljii-check-") as tmp_s:
        tmp = Path(tmp_s)

        sample = root / "samples/dreamprint/laserjet-ii-text-attributes.bin"
        sample_pdf = tmp / "sample.pdf"
        render(dreamprint, sample, sample_pdf)
        text = pdftotext(sample_pdf)
        for needle in (
            "LaserJet II text attribute matrix",
            "Courier 10 cpi matrix",
            "Courier 12 cpi request matrix",
            "ROM pitch fallback selects the nearest resident window above 12 cpi",
            "Line-printer 16.66 cpi matrix",
            "Compatibility pitch command ESC &k2S (16.66 cpi)",
            "Primary and secondary font slots",
            "Font ID selection and default reset",
            "Line termination modes",
            "Cursor stack, relative position, and wrap",
            "# \\ ^ ~",
            "Transparent payload",
            "Macro execute and overlay text",
            "execute: macro-body",
            "Downloaded glyph selected by font id",
            "Text-facing raster and rule placement smoke",
            "Rule above came from ESC *c0P",
            "Raster icon above came from ESC *b#W",
            ")",
        ):
            if needle not in text:
                raise AssertionError(f"sample text missing {needle!r}")
        if pdf_pages(sample_pdf) != 5:
            raise AssertionError("sample pitch sections did not stay page-bounded")
        if text.count("The quick brown fox jumps 0123456789") < 36:
            raise AssertionError("sample matrix lost complete selectable text rows")
        if ppm_nonwhite(sample_pdf, tmp / "sample") < 100:
            raise AssertionError("sample render looks blank")

        display = write(tmp / "display.pcl",
                        ESC + b"Y!\x05!" + ESC + b"Z" + FF)
        display_pdf = tmp / "display.pdf"
        render(dreamprint, display, display_pdf)
        display_raw_text = pdftotext(display_pdf)
        display_text = "".join(display_raw_text.split())
        if "!!Z" not in display_text:
            raise AssertionError("display-functions terminator did not route")
        if not display_raw_text.startswith("! ! Z"):
            raise AssertionError("display fixed spaces lost selectable text")

        display_esc = write(tmp / "display-esc.pcl",
                            ESC + b"YA" + ESC + b"EB" + ESC + b"Z" + FF)
        display_esc_pdf = tmp / "display-esc.pdf"
        render(dreamprint, display_esc, display_esc_pdf)
        display_esc_text = "".join(pdftotext(display_esc_pdf).split())
        if "AEBZ" not in display_esc_text:
            raise AssertionError("display-functions embedded ESC became command")

        esc_question_swallow = write(tmp / "esc-question-swallow.pcl",
                                     ESC + b"?\x11" + b"A" + FF)
        esc_question_reparse = write(tmp / "esc-question-reparse.pcl",
                                     ESC + b"?A" + b"!" + FF)
        esc_question_swallow_pdf = tmp / "esc-question-swallow.pdf"
        esc_question_reparse_pdf = tmp / "esc-question-reparse.pdf"
        render(dreamprint, esc_question_swallow, esc_question_swallow_pdf)
        render(dreamprint, esc_question_reparse, esc_question_reparse_pdf)
        if "".join(pdftotext(esc_question_swallow_pdf).split()) != "A":
            raise AssertionError("ESC ? 0x11 did not swallow only the status byte")
        if "".join(pdftotext(esc_question_reparse_pdf).split()) != "A!":
            raise AssertionError("ESC ? non-0x11 byte did not re-enter parser")

        high_mask = write(tmp / "high-mask.pcl", b"\xa1" + FF)
        high_mask_bang = write(tmp / "high-mask-bang.pcl",
                               b"\x0e!" + FF)
        high_mask_restore = write(tmp / "high-mask-restore.pcl",
                                  b"\xa1!" + FF)
        high_mask_restore_expected = write(
            tmp / "high-mask-restore-expected.pcl",
            b"\x0e!\x0f!" + FF)
        high_mask_pdf = tmp / "high-mask.pdf"
        high_mask_bang_pdf = tmp / "high-mask-bang.pdf"
        high_mask_restore_pdf = tmp / "high-mask-restore.pdf"
        high_mask_restore_expected_pdf = \
            tmp / "high-mask-restore-expected.pdf"
        render(dreamprint, high_mask, high_mask_pdf)
        render(dreamprint, high_mask_bang, high_mask_bang_pdf)
        render(dreamprint, high_mask_restore, high_mask_restore_pdf)
        render(dreamprint, high_mask_restore_expected,
               high_mask_restore_expected_pdf)
        if ppm_sha256(high_mask_pdf, tmp / "high-mask", dpi=300) != \
           ppm_sha256(high_mask_bang_pdf, tmp / "high-mask-bang", dpi=300):
            raise AssertionError("high printable byte did not use secondary context")
        if ppm_sha256(high_mask_restore_pdf,
                      tmp / "high-mask-restore", dpi=300) != \
           ppm_sha256(high_mask_restore_expected_pdf,
                      tmp / "high-mask-restore-expected", dpi=300):
            raise AssertionError("high printable byte did not restore primary context")
        if "\xc0" not in pdftotext(high_mask_pdf):
            raise AssertionError("high printable byte lost selectable source text")

        roman8_black_square = write(tmp / "roman8-black-square.pcl",
                                    b"A\xfcB" + FF)
        roman8_black_square_pdf = tmp / "roman8-black-square.pdf"
        render(dreamprint, roman8_black_square, roman8_black_square_pdf)
        if "A\u25a0B" not in "".join(
                pdftotext(roman8_black_square_pdf).split()):
            raise AssertionError("Roman-8 black square lost selectable source text")

        transparent_fixed = write(tmp / "transparent-fixed.pcl",
                                  ESC + b"&p4X" + b"!\x05\x85!" + FF)
        explicit_spaces = write(tmp / "explicit-spaces.pcl", b"!  !" + FF)
        transparent_fixed_pdf = tmp / "transparent-fixed.pdf"
        explicit_spaces_pdf = tmp / "explicit-spaces.pdf"
        render(dreamprint, transparent_fixed, transparent_fixed_pdf)
        render(dreamprint, explicit_spaces, explicit_spaces_pdf)
        if ppm_sha256(transparent_fixed_pdf, tmp / "transparent-fixed",
                      dpi=150) != \
           ppm_sha256(explicit_spaces_pdf, tmp / "explicit-spaces", dpi=150):
            raise AssertionError("transparent controls did not advance as spaces")
        if not pdftotext(transparent_fixed_pdf).startswith("!  !"):
            raise AssertionError("transparent fixed spaces lost selectable text")

        explicit_space_only = write(tmp / "explicit-space-only.pcl",
                                    b"  " + FF)
        transparent_space_only = write(
            tmp / "transparent-space-only.pcl",
            ESC + b"&p2X" + b"\x05\x85" + FF)
        explicit_space_only_pdf = tmp / "explicit-space-only.pdf"
        transparent_space_only_pdf = tmp / "transparent-space-only.pdf"
        render(dreamprint, explicit_space_only, explicit_space_only_pdf)
        render(dreamprint, transparent_space_only, transparent_space_only_pdf)
        if pdf_pages(explicit_space_only_pdf) != 1:
            raise AssertionError("ordinary space-only text did not publish a page")
        if pdf_pages(transparent_space_only_pdf) != 1:
            raise AssertionError("transparent fixed-space text did not publish a page")
        if not pdftotext(explicit_space_only_pdf).startswith("  "):
            raise AssertionError("ordinary space-only text did not extract")
        if not pdftotext(transparent_space_only_pdf).startswith("  "):
            raise AssertionError("transparent space-only text did not extract")

        transparent_nonroman = write(tmp / "transparent-nonroman.pcl",
                                     ESC + b"(0N" + ESC + b"&p3X" +
                                     b"!\x80!" + FF)
        nonroman_spaces = write(tmp / "nonroman-spaces.pcl",
                                ESC + b"(0N" + b"! !" + FF)
        transparent_nonroman_pdf = tmp / "transparent-nonroman.pdf"
        nonroman_spaces_pdf = tmp / "nonroman-spaces.pdf"
        render(dreamprint, transparent_nonroman, transparent_nonroman_pdf)
        render(dreamprint, nonroman_spaces, nonroman_spaces_pdf)
        if ppm_sha256(transparent_nonroman_pdf, tmp / "transparent-nonroman",
                      dpi=150) == \
           ppm_sha256(nonroman_spaces_pdf, tmp / "nonroman-spaces", dpi=150):
            raise AssertionError("non-Roman transparent controls stayed fixed-space")
        transparent_nonroman_text = "".join(
            pdftotext(transparent_nonroman_pdf).split())
        if transparent_nonroman_text.count("!") < 2:
            raise AssertionError("non-Roman transparent controls lost surrounding text")

        transparent_probe_x = write(tmp / "transparent-probe-x.pcl",
                                    ESC + b"&p3X" + b"A\x1aXB" + FF)
        transparent_probe_del = write(tmp / "transparent-probe-del.pcl",
                                      ESC + b"&p3X" + b"A\x7fB" + FF)
        transparent_probe_x_pdf = tmp / "transparent-probe-x.pdf"
        transparent_probe_del_pdf = tmp / "transparent-probe-del.pdf"
        render(dreamprint, transparent_probe_x, transparent_probe_x_pdf)
        render(dreamprint, transparent_probe_del, transparent_probe_del_pdf)
        if ppm_sha256(transparent_probe_x_pdf, tmp / "transparent-probe-x",
                      dpi=150) != \
           ppm_sha256(transparent_probe_del_pdf, tmp / "transparent-probe-del",
                      dpi=150):
            raise AssertionError("transparent 0x1a X did not normalize to 0x7f")

        transparent_probe_q = write(tmp / "transparent-probe-q.pcl",
                                    ESC + b"&p3X" + b"A\x1aQB" + FF)
        transparent_probe_plain = write(tmp / "transparent-probe-plain.pcl",
                                        ESC + b"&p3X" + b"AQB" + FF)
        transparent_probe_q_pdf = tmp / "transparent-probe-q.pdf"
        transparent_probe_plain_pdf = tmp / "transparent-probe-plain.pdf"
        render(dreamprint, transparent_probe_q, transparent_probe_q_pdf)
        render(dreamprint, transparent_probe_plain, transparent_probe_plain_pdf)
        if ppm_sha256(transparent_probe_q_pdf, tmp / "transparent-probe-q",
                      dpi=150) != \
           ppm_sha256(transparent_probe_plain_pdf,
                      tmp / "transparent-probe-plain", dpi=150):
            raise AssertionError("transparent 0x1a non-X probe did not keep probe byte")

        transparent_negative = write(tmp / "transparent-negative.pcl",
                                     ESC + b"&p-2X" + ESC + b"EAB" + FF)
        transparent_negative_pdf = tmp / "transparent-negative.pdf"
        render(dreamprint, transparent_negative, transparent_negative_pdf)
        if "".join(pdftotext(transparent_negative_pdf).split()) != "EAB":
            raise AssertionError("negative transparent count did not consume payload")

        transparent_integer_count = write(
            tmp / "transparent-integer-count.pcl",
            ESC + b"&p2X" + b"AB" + ESC + b"&a20C" + b"!" + FF)
        transparent_fractional_count = write(
            tmp / "transparent-fractional-count.pcl",
            ESC + b"&p2.9X" + b"AB" + ESC + b"&a20C" + b"!" + FF)
        transparent_integer_count_pdf = tmp / "transparent-integer-count.pdf"
        transparent_fractional_count_pdf = \
            tmp / "transparent-fractional-count.pdf"
        render(dreamprint, transparent_integer_count,
               transparent_integer_count_pdf)
        render(dreamprint, transparent_fractional_count,
               transparent_fractional_count_pdf)
        if ppm_sha256(transparent_integer_count_pdf,
                      tmp / "transparent-integer-count", dpi=150) != \
           ppm_sha256(transparent_fractional_count_pdf,
                      tmp / "transparent-fractional-count", dpi=150):
            raise AssertionError("fractional transparent count rounded")

        transparent_lower_chain = write(
            tmp / "transparent-lower-chain.pcl",
            ESC + b"&p2x3XABC" + FF)
        transparent_lower_expected = write(
            tmp / "transparent-lower-expected.pcl",
            b"ABC" + FF)
        transparent_lower_chain_pdf = tmp / "transparent-lower-chain.pdf"
        transparent_lower_expected_pdf = tmp / "transparent-lower-expected.pdf"
        render(dreamprint, transparent_lower_chain,
               transparent_lower_chain_pdf)
        render(dreamprint, transparent_lower_expected,
               transparent_lower_expected_pdf)
        if "".join(pdftotext(transparent_lower_chain_pdf).split()) != "ABC":
            raise AssertionError("lowercase transparent count consumed command bytes")
        if ppm_sha256(transparent_lower_chain_pdf,
                      tmp / "transparent-lower-chain", dpi=150) != \
           ppm_sha256(transparent_lower_expected_pdf,
                      tmp / "transparent-lower-expected", dpi=150):
            raise AssertionError("lowercase transparent count did not defer to uppercase X")

        generic_drain = write(tmp / "generic-drain.pcl",
                              ESC + b"&z3WABC!" + FF)
        generic_drain_probe = write(tmp / "generic-drain-probe.pcl",
                                    ESC + b"&z2W" + b"A\x1aXB!" + FF)
        generic_drain_lower = write(tmp / "generic-drain-lower.pcl",
                                    ESC + b"&z2w3WABC!" + FF)
        generic_drain_fractional = write(
            tmp / "generic-drain-fractional.pcl",
            ESC + b"&z2.9W" + b"AB" + ESC + b"&a20C" + b"!" + FF)
        generic_drain_integer = write(
            tmp / "generic-drain-integer.pcl",
            ESC + b"&z2W" + b"AB" + ESC + b"&a20C" + b"!" + FF)
        generic_drain_lower_fractional = write(
            tmp / "generic-drain-lower-fractional.pcl",
            ESC + b"&z2.9w3WABC!" + FF)
        generic_drain_pdf = tmp / "generic-drain.pdf"
        generic_drain_probe_pdf = tmp / "generic-drain-probe.pdf"
        generic_drain_lower_pdf = tmp / "generic-drain-lower.pdf"
        generic_drain_fractional_pdf = tmp / "generic-drain-fractional.pdf"
        generic_drain_integer_pdf = tmp / "generic-drain-integer.pdf"
        generic_drain_lower_fractional_pdf = \
            tmp / "generic-drain-lower-fractional.pdf"
        render(dreamprint, generic_drain, generic_drain_pdf)
        render(dreamprint, generic_drain_probe, generic_drain_probe_pdf)
        render(dreamprint, generic_drain_lower, generic_drain_lower_pdf)
        render(dreamprint, generic_drain_fractional,
               generic_drain_fractional_pdf)
        render(dreamprint, generic_drain_integer, generic_drain_integer_pdf)
        render(dreamprint, generic_drain_lower_fractional,
               generic_drain_lower_fractional_pdf)
        if pdftotext(generic_drain_pdf).strip() != "!":
            raise AssertionError("generic unsupported W payload leaked text")
        if pdftotext(generic_drain_probe_pdf).strip() != "B!":
            raise AssertionError("generic unsupported W payload control drain was wrong")
        if pdftotext(generic_drain_lower_pdf).strip() != "C!":
            raise AssertionError("generic lowercase w drain count was not preserved")
        if ppm_sha256(generic_drain_fractional_pdf,
                      tmp / "generic-drain-fractional", dpi=150) != \
           ppm_sha256(generic_drain_integer_pdf,
                      tmp / "generic-drain-integer", dpi=150):
            raise AssertionError("generic unsupported W fractional count rounded")
        if pdftotext(generic_drain_lower_fractional_pdf).strip() != "C!":
            raise AssertionError("generic lowercase w fractional count rounded")

        c0_zero_rows = write(tmp / "c0-zero-rows.pcl",
                             ESC + b"(0N" + b"A\x00\x07\x0bB" + FF)
        c0_zero_expected = write(tmp / "c0-zero-expected.pcl",
                                 ESC + b"(0N" + b"AB" + FF)
        c0_zero_rows_pdf = tmp / "c0-zero-rows.pdf"
        c0_zero_expected_pdf = tmp / "c0-zero-expected.pdf"
        render(dreamprint, c0_zero_rows, c0_zero_rows_pdf)
        render(dreamprint, c0_zero_expected, c0_zero_expected_pdf)
        if pdftotext(c0_zero_rows_pdf).strip() != "AB":
            raise AssertionError("normal C0 zero-handler rows leaked selectable text")
        if ppm_sha256(c0_zero_rows_pdf, tmp / "c0-zero-rows", dpi=150) != \
           ppm_sha256(c0_zero_expected_pdf,
                      tmp / "c0-zero-expected", dpi=150):
            raise AssertionError("normal C0 zero-handler rows changed pixels")

        tabbed = write(tmp / "tabbed.pcl", b"A\tB" + FF)
        explicit_tab = write(tmp / "explicit-tab.pcl",
                             b"A" + ESC + b"*p290X" + b"B" + FF)
        tabbed_pdf = tmp / "tabbed.pdf"
        explicit_tab_pdf = tmp / "explicit-tab.pdf"
        render(dreamprint, tabbed, tabbed_pdf)
        render(dreamprint, explicit_tab, explicit_tab_pdf)
        if "AB" not in "".join(pdftotext(tabbed_pdf).split()):
            raise AssertionError("tabbed text did not extract")
        if ppm_sha256(tabbed_pdf, tmp / "tabbed", dpi=150) != \
           ppm_sha256(explicit_tab_pdf, tmp / "explicit-tab", dpi=150):
            raise AssertionError("horizontal tab did not use next tab stop")

        tab_from_page_left = write(tmp / "tab-from-page-left.pcl",
                                   ESC + b"*p0X\t!" + FF)
        tab_from_page_left_expected = write(
            tmp / "tab-from-page-left-expected.pcl",
            ESC + b"*p50X!" + FF)
        tab_from_page_left_pdf = tmp / "tab-from-page-left.pdf"
        tab_from_page_left_expected_pdf = \
            tmp / "tab-from-page-left-expected.pdf"
        render(dreamprint, tab_from_page_left, tab_from_page_left_pdf)
        render(dreamprint, tab_from_page_left_expected,
               tab_from_page_left_expected_pdf)
        if ppm_sha256(tab_from_page_left_pdf,
                      tmp / "tab-from-page-left", dpi=300) != \
           ppm_sha256(tab_from_page_left_expected_pdf,
                      tmp / "tab-from-page-left-expected", dpi=300):
            raise AssertionError("tab left of margin did not clamp to margin")

        tab_beyond_right = write(
            tmp / "tab-beyond-right.pcl",
            ESC + b"&a1M" + ESC + b"*p600X\t" + ESC + b"*c10a10b0P" + FF)
        tab_beyond_right_pdf = tmp / "tab-beyond-right.pdf"
        render(dreamprint, tab_beyond_right, tab_beyond_right_pdf)
        tab_beyond_box = ppm_bbox(tab_beyond_right_pdf,
                                  tmp / "tab-beyond-right", dpi=300)
        if tab_beyond_box is None or tab_beyond_box[0] != 770:
            raise AssertionError("tab beyond right margin clamped to margin")

        dot_position = write(tmp / "dot-position.pcl",
                             ESC + b"*p300X" + b"A" + FF)
        dot_position_fractional = write(tmp / "dot-position-fractional.pcl",
                                        ESC + b"*p300.9X" + b"A" + FF)
        dot_position_y = write(tmp / "dot-position-y.pcl",
                               ESC + b"*p300Y" + b"A" + FF)
        dot_position_y_fractional = write(tmp / "dot-position-y-fractional.pcl",
                                          ESC + b"*p300.9Y" + b"A" + FF)
        column_position = write(tmp / "column-position.pcl",
                                ESC + b"&a10C" + b"A" + FF)
        default_position = write(tmp / "default-position.pcl", b"A" + FF)
        dot_position_pdf = tmp / "dot-position.pdf"
        dot_position_fractional_pdf = tmp / "dot-position-fractional.pdf"
        dot_position_y_pdf = tmp / "dot-position-y.pdf"
        dot_position_y_fractional_pdf = tmp / "dot-position-y-fractional.pdf"
        column_position_pdf = tmp / "column-position.pdf"
        default_position_pdf = tmp / "default-position.pdf"
        render(dreamprint, dot_position, dot_position_pdf)
        render(dreamprint, dot_position_fractional,
               dot_position_fractional_pdf)
        render(dreamprint, dot_position_y, dot_position_y_pdf)
        render(dreamprint, dot_position_y_fractional,
               dot_position_y_fractional_pdf)
        render(dreamprint, column_position, column_position_pdf)
        render(dreamprint, default_position, default_position_pdf)
        if ppm_sha256(dot_position_pdf, tmp / "dot-position", dpi=150) != \
           ppm_sha256(column_position_pdf, tmp / "column-position", dpi=150):
            raise AssertionError("dot and column horizontal positions diverged")
        if ppm_sha256(dot_position_pdf, tmp / "dot-position", dpi=150) == \
           ppm_sha256(default_position_pdf, tmp / "default-position", dpi=150):
            raise AssertionError("absolute horizontal position did not move pixels")
        if ppm_sha256(dot_position_pdf, tmp / "dot-position", dpi=300) != \
           ppm_sha256(dot_position_fractional_pdf,
                      tmp / "dot-position-fractional", dpi=300):
            raise AssertionError("horizontal dot position used fractional word")
        if ppm_sha256(dot_position_y_pdf, tmp / "dot-position-y", dpi=300) != \
           ppm_sha256(dot_position_y_fractional_pdf,
                      tmp / "dot-position-y-fractional", dpi=300):
            raise AssertionError("vertical dot position used fractional word")

        dot_position_zero = write(tmp / "dot-position-zero.pcl",
                                  ESC + b"*p0X" + b"!" + FF)
        column_position_zero = write(tmp / "column-position-zero.pcl",
                                     ESC + b"&a0C" + b"!" + FF)
        decipoint_position_zero = write(
            tmp / "decipoint-position-zero.pcl",
            ESC + b"&a0H" + b"!" + FF)
        dot_position_zero_pdf = tmp / "dot-position-zero.pdf"
        column_position_zero_pdf = tmp / "column-position-zero.pdf"
        decipoint_position_zero_pdf = tmp / "decipoint-position-zero.pdf"
        render(dreamprint, dot_position_zero, dot_position_zero_pdf)
        render(dreamprint, column_position_zero, column_position_zero_pdf)
        render(dreamprint, decipoint_position_zero,
               decipoint_position_zero_pdf)
        if ppm_sha256(dot_position_zero_pdf, tmp / "dot-position-zero",
                      dpi=300) != \
           ppm_sha256(column_position_zero_pdf,
                      tmp / "column-position-zero", dpi=300):
            raise AssertionError("zero dot and column positions diverged")
        if ppm_sha256(dot_position_zero_pdf,
                      tmp / "dot-position-zero-again", dpi=300) != \
           ppm_sha256(decipoint_position_zero_pdf,
                      tmp / "decipoint-position-zero", dpi=300):
            raise AssertionError("zero dot and decipoint positions diverged")
        default_position_box = ppm_bbox(default_position_pdf,
                                        tmp / "default-position-box",
                                        dpi=300)
        dot_zero_box = ppm_bbox(dot_position_zero_pdf,
                                tmp / "dot-position-zero-box", dpi=300)
        if default_position_box is None or dot_zero_box is None or \
           default_position_box[0] - dot_zero_box[0] < 40:
            raise AssertionError("absolute horizontal zero missed page-left")

        default_first_line = write(tmp / "default-first-line.pcl", b"A" + FF)
        explicit_first_line = write(tmp / "explicit-first-line.pcl",
                                    ESC + b"*p36Y" + b"A" + FF)
        default_first_line_pdf = tmp / "default-first-line.pdf"
        explicit_first_line_pdf = tmp / "explicit-first-line.pdf"
        render(dreamprint, default_first_line, default_first_line_pdf)
        render(dreamprint, explicit_first_line, explicit_first_line_pdf)
        if ppm_sha256(default_first_line_pdf, tmp / "default-first-line",
                      dpi=150) != \
           ppm_sha256(explicit_first_line_pdf, tmp / "explicit-first-line",
                      dpi=150):
            raise AssertionError("default first-line cursor did not use 18/25 VMI")

        vmi_default_row = write(tmp / "vmi-default-row.pcl",
                                ESC + b"&a1R" + b"A" + FF)
        vmi_fractional_max = write(tmp / "vmi-fractional-max.pcl",
                                   ESC + b"&l336.5C" + ESC + b"&a1R" +
                                   b"A" + FF)
        vmi_over_limit = write(tmp / "vmi-over-limit.pcl",
                               ESC + b"&l337C" + ESC + b"&a1R" + b"A" + FF)
        vmi_zero = write(tmp / "vmi-zero.pcl", ESC + b"&l0C" + b"A" + FF)
        vmi_default_row_pdf = tmp / "vmi-default-row.pdf"
        vmi_fractional_max_pdf = tmp / "vmi-fractional-max.pdf"
        vmi_over_limit_pdf = tmp / "vmi-over-limit.pdf"
        vmi_zero_pdf = tmp / "vmi-zero.pdf"
        render(dreamprint, vmi_default_row, vmi_default_row_pdf)
        render(dreamprint, vmi_fractional_max, vmi_fractional_max_pdf)
        render(dreamprint, vmi_over_limit, vmi_over_limit_pdf)
        render(dreamprint, vmi_zero, vmi_zero_pdf)
        if ppm_sha256(vmi_default_row_pdf, tmp / "vmi-default-row",
                      dpi=150) != \
           ppm_sha256(vmi_over_limit_pdf, tmp / "vmi-over-limit", dpi=150):
            raise AssertionError("VMI integer over-limit command was not rejected")
        if ppm_sha256(vmi_default_row_pdf, tmp / "vmi-default-row-again",
                      dpi=150) != \
           ppm_sha256(explicit_first_line_pdf,
                      tmp / "explicit-first-line-again", dpi=150):
            raise AssertionError("absolute row command missed first-line bias")
        vmi_default_box = ppm_bbox(vmi_default_row_pdf,
                                   tmp / "vmi-default-row", dpi=150)
        vmi_fractional_box = ppm_bbox(vmi_fractional_max_pdf,
                                      tmp / "vmi-fractional-max", dpi=150)
        if vmi_default_box is None or vmi_fractional_box is None or \
           vmi_fractional_box[1] <= vmi_default_box[1] + 500:
            raise AssertionError("VMI fractional maximum command was not accepted")
        default_first_line_box = ppm_bbox(default_first_line_pdf,
                                          tmp / "default-first-line-box",
                                          dpi=300)
        vmi_zero_box = ppm_bbox(vmi_zero_pdf, tmp / "vmi-zero", dpi=300)
        if default_first_line_box is None or vmi_zero_box is None or \
           default_first_line_box[1] - vmi_zero_box[1] < 25:
            raise AssertionError("zero VMI did not refresh pending cursor y")

        vmi_short_page_base = write(tmp / "vmi-short-page-base.pcl",
                                    ESC + b"&l20P" + ESC + b"&a1R" +
                                    b"A" + FF)
        vmi_short_page_reject = write(tmp / "vmi-short-page-reject.pcl",
                                      ESC + b"&l20P" + ESC + b"&l336C" +
                                      ESC + b"&a1R" + b"A" + FF)
        lpi_short_page_base = write(tmp / "lpi-short-page-base.pcl",
                                    ESC + b"&l5P" + ESC + b"&a1R" +
                                    b"A" + FF)
        lpi_short_page_reject = write(tmp / "lpi-short-page-reject.pcl",
                                      ESC + b"&l5P" + ESC + b"&l1D" +
                                      ESC + b"&a1R" + b"A" + FF)
        vmi_short_page_base_pdf = tmp / "vmi-short-page-base.pdf"
        vmi_short_page_reject_pdf = tmp / "vmi-short-page-reject.pdf"
        lpi_short_page_base_pdf = tmp / "lpi-short-page-base.pdf"
        lpi_short_page_reject_pdf = tmp / "lpi-short-page-reject.pdf"
        render(dreamprint, vmi_short_page_base, vmi_short_page_base_pdf)
        render(dreamprint, vmi_short_page_reject, vmi_short_page_reject_pdf)
        render(dreamprint, lpi_short_page_base, lpi_short_page_base_pdf)
        render(dreamprint, lpi_short_page_reject, lpi_short_page_reject_pdf)
        if ppm_sha256(vmi_short_page_base_pdf,
                      tmp / "vmi-short-page-base", dpi=150) != \
           ppm_sha256(vmi_short_page_reject_pdf,
                      tmp / "vmi-short-page-reject", dpi=150):
            raise AssertionError("VMI beyond page extent was not rejected")
        if ppm_sha256(lpi_short_page_base_pdf,
                      tmp / "lpi-short-page-base", dpi=150) != \
           ppm_sha256(lpi_short_page_reject_pdf,
                      tmp / "lpi-short-page-reject", dpi=150):
            raise AssertionError("LPI beyond page extent was not rejected")

        hmi_positive = write(tmp / "hmi-positive.pcl",
                             ESC + b"&k6H" + b"!!" + FF)
        hmi_negative = write(tmp / "hmi-negative.pcl",
                             ESC + b"&k-6H" + b"!!" + FF)
        hmi_positive_pdf = tmp / "hmi-positive.pdf"
        hmi_negative_pdf = tmp / "hmi-negative.pdf"
        render(dreamprint, hmi_positive, hmi_positive_pdf)
        render(dreamprint, hmi_negative, hmi_negative_pdf)
        if ppm_sha256(hmi_positive_pdf, tmp / "hmi-positive", dpi=150) != \
           ppm_sha256(hmi_negative_pdf, tmp / "hmi-negative", dpi=150):
            raise AssertionError("negative HMI value did not match positive value")

        hmi_default_glyph = write(tmp / "hmi-default-glyph.pcl", b"!" + FF)
        hmi_narrow_glyph = write(tmp / "hmi-narrow-glyph.pcl",
                                 ESC + b"&k6H" + b"!" + FF)
        hmi_default_pair = write(tmp / "hmi-default-pair.pcl", b"!!" + FF)
        hmi_narrow_pair = write(tmp / "hmi-narrow-pair.pcl",
                                ESC + b"&k6H" + b"!!" + FF)
        hmi_default_glyph_pdf = tmp / "hmi-default-glyph.pdf"
        hmi_narrow_glyph_pdf = tmp / "hmi-narrow-glyph.pdf"
        hmi_default_pair_pdf = tmp / "hmi-default-pair.pdf"
        hmi_narrow_pair_pdf = tmp / "hmi-narrow-pair.pdf"
        render(dreamprint, hmi_default_glyph, hmi_default_glyph_pdf)
        render(dreamprint, hmi_narrow_glyph, hmi_narrow_glyph_pdf)
        render(dreamprint, hmi_default_pair, hmi_default_pair_pdf)
        render(dreamprint, hmi_narrow_pair, hmi_narrow_pair_pdf)
        if ppm_sha256(hmi_default_glyph_pdf, tmp / "hmi-default-glyph",
                      dpi=300) != \
           ppm_sha256(hmi_narrow_glyph_pdf, tmp / "hmi-narrow-glyph",
                      dpi=300):
            raise AssertionError("HMI changed selected glyph pixels")
        default_pair_box = ppm_bbox(hmi_default_pair_pdf,
                                    tmp / "hmi-default-pair", dpi=300)
        narrow_pair_box = ppm_bbox(hmi_narrow_pair_pdf,
                                   tmp / "hmi-narrow-pair", dpi=300)
        if default_pair_box is None or narrow_pair_box is None or \
           (narrow_pair_box[2] - narrow_pair_box[0]) >= \
           (default_pair_box[2] - default_pair_box[0]):
            raise AssertionError("HMI did not reduce printable advance")

        hmi_symbol_refresh = write(tmp / "hmi-symbol-refresh.pcl",
                                   ESC + b"&k6H" + ESC + b"(8U" +
                                   b"!!" + FF)
        hmi_symbol_refresh_pdf = tmp / "hmi-symbol-refresh.pdf"
        render(dreamprint, hmi_symbol_refresh, hmi_symbol_refresh_pdf)
        if ppm_sha256(hmi_default_pair_pdf, tmp / "hmi-default-pair-again",
                      dpi=150) != \
           ppm_sha256(hmi_symbol_refresh_pdf, tmp / "hmi-symbol-refresh",
                      dpi=150):
            raise AssertionError("symbol-set designation did not refresh HMI")

        hmi_probe_rule = ESC + b"*c10a10b0P"
        hmi_default_column = write(tmp / "hmi-default-column.pcl",
                                   ESC + b"&a1C" + hmi_probe_rule + FF)
        hmi_fractional_max = write(tmp / "hmi-fractional-max.pcl",
                                   ESC + b"&k840.5H" + ESC + b"&a1C" +
                                   hmi_probe_rule + FF)
        hmi_over_limit = write(tmp / "hmi-over-limit.pcl",
                               ESC + b"&k841H" + ESC + b"&a1C" +
                               hmi_probe_rule + FF)
        hmi_default_column_pdf = tmp / "hmi-default-column.pdf"
        hmi_fractional_max_pdf = tmp / "hmi-fractional-max.pdf"
        hmi_over_limit_pdf = tmp / "hmi-over-limit.pdf"
        render(dreamprint, hmi_default_column, hmi_default_column_pdf)
        render(dreamprint, hmi_fractional_max, hmi_fractional_max_pdf)
        render(dreamprint, hmi_over_limit, hmi_over_limit_pdf)
        if ppm_sha256(hmi_default_column_pdf, tmp / "hmi-default-column",
                      dpi=300) != \
           ppm_sha256(hmi_over_limit_pdf, tmp / "hmi-over-limit", dpi=300):
            raise AssertionError("HMI integer over-limit command was not rejected")
        default_column_box = ppm_bbox(hmi_default_column_pdf,
                                      tmp / "hmi-default-column", dpi=300)
        fractional_max_box = ppm_bbox(hmi_fractional_max_pdf,
                                      tmp / "hmi-fractional-max", dpi=300)
        if fractional_max_box is None or default_column_box is None or \
           fractional_max_box[0] <= default_column_box[0] + 1000:
            raise AssertionError("HMI fractional maximum command was not accepted")

        hmi_zero_plain = write(tmp / "hmi-zero-plain.pcl",
                               ESC + b"&k0H" + b"!" + FF)
        hmi_zero_pair = write(tmp / "hmi-zero-pair.pcl",
                              ESC + b"&k0H" + b"!!" + FF)
        hmi_zero_tab = write(tmp / "hmi-zero-tab.pcl",
                             ESC + b"&k0H\t!" + FF)
        hmi_zero_plain_pdf = tmp / "hmi-zero-plain.pdf"
        hmi_zero_pair_pdf = tmp / "hmi-zero-pair.pdf"
        hmi_zero_tab_pdf = tmp / "hmi-zero-tab.pdf"
        render(dreamprint, hmi_zero_plain, hmi_zero_plain_pdf)
        render(dreamprint, hmi_zero_pair, hmi_zero_pair_pdf)
        render(dreamprint, hmi_zero_tab, hmi_zero_tab_pdf)
        if ppm_sha256(hmi_zero_plain_pdf, tmp / "hmi-zero-plain",
                      dpi=300) != \
           ppm_sha256(hmi_zero_pair_pdf, tmp / "hmi-zero-pair", dpi=300):
            raise AssertionError("zero HMI printable pair changed pixels")
        if pdftotext(hmi_zero_pair_pdf).strip() != "!!":
            raise AssertionError("zero HMI printable pair lost selectable text")
        if ppm_sha256(hmi_zero_plain_pdf, tmp / "hmi-zero-plain",
                      dpi=300) != \
           ppm_sha256(hmi_zero_tab_pdf, tmp / "hmi-zero-tab", dpi=300):
            raise AssertionError("zero HMI tab changed cursor position")

        pitch_mode_positive = write(tmp / "pitch-mode-positive.pcl",
                                    ESC + b"&k2S" + b"Pitch mode" + FF)
        pitch_mode_negative = write(tmp / "pitch-mode-negative.pcl",
                                    ESC + b"&k-2S" + b"Pitch mode" + FF)
        pitch_mode_fractional = write(tmp / "pitch-mode-fractional.pcl",
                                      ESC + b"&k2.9S" + b"Pitch mode" + FF)
        pitch_mode_invalid = write(tmp / "pitch-mode-invalid.pcl",
                                   ESC + b"&k6H" + ESC + b"&k1S" + b"!" + FF)
        pitch_mode_positive_pdf = tmp / "pitch-mode-positive.pdf"
        pitch_mode_negative_pdf = tmp / "pitch-mode-negative.pdf"
        pitch_mode_fractional_pdf = tmp / "pitch-mode-fractional.pdf"
        pitch_mode_invalid_pdf = tmp / "pitch-mode-invalid.pdf"
        render(dreamprint, pitch_mode_positive, pitch_mode_positive_pdf)
        render(dreamprint, pitch_mode_negative, pitch_mode_negative_pdf)
        render(dreamprint, pitch_mode_fractional, pitch_mode_fractional_pdf)
        render(dreamprint, pitch_mode_invalid, pitch_mode_invalid_pdf)
        if ppm_sha256(pitch_mode_positive_pdf, tmp / "pitch-mode-positive",
                      dpi=150) != \
           ppm_sha256(pitch_mode_negative_pdf, tmp / "pitch-mode-negative",
                      dpi=150):
            raise AssertionError("negative pitch-mode selector was not absolute")
        if ppm_sha256(pitch_mode_positive_pdf, tmp / "pitch-mode-positive",
                      dpi=150) != \
           ppm_sha256(pitch_mode_fractional_pdf,
                      tmp / "pitch-mode-fractional", dpi=150):
            raise AssertionError("fractional pitch-mode selector rounded")
        if ppm_sha256(hmi_default_glyph_pdf, tmp / "hmi-default-glyph",
                      dpi=300) != \
           ppm_sha256(pitch_mode_invalid_pdf, tmp / "pitch-mode-invalid",
                      dpi=300):
            raise AssertionError("invalid pitch-mode selector changed font pitch")

        pitch_mode_default = write(tmp / "pitch-mode-default.pcl",
                                   b"Pitch mode" + FF)
        pitch_mode_default_pdf = tmp / "pitch-mode-default.pdf"
        render(dreamprint, pitch_mode_default, pitch_mode_default_pdf)
        if "".join(pdftotext(pitch_mode_positive_pdf).split()) != "Pitchmode":
            raise AssertionError("line-printer pitch lost selectable text")
        line_box = ppm_bbox(pitch_mode_positive_pdf,
                            tmp / "pitch-mode-positive-box", dpi=300)
        default_box = ppm_bbox(pitch_mode_default_pdf,
                               tmp / "pitch-mode-default-box", dpi=300)
        if line_box is None or default_box is None:
            raise AssertionError("line-printer pitch rendered blank")
        line_w = line_box[2] - line_box[0] + 1
        line_h = line_box[3] - line_box[1] + 1
        default_w = default_box[2] - default_box[0] + 1
        default_h = default_box[3] - default_box[1] + 1
        if line_w >= default_w * 3 // 4 or line_h >= default_h:
            raise AssertionError("line-printer pitch did not select resident compact glyphs")

        vmi_zero_base = write(tmp / "vmi-zero-base.pcl",
                              ESC + b"&l12D" + b"A\nB" + FF)
        vmi_zero_ignored = write(tmp / "vmi-zero-ignored.pcl",
                                 ESC + b"&l12D" + ESC + b"&l0C" +
                                 b"A\nB" + FF)
        vmi_zero_base_pdf = tmp / "vmi-zero-base.pdf"
        vmi_zero_ignored_pdf = tmp / "vmi-zero-ignored.pdf"
        render(dreamprint, vmi_zero_base, vmi_zero_base_pdf)
        render(dreamprint, vmi_zero_ignored, vmi_zero_ignored_pdf)
        if ppm_sha256(vmi_zero_base_pdf, tmp / "vmi-zero-base",
                      dpi=150) == \
           ppm_sha256(vmi_zero_ignored_pdf, tmp / "vmi-zero-ignored",
                      dpi=150):
            raise AssertionError("zero VMI did not affect LF line spacing")
        vmi_zero_base_box = ppm_bbox(vmi_zero_base_pdf,
                                     tmp / "vmi-zero-base-box", dpi=300)
        vmi_zero_ignored_box = ppm_bbox(vmi_zero_ignored_pdf,
                                        tmp / "vmi-zero-ignored-box",
                                        dpi=300)
        if vmi_zero_base_box is None or vmi_zero_ignored_box is None or \
           vmi_zero_ignored_box[3] >= vmi_zero_base_box[3] - 20:
            raise AssertionError("zero VMI LF did not collapse line advance")

        top_margin_default = write(tmp / "top-margin-default.pcl", b"A" + FF)
        top_margin_invalid = write(tmp / "top-margin-invalid.pcl",
                                   ESC + b"&l999E" + b"A" + FF)
        top_margin_default_pdf = tmp / "top-margin-default.pdf"
        top_margin_invalid_pdf = tmp / "top-margin-invalid.pdf"
        render(dreamprint, top_margin_default, top_margin_default_pdf)
        render(dreamprint, top_margin_invalid, top_margin_invalid_pdf)
        if ppm_sha256(top_margin_default_pdf, tmp / "top-margin-default",
                      dpi=150) != \
           ppm_sha256(top_margin_invalid_pdf, tmp / "top-margin-invalid",
                      dpi=150):
            raise AssertionError("invalid top margin changed placement")

        text_length_short = write(tmp / "text-length-short.pcl",
                                  ESC + b"&l2F" + ESC + b"&l2V" + b"A" + FF)
        text_length_invalid = write(tmp / "text-length-invalid.pcl",
                                    ESC + b"&l2F" + ESC + b"&l999F" +
                                    ESC + b"&l2V" + b"A" + FF)
        text_length_short_pdf = tmp / "text-length-short.pdf"
        text_length_invalid_pdf = tmp / "text-length-invalid.pdf"
        render(dreamprint, text_length_short, text_length_short_pdf)
        render(dreamprint, text_length_invalid, text_length_invalid_pdf)
        if ppm_sha256(text_length_short_pdf, tmp / "text-length-short",
                      dpi=150) != \
           ppm_sha256(text_length_invalid_pdf, tmp / "text-length-invalid",
                      dpi=150):
            raise AssertionError("invalid text length changed VFC state")

        prop_prev_width = write(tmp / "prop-prev-width.pcl",
                                ESC + b"(s1P" + b"Wi\bX" + FF)
        prop_hmi_backspace = write(tmp / "prop-hmi-backspace.pcl",
                                   ESC + b"(s1P" + b"Wi" +
                                   ESC + b"&a-72H" + b"X" + FF)
        prop_prev_width_pdf = tmp / "prop-prev-width.pdf"
        prop_hmi_backspace_pdf = tmp / "prop-hmi-backspace.pdf"
        render(dreamprint, prop_prev_width, prop_prev_width_pdf)
        render(dreamprint, prop_hmi_backspace, prop_hmi_backspace_pdf)
        if ppm_sha256(prop_prev_width_pdf, tmp / "prop-prev-width",
                      dpi=300) == \
           ppm_sha256(prop_hmi_backspace_pdf, tmp / "prop-hmi-backspace",
                      dpi=300):
            raise AssertionError("proportional BS still matched HMI backspace")
        if "WiX" not in "".join(pdftotext(prop_prev_width_pdf).split()):
            raise AssertionError("proportional BS lost selectable text")

        prop_bs_cr_flush = write(tmp / "prop-bs-cr-flush.pcl",
                                 ESC + b"(s1P" + b"Wi\b\rX" + FF)
        prop_cr_flush = write(tmp / "prop-cr-flush.pcl",
                              ESC + b"(s1P" + b"Wi\rX" + FF)
        prop_bs_cr_flush_pdf = tmp / "prop-bs-cr-flush.pdf"
        prop_cr_flush_pdf = tmp / "prop-cr-flush.pdf"
        render(dreamprint, prop_bs_cr_flush, prop_bs_cr_flush_pdf)
        render(dreamprint, prop_cr_flush, prop_cr_flush_pdf)
        if ppm_sha256(prop_bs_cr_flush_pdf, tmp / "prop-bs-cr-flush",
                      dpi=300) != \
           ppm_sha256(prop_cr_flush_pdf, tmp / "prop-cr-flush", dpi=300):
            raise AssertionError(
                "CR flush did not clear pending previous-width latch")
        if "WiX" not in "".join(pdftotext(prop_bs_cr_flush_pdf).split()):
            raise AssertionError(
                "CR previous-width flush lost selectable text")

        left_margin_positive = write(tmp / "left-margin-positive.pcl",
                                     ESC + b"&a6L" + b"!" + FF)
        left_margin_negative = write(tmp / "left-margin-negative.pcl",
                                     ESC + b"&a-6L" + b"!" + FF)
        right_margin_positive = write(tmp / "right-margin-positive.pcl",
                                      ESC + b"&a1l9M" + b"!" + FF)
        right_margin_negative = write(tmp / "right-margin-negative.pcl",
                                      ESC + b"&a1l-9M" + b"!" + FF)
        left_margin_positive_pdf = tmp / "left-margin-positive.pdf"
        left_margin_negative_pdf = tmp / "left-margin-negative.pdf"
        right_margin_positive_pdf = tmp / "right-margin-positive.pdf"
        right_margin_negative_pdf = tmp / "right-margin-negative.pdf"
        render(dreamprint, left_margin_positive, left_margin_positive_pdf)
        render(dreamprint, left_margin_negative, left_margin_negative_pdf)
        render(dreamprint, right_margin_positive, right_margin_positive_pdf)
        render(dreamprint, right_margin_negative, right_margin_negative_pdf)
        if ppm_sha256(left_margin_positive_pdf, tmp / "left-margin-positive",
                      dpi=150) != \
           ppm_sha256(left_margin_negative_pdf, tmp / "left-margin-negative",
                      dpi=150):
            raise AssertionError("negative left margin did not match positive value")
        if ppm_sha256(right_margin_positive_pdf, tmp / "right-margin-positive",
                      dpi=150) != \
           ppm_sha256(right_margin_negative_pdf, tmp / "right-margin-negative",
                      dpi=150):
            raise AssertionError("negative right margin did not match positive value")

        left_margin_invalid_base = write(tmp / "left-margin-invalid-base.pcl",
                                         b"!" + FF)
        left_margin_invalid = write(tmp / "left-margin-invalid.pcl",
                                    ESC + b"&a100L" + b"!" + FF)
        right_margin_invalid_base = write(
            tmp / "right-margin-invalid-base.pcl",
            ESC + b"&a6L" + ESC + b"&s1C" + b"!" + FF)
        right_margin_invalid = write(
            tmp / "right-margin-invalid.pcl",
            ESC + b"&a6L" + ESC + b"&a1M" + ESC + b"&s1C" + b"!" + FF)
        left_margin_invalid_base_pdf = tmp / "left-margin-invalid-base.pdf"
        left_margin_invalid_pdf = tmp / "left-margin-invalid.pdf"
        right_margin_invalid_base_pdf = tmp / "right-margin-invalid-base.pdf"
        right_margin_invalid_pdf = tmp / "right-margin-invalid.pdf"
        render(dreamprint, left_margin_invalid_base,
               left_margin_invalid_base_pdf)
        render(dreamprint, left_margin_invalid, left_margin_invalid_pdf)
        render(dreamprint, right_margin_invalid_base,
               right_margin_invalid_base_pdf)
        render(dreamprint, right_margin_invalid, right_margin_invalid_pdf)
        if ppm_sha256(left_margin_invalid_base_pdf,
                      tmp / "left-margin-invalid-base", dpi=150) != \
           ppm_sha256(left_margin_invalid_pdf, tmp / "left-margin-invalid",
                      dpi=150):
            raise AssertionError("invalid left margin changed placement")
        if ppm_sha256(right_margin_invalid_base_pdf,
                      tmp / "right-margin-invalid-base", dpi=150) != \
           ppm_sha256(right_margin_invalid_pdf, tmp / "right-margin-invalid",
                      dpi=150):
            raise AssertionError("invalid right margin changed placement")

        margin_reset_plain = write(tmp / "margin-reset-plain.pcl",
                                   ESC + b"&a20C" + b"!" + FF)
        margin_reset_esc9 = write(tmp / "margin-reset-esc9.pcl",
                                  ESC + b"&a20C" + ESC + b"9" + b"!" + FF)
        margin_reset_plain_pdf = tmp / "margin-reset-plain.pdf"
        margin_reset_esc9_pdf = tmp / "margin-reset-esc9.pdf"
        render(dreamprint, margin_reset_plain, margin_reset_plain_pdf)
        render(dreamprint, margin_reset_esc9, margin_reset_esc9_pdf)
        if ppm_sha256(margin_reset_plain_pdf, tmp / "margin-reset-plain",
                      dpi=150) != \
           ppm_sha256(margin_reset_esc9_pdf, tmp / "margin-reset-esc9",
                      dpi=150):
            raise AssertionError("ESC 9 moved current x before a consumer")

        margin_reset_cr_default = write(tmp / "margin-reset-cr-default.pcl",
                                        b"\r!" + FF)
        margin_reset_cr_esc9 = write(tmp / "margin-reset-cr-esc9.pcl",
                                     ESC + b"9\r!" + FF)
        margin_reset_cr_default_pdf = tmp / "margin-reset-cr-default.pdf"
        margin_reset_cr_esc9_pdf = tmp / "margin-reset-cr-esc9.pdf"
        render(dreamprint, margin_reset_cr_default,
               margin_reset_cr_default_pdf)
        render(dreamprint, margin_reset_cr_esc9, margin_reset_cr_esc9_pdf)
        default_box = ppm_bbox(margin_reset_cr_default_pdf,
                               tmp / "margin-reset-cr-default", dpi=300)
        reset_box = ppm_bbox(margin_reset_cr_esc9_pdf,
                             tmp / "margin-reset-cr-esc9", dpi=300)
        if default_box is None or reset_box is None or \
           default_box[0] - reset_box[0] < 40:
            raise AssertionError("ESC 9 CR did not reset to page-left margin")

        cursor_pop_positive = write(tmp / "cursor-pop-positive.pcl",
                                    ESC + b"&a20C" + ESC + b"&f0S" +
                                    ESC + b"&a30C" + ESC + b"&f1S" +
                                    b"!" + FF)
        cursor_pop_negative = write(tmp / "cursor-pop-negative.pcl",
                                    ESC + b"&a20C" + ESC + b"&f0S" +
                                    ESC + b"&a30C" + ESC + b"&f-1S" +
                                    b"!" + FF)
        cursor_pop_fractional = write(tmp / "cursor-pop-fractional.pcl",
                                      ESC + b"&a20C" + ESC + b"&f0.9S" +
                                      ESC + b"&a30C" + ESC + b"&f1.9S" +
                                      b"!" + FF)
        cursor_pop_positive_pdf = tmp / "cursor-pop-positive.pdf"
        cursor_pop_negative_pdf = tmp / "cursor-pop-negative.pdf"
        cursor_pop_fractional_pdf = tmp / "cursor-pop-fractional.pdf"
        render(dreamprint, cursor_pop_positive, cursor_pop_positive_pdf)
        render(dreamprint, cursor_pop_negative, cursor_pop_negative_pdf)
        render(dreamprint, cursor_pop_fractional, cursor_pop_fractional_pdf)
        if ppm_sha256(cursor_pop_positive_pdf, tmp / "cursor-pop-positive",
                      dpi=150) != \
           ppm_sha256(cursor_pop_negative_pdf, tmp / "cursor-pop-negative",
                      dpi=150):
            raise AssertionError("negative cursor-stack selector did not pop")
        if ppm_sha256(cursor_pop_positive_pdf, tmp / "cursor-pop-positive",
                      dpi=150) != \
           ppm_sha256(cursor_pop_fractional_pdf,
                      tmp / "cursor-pop-fractional", dpi=150):
            raise AssertionError("fractional cursor-stack selector rounded")

        line_term_positive = write(tmp / "line-term-positive.pcl",
                                   ESC + b"&k2G" + b"A\nB" + FF)
        line_term_negative = write(tmp / "line-term-negative.pcl",
                                   ESC + b"&k-2G" + b"A\nB" + FF)
        line_term_fractional = write(tmp / "line-term-fractional.pcl",
                                     ESC + b"&k2G" + b"A\rB" + FF)
        line_term_fractional_probe = write(
            tmp / "line-term-fractional-probe.pcl",
            ESC + b"&k2.9G" + b"A\rB" + FF)
        line_term_positive_pdf = tmp / "line-term-positive.pdf"
        line_term_negative_pdf = tmp / "line-term-negative.pdf"
        line_term_fractional_pdf = tmp / "line-term-fractional.pdf"
        line_term_fractional_probe_pdf = \
            tmp / "line-term-fractional-probe.pdf"
        render(dreamprint, line_term_positive, line_term_positive_pdf)
        render(dreamprint, line_term_negative, line_term_negative_pdf)
        render(dreamprint, line_term_fractional, line_term_fractional_pdf)
        render(dreamprint, line_term_fractional_probe,
               line_term_fractional_probe_pdf)
        if ppm_sha256(line_term_positive_pdf, tmp / "line-term-positive",
                      dpi=150) != \
           ppm_sha256(line_term_negative_pdf, tmp / "line-term-negative",
                      dpi=150):
            raise AssertionError("negative line termination selector was not absolute")
        if ppm_sha256(line_term_fractional_pdf, tmp / "line-term-fractional",
                      dpi=150) != \
           ppm_sha256(line_term_fractional_probe_pdf,
                      tmp / "line-term-fractional-probe", dpi=150):
            raise AssertionError("fractional line termination selector rounded")

        line_term_default = write(tmp / "line-term-default.pcl",
                                  ESC + b"&k0G" + b"A\rB" + FF)
        line_term_invalid = write(tmp / "line-term-invalid.pcl",
                                  ESC + b"&k4G" + b"A\rB" + FF)
        line_term_default_pdf = tmp / "line-term-default.pdf"
        line_term_invalid_pdf = tmp / "line-term-invalid.pdf"
        render(dreamprint, line_term_default, line_term_default_pdf)
        render(dreamprint, line_term_invalid, line_term_invalid_pdf)
        if ppm_sha256(line_term_default_pdf, tmp / "line-term-default",
                      dpi=150) != \
           ppm_sha256(line_term_invalid_pdf, tmp / "line-term-invalid",
                      dpi=150):
            raise AssertionError("invalid line termination selector changed mode")

        wrap_default = write(tmp / "wrap-default.pcl",
                             b"X" + ESC + b"&a81C" + b"A" + FF)
        wrap_reset = write(tmp / "wrap-reset.pcl",
                           ESC + b"&s0C" + ESC + b"E" + b"X" +
                           ESC + b"&a81C" + b"A" + FF)
        wrap_positive = write(tmp / "wrap-positive.pcl",
                              b"X" + ESC + b"&s1C" + ESC + b"&a81C" +
                              b"A" + FF)
        wrap_negative = write(tmp / "wrap-negative.pcl",
                              b"X" + ESC + b"&s-1C" + ESC + b"&a81C" +
                              b"A" + FF)
        wrap_enabled = write(tmp / "wrap-enabled.pcl",
                             b"X" + ESC + b"&s0C" + ESC + b"&a81C" +
                             b"A" + FF)
        wrap_fractional = write(tmp / "wrap-fractional.pcl",
                                b"X" + ESC + b"&s0.9C" + ESC + b"&a81C" +
                                b"A" + FF)
        wrap_default_pdf = tmp / "wrap-default.pdf"
        wrap_reset_pdf = tmp / "wrap-reset.pdf"
        wrap_positive_pdf = tmp / "wrap-positive.pdf"
        wrap_negative_pdf = tmp / "wrap-negative.pdf"
        wrap_enabled_pdf = tmp / "wrap-enabled.pdf"
        wrap_fractional_pdf = tmp / "wrap-fractional.pdf"
        render(dreamprint, wrap_default, wrap_default_pdf)
        render(dreamprint, wrap_reset, wrap_reset_pdf)
        render(dreamprint, wrap_positive, wrap_positive_pdf)
        render(dreamprint, wrap_negative, wrap_negative_pdf)
        render(dreamprint, wrap_enabled, wrap_enabled_pdf)
        render(dreamprint, wrap_fractional, wrap_fractional_pdf)
        if "".join(pdftotext(wrap_default_pdf).split()) != "X":
            raise AssertionError("default wrap state allowed overflow text")
        if "".join(pdftotext(wrap_reset_pdf).split()) != "X":
            raise AssertionError("ESC E did not reset wrap state")
        if "".join(pdftotext(wrap_negative_pdf).split()) != "X":
            raise AssertionError("negative wrap-disable selector allowed overflow text")
        if ppm_sha256(wrap_default_pdf, tmp / "wrap-default", dpi=150) != \
           ppm_sha256(wrap_positive_pdf, tmp / "wrap-positive", dpi=150):
            raise AssertionError("default wrap state did not match disabled wrap")
        if ppm_sha256(wrap_reset_pdf, tmp / "wrap-reset", dpi=150) != \
           ppm_sha256(wrap_positive_pdf, tmp / "wrap-positive-again", dpi=150):
            raise AssertionError("reset wrap state did not match disabled wrap")
        if ppm_sha256(wrap_positive_pdf, tmp / "wrap-positive", dpi=150) != \
           ppm_sha256(wrap_negative_pdf, tmp / "wrap-negative", dpi=150):
            raise AssertionError("negative wrap-disable selector did not match positive")
        if ppm_sha256(wrap_enabled_pdf, tmp / "wrap-enabled", dpi=150) != \
           ppm_sha256(wrap_fractional_pdf, tmp / "wrap-fractional", dpi=150):
            raise AssertionError("fractional wrap selector rounded")

        control_z = write(tmp / "control-z.pcl",
                          b"A" + bytes([0x1a, 0x58]) + b"B" + FF)
        control_z_pdf = tmp / "control-z.pdf"
        render(dreamprint, control_z, control_z_pdf)
        if "".join(pdftotext(control_z_pdf).split()) != "AB":
            raise AssertionError("normal Control-Z X leaked printable text")

        direct_del = write(tmp / "direct-del.pcl", b"A\x7fB" + FF)
        control_z_del = write(tmp / "control-z-del.pcl",
                              b"A" + bytes([0x1a, 0x58]) + b"B" + FF)
        transparent_del = write(tmp / "transparent-del.pcl",
                                ESC + b"&p3X" + b"A\x7fB" + FF)
        direct_c1_default = write(tmp / "direct-c1-default.pcl",
                                  b"A\x85B" + FF)
        direct_c1_default_expected = write(
            tmp / "direct-c1-default-expected.pcl", b"AB" + FF)
        direct_c1_nonroman = write(tmp / "direct-c1-nonroman.pcl",
                                   ESC + b"(0N" + b"A\x85B" + FF)
        direct_c1_nonroman_expected = write(
            tmp / "direct-c1-nonroman-expected.pcl",
            ESC + b"(0N" + b"AB" + FF)
        control_z_nested_nonroman = write(
            tmp / "control-z-nested-nonroman.pcl",
            ESC + b"(0N" + b"A" + bytes([0x1a, 0x1a]) + b"B" + FF)
        control_z_nested_nonroman_expected = write(
            tmp / "control-z-nested-nonroman-expected.pcl",
            ESC + b"(0N" + b"AB" + FF)
        direct_del_pdf = tmp / "direct-del.pdf"
        control_z_del_pdf = tmp / "control-z-del.pdf"
        transparent_del_pdf = tmp / "transparent-del.pdf"
        direct_c1_default_pdf = tmp / "direct-c1-default.pdf"
        direct_c1_default_expected_pdf = \
            tmp / "direct-c1-default-expected.pdf"
        direct_c1_nonroman_pdf = tmp / "direct-c1-nonroman.pdf"
        direct_c1_nonroman_expected_pdf = \
            tmp / "direct-c1-nonroman-expected.pdf"
        control_z_nested_nonroman_pdf = tmp / "control-z-nested-nonroman.pdf"
        control_z_nested_nonroman_expected_pdf = \
            tmp / "control-z-nested-nonroman-expected.pdf"
        render(dreamprint, direct_del, direct_del_pdf)
        render(dreamprint, control_z_del, control_z_del_pdf)
        render(dreamprint, transparent_del, transparent_del_pdf)
        render(dreamprint, direct_c1_default, direct_c1_default_pdf)
        render(dreamprint, direct_c1_default_expected,
               direct_c1_default_expected_pdf)
        render(dreamprint, direct_c1_nonroman, direct_c1_nonroman_pdf)
        render(dreamprint, direct_c1_nonroman_expected,
               direct_c1_nonroman_expected_pdf)
        render(dreamprint, control_z_nested_nonroman,
               control_z_nested_nonroman_pdf)
        render(dreamprint, control_z_nested_nonroman_expected,
               control_z_nested_nonroman_expected_pdf)
        if ppm_sha256(direct_del_pdf, tmp / "direct-del", dpi=150) != \
           ppm_sha256(control_z_del_pdf, tmp / "control-z-del", dpi=150):
            raise AssertionError("normal Control-Z X did not route synthetic DEL")
        if ppm_sha256(direct_del_pdf, tmp / "direct-del", dpi=150) != \
           ppm_sha256(transparent_del_pdf, tmp / "transparent-del", dpi=150):
            raise AssertionError("direct DEL did not use printable fast path")
        if ppm_sha256(direct_c1_default_pdf, tmp / "direct-c1-default",
                      dpi=150) != \
           ppm_sha256(direct_c1_default_expected_pdf,
                      tmp / "direct-c1-default-expected", dpi=150):
            raise AssertionError("default direct C1 byte bypassed parser gate")
        if ppm_sha256(direct_c1_nonroman_pdf, tmp / "direct-c1-nonroman",
                      dpi=150) == \
           ppm_sha256(direct_c1_nonroman_expected_pdf,
                      tmp / "direct-c1-nonroman-expected", dpi=150):
            raise AssertionError("non-Roman direct C1 byte did not print")
        if ppm_sha256(control_z_nested_nonroman_pdf,
                      tmp / "control-z-nested-nonroman", dpi=150) == \
           ppm_sha256(control_z_nested_nonroman_expected_pdf,
                      tmp / "control-z-nested-nonroman-expected", dpi=150):
            raise AssertionError("non-Roman nested Control-Z did not route")

        control_z_255 = write(
            tmp / "control-z-255.pcl",
            b"A" + (bytes([0x1a, 0x58]) * 255) + b"B" + FF)
        control_z_256 = write(
            tmp / "control-z-256.pcl",
            b"A" + (bytes([0x1a, 0x58]) * 256) + b"B" + FF)
        transparent_control_256 = write(
            tmp / "transparent-control-256.pcl",
            b"A" + ESC + b"&p256X" +
            (bytes([0x1a, 0x58]) * 256) + b"B" + FF)
        display_control_256 = write(
            tmp / "display-control-256.pcl",
            b"A" + ESC + b"Y" +
            (bytes([0x1a, 0x58]) * 256) + ESC + b"ZB" + FF)
        control_z_255_pdf = tmp / "control-z-255.pdf"
        control_z_256_pdf = tmp / "control-z-256.pdf"
        transparent_control_256_pdf = tmp / "transparent-control-256.pdf"
        display_control_256_pdf = tmp / "display-control-256.pdf"
        render(dreamprint, control_z_255, control_z_255_pdf)
        render(dreamprint, control_z_256, control_z_256_pdf)
        render(dreamprint, transparent_control_256,
               transparent_control_256_pdf)
        render(dreamprint, display_control_256, display_control_256_pdf)
        if pdf_pages(control_z_255_pdf) != 1:
            raise AssertionError("payload-control counter overflowed before 256")
        if pdf_pages(control_z_256_pdf) != 2:
            raise AssertionError("normal payload-control overflow did not publish")
        if pdf_pages(transparent_control_256_pdf) != 2:
            raise AssertionError("transparent payload-control overflow did not publish")
        if pdf_pages(display_control_256_pdf) != 2:
            raise AssertionError("display payload-control overflow did not publish")

        raster_query = write(tmp / "raster-query.pcl",
                             ESC + b"*r1K" + b"QAB" + FF)
        raster_lower_k = write(tmp / "raster-lower-k.pcl",
                               ESC + b"*r1k" + b"QAB" + FF)
        model_query = write(tmp / "model-query.pcl",
                            ESC + b"*s1^" + b"QAB" + FF)
        raster_query_pdf = tmp / "raster-query.pdf"
        raster_lower_k_pdf = tmp / "raster-lower-k.pdf"
        model_query_pdf = tmp / "model-query.pdf"
        render(dreamprint, raster_query, raster_query_pdf)
        render(dreamprint, raster_lower_k, raster_lower_k_pdf)
        render(dreamprint, model_query, model_query_pdf)
        if "".join(pdftotext(raster_query_pdf).split()) != "AB":
            raise AssertionError("raster query byte leaked printable text")
        if "".join(pdftotext(raster_lower_k_pdf).split()) != "QAB":
            raise AssertionError("unsupported lowercase *rK consumed query byte")
        if "".join(pdftotext(model_query_pdf).split()) != "AB":
            raise AssertionError("model query byte leaked printable text")

        symbol_positive = write(tmp / "symbol-positive.pcl",
                                ESC + b"(2S" + b"@#[]" + FF)
        symbol_negative = write(tmp / "symbol-negative.pcl",
                                ESC + b"(-2S" + b"@#[]" + FF)
        symbol_fractional = write(tmp / "symbol-fractional.pcl",
                                  ESC + b"(2.9S" + b"@#[]" + FF)
        symbol_positive_pdf = tmp / "symbol-positive.pdf"
        symbol_negative_pdf = tmp / "symbol-negative.pdf"
        symbol_fractional_pdf = tmp / "symbol-fractional.pdf"
        render(dreamprint, symbol_positive, symbol_positive_pdf)
        render(dreamprint, symbol_negative, symbol_negative_pdf)
        render(dreamprint, symbol_fractional, symbol_fractional_pdf)
        if ppm_sha256(symbol_positive_pdf, tmp / "symbol-positive",
                      dpi=150) != \
           ppm_sha256(symbol_negative_pdf, tmp / "symbol-negative",
                      dpi=150):
            raise AssertionError("negative symbol-set parameter was not absolute")
        if ppm_sha256(symbol_positive_pdf, tmp / "symbol-positive",
                      dpi=150) != \
           ppm_sha256(symbol_fractional_pdf, tmp / "symbol-fractional",
                      dpi=150):
            raise AssertionError("fractional symbol-set parameter rounded")

        symbol_patch_samples = (
            (b"2U", ord("$"), 0xba),
            (b"1E", ord("#"), 0xbb),
            (b"0F", ord("#"), 0xbb),
            (b"1F", ord("#"), 0xbb),
            (b"0G", ord("#"), 0xbb),
            (b"1G", ord("@"), 0xbd),
            (b"0I", ord("#"), 0xbb),
            (b"0K", ord("\\"), 0xbc),
            (b"2K", ord("$"), 0xbc),
            (b"3S", ord("$"), 0xba),
            (b"0S", ord("$"), 0xba),
            (b"1S", ord("["), 0xb8),
            (b"2S", ord("#"), 0xbb),
            (b"6S", ord("@"), 0xf2),
            (b"4S", ord("@"), 0xbd),
            (b"5S", ord("@"), 0xa8),
            (b"0D", ord("["), 0xd3),
            (b"1D", ord("#"), 0xbd),
        )
        symbol_patch_stream = bytearray()
        symbol_patch_expected_stream = bytearray()
        symbol_patch_text = []
        for pcl, dst, src in symbol_patch_samples:
            symbol_patch_stream += ESC + b"(" + pcl + bytes([dst]) + b"\n"
            symbol_patch_expected_stream += \
                ESC + b"(" + pcl + bytes([src]) + b"\n"
            symbol_patch_text.append(chr(dst))
        symbol_patch = write(tmp / "symbol-patch.pcl",
                             bytes(symbol_patch_stream) + FF)
        symbol_patch_expected = write(
            tmp / "symbol-patch-expected.pcl",
            bytes(symbol_patch_expected_stream) + FF)
        symbol_patch_pdf = tmp / "symbol-patch.pdf"
        symbol_patch_expected_pdf = tmp / "symbol-patch-expected.pdf"
        render(dreamprint, symbol_patch, symbol_patch_pdf)
        render(dreamprint, symbol_patch_expected, symbol_patch_expected_pdf)
        if ppm_sha256(symbol_patch_pdf, tmp / "symbol-patch", dpi=150) != \
           ppm_sha256(symbol_patch_expected_pdf,
                      tmp / "symbol-patch-expected", dpi=150):
            raise AssertionError("symbol patch table did not select ROM source glyphs")
        if "".join(symbol_patch_text) not in \
           "".join(pdftotext(symbol_patch_pdf).split()):
            raise AssertionError("symbol patch stream lost selectable source text")

        cursor_stack_clamp = write(tmp / "cursor-stack-clamp.pcl",
                                   ESC + b"&a65R" +
                                   ESC + b"&f0S" +
                                   ESC + b"&l20P" +
                                   ESC + b"&f1S" + b"!" + FF)
        cursor_stack_clamp_pdf = tmp / "cursor-stack-clamp.pdf"
        render(dreamprint, cursor_stack_clamp, cursor_stack_clamp_pdf)
        if ppm_bbox(cursor_stack_clamp_pdf, tmp / "cursor-stack-clamp",
                    dpi=72) is None:
            raise AssertionError("cursor stack pop did not clamp to page extent")
        if "!" not in pdftotext(cursor_stack_clamp_pdf):
            raise AssertionError("cursor stack clamp lost selectable text")

        cursor_stack_page_left = write(
            tmp / "cursor-stack-page-left.pcl",
            ESC + b"*p0X" + ESC + b"&f0S" + ESC + b"*p300X" +
            ESC + b"&f1S" + b"!" + FF)
        cursor_stack_page_left_pdf = tmp / "cursor-stack-page-left.pdf"
        render(dreamprint, cursor_stack_page_left,
               cursor_stack_page_left_pdf)
        cursor_stack_page_left_box = ppm_bbox(
            cursor_stack_page_left_pdf, tmp / "cursor-stack-page-left",
            dpi=300)
        default_position_box_for_stack = ppm_bbox(
            default_position_pdf, tmp / "default-position-stack-box",
            dpi=300)
        if cursor_stack_page_left_box is None or \
           default_position_box_for_stack is None or \
           default_position_box_for_stack[0] - \
           cursor_stack_page_left_box[0] < 40:
            raise AssertionError("cursor stack pop missed page-left x")

        same_orientation = write(tmp / "same-orientation.pcl",
                                 b"A" + ESC + b"&l0O" + b"B" + FF)
        same_orientation_pdf = tmp / "same-orientation.pdf"
        render(dreamprint, same_orientation, same_orientation_pdf)
        if pdf_pages(same_orientation_pdf) != 1:
            raise AssertionError("unchanged orientation published a page")
        if "AB" not in "".join(pdftotext(same_orientation_pdf).split()):
            raise AssertionError("unchanged orientation shifted text output")

        orientation_positive = write(tmp / "orientation-positive.pcl",
                                     b"A" + ESC + b"&l1O" + b"B" + FF)
        orientation_negative = write(tmp / "orientation-negative.pcl",
                                     b"A" + ESC + b"&l-1O" + b"B" + FF)
        orientation_fractional = write(tmp / "orientation-fractional.pcl",
                                       b"A" + ESC + b"&l1.9O" + b"B" + FF)
        orientation_positive_pdf = tmp / "orientation-positive.pdf"
        orientation_negative_pdf = tmp / "orientation-negative.pdf"
        orientation_fractional_pdf = tmp / "orientation-fractional.pdf"
        render(dreamprint, orientation_positive, orientation_positive_pdf)
        render(dreamprint, orientation_negative, orientation_negative_pdf)
        render(dreamprint, orientation_fractional, orientation_fractional_pdf)
        if pdf_pages(orientation_negative_pdf) != pdf_pages(orientation_positive_pdf):
            raise AssertionError("negative orientation selector was not absolute")
        if pdf_pages(orientation_fractional_pdf) != \
           pdf_pages(orientation_positive_pdf):
            raise AssertionError("fractional orientation selector rounded")

        orientation_hmi_base = write(tmp / "orientation-hmi-base.pcl",
                                     ESC + b"&l1O" + b"!!" + FF)
        orientation_hmi_refresh = write(tmp / "orientation-hmi-refresh.pcl",
                                        ESC + b"&k6H" + ESC + b"&l1O" +
                                        b"!!" + FF)
        orientation_hmi_base_pdf = tmp / "orientation-hmi-base.pdf"
        orientation_hmi_refresh_pdf = tmp / "orientation-hmi-refresh.pdf"
        render(dreamprint, orientation_hmi_base, orientation_hmi_base_pdf)
        render(dreamprint, orientation_hmi_refresh,
               orientation_hmi_refresh_pdf)
        if ppm_sha256(orientation_hmi_base_pdf,
                      tmp / "orientation-hmi-base", dpi=150) != \
           ppm_sha256(orientation_hmi_refresh_pdf,
                      tmp / "orientation-hmi-refresh", dpi=150):
            raise AssertionError("orientation change did not refresh HMI")

        copies_zero_ignored = write(tmp / "copies-zero-ignored.pcl",
                                    ESC + b"&l2X" + ESC + b"&l0X" +
                                    b"!" + FF)
        copies_zero_ignored_pdf = tmp / "copies-zero-ignored.pdf"
        render(dreamprint, copies_zero_ignored, copies_zero_ignored_pdf)
        if pdf_pages(copies_zero_ignored_pdf) != 2:
            raise AssertionError("zero copy selector changed prior copy count")

        page_size_positive = write(tmp / "page-size-positive.pcl",
                                   ESC + b"&l3A" + b"!" + FF)
        page_size_negative = write(tmp / "page-size-negative.pcl",
                                   ESC + b"&l-3A" + b"!" + FF)
        page_size_fractional = write(tmp / "page-size-fractional.pcl",
                                     ESC + b"&l2.9A" + b"!" + FF)
        page_size_integer = write(tmp / "page-size-integer.pcl",
                                  ESC + b"&l2A" + b"!" + FF)
        page_size_invalid = write(tmp / "page-size-invalid.pcl",
                                  ESC + b"&l3A" + ESC + b"&l999A" +
                                  b"!" + FF)
        page_size_zero = write(tmp / "page-size-zero.pcl",
                               ESC + b"&l3A" + ESC + b"&l0A" +
                               b"!" + FF)
        page_size_zero_after_text = write(tmp / "page-size-zero-after-text.pcl",
                                          b"A" + ESC + b"&l0A" + b"B" + FF)
        page_size_missing = write(tmp / "page-size-missing.pcl",
                                  ESC + b"&l3A" + ESC + b"&lA" +
                                  b"!" + FF)
        page_size_missing_after_text = write(
            tmp / "page-size-missing-after-text.pcl",
            b"A" + ESC + b"&lA" + b"B" + FF)
        page_size_positive_pdf = tmp / "page-size-positive.pdf"
        page_size_negative_pdf = tmp / "page-size-negative.pdf"
        page_size_fractional_pdf = tmp / "page-size-fractional.pdf"
        page_size_integer_pdf = tmp / "page-size-integer.pdf"
        page_size_invalid_pdf = tmp / "page-size-invalid.pdf"
        page_size_zero_pdf = tmp / "page-size-zero.pdf"
        page_size_zero_after_text_pdf = tmp / "page-size-zero-after-text.pdf"
        page_size_missing_pdf = tmp / "page-size-missing.pdf"
        page_size_missing_after_text_pdf = \
            tmp / "page-size-missing-after-text.pdf"
        render(dreamprint, page_size_positive, page_size_positive_pdf)
        render(dreamprint, page_size_negative, page_size_negative_pdf)
        render(dreamprint, page_size_fractional, page_size_fractional_pdf)
        render(dreamprint, page_size_integer, page_size_integer_pdf)
        render(dreamprint, page_size_invalid, page_size_invalid_pdf)
        render(dreamprint, page_size_zero, page_size_zero_pdf)
        render(dreamprint, page_size_zero_after_text,
               page_size_zero_after_text_pdf)
        render(dreamprint, page_size_missing, page_size_missing_pdf)
        render(dreamprint, page_size_missing_after_text,
               page_size_missing_after_text_pdf)
        if ppm_sha256(page_size_positive_pdf, tmp / "page-size-positive",
                      dpi=72) != \
           ppm_sha256(page_size_negative_pdf, tmp / "page-size-negative",
                      dpi=72):
            raise AssertionError("negative page-size selector was not absolute")
        if ppm_sha256(page_size_positive_pdf, tmp / "page-size-positive",
                      dpi=72) != \
           ppm_sha256(page_size_invalid_pdf, tmp / "page-size-invalid",
                      dpi=72):
            raise AssertionError("invalid page-size selector did not preserve state")
        if ppm_sha256(page_size_positive_pdf, tmp / "page-size-positive",
                      dpi=72) != \
           ppm_sha256(page_size_zero_pdf, tmp / "page-size-zero", dpi=72):
            raise AssertionError("zero page-size selector did not preserve state")
        if pdf_pages(page_size_zero_after_text_pdf) != 1:
            raise AssertionError("zero page-size selector published current text")
        if "AB" not in "".join(pdftotext(page_size_zero_after_text_pdf).split()):
            raise AssertionError("zero page-size selector shifted text output")
        if ppm_sha256(page_size_missing_pdf, tmp / "page-size-missing",
                      dpi=72) != \
           ppm_sha256(page_size_integer_pdf, tmp / "page-size-integer",
                      dpi=72):
            raise AssertionError("missing page-size selector did not default")
        if pdf_pages(page_size_missing_after_text_pdf) != 2:
            raise AssertionError("missing page-size selector did not publish current text")
        if "A" not in pdftotext(page_size_missing_after_text_pdf) or \
           "B" not in pdftotext(page_size_missing_after_text_pdf):
            raise AssertionError("missing page-size selector lost text output")
        if ppm_sha256(page_size_fractional_pdf, tmp / "page-size-fractional",
                      dpi=72) != \
           ppm_sha256(page_size_integer_pdf, tmp / "page-size-integer",
                      dpi=72):
            raise AssertionError("fractional page-size selector rounded")

        page_size_hmi_base = write(tmp / "page-size-hmi-base.pcl",
                                   ESC + b"&l3A" + b"!!" + FF)
        page_size_hmi_refresh = write(tmp / "page-size-hmi-refresh.pcl",
                                      ESC + b"&k6H" + ESC + b"&l3A" +
                                      b"!!" + FF)
        page_size_hmi_base_pdf = tmp / "page-size-hmi-base.pdf"
        page_size_hmi_refresh_pdf = tmp / "page-size-hmi-refresh.pdf"
        render(dreamprint, page_size_hmi_base, page_size_hmi_base_pdf)
        render(dreamprint, page_size_hmi_refresh,
               page_size_hmi_refresh_pdf)
        if ppm_sha256(page_size_hmi_base_pdf,
                      tmp / "page-size-hmi-base", dpi=150) != \
           ppm_sha256(page_size_hmi_refresh_pdf,
                      tmp / "page-size-hmi-refresh", dpi=150):
            raise AssertionError("page-size change did not refresh HMI")

        upright = write(tmp / "upright.pcl",
                        ESC + b"(s0p10h0s0b3TStyle sample" + FF)
        style_request = write(tmp / "style-request.pcl",
                              ESC + b"(s0p10h1s0b3TStyle sample" + FF)
        upright_pdf = tmp / "upright.pdf"
        style_request_pdf = tmp / "style-request.pdf"
        render(dreamprint, upright, upright_pdf)
        render(dreamprint, style_request, style_request_pdf)
        if ppm_sha256(upright_pdf, tmp / "upright", dpi=150) != \
           ppm_sha256(style_request_pdf, tmp / "style-request", dpi=150):
            raise AssertionError("style fallback changed resident Courier pixels")
        if pdftotext(style_request_pdf).strip() != "Style sample":
            raise AssertionError("style fallback lost selectable input text")

        medium = write(tmp / "medium.pcl",
                       ESC + b"(s0p10h12v0s0b3TStroke sample" + FF)
        bold = write(tmp / "bold.pcl",
                     ESC + b"(s0p10h12v0s3b3TStroke sample" + FF)
        bold_fractional = write(tmp / "bold-fractional.pcl",
                                ESC + b"(s0p10h12v0s3.9b3TStroke sample" +
                                FF)
        medium_pdf = tmp / "medium.pdf"
        bold_pdf = tmp / "bold.pdf"
        bold_fractional_pdf = tmp / "bold-fractional.pdf"
        render(dreamprint, medium, medium_pdf)
        render(dreamprint, bold, bold_pdf)
        render(dreamprint, bold_fractional, bold_fractional_pdf)
        if ppm_sha256(medium_pdf, tmp / "medium", dpi=150) == \
           ppm_sha256(bold_pdf, tmp / "bold", dpi=150):
            raise AssertionError("bold stroke request selected medium glyph pixels")
        if ppm_nonwhite(bold_pdf, tmp / "bold", dpi=150) <= \
           ppm_nonwhite(medium_pdf, tmp / "medium", dpi=150):
            raise AssertionError("bold stroke request did not increase ink")
        if ppm_sha256(bold_pdf, tmp / "bold", dpi=150) != \
           ppm_sha256(bold_fractional_pdf, tmp / "bold-fractional", dpi=150):
            raise AssertionError("fractional stroke request rounded")

        invalid_default = write(tmp / "invalid-default.pcl",
                                ESC + b"(s0p10h12v0s3b3T" +
                                ESC + b"(99@" + b"Stroke sample" + FF)
        explicit_bold = write(tmp / "explicit-bold.pcl",
                              ESC + b"(s0p10h12v0s3b3T" +
                              b"Stroke sample" + FF)
        default_font = write(tmp / "default-font.pcl",
                             ESC + b"(s0p10h12v0s3b3T" +
                             ESC + b"(3@" + b"Stroke sample" + FF)
        default_font_fractional = write(tmp / "default-font-fractional.pcl",
                                        ESC + b"(s0p10h12v0s3b3T" +
                                        ESC + b"(3.9@" +
                                        b"Stroke sample" + FF)
        explicit_medium = write(tmp / "explicit-medium.pcl",
                                ESC + b"(s0p10h12v0s0b3T" +
                                b"Stroke sample" + FF)
        invalid_default_pdf = tmp / "invalid-default.pdf"
        explicit_bold_pdf = tmp / "explicit-bold.pdf"
        default_font_pdf = tmp / "default-font.pdf"
        default_font_fractional_pdf = tmp / "default-font-fractional.pdf"
        explicit_medium_pdf = tmp / "explicit-medium.pdf"
        render(dreamprint, invalid_default, invalid_default_pdf)
        render(dreamprint, explicit_bold, explicit_bold_pdf)
        render(dreamprint, default_font, default_font_pdf)
        render(dreamprint, default_font_fractional, default_font_fractional_pdf)
        render(dreamprint, explicit_medium, explicit_medium_pdf)
        if ppm_sha256(invalid_default_pdf, tmp / "invalid-default",
                      dpi=150) != \
           ppm_sha256(explicit_bold_pdf, tmp / "explicit-bold", dpi=150):
            raise AssertionError("invalid final-@ reset the selected font")
        if ppm_sha256(default_font_pdf, tmp / "default-font", dpi=150) != \
           ppm_sha256(explicit_medium_pdf, tmp / "explicit-medium", dpi=150):
            raise AssertionError("final-3@ did not reset to default font")
        if ppm_sha256(default_font_fractional_pdf,
                      tmp / "default-font-fractional", dpi=150) != \
           ppm_sha256(explicit_medium_pdf, tmp / "explicit-medium", dpi=150):
            raise AssertionError("fractional final-@ selector rounded")

        final_at_primary_0 = write(tmp / "final-at-primary-0.pcl",
                                   ESC + b"(0@" + b"#\\^~" + FF)
        explicit_primary_0e = write(tmp / "explicit-primary-0e.pcl",
                                    ESC + b"(0E" + b"#\\^~" + FF)
        final_at_secondary_1 = write(tmp / "final-at-secondary-1.pcl",
                                     ESC + b")1@" + b"\x0e" +
                                     b"#\\^~" + FF)
        explicit_secondary_0e = write(tmp / "explicit-secondary-0e.pcl",
                                      ESC + b")0E" + b"\x0e" +
                                      b"#\\^~" + FF)
        final_at_secondary_2 = write(tmp / "final-at-secondary-2.pcl",
                                     ESC + b"(2U" + ESC + b")2@" +
                                     b"\x0e" + b"$^`~" + FF)
        explicit_secondary_2u = write(tmp / "explicit-secondary-2u.pcl",
                                      ESC + b")2U" + b"\x0e" +
                                      b"$^`~" + FF)
        final_at_primary_2 = write(tmp / "final-at-primary-2.pcl",
                                   ESC + b"(1E" + ESC + b"(2@" +
                                   b"#\\^~" + FF)
        explicit_primary_1e = write(tmp / "explicit-primary-1e.pcl",
                                    ESC + b"(1E" + b"#\\^~" + FF)
        final_at_default_font_stream = write(
            tmp / "final-at-default-font-stream.pcl",
            ESC + b"(0@" + ESC + b")0@" + ESC + b")1@" +
            ESC + b")2@" + ESC + b"(3@" +
            ESC + b"(s0p10h12v0s0b3T" + bytes([0x85, 0x86]) + FF)
        explicit_primary_0n = write(
            tmp / "explicit-primary-0n.pcl",
            ESC + b"(0N" + ESC + b"(s0p10h12v0s0b3T" +
            bytes([0x85, 0x86]) + FF)
        final_at_primary_0_pdf = tmp / "final-at-primary-0.pdf"
        explicit_primary_0e_pdf = tmp / "explicit-primary-0e.pdf"
        final_at_secondary_1_pdf = tmp / "final-at-secondary-1.pdf"
        explicit_secondary_0e_pdf = tmp / "explicit-secondary-0e.pdf"
        final_at_secondary_2_pdf = tmp / "final-at-secondary-2.pdf"
        explicit_secondary_2u_pdf = tmp / "explicit-secondary-2u.pdf"
        final_at_primary_2_pdf = tmp / "final-at-primary-2.pdf"
        explicit_primary_1e_pdf = tmp / "explicit-primary-1e.pdf"
        final_at_default_font_stream_pdf = \
            tmp / "final-at-default-font-stream.pdf"
        explicit_primary_0n_pdf = tmp / "explicit-primary-0n.pdf"
        render(dreamprint, final_at_primary_0, final_at_primary_0_pdf)
        render(dreamprint, explicit_primary_0e, explicit_primary_0e_pdf)
        render(dreamprint, final_at_secondary_1, final_at_secondary_1_pdf)
        render(dreamprint, explicit_secondary_0e, explicit_secondary_0e_pdf)
        render(dreamprint, final_at_secondary_2, final_at_secondary_2_pdf)
        render(dreamprint, explicit_secondary_2u, explicit_secondary_2u_pdf)
        render(dreamprint, final_at_primary_2, final_at_primary_2_pdf)
        render(dreamprint, explicit_primary_1e, explicit_primary_1e_pdf)
        render(dreamprint, final_at_default_font_stream,
               final_at_default_font_stream_pdf)
        render(dreamprint, explicit_primary_0n, explicit_primary_0n_pdf)
        if ppm_sha256(final_at_primary_0_pdf, tmp / "final-at-primary-0",
                      dpi=150) != \
           ppm_sha256(explicit_primary_0e_pdf, tmp / "explicit-primary-0e",
                      dpi=150):
            raise AssertionError("final-@0 did not use the primary default-symbol word")
        if ppm_sha256(final_at_secondary_1_pdf,
                      tmp / "final-at-secondary-1", dpi=150) != \
           ppm_sha256(explicit_secondary_0e_pdf,
                      tmp / "explicit-secondary-0e", dpi=150):
            raise AssertionError("secondary final-@1 did not use the primary default-symbol word")
        if ppm_sha256(final_at_secondary_2_pdf,
                      tmp / "final-at-secondary-2", dpi=150) != \
           ppm_sha256(explicit_secondary_2u_pdf,
                      tmp / "explicit-secondary-2u", dpi=150):
            raise AssertionError("secondary final-@2 did not copy the primary requested word")
        if ppm_sha256(final_at_primary_2_pdf, tmp / "final-at-primary-2",
                      dpi=150) != \
           ppm_sha256(explicit_primary_1e_pdf, tmp / "explicit-primary-1e",
                      dpi=150):
            raise AssertionError("primary final-@2 did not preserve the requested word")
        if ppm_sha256(final_at_default_font_stream_pdf,
                      tmp / "final-at-default-font-stream", dpi=150) != \
           ppm_sha256(explicit_primary_0n_pdf, tmp / "explicit-primary-0n",
                      dpi=150):
            raise AssertionError("final-@3 did not select the default-font symbol word")

        font_id_bold = write(tmp / "font-id-bold.pcl",
                             ESC + b"(7X" + b"Stroke sample" + FF)
        font_id_bold_fractional = write(tmp / "font-id-bold-fractional.pcl",
                                        ESC + b"(7.9X" +
                                        b"Stroke sample" + FF)
        font_id_bold_pdf = tmp / "font-id-bold.pdf"
        font_id_bold_fractional_pdf = tmp / "font-id-bold-fractional.pdf"
        render(dreamprint, font_id_bold, font_id_bold_pdf)
        render(dreamprint, font_id_bold_fractional,
               font_id_bold_fractional_pdf)
        if ppm_sha256(font_id_bold_pdf, tmp / "font-id-bold", dpi=150) != \
           ppm_sha256(explicit_bold_pdf, tmp / "explicit-bold", dpi=150):
            raise AssertionError("built-in primary font ID 7 did not select bold Courier")
        if ppm_sha256(font_id_bold_fractional_pdf,
                      tmp / "font-id-bold-fractional", dpi=150) != \
           ppm_sha256(explicit_bold_pdf, tmp / "explicit-bold", dpi=150):
            raise AssertionError("fractional final-X font ID rounded")

        secondary_line = write(tmp / "secondary-line.pcl",
                               ESC + b")8X" + b"\x0e" +
                               b"Line sample" + FF)
        explicit_secondary_line = write(
            tmp / "explicit-secondary-line.pcl",
            ESC + b")s0p16.66h8.5v0s0b0T" + b"\x0e" +
            b"Line sample" + FF,
        )
        secondary_line_pdf = tmp / "secondary-line.pdf"
        explicit_secondary_line_pdf = tmp / "explicit-secondary-line.pdf"
        render(dreamprint, secondary_line, secondary_line_pdf)
        render(dreamprint, explicit_secondary_line, explicit_secondary_line_pdf)
        if ppm_sha256(secondary_line_pdf, tmp / "secondary-line",
                      dpi=150) != \
           ppm_sha256(explicit_secondary_line_pdf,
                      tmp / "explicit-secondary-line", dpi=150):
            raise AssertionError("built-in secondary font ID 8 did not select line-printer context")

        primary_symbol_miss = write(
            tmp / "primary-symbol-miss.pcl",
            ESC + b"(1234U" +
            ESC + b"(s0p10h12v0s0b3T" + b"!!" + FF,
        )
        primary_symbol_fallback = write(
            tmp / "primary-symbol-fallback.pcl",
            ESC + b"(s0p10h12v0s0b3T" + b"!!" + FF,
        )
        secondary_symbol_miss = write(
            tmp / "secondary-symbol-miss.pcl",
            ESC + b")1234U" +
            ESC + b")s0p16h8v0s0b0T" + b"\x0e" + b"!!" + FF,
        )
        secondary_symbol_fallback = write(
            tmp / "secondary-symbol-fallback.pcl",
            ESC + b")s0p16h8v0s0b0T" + b"\x0e" + b"!!" + FF,
        )
        primary_symbol_miss_pdf = tmp / "primary-symbol-miss.pdf"
        primary_symbol_fallback_pdf = tmp / "primary-symbol-fallback.pdf"
        secondary_symbol_miss_pdf = tmp / "secondary-symbol-miss.pdf"
        secondary_symbol_fallback_pdf = tmp / "secondary-symbol-fallback.pdf"
        render(dreamprint, primary_symbol_miss, primary_symbol_miss_pdf)
        render(dreamprint, primary_symbol_fallback,
               primary_symbol_fallback_pdf)
        render(dreamprint, secondary_symbol_miss, secondary_symbol_miss_pdf)
        render(dreamprint, secondary_symbol_fallback,
               secondary_symbol_fallback_pdf)
        if ppm_sha256(primary_symbol_miss_pdf,
                      tmp / "primary-symbol-miss", dpi=150) != \
           ppm_sha256(primary_symbol_fallback_pdf,
                      tmp / "primary-symbol-fallback", dpi=150):
            raise AssertionError("primary symbol miss did not fall back before rendering")
        if ppm_sha256(secondary_symbol_miss_pdf,
                      tmp / "secondary-symbol-miss", dpi=150) != \
           ppm_sha256(secondary_symbol_fallback_pdf,
                      tmp / "secondary-symbol-fallback", dpi=150):
            raise AssertionError("secondary symbol miss did not fall back before rendering")
        if "!!" not in "".join(pdftotext(primary_symbol_miss_pdf).split()):
            raise AssertionError("primary symbol miss lost selectable text")
        if "!!" not in "".join(pdftotext(secondary_symbol_miss_pdf).split()):
            raise AssertionError("secondary symbol miss lost selectable text")

        typeface_low_priority = write(
            tmp / "typeface-low-priority.pcl",
            ESC + b"(s0p10h12v0s0b0T" + b"Stroke sample" + FF,
        )
        spacing_fractional = write(
            tmp / "spacing-fractional.pcl",
            ESC + b"(s0.9p10h12v0s0b3T" + b"Stroke sample" + FF,
        )
        typeface_fractional = write(
            tmp / "typeface-fractional.pcl",
            ESC + b"(s0p10h12v0s0b3.9T" + b"Stroke sample" + FF,
        )
        typeface_low_priority_pdf = tmp / "typeface-low-priority.pdf"
        spacing_fractional_pdf = tmp / "spacing-fractional.pdf"
        typeface_fractional_pdf = tmp / "typeface-fractional.pdf"
        render(dreamprint, typeface_low_priority, typeface_low_priority_pdf)
        render(dreamprint, spacing_fractional, spacing_fractional_pdf)
        render(dreamprint, typeface_fractional, typeface_fractional_pdf)
        if ppm_sha256(typeface_low_priority_pdf,
                      tmp / "typeface-low-priority", dpi=150) != \
           ppm_sha256(explicit_medium_pdf, tmp / "explicit-medium", dpi=150):
            raise AssertionError("typeface request overrode higher-priority resident font filters")
        if ppm_sha256(spacing_fractional_pdf, tmp / "spacing-fractional",
                      dpi=150) != \
           ppm_sha256(explicit_medium_pdf, tmp / "explicit-medium", dpi=150):
            raise AssertionError("fractional spacing request rounded")
        if ppm_sha256(typeface_fractional_pdf, tmp / "typeface-fractional",
                      dpi=150) != \
           ppm_sha256(explicit_medium_pdf, tmp / "explicit-medium", dpi=150):
            raise AssertionError("fractional typeface request rounded")

        prop_pitch_10 = write(
            tmp / "prop-pitch-10.pcl",
            ESC + b"(s1p10h12v0s0b3T" + b"ii" + FF,
        )
        prop_pitch_line = write(
            tmp / "prop-pitch-line.pcl",
            ESC + b"(s1p16.66h12v0s0b3T" + b"ii" + FF,
        )
        fixed_pitch_10 = write(
            tmp / "fixed-pitch-10.pcl",
            ESC + b"(s0p10h12v0s0b3T" + b"ii" + FF,
        )
        fixed_pitch_line = write(
            tmp / "fixed-pitch-line.pcl",
            ESC + b"(s0p16.66h12v0s0b3T" + b"ii" + FF,
        )
        prop_pitch_10_pdf = tmp / "prop-pitch-10.pdf"
        prop_pitch_line_pdf = tmp / "prop-pitch-line.pdf"
        fixed_pitch_10_pdf = tmp / "fixed-pitch-10.pdf"
        fixed_pitch_line_pdf = tmp / "fixed-pitch-line.pdf"
        render(dreamprint, prop_pitch_10, prop_pitch_10_pdf)
        render(dreamprint, prop_pitch_line, prop_pitch_line_pdf)
        render(dreamprint, fixed_pitch_10, fixed_pitch_10_pdf)
        render(dreamprint, fixed_pitch_line, fixed_pitch_line_pdf)
        if ppm_sha256(prop_pitch_10_pdf, tmp / "prop-pitch-10",
                      dpi=150) != \
           ppm_sha256(prop_pitch_line_pdf, tmp / "prop-pitch-line",
                      dpi=150):
            raise AssertionError("proportional spacing miss applied pitch filtering")
        if ppm_sha256(fixed_pitch_10_pdf, tmp / "fixed-pitch-10",
                      dpi=150) == \
           ppm_sha256(fixed_pitch_line_pdf, tmp / "fixed-pitch-line",
                      dpi=150):
            raise AssertionError("fixed spacing match skipped pitch filtering")

        prop_pitch_mode_line = write(
            tmp / "prop-pitch-mode-line.pcl",
            ESC + b"(s1p10h12v0s0b3T" + ESC + b"&k2S" + b"ii" + FF,
        )
        prop_pitch_mode_line_pdf = tmp / "prop-pitch-mode-line.pdf"
        render(dreamprint, prop_pitch_mode_line, prop_pitch_mode_line_pdf)
        if ppm_sha256(prop_pitch_10_pdf, tmp / "prop-pitch-10",
                      dpi=150) != \
           ppm_sha256(prop_pitch_mode_line_pdf,
                      tmp / "prop-pitch-mode-line", dpi=150):
            raise AssertionError("pitch-mode refresh bypassed selected-context HMI")

        pitch_positive = write(tmp / "pitch-positive.pcl",
                               ESC + b"(s10H" + b"Pitch sample" + FF)
        pitch_negative = write(tmp / "pitch-negative.pcl",
                               ESC + b"(s-10H" + b"Pitch sample" + FF)
        style_positive = write(tmp / "style-positive.pcl",
                               ESC + b"(s1S" + b"Italic sample" + FF)
        style_negative = write(tmp / "style-negative.pcl",
                               ESC + b"(s-1S" + b"Italic sample" + FF)
        style_fractional = write(tmp / "style-fractional.pcl",
                                 ESC + b"(s1.9S" + b"Italic sample" + FF)
        pitch_positive_pdf = tmp / "pitch-positive.pdf"
        pitch_negative_pdf = tmp / "pitch-negative.pdf"
        style_positive_pdf = tmp / "style-positive.pdf"
        style_negative_pdf = tmp / "style-negative.pdf"
        style_fractional_pdf = tmp / "style-fractional.pdf"
        render(dreamprint, pitch_positive, pitch_positive_pdf)
        render(dreamprint, pitch_negative, pitch_negative_pdf)
        render(dreamprint, style_positive, style_positive_pdf)
        render(dreamprint, style_negative, style_negative_pdf)
        render(dreamprint, style_fractional, style_fractional_pdf)
        if ppm_sha256(pitch_positive_pdf, tmp / "pitch-positive", dpi=150) != \
           ppm_sha256(pitch_negative_pdf, tmp / "pitch-negative", dpi=150):
            raise AssertionError("negative pitch request did not match positive pitch")
        if ppm_sha256(style_positive_pdf, tmp / "style-positive", dpi=150) != \
           ppm_sha256(style_negative_pdf, tmp / "style-negative", dpi=150):
            raise AssertionError("negative style request did not match positive style")
        if ppm_sha256(style_positive_pdf, tmp / "style-positive", dpi=150) != \
           ppm_sha256(style_fractional_pdf, tmp / "style-fractional", dpi=150):
            raise AssertionError("fractional style request rounded")
        if pdftotext(style_positive_pdf).strip() != "Italic sample":
            raise AssertionError("style selector lost selectable input text")

        underline_fixed = write(tmp / "underline-fixed.pcl",
                                ESC + b"&d0D" + b"A\tB" +
                                ESC + b"&d@" + FF)
        underline_span = write(tmp / "underline-span.pcl",
                               ESC + b"&d3D" + b"A\tB" +
                               ESC + b"&d@" + FF)
        underline_space = write(tmp / "underline-space.pcl",
                                ESC + b"&d0D" + b"A B" +
                                ESC + b"&d@" + FF)
        underline_fixed_pdf = tmp / "underline-fixed.pdf"
        underline_span_pdf = tmp / "underline-span.pdf"
        underline_space_pdf = tmp / "underline-space.pdf"
        render(dreamprint, underline_fixed, underline_fixed_pdf)
        render(dreamprint, underline_span, underline_span_pdf)
        render(dreamprint, underline_space, underline_space_pdf)
        if "AB" not in "".join(pdftotext(underline_fixed_pdf).split()):
            raise AssertionError("fixed underline span text did not extract")
        if "AB" not in "".join(pdftotext(underline_span_pdf).split()):
            raise AssertionError("floating underline span text did not extract")
        if pdftotext(underline_space_pdf).strip() != "A B":
            raise AssertionError("underlined space did not remain selectable")
        if ppm_rect_nonwhite(underline_fixed_pdf, tmp / "underline-fixed",
                             130, 113, 180, 118, dpi=300) == 0:
            raise AssertionError("fixed underline span did not cover tab gap")
        if ppm_rect_nonwhite(underline_span_pdf, tmp / "underline-span",
                             130, 90, 180, 95, dpi=300) == 0:
            raise AssertionError("floating underline span did not cover tab gap")
        if ppm_sha256(underline_fixed_pdf, tmp / "underline-fixed",
                      dpi=150) == \
           ppm_sha256(underline_span_pdf, tmp / "underline-span",
                      dpi=150):
            raise AssertionError("fixed and floating underline selectors rendered identically")

        underline_negative = write(tmp / "underline-negative.pcl",
                                   ESC + b"&d-3D" + b"A\tB" +
                                   ESC + b"&d@" + FF)
        underline_fractional = write(tmp / "underline-fractional.pcl",
                                     ESC + b"&d3.9D" + b"A\tB" +
                                     ESC + b"&d@" + FF)
        underline_other = write(tmp / "underline-other.pcl",
                                ESC + b"&d4D" + b"A\tB" +
                                ESC + b"&d@" + FF)
        underline_negative_pdf = tmp / "underline-negative.pdf"
        underline_fractional_pdf = tmp / "underline-fractional.pdf"
        underline_other_pdf = tmp / "underline-other.pdf"
        render(dreamprint, underline_negative, underline_negative_pdf)
        render(dreamprint, underline_fractional, underline_fractional_pdf)
        render(dreamprint, underline_other, underline_other_pdf)
        if ppm_sha256(underline_span_pdf, tmp / "underline-span",
                      dpi=150) != \
           ppm_sha256(underline_negative_pdf, tmp / "underline-negative",
                      dpi=150):
            raise AssertionError("negative underline selector did not match positive")
        if ppm_sha256(underline_span_pdf, tmp / "underline-span",
                      dpi=150) != \
           ppm_sha256(underline_fractional_pdf, tmp / "underline-fractional",
                      dpi=150):
            raise AssertionError("fractional underline selector rounded")
        if ppm_sha256(underline_fixed_pdf, tmp / "underline-fixed",
                      dpi=150) != \
           ppm_sha256(underline_other_pdf, tmp / "underline-other",
                      dpi=150):
            raise AssertionError("non-3D underline selector did not stay fixed")

        soft = write(tmp / "soft.pcl",
                     ESC + b"*c4660D" +
                     ESC + b"*c41E" +
                     ESC + b")s18W" +
                     bytes.fromhex(
                         "f0 0f aa 55 3c c3 81 7e ff 00 18 e7 "
                         "24 db 42 bd 66 99") +
                     ESC + b"(4660X" + b")" + FF)
        soft_negative = write(tmp / "soft-negative.pcl",
                              ESC + b"*c-4660D" +
                              ESC + b"*c-41E" +
                              ESC + b")s18W" +
                              bytes.fromhex(
                                  "f0 0f aa 55 3c c3 81 7e ff 00 18 e7 "
                                  "24 db 42 bd 66 99") +
                              ESC + b"(4660X" + b")" + FF)
        soft_fractional = write(tmp / "soft-fractional.pcl",
                                ESC + b"*c4660.9D" +
                                ESC + b"*c41.9E" +
                                ESC + b")s18W" +
                                bytes.fromhex(
                                    "f0 0f aa 55 3c c3 81 7e ff 00 18 e7 "
                                    "24 db 42 bd 66 99") +
                                ESC + b"(4660X" + b")" + FF)
        soft_lowercase_w = write(tmp / "soft-lowercase-w.pcl",
                                 ESC + b"*c4660D" +
                                 ESC + b"*c41E" +
                                 ESC + b")s18w1W" +
                                 bytes.fromhex(
                                     "f0 0f aa 55 3c c3 81 7e ff 00 18 e7 "
                                     "24 db 42 bd 66 99") +
                                 ESC + b"(4660X" + b")" + FF)
        soft_pdf = tmp / "soft.pdf"
        soft_negative_pdf = tmp / "soft-negative.pdf"
        soft_fractional_pdf = tmp / "soft-fractional.pdf"
        soft_lowercase_w_pdf = tmp / "soft-lowercase-w.pdf"
        render(dreamprint, soft, soft_pdf)
        render(dreamprint, soft_negative, soft_negative_pdf)
        render(dreamprint, soft_fractional, soft_fractional_pdf)
        render(dreamprint, soft_lowercase_w, soft_lowercase_w_pdf)
        if ")" not in pdftotext(soft_pdf):
            raise AssertionError("downloaded glyph text did not extract")
        if ppm_nonwhite(soft_pdf, tmp / "soft") < 5:
            raise AssertionError("downloaded glyph render looks blank")
        if ppm_sha256(soft_pdf, tmp / "soft", dpi=150) != \
           ppm_sha256(soft_negative_pdf, tmp / "soft-negative", dpi=150):
            raise AssertionError("negative downloaded font id/code did not normalize")
        if ppm_sha256(soft_pdf, tmp / "soft", dpi=150) != \
           ppm_sha256(soft_fractional_pdf, tmp / "soft-fractional", dpi=150):
            raise AssertionError("fractional downloaded font id/code rounded")
        if ppm_sha256(soft_pdf, tmp / "soft", dpi=150) != \
           ppm_sha256(soft_lowercase_w_pdf, tmp / "soft-lowercase-w",
                      dpi=150):
            raise AssertionError(
                "lowercase downloaded-font W record did not survive to uppercase W")

        soft_metric_header = bytearray(64)
        soft_metric_header[0] = 0x00
        soft_metric_header[1] = 0x01
        soft_metric_header[3] = 0x00
        soft_metric_header[8] = 0x03
        soft_metric_header[11] = 0x01
        soft_metric_header[16] = 0x07
        soft_metric_header[17] = 0xd0
        soft_metric_header[18] = 0x04
        soft_metric_header[19] = 0xb0
        soft_metric_payload = bytes(soft_metric_header)
        soft_metric_glyph = b"\xe0\xe0\xe0"
        soft_metric_download = write(
            tmp / "soft-metric-download.pcl",
            ESC + b"*c4661D" +
            ESC + b"(s64W" + soft_metric_payload +
            ESC + b"*c65E" +
            ESC + b"(s3W" + soft_metric_glyph +
            b"AA" + FF)
        soft_metric_resync = write(
            tmp / "soft-metric-resync.pcl",
            ESC + b"*c4661D" +
            ESC + b"(s64W" + soft_metric_payload +
            ESC + b"(s20H" +
            ESC + b"*c65E" +
            ESC + b"(s3W" + soft_metric_glyph +
            b"AA" + FF)
        soft_metric_housekeeping = write(
            tmp / "soft-metric-housekeeping.pcl",
            ESC + b"*c4661D" +
            ESC + b"(s64W" + soft_metric_payload +
            ESC + b"(4661X" +
            ESC + b"(s20H" +
            ESC + b"*c6F" +
            ESC + b"*c65E" +
            ESC + b"(s3W" + soft_metric_glyph +
            b"AA" + FF)
        soft_metric_download_pdf = tmp / "soft-metric-download.pdf"
        soft_metric_resync_pdf = tmp / "soft-metric-resync.pdf"
        soft_metric_housekeeping_pdf = tmp / "soft-metric-housekeeping.pdf"
        render(dreamprint, soft_metric_download, soft_metric_download_pdf)
        render(dreamprint, soft_metric_resync, soft_metric_resync_pdf)
        render(dreamprint, soft_metric_housekeeping,
               soft_metric_housekeeping_pdf)
        if "AA" not in pdftotext(soft_metric_download_pdf):
            raise AssertionError(
                "downloaded font metric stream text did not extract")
        if ppm_nonwhite(soft_metric_download_pdf,
                        tmp / "soft-metric-download") < 5:
            raise AssertionError("downloaded font metric stream looks blank")
        if ppm_sha256(soft_metric_download_pdf,
                      tmp / "soft-metric-download", dpi=150) != \
           ppm_sha256(soft_metric_resync_pdf,
                      tmp / "soft-metric-resync", dpi=150):
            raise AssertionError(
                "downloaded font payload metrics did not refresh active HMI")
        if ppm_sha256(soft_metric_download_pdf,
                      tmp / "soft-metric-download", dpi=150) != \
           ppm_sha256(soft_metric_housekeeping_pdf,
                      tmp / "soft-metric-housekeeping", dpi=150):
            raise AssertionError(
                "downloaded font housekeeping did not refresh active HMI")

        soft_descriptor_oversized = bytearray(64)
        soft_descriptor_capped = bytearray(64)
        for descriptor in (soft_descriptor_oversized, soft_descriptor_capped):
            descriptor[0x22] = 0x01
            descriptor[0x23] = 0x15
        soft_descriptor_oversized[0x24] = 0xff
        soft_descriptor_oversized[0x25] = 0xff
        soft_descriptor_oversized[0x28] = 0xff
        soft_descriptor_oversized[0x29] = 0xff
        soft_descriptor_capped[0x24] = 0x41
        soft_descriptor_capped[0x25] = 0xa0
        soft_descriptor_capped[0x28] = 0x2a
        soft_descriptor_capped[0x29] = 0xaa
        soft_descriptor_glyph = b"\xe0\xe0\xe0"
        soft_descriptor_clamped = write(
            tmp / "soft-descriptor-clamped.pcl",
            ESC + b"*c4662D" +
            ESC + b"(s64W" + bytes(soft_descriptor_oversized) +
            ESC + b"*c65E" +
            ESC + b"(s3W" + soft_descriptor_glyph +
            b"AA" + FF)
        soft_descriptor_explicit = write(
            tmp / "soft-descriptor-explicit.pcl",
            ESC + b"*c4662D" +
            ESC + b"(s64W" + bytes(soft_descriptor_capped) +
            ESC + b"*c65E" +
            ESC + b"(s3W" + soft_descriptor_glyph +
            b"AA" + FF)
        soft_descriptor_clamped_pdf = tmp / "soft-descriptor-clamped.pdf"
        soft_descriptor_explicit_pdf = tmp / "soft-descriptor-explicit.pdf"
        render(dreamprint, soft_descriptor_clamped,
               soft_descriptor_clamped_pdf)
        render(dreamprint, soft_descriptor_explicit,
               soft_descriptor_explicit_pdf)
        if "AA" not in pdftotext(soft_descriptor_clamped_pdf):
            raise AssertionError(
                "downloaded descriptor clamp stream text did not extract")
        if ppm_sha256(soft_descriptor_clamped_pdf,
                      tmp / "soft-descriptor-clamped", dpi=300) != \
           ppm_sha256(soft_descriptor_explicit_pdf,
                      tmp / "soft-descriptor-explicit", dpi=300):
            raise AssertionError(
                "downloaded descriptor pitch/height words were not capped")

        partial_descriptor_glyph = bytes.fromhex(
            "00 00 00 00 0c 01 00 03 00 10 00 00 f0 0f aa 55")
        padded_descriptor_glyph = partial_descriptor_glyph + b"\x00\x00"
        soft_partial_descriptor = write(
            tmp / "soft-partial-descriptor.pcl",
            ESC + b"*c4664D" +
            ESC + b"*c65E" +
            ESC + b"(s16W" + partial_descriptor_glyph +
            ESC + b"(4664X" +
            b"A" + FF)
        soft_padded_descriptor = write(
            tmp / "soft-padded-descriptor.pcl",
            ESC + b"*c4664D" +
            ESC + b"*c65E" +
            ESC + b"(s18W" + padded_descriptor_glyph +
            ESC + b"(4664X" +
            b"A" + FF)
        soft_partial_descriptor_pdf = tmp / "soft-partial-descriptor.pdf"
        soft_padded_descriptor_pdf = tmp / "soft-padded-descriptor.pdf"
        render(dreamprint, soft_partial_descriptor,
               soft_partial_descriptor_pdf)
        render(dreamprint, soft_padded_descriptor,
               soft_padded_descriptor_pdf)
        if pdftotext(soft_partial_descriptor_pdf).strip() != "A":
            raise AssertionError(
                "partial descriptor glyph text did not extract")
        if ppm_sha256(soft_partial_descriptor_pdf,
                      tmp / "soft-partial-descriptor", dpi=300) != \
           ppm_sha256(soft_padded_descriptor_pdf,
                      tmp / "soft-padded-descriptor", dpi=300):
            raise AssertionError(
                "partial descriptor glyph shape was not zero-filled")

        completed_descriptor_glyph = padded_descriptor_glyph[:-2] + b"\xc3\x3c"
        soft_descriptor_continued = write(
            tmp / "soft-descriptor-continued.pcl",
            ESC + b"*c4665D" +
            ESC + b"*c65E" +
            ESC + b"(s16W" + partial_descriptor_glyph +
            ESC + b"(s2W" + b"\xc3\x3c" +
            ESC + b"(4665X" +
            b"A" + FF)
        soft_descriptor_complete = write(
            tmp / "soft-descriptor-complete.pcl",
            ESC + b"*c4665D" +
            ESC + b"*c65E" +
            ESC + b"(s18W" + completed_descriptor_glyph +
            ESC + b"(4665X" +
            b"A" + FF)
        soft_descriptor_continued_pdf = tmp / "soft-descriptor-continued.pdf"
        soft_descriptor_complete_pdf = tmp / "soft-descriptor-complete.pdf"
        render(dreamprint, soft_descriptor_continued,
               soft_descriptor_continued_pdf)
        render(dreamprint, soft_descriptor_complete,
               soft_descriptor_complete_pdf)
        if pdftotext(soft_descriptor_continued_pdf).strip() != "A":
            raise AssertionError(
                "continued descriptor glyph text did not extract")
        if ppm_sha256(soft_descriptor_continued_pdf,
                      tmp / "soft-descriptor-continued", dpi=300) != \
           ppm_sha256(soft_descriptor_complete_pdf,
                      tmp / "soft-descriptor-complete", dpi=300):
            raise AssertionError(
                "downloaded descriptor continuation did not complete glyph")

        split_partial_descriptor_glyph = bytes.fromhex(
            "00 00 00 00 0c 02 00 02 00 18 00 00 a0 a1 b0")
        split_completed_descriptor_glyph = split_partial_descriptor_glyph + \
            b"\xc0\xc1\xd0"
        soft_split_descriptor_continued = write(
            tmp / "soft-split-descriptor-continued.pcl",
            ESC + b"*c4666D" +
            ESC + b"*c65E" +
            ESC + b"(s15W" + split_partial_descriptor_glyph +
            ESC + b"(s3W" + b"\xc0\xc1\xd0" +
            ESC + b"(4666X" +
            b"A" + FF)
        soft_split_descriptor_complete = write(
            tmp / "soft-split-descriptor-complete.pcl",
            ESC + b"*c4666D" +
            ESC + b"*c65E" +
            ESC + b"(s18W" + split_completed_descriptor_glyph +
            ESC + b"(4666X" +
            b"A" + FF)
        soft_split_descriptor_continued_pdf = \
            tmp / "soft-split-descriptor-continued.pdf"
        soft_split_descriptor_complete_pdf = \
            tmp / "soft-split-descriptor-complete.pdf"
        render(dreamprint, soft_split_descriptor_continued,
               soft_split_descriptor_continued_pdf)
        render(dreamprint, soft_split_descriptor_complete,
               soft_split_descriptor_complete_pdf)
        if pdftotext(soft_split_descriptor_continued_pdf).strip() != "A":
            raise AssertionError(
                "continued split-plane descriptor text did not extract")
        if ppm_sha256(soft_split_descriptor_continued_pdf,
                      tmp / "soft-split-descriptor-continued", dpi=300) != \
           ppm_sha256(soft_split_descriptor_complete_pdf,
                      tmp / "soft-split-descriptor-complete", dpi=300):
            raise AssertionError(
                "split-plane descriptor continuation did not complete glyph")

        soft_delete_refresh_header = bytearray(64)
        soft_delete_refresh_header[0] = 0x00
        soft_delete_refresh_header[1] = 0x01
        soft_delete_refresh_header[8] = 0x03
        soft_delete_refresh_header[11] = 0x01
        soft_delete_refresh_header[16] = 0x07
        soft_delete_refresh_header[17] = 0xd0
        soft_delete_refresh_header[18] = 0x04
        soft_delete_refresh_header[19] = 0xb0
        soft_delete_refresh_prefix = (
            ESC + b"*c4663D" +
            ESC + b"(s64W" + bytes(soft_delete_refresh_header) +
            ESC + b"*c0F")
        soft_delete_refresh = write(
            tmp / "soft-delete-refresh.pcl",
            soft_delete_refresh_prefix + b"ii" + FF)
        soft_delete_refresh_expected = write(
            tmp / "soft-delete-refresh-expected.pcl",
            soft_delete_refresh_prefix + b"\x0f" + b"ii" + FF)
        soft_delete_refresh_pdf = tmp / "soft-delete-refresh.pdf"
        soft_delete_refresh_expected_pdf = \
            tmp / "soft-delete-refresh-expected.pdf"
        render(dreamprint, soft_delete_refresh, soft_delete_refresh_pdf)
        render(dreamprint, soft_delete_refresh_expected,
               soft_delete_refresh_expected_pdf)
        if "ii" not in pdftotext(soft_delete_refresh_pdf):
            raise AssertionError("downloaded font delete refresh lost text")
        if ppm_sha256(soft_delete_refresh_pdf,
                      tmp / "soft-delete-refresh", dpi=300) != \
           ppm_sha256(soft_delete_refresh_expected_pdf,
                      tmp / "soft-delete-refresh-expected", dpi=300):
            raise AssertionError(
                "downloaded font delete did not refresh active metrics")

        soft_after_reset_permanent = write(
            tmp / "soft-after-reset-permanent.pcl",
            ESC + b"*c4660D" +
            ESC + b"*c41E" +
            ESC + b")s18W" +
            bytes.fromhex(
                "f0 0f aa 55 3c c3 81 7e ff 00 18 e7 "
                "24 db 42 bd 66 99") +
            ESC + b"*c5F" + ESC + b"E" +
            ESC + b"(4660X" + b")" + FF)
        soft_after_reset_temporary = write(
            tmp / "soft-after-reset-temporary.pcl",
            ESC + b"*c4660D" +
            ESC + b"*c41E" +
            ESC + b")s18W" +
            bytes.fromhex(
                "f0 0f aa 55 3c c3 81 7e ff 00 18 e7 "
                "24 db 42 bd 66 99") +
            ESC + b"E" + ESC + b"(4660X" + b")" + FF)
        soft_after_reset_default = write(
            tmp / "soft-after-reset-default.pcl", b")" + FF)
        soft_after_reset_permanent_pdf = tmp / "soft-after-reset-permanent.pdf"
        soft_after_reset_temporary_pdf = tmp / "soft-after-reset-temporary.pdf"
        soft_after_reset_default_pdf = tmp / "soft-after-reset-default.pdf"
        render(dreamprint, soft_after_reset_permanent,
               soft_after_reset_permanent_pdf)
        render(dreamprint, soft_after_reset_temporary,
               soft_after_reset_temporary_pdf)
        render(dreamprint, soft_after_reset_default,
               soft_after_reset_default_pdf)
        if ppm_sha256(soft_after_reset_permanent_pdf,
                      tmp / "soft-after-reset-permanent", dpi=150) != \
           ppm_sha256(soft_pdf, tmp / "soft-again", dpi=150):
            raise AssertionError("permanent downloaded font did not survive reset")
        if ppm_sha256(soft_after_reset_temporary_pdf,
                      tmp / "soft-after-reset-temporary", dpi=150) != \
           ppm_sha256(soft_after_reset_default_pdf,
                      tmp / "soft-after-reset-default", dpi=150):
            raise AssertionError("temporary downloaded font survived reset")

        payload_control_download = write(
            tmp / "payload-control-download.pcl",
            ESC + b"*c4660D" +
            ESC + b"*c38E" +
            ESC + b")s18W" +
            bytes.fromhex(
                "1a 58 0f aa 55 3c c3 81 7e ff 00 18 e7 "
                "24 db 42 bd 66") +
            b"&" + FF)
        payload_control_download_pdf = tmp / "payload-control-download.pdf"
        render(dreamprint, payload_control_download,
               payload_control_download_pdf)
        if pdftotext(payload_control_download_pdf).strip() != "":
            raise AssertionError("payload-control download leaked drained text")
        if ppm_nonwhite(payload_control_download_pdf,
                        tmp / "payload-control-download", dpi=300) < 100:
            raise AssertionError("payload-control download did not render glyph")

        soft_glyph = bytes.fromhex(
            "f0 0f aa 55 3c c3 81 7e ff 00 18 e7 24 db 42 bd 66 99")
        soft_two_glyphs = (
            ESC + b"*c4661D" +
            ESC + b"*c41E" + ESC + b")s18W" + soft_glyph +
            ESC + b"*c40E" + ESC + b")s18W" + soft_glyph +
            ESC + b"(4661X")
        soft_delete_char = write(tmp / "soft-delete-char.pcl",
                                 soft_two_glyphs +
                                 ESC + b"*c41E" + ESC + b"*c3F" +
                                 b"(" + FF)
        soft_delete_target = write(tmp / "soft-delete-target.pcl",
                                   soft_two_glyphs +
                                   ESC + b"*c41E" + ESC + b"*c3F" +
                                   b")" + FF)
        soft_delete_target_fractional = write(
            tmp / "soft-delete-target-fractional.pcl",
            soft_two_glyphs + ESC + b"*c41E" + ESC + b"*c3.9F" +
            b")" + FF)
        soft_keep_target = write(tmp / "soft-keep-target.pcl",
                                 soft_two_glyphs + b")" + FF)
        soft_keep_char = write(tmp / "soft-keep-char.pcl",
                               soft_two_glyphs + b"(" + FF)
        soft_housekeeping = write(tmp / "soft-housekeeping.pcl",
                                  soft_two_glyphs + ESC + b"*c6F" + b"(" + FF)
        soft_delete_char_pdf = tmp / "soft-delete-char.pdf"
        soft_delete_target_pdf = tmp / "soft-delete-target.pdf"
        soft_delete_target_fractional_pdf = \
            tmp / "soft-delete-target-fractional.pdf"
        soft_keep_target_pdf = tmp / "soft-keep-target.pdf"
        soft_keep_char_pdf = tmp / "soft-keep-char.pdf"
        soft_housekeeping_pdf = tmp / "soft-housekeeping.pdf"
        render(dreamprint, soft_delete_char, soft_delete_char_pdf)
        render(dreamprint, soft_delete_target, soft_delete_target_pdf)
        render(dreamprint, soft_delete_target_fractional,
               soft_delete_target_fractional_pdf)
        render(dreamprint, soft_keep_target, soft_keep_target_pdf)
        render(dreamprint, soft_keep_char, soft_keep_char_pdf)
        render(dreamprint, soft_housekeeping, soft_housekeeping_pdf)
        keep_hash = ppm_sha256(soft_keep_char_pdf, tmp / "soft-keep-char",
                               dpi=150)
        if ppm_sha256(soft_delete_char_pdf, tmp / "soft-delete-char",
                      dpi=150) != keep_hash:
            raise AssertionError("downloaded character delete removed sibling glyph")
        if ppm_sha256(soft_housekeeping_pdf, tmp / "soft-housekeeping",
                      dpi=150) != keep_hash:
            raise AssertionError("downloaded font housekeeping deleted glyphs")
        delete_target_hash = ppm_sha256(soft_delete_target_pdf,
                                        tmp / "soft-delete-target", dpi=150)
        if ppm_sha256(soft_delete_target_fractional_pdf,
                      tmp / "soft-delete-target-fractional",
                      dpi=150) != delete_target_hash:
            raise AssertionError("fractional downloaded font control selector rounded")
        if ppm_sha256(soft_keep_target_pdf, tmp / "soft-keep-target",
                      dpi=150) == delete_target_hash:
            raise AssertionError("downloaded font delete-target regression is not sensitive")

        invalid_resource = (
            ESC + b"*c33E" +
            ESC + b")s80W" +
            bytes.fromhex("00 01 02 03") +
            bytes(76) +
            b"!" + FF
        )
        bang = write(tmp / "bang.pcl", b"!" + FF)
        invalid_resource_pcl = write(tmp / "invalid-resource.pcl",
                                     invalid_resource)
        bang_pdf = tmp / "bang.pdf"
        invalid_resource_pdf = tmp / "invalid-resource.pdf"
        render(dreamprint, bang, bang_pdf)
        render(dreamprint, invalid_resource_pcl, invalid_resource_pdf)
        if pdftotext(invalid_resource_pdf).strip() != "!":
            raise AssertionError("invalid resource header shifted text output")
        if ppm_sha256(bang_pdf, tmp / "bang", dpi=150) != \
           ppm_sha256(invalid_resource_pdf, tmp / "invalid-resource",
                      dpi=150):
            raise AssertionError("invalid resource header installed a glyph")

        resource_header = bytes.fromhex(
            "00 01 02 01 ff ff 00 04 00 06 00 09 01 05 12 34"
            " 50 00 30 00 00 20 99 ab f0 cd 01 02 03 04 05 06"
            " 00 07 00 08 00 00 00 09 ee f0 00 0a 00 0b 00 0c"
            " 41 42 43 44 45 46 47 48 49 4a 4b 4c 4d 4e 4f 50"
        )

        def invalid_resource_case(name, payload):
            stream = (
                ESC + b"*c33E" +
                ESC + b")s" + str(len(payload)).encode("ascii") + b"W" +
                payload +
                b"!" + FF
            )
            source = write(tmp / f"{name}.pcl", stream)
            pdf = tmp / f"{name}.pdf"
            render(dreamprint, source, pdf)
            if pdftotext(pdf).strip() != "!":
                raise AssertionError(f"{name} shifted following printable text")
            if ppm_sha256(bang_pdf, tmp / f"{name}-bang", dpi=150) != \
               ppm_sha256(pdf, tmp / name, dpi=150):
                raise AssertionError(f"{name} installed a resource glyph")

        invalid_resource_payloads = []
        invalid_resource_payloads.append(
            ("resource-invalid-type", resource_header[:3] + b"\x03" +
             resource_header[4:] + bytes(16)))
        invalid_resource_payloads.append(
            ("resource-first-overflow", resource_header[:6] +
             b"\x10\x68" + resource_header[8:] + bytes(16)))
        invalid_resource_payloads.append(
            ("resource-zero-line-count", resource_header[:8] +
             b"\x00\x00" + resource_header[10:] + bytes(16)))
        invalid_resource_payloads.append(
            ("resource-high-line-count", resource_header[:8] +
             b"\x10\x69" + resource_header[10:] + bytes(16)))
        invalid_resource_payloads.append(
            ("resource-reversed-range", resource_header[:6] +
             b"\x00\x0a" + resource_header[8:10] + b"\x00\x05" +
             resource_header[12:] + bytes(16)))
        invalid_resource_payloads.append(
            ("resource-high-range", resource_header[:10] +
             b"\x10\x69" + resource_header[12:] + bytes(16)))
        invalid_resource_payloads.append(
            ("resource-invalid-class", resource_header[:12] + b"\x02" +
             resource_header[13:] + bytes(16)))
        invalid_resource_payloads.append(
            ("resource-short-budget", resource_header[:8]))
        for name, payload in invalid_resource_payloads:
            invalid_resource_case(name, payload)

        def resource_width_case(name, header):
            stream = (
                ESC + b"*c4660D" +
                ESC + b"*c33E" +
                header +
                ESC + b")s3W" + b"\xff\xff\xff" +
                ESC + b"(4660X" +
                b"!" + FF
            )
            source = write(tmp / f"{name}.pcl", stream)
            pdf = tmp / f"{name}.pdf"
            render(dreamprint, source, pdf)
            if pdftotext(pdf).strip() != "!":
                raise AssertionError(f"{name} resource text did not extract")
            box = ppm_bbox(pdf, tmp / name, dpi=300)
            if box is None:
                raise AssertionError(f"{name} resource render is blank")
            return box[2] - box[0] + 1

        no_resource_width = resource_width_case("soft-no-resource", b"")
        type1_width = resource_width_case(
            "soft-type1-resource",
            ESC + b")s80W" + resource_header + bytes(16),
        )
        type2_width = resource_width_case(
            "soft-type2-resource",
            ESC + b")s80W" + resource_header[:3] + b"\x02" +
            resource_header[4:] + bytes(16),
        )
        if not (type1_width == type2_width and
                type1_width < no_resource_width):
            raise AssertionError("resource header did not select glyph shape")

        resource_header_class0 = resource_header[:12] + b"\x00" + \
            resource_header[13:]
        resource_header_class1 = resource_header[:12] + b"\x01" + \
            resource_header[13:]

        def resource_high_char_case(name, header, include_glyph):
            stream = (
                ESC + b"*c4668D" +
                ESC + b"*c160E" +
                ESC + b")s80W" + header + bytes(16)
            )
            if include_glyph:
                stream += ESC + b")s3W" + b"\xff\xff\xff"
            stream += ESC + b"(4668X" + bytes([0xa0]) + FF
            source = write(tmp / f"{name}.pcl", stream)
            pdf = tmp / f"{name}.pdf"
            render(dreamprint, source, pdf)
            return pdf

        resource_high_reject_pdf = resource_high_char_case(
            "resource-high-char-reject", resource_header_class0, True)
        resource_high_baseline_pdf = resource_high_char_case(
            "resource-high-char-baseline", resource_header_class0, False)
        resource_high_allowed_pdf = resource_high_char_case(
            "resource-high-char-allowed", resource_header_class1, True)
        if ppm_sha256(resource_high_reject_pdf,
                      tmp / "resource-high-char-reject", dpi=300) != \
           ppm_sha256(resource_high_baseline_pdf,
                      tmp / "resource-high-char-baseline", dpi=300):
            raise AssertionError("resource header class-0 installed high downloaded character")
        if ppm_sha256(resource_high_allowed_pdf,
                      tmp / "resource-high-char-allowed", dpi=300) == \
           ppm_sha256(resource_high_baseline_pdf,
                      tmp / "resource-high-char-baseline-again", dpi=300):
            raise AssertionError("resource header class-1 did not install high downloaded character")

        fixed_record_type0 = bytearray(64)
        fixed_record_type1 = bytearray(64)
        fixed_record_type1[0x0e] = 1
        fixed_record_glyph = bytes.fromhex(
            "f0 0f aa 55 3c c3 81 7e ff 00 18 e7 "
            "24 db 42 bd 66 99")

        def fixed_record_high_char_case(name, descriptor, char, include_glyph):
            stream = (
                ESC + b"*c4669D" +
                ESC + b"(s64W" + bytes(descriptor) +
                ESC + f"*c{char}E".encode("ascii")
            )
            if include_glyph:
                stream += ESC + b"(s18W" + fixed_record_glyph
            stream += ESC + b"(4669X" + bytes([char]) + FF
            source = write(tmp / f"{name}.pcl", stream)
            pdf = tmp / f"{name}.pdf"
            render(dreamprint, source, pdf)
            return pdf

        fixed_type0_reject_pdf = fixed_record_high_char_case(
            "fixed-record-type0-high-reject", fixed_record_type0, 0xa0, True)
        fixed_type0_baseline_pdf = fixed_record_high_char_case(
            "fixed-record-type0-high-baseline", fixed_record_type0, 0xa0, False)
        fixed_type1_allowed_pdf = fixed_record_high_char_case(
            "fixed-record-type1-high-allowed", fixed_record_type1, 0xa0, True)
        fixed_type1_baseline_pdf = fixed_record_high_char_case(
            "fixed-record-type1-high-baseline", fixed_record_type1, 0xa0, False)
        if ppm_sha256(fixed_type0_reject_pdf,
                      tmp / "fixed-record-type0-high-reject", dpi=300) != \
           ppm_sha256(fixed_type0_baseline_pdf,
                      tmp / "fixed-record-type0-high-baseline", dpi=300):
            raise AssertionError("fixed-record type-0 installed high downloaded character")
        if ppm_sha256(fixed_type1_allowed_pdf,
                      tmp / "fixed-record-type1-high-allowed", dpi=300) == \
           ppm_sha256(fixed_type1_baseline_pdf,
                      tmp / "fixed-record-type1-high-baseline", dpi=300):
            raise AssertionError("fixed-record type-1 did not install high downloaded character")

        def descriptor_glyph(rows, span):
            width = span * 8
            return (
                bytes([0, 0, 0, 0, 0x0c, 1]) +
                rows.to_bytes(2, "big") +
                width.to_bytes(2, "big") +
                b"\x00\x00" +
                bytes([0xff]) * (rows * span)
            )

        def unresolved_glyph_case(name, rows, span):
            payload = descriptor_glyph(rows, span)
            stream = (
                ESC + b"*c4670D" +
                ESC + b"*c33E" +
                ESC + b")s" + str(len(payload)).encode("ascii") + b"W" +
                payload +
                ESC + b"(4670X" +
                b"!" + FF
            )
            source = write(tmp / f"{name}.pcl", stream)
            pdf = tmp / f"{name}.pdf"
            render(dreamprint, source, pdf)
            if pdftotext(pdf).strip() != "!":
                raise AssertionError(f"{name} lost selectable text")
            if ppm_bbox(pdf, tmp / name, dpi=150) is not None:
                raise AssertionError(f"{name} invented unresolved glyph pixels")

        unresolved_glyph_case("download-high-row-short-boundary", 0x0102, 2)
        unresolved_glyph_case("download-wrapped-width-boundary", 1, 0x0102)
        unresolved_glyph_case("download-span31-source-boundary", 0x0181, 31)

        bad_char_payload = write(
            tmp / "bad-char-payload.pcl",
            ESC + b"*c4660D" +
            ESC + b"*c33E" +
            ESC + b")s6W" + bytes.fromhex("00 00 00 00 0c 00") +
            ESC + b"(4660X" +
            b"!" + FF,
        )
        bad_char_payload_pdf = tmp / "bad-char-payload.pdf"
        render(dreamprint, bad_char_payload, bad_char_payload_pdf)
        if pdftotext(bad_char_payload_pdf).strip() != "!":
            raise AssertionError("bad character payload shifted text output")
        if ppm_sha256(bang_pdf, tmp / "bang2", dpi=150) != \
           ppm_sha256(bad_char_payload_pdf, tmp / "bad-char-payload",
                      dpi=150):
            raise AssertionError("bad character payload installed a glyph")

        negative_download = write(tmp / "negative-download.pcl",
                                  ESC + b")s-2W" + b"ZZ" + b"!" + FF)
        fractional_download = write(tmp / "fractional-download.pcl",
                                    ESC + b")s2.9W" + b"ZZ" + b"!" + FF)
        negative_download_pdf = tmp / "negative-download.pdf"
        fractional_download_pdf = tmp / "fractional-download.pdf"
        render(dreamprint, negative_download, negative_download_pdf)
        render(dreamprint, fractional_download, fractional_download_pdf)
        if pdftotext(negative_download_pdf).strip() != "!":
            raise AssertionError("negative downloaded-font count leaked payload")
        if pdftotext(fractional_download_pdf).strip() != "!":
            raise AssertionError("fractional downloaded-font count rounded")

        capped_payload = (
            bytes([0, 0, 0, 0, 0x0c, 1]) +
            (0x0788).to_bytes(2, "big") +
            (17 * 8).to_bytes(2, "big") +
            b"\x00\x00" +
            bytes(0x7fff - 12)
        )
        payload_count_cap = write(tmp / "payload-count-cap.pcl",
                                  ESC + b"*c4671D" +
                                  ESC + b"*c33E" +
                                  ESC + b")s32768W" + capped_payload +
                                  b"!" + FF)
        payload_count_cap_pdf = tmp / "payload-count-cap.pdf"
        render(dreamprint, payload_count_cap, payload_count_cap_pdf)
        if pdftotext(payload_count_cap_pdf).strip() != "!":
            raise AssertionError("downloaded-font payload count cap consumed following text")
        if ppm_bbox(payload_count_cap_pdf, tmp / "payload-count-cap",
                    dpi=150) is None:
            raise AssertionError("downloaded-font payload count cap lost visible text")

        raster_control = write(tmp / "raster-control.pcl",
                               ESC + b"*t300R" +
                               ESC + b"*r0A" +
                               ESC + b"*b4W" +
                               bytes([0xf0, 0x1a, 0x58, 0xaa, 0x55]) +
                               b"Z" + FF)
        raster_control_pdf = tmp / "raster-control.pdf"
        render(dreamprint, raster_control, raster_control_pdf)
        if pdftotext(raster_control_pdf).strip() != "Z":
            raise AssertionError("raster payload control shifted stream")
        if ppm_nonwhite(raster_control_pdf, tmp / "raster-control") < 5:
            raise AssertionError("raster payload control render looks blank")

        raster_negative = write(tmp / "raster-negative.pcl",
                                ESC + b"*t300R" +
                                ESC + b"*r0A" +
                                ESC + b"*b-2W" +
                                b"QZ" + b"!" + FF)
        raster_fractional = write(tmp / "raster-fractional.pcl",
                                  ESC + b"*t300R" +
                                  ESC + b"*r0A" +
                                  ESC + b"*b2.9W" +
                                  b"QZ" + b"!" + FF)
        raster_negative_pdf = tmp / "raster-negative.pdf"
        raster_fractional_pdf = tmp / "raster-fractional.pdf"
        render(dreamprint, raster_negative, raster_negative_pdf)
        render(dreamprint, raster_fractional, raster_fractional_pdf)
        if pdftotext(raster_negative_pdf).strip() != "!":
            raise AssertionError("negative raster count leaked payload")
        if ppm_nonwhite(raster_negative_pdf, tmp / "raster-negative") < 5:
            raise AssertionError("negative raster payload did not render")
        if pdftotext(raster_fractional_pdf).strip() != "!":
            raise AssertionError("fractional raster count rounded")
        if ppm_sha256(raster_fractional_pdf, tmp / "raster-fractional",
                      dpi=150) != \
           ppm_sha256(raster_negative_pdf, tmp / "raster-negative", dpi=150):
            raise AssertionError("fractional raster count did not match integer word")

        raster_lower_negative = write(tmp / "raster-lower-negative.pcl",
                                      ESC + b"*t300R" +
                                      ESC + b"*r0A" +
                                      ESC + b"*b-2w2W" +
                                      b"QZ" + b"!" + FF)
        raster_lower_fractional = write(tmp / "raster-lower-fractional.pcl",
                                        ESC + b"*t300R" +
                                        ESC + b"*r0A" +
                                        ESC + b"*b2.9w2W" +
                                        b"QZ" + b"!" + FF)
        raster_lower_negative_pdf = tmp / "raster-lower-negative.pdf"
        raster_lower_fractional_pdf = tmp / "raster-lower-fractional.pdf"
        render(dreamprint, raster_lower_negative, raster_lower_negative_pdf)
        render(dreamprint, raster_lower_fractional,
               raster_lower_fractional_pdf)
        if pdftotext(raster_lower_negative_pdf).strip() != "!":
            raise AssertionError("lowercase negative raster count leaked payload")
        if pdftotext(raster_lower_fractional_pdf).strip() != "!":
            raise AssertionError("lowercase fractional raster count rounded")
        if ppm_sha256(raster_lower_fractional_pdf,
                      tmp / "raster-lower-fractional", dpi=150) != \
           ppm_sha256(raster_lower_negative_pdf,
                      tmp / "raster-lower-negative", dpi=150):
            raise AssertionError("lowercase fractional raster count did not match integer word")

        raster_full = write(tmp / "raster-full.pcl",
                            ESC + b"*t300R" +
                            ESC + b"*r0A" +
                            ESC + b"*b4W" +
                            bytes([0xf0, 0x0f, 0xaa, 0x55]) + FF)
        raster_cap = write(tmp / "raster-cap.pcl",
                           ESC + b"*p2384X" +
                           ESC + b"*t300R" +
                           ESC + b"*r1A" +
                           ESC + b"*b4W" +
                           bytes([0xf0, 0x0f, 0xaa, 0x55]) + FF)
        raster_start_zero = write(tmp / "raster-start-zero.pcl",
                                  ESC + b"*p2384X" +
                                  ESC + b"*t300R" +
                                  ESC + b"*r0A" +
                                  ESC + b"*b4W" +
                                  bytes([0xf0, 0x0f, 0xaa, 0x55]) + FF)
        raster_start_fractional = write(
            tmp / "raster-start-fractional.pcl",
            ESC + b"*p2384X" +
            ESC + b"*t300R" +
            ESC + b"*r0.9A" +
            ESC + b"*b4W" +
            bytes([0xf0, 0x0f, 0xaa, 0x55]) + FF)
        raster_full_pdf = tmp / "raster-full.pdf"
        raster_cap_pdf = tmp / "raster-cap.pdf"
        raster_start_zero_pdf = tmp / "raster-start-zero.pdf"
        raster_start_fractional_pdf = tmp / "raster-start-fractional.pdf"
        render(dreamprint, raster_full, raster_full_pdf)
        render(dreamprint, raster_cap, raster_cap_pdf)
        render(dreamprint, raster_start_zero, raster_start_zero_pdf)
        render(dreamprint, raster_start_fractional,
               raster_start_fractional_pdf)
        full_pixels = ppm_nonwhite(raster_full_pdf, tmp / "raster-full",
                                   dpi=150)
        cap_pixels = ppm_nonwhite(raster_cap_pdf, tmp / "raster-cap",
                                  dpi=150)
        if not (0 < cap_pixels < full_pixels):
            raise AssertionError("raster transfer cap did not reduce row")
        if ppm_sha256(raster_start_zero_pdf, tmp / "raster-start-zero",
                      dpi=150) != \
           ppm_sha256(raster_start_fractional_pdf,
                      tmp / "raster-start-fractional", dpi=150):
            raise AssertionError("fractional raster start selector rounded")

        raster_cursor_before_start = write(
            tmp / "raster-cursor-before-start.pcl",
            ESC + b"*t300R" +
            ESC + b"*p900Y" +
            ESC + b"*r0A" +
            ESC + b"*b2W" +
            bytes([0xf0, 0x0f]) + FF)
        raster_cursor_after_start = write(
            tmp / "raster-cursor-after-start.pcl",
            ESC + b"*t300R" +
            ESC + b"*r0A" +
            ESC + b"*p900Y" +
            ESC + b"*b2W" +
            bytes([0xf0, 0x0f]) + FF)
        raster_cursor_before_start_pdf = \
            tmp / "raster-cursor-before-start.pdf"
        raster_cursor_after_start_pdf = tmp / "raster-cursor-after-start.pdf"
        render(dreamprint, raster_cursor_before_start,
               raster_cursor_before_start_pdf)
        render(dreamprint, raster_cursor_after_start,
               raster_cursor_after_start_pdf)
        if ppm_nonwhite(raster_cursor_after_start_pdf,
                        tmp / "raster-cursor-after-start", dpi=300) <= 0:
            raise AssertionError("raster cursor-transfer regression is blank")
        if ppm_sha256(raster_cursor_before_start_pdf,
                      tmp / "raster-cursor-before-start", dpi=300) != \
           ppm_sha256(raster_cursor_after_start_pdf,
                      tmp / "raster-cursor-after-start", dpi=300):
            raise AssertionError(
                "raster transfer did not use transfer-time vertical cursor")

        raster_page_edge_payload = bytes([0] * 312 + [0xff])
        raster_page_edge = write(tmp / "raster-page-edge.pcl",
                                 ESC + b"*t300R" +
                                 ESC + b"*r0A" +
                                 ESC + b"*b313W" +
                                 raster_page_edge_payload + FF)
        raster_page_edge_pdf = tmp / "raster-page-edge.pdf"
        render(dreamprint, raster_page_edge, raster_page_edge_pdf)
        if ppm_nonwhite(raster_page_edge_pdf, tmp / "raster-page-edge",
                        dpi=300) <= 0:
            raise AssertionError("raster row clipped at text right margin")

        raster_no_skip = write(tmp / "raster-no-skip.pcl",
                               ESC + b"*t300R" +
                               ESC + b"*r0A" +
                               ESC + b"*b2W" +
                               bytes([0xf0, 0x0f]) + FF)
        raster_skip_integer = write(tmp / "raster-skip-integer.pcl",
                                    ESC + b"*t300R" +
                                    ESC + b"*r0A" +
                                    ESC + b"*b2Y" +
                                    ESC + b"*b2W" +
                                    bytes([0xf0, 0x0f]) + FF)
        raster_skip_fractional = write(tmp / "raster-skip-fractional.pcl",
                                       ESC + b"*t300R" +
                                       ESC + b"*r0A" +
                                       ESC + b"*b2.9Y" +
                                       ESC + b"*b2W" +
                                       bytes([0xf0, 0x0f]) + FF)
        raster_skip_next = write(tmp / "raster-skip-next.pcl",
                                 ESC + b"*t300R" +
                                 ESC + b"*r0A" +
                                 ESC + b"*b3Y" +
                                 ESC + b"*b2W" +
                                 bytes([0xf0, 0x0f]) + FF)
        raster_lower_m_no_chain = write(
            tmp / "raster-lower-m-no-chain.pcl",
            ESC + b"*t300R" +
            ESC + b"*r0A" +
            ESC + b"*b1m2W!!" + FF)
        raster_no_skip_pdf = tmp / "raster-no-skip.pdf"
        raster_skip_integer_pdf = tmp / "raster-skip-integer.pdf"
        raster_skip_fractional_pdf = tmp / "raster-skip-fractional.pdf"
        raster_skip_next_pdf = tmp / "raster-skip-next.pdf"
        raster_lower_m_no_chain_pdf = tmp / "raster-lower-m-no-chain.pdf"
        render(dreamprint, raster_no_skip, raster_no_skip_pdf)
        render(dreamprint, raster_skip_integer, raster_skip_integer_pdf)
        render(dreamprint, raster_skip_fractional, raster_skip_fractional_pdf)
        render(dreamprint, raster_skip_next, raster_skip_next_pdf)
        render(dreamprint, raster_lower_m_no_chain,
               raster_lower_m_no_chain_pdf)
        if ppm_sha256(raster_no_skip_pdf,
                      tmp / "raster-no-skip", dpi=300) != \
           ppm_sha256(raster_skip_integer_pdf,
                      tmp / "raster-skip-integer", dpi=300):
            raise AssertionError("unsupported *bY changed raster row")
        if ppm_sha256(raster_no_skip_pdf,
                      tmp / "raster-no-skip-again", dpi=300) != \
           ppm_sha256(raster_skip_fractional_pdf,
                      tmp / "raster-skip-fractional", dpi=300):
            raise AssertionError("unsupported fractional *bY changed raster row")
        if ppm_sha256(raster_no_skip_pdf,
                      tmp / "raster-no-skip-third", dpi=300) != \
           ppm_sha256(raster_skip_next_pdf,
                      tmp / "raster-skip-next", dpi=300):
            raise AssertionError("unsupported alternate *bY changed raster row")
        if "2W!!" not in pdftotext(raster_lower_m_no_chain_pdf):
            raise AssertionError("unsupported lowercase *b terminal chained into raster payload")

        raster_150 = write(tmp / "raster-150.pcl",
                           ESC + b"*t150R" +
                           ESC + b"*r0A" +
                           ESC + b"*b2W" +
                           bytes([0xf0, 0x0f]) + FF)
        raster_neg_150 = write(tmp / "raster-neg-150.pcl",
                               ESC + b"*t-150R" +
                               ESC + b"*r0A" +
                               ESC + b"*b2W" +
                               bytes([0xf0, 0x0f]) + FF)
        raster_frac_150 = write(tmp / "raster-frac-150.pcl",
                                ESC + b"*t150.9R" +
                                ESC + b"*r0A" +
                                ESC + b"*b2W" +
                                bytes([0xf0, 0x0f]) + FF)
        raster_150_pdf = tmp / "raster-150.pdf"
        raster_neg_150_pdf = tmp / "raster-neg-150.pdf"
        raster_frac_150_pdf = tmp / "raster-frac-150.pdf"
        render(dreamprint, raster_150, raster_150_pdf)
        render(dreamprint, raster_neg_150, raster_neg_150_pdf)
        render(dreamprint, raster_frac_150, raster_frac_150_pdf)
        if ppm_sha256(raster_150_pdf, tmp / "raster-150", dpi=300) != \
           ppm_sha256(raster_neg_150_pdf, tmp / "raster-neg-150", dpi=300):
            raise AssertionError("negative raster resolution did not match positive")
        if ppm_sha256(raster_150_pdf, tmp / "raster-150", dpi=300) != \
           ppm_sha256(raster_frac_150_pdf, tmp / "raster-frac-150",
                      dpi=300):
            raise AssertionError("fractional raster resolution rounded")

        raster_151 = write(tmp / "raster-151.pcl",
                           ESC + b"*t151R" +
                           ESC + b"*r0A" +
                           ESC + b"*b2W" +
                           bytes([0xf0, 0x0f]) + FF)
        raster_300 = write(tmp / "raster-300.pcl",
                           ESC + b"*t300R" +
                           ESC + b"*r0A" +
                           ESC + b"*b2W" +
                           bytes([0xf0, 0x0f]) + FF)
        raster_151_pdf = tmp / "raster-151.pdf"
        raster_300_pdf = tmp / "raster-300.pdf"
        render(dreamprint, raster_151, raster_151_pdf)
        render(dreamprint, raster_300, raster_300_pdf)
        if ppm_sha256(raster_151_pdf, tmp / "raster-151", dpi=300) != \
           ppm_sha256(raster_300_pdf, tmp / "raster-300", dpi=300):
            raise AssertionError("151 dpi raster did not select mode 0")
        if ppm_sha256(raster_151_pdf, tmp / "raster-151-again", dpi=300) == \
           ppm_sha256(raster_150_pdf, tmp / "raster-150-again", dpi=300):
            raise AssertionError("151 dpi raster still matched 150 dpi mode")

        raster_default_mode = write(tmp / "raster-default-mode.pcl",
                                    ESC + b"*r0A" +
                                    ESC + b"*b2W" +
                                    bytes([0xf0, 0x0f]) + FF)
        raster_explicit_75 = write(tmp / "raster-explicit-75.pcl",
                                   ESC + b"*t75R" +
                                   ESC + b"*r0A" +
                                   ESC + b"*b2W" +
                                   bytes([0xf0, 0x0f]) + FF)
        raster_reset_mode = write(tmp / "raster-reset-mode.pcl",
                                  ESC + b"*t300R" + ESC + b"E" +
                                  ESC + b"*r0A" +
                                  ESC + b"*b2W" +
                                  bytes([0xf0, 0x0f]) + FF)
        raster_default_mode_pdf = tmp / "raster-default-mode.pdf"
        raster_explicit_75_pdf = tmp / "raster-explicit-75.pdf"
        raster_reset_mode_pdf = tmp / "raster-reset-mode.pdf"
        render(dreamprint, raster_default_mode, raster_default_mode_pdf)
        render(dreamprint, raster_explicit_75, raster_explicit_75_pdf)
        render(dreamprint, raster_reset_mode, raster_reset_mode_pdf)
        if ppm_sha256(raster_default_mode_pdf,
                      tmp / "raster-default-mode", dpi=300) != \
           ppm_sha256(raster_explicit_75_pdf,
                      tmp / "raster-explicit-75", dpi=300):
            raise AssertionError("default raster mode did not match 75 dpi mode")
        if ppm_sha256(raster_reset_mode_pdf,
                      tmp / "raster-reset-mode", dpi=300) != \
           ppm_sha256(raster_explicit_75_pdf,
                      tmp / "raster-explicit-75-again", dpi=300):
            raise AssertionError("ESC E did not reset raster mode")
        if ppm_sha256(raster_default_mode_pdf,
                      tmp / "raster-default-mode-again", dpi=300) == \
           ppm_sha256(raster_300_pdf, tmp / "raster-300-again", dpi=300):
            raise AssertionError("default raster mode still matched 300 dpi mode")

        raster_active_resolution_ignored = write(
            tmp / "raster-active-resolution-ignored.pcl",
            ESC + b"*t300R" +
            ESC + b"*r0A" +
            ESC + b"*t75R" +
            ESC + b"*b2W" +
            bytes([0xf0, 0x0f]) + FF)
        raster_end_reenables_resolution = write(
            tmp / "raster-end-reenables-resolution.pcl",
            ESC + b"*t300R" +
            ESC + b"*r0A" +
            ESC + b"*rB" +
            ESC + b"*t150R" +
            ESC + b"*r0A" +
            ESC + b"*b2W" +
            bytes([0xf0, 0x0f]) + FF)
        raster_active_start_ignored = write(
            tmp / "raster-active-start-ignored.pcl",
            ESC + b"*t300R" +
            ESC + b"&a5C" +
            ESC + b"*r1A" +
            ESC + b"&a25C" +
            ESC + b"*r1A" +
            ESC + b"*b2W" +
            bytes([0xf0, 0x0f]) + FF)
        raster_start_first_origin = write(
            tmp / "raster-start-first-origin.pcl",
            ESC + b"*t300R" +
            ESC + b"&a5C" +
            ESC + b"*r1A" +
            ESC + b"*b2W" +
            bytes([0xf0, 0x0f]) + FF)
        raster_start_second_origin = write(
            tmp / "raster-start-second-origin.pcl",
            ESC + b"*t300R" +
            ESC + b"&a25C" +
            ESC + b"*r1A" +
            ESC + b"*b2W" +
            bytes([0xf0, 0x0f]) + FF)
        raster_active_resolution_ignored_pdf = \
            tmp / "raster-active-resolution-ignored.pdf"
        raster_end_reenables_resolution_pdf = \
            tmp / "raster-end-reenables-resolution.pdf"
        raster_active_start_ignored_pdf = \
            tmp / "raster-active-start-ignored.pdf"
        raster_start_first_origin_pdf = tmp / "raster-start-first-origin.pdf"
        raster_start_second_origin_pdf = tmp / "raster-start-second-origin.pdf"
        render(dreamprint, raster_active_resolution_ignored,
               raster_active_resolution_ignored_pdf)
        render(dreamprint, raster_end_reenables_resolution,
               raster_end_reenables_resolution_pdf)
        render(dreamprint, raster_active_start_ignored,
               raster_active_start_ignored_pdf)
        render(dreamprint, raster_start_first_origin,
               raster_start_first_origin_pdf)
        render(dreamprint, raster_start_second_origin,
               raster_start_second_origin_pdf)
        if ppm_sha256(raster_active_resolution_ignored_pdf,
                      tmp / "raster-active-resolution-ignored", dpi=300) != \
           ppm_sha256(raster_300_pdf, tmp / "raster-300-active", dpi=300):
            raise AssertionError("active raster resolution update was not ignored")
        if ppm_sha256(raster_end_reenables_resolution_pdf,
                      tmp / "raster-end-reenables-resolution", dpi=300) != \
           ppm_sha256(raster_150_pdf, tmp / "raster-150-after-end", dpi=300):
            raise AssertionError("raster end did not re-enable resolution update")
        if ppm_sha256(raster_active_start_ignored_pdf,
                      tmp / "raster-active-start-ignored", dpi=300) != \
           ppm_sha256(raster_start_first_origin_pdf,
                      tmp / "raster-start-first-origin", dpi=300):
            raise AssertionError("active raster start changed origin")
        if ppm_sha256(raster_active_start_ignored_pdf,
                      tmp / "raster-active-start-ignored-again", dpi=300) == \
           ppm_sha256(raster_start_second_origin_pdf,
                      tmp / "raster-start-second-origin", dpi=300):
            raise AssertionError("active raster start still matched second origin")

        raster_only = bytearray(ESC + b"*t75R" + ESC + b"*r0A")
        raster_text = bytearray(raster_only)
        for _ in range(30):
            raster_only += ESC + b"*b1W" + b"\xff"
            raster_text += ESC + b"*b1W" + b"\xff"
        raster_only += FF
        raster_text += ESC + b"&a5C" + b"T" + FF
        raster_only_pcl = write(tmp / "raster-only.pcl", bytes(raster_only))
        raster_text_pcl = write(tmp / "raster-text.pcl", bytes(raster_text))
        raster_only_pdf = tmp / "raster-only.pdf"
        raster_text_pdf = tmp / "raster-text.pdf"
        render(dreamprint, raster_only_pcl, raster_only_pdf)
        render(dreamprint, raster_text_pcl, raster_text_pdf)
        raster_text_box = ppm_bbox(raster_text_pdf, tmp / "raster-text",
                                   dpi=150, min_x_filter=70)
        if raster_text_box is None or raster_text_box[1] < 100:
            raise AssertionError("raster transfer did not advance text cursor")

        rule_solid = write(tmp / "rule-solid.pcl",
                           ESC + b"*c64a64b0P" + FF)
        rule_dot_integer = write(tmp / "rule-dot-integer.pcl",
                                 ESC + b"*c64a64b0P" +
                                 ESC + b"*p100Y" +
                                 ESC + b"*c10a10b0P" + FF)
        rule_dot_fractional = write(tmp / "rule-dot-fractional.pcl",
                                    ESC + b"*c64a64b0P" +
                                    ESC + b"*p100Y" +
                                    ESC + b"*c10.9a10.9b0P" + FF)
        rule_decipoint_bias = write(tmp / "rule-decipoint-bias.pcl",
                                    ESC + b"*c72H" + ESC + b"*c72V" +
                                    ESC + b"*c0P" + FF)
        rule_decipoint_bias_expected = write(
            tmp / "rule-decipoint-bias-expected.pcl",
            ESC + b"*c31a31b0P" + FF)
        rule_decipoint_fraction = write(tmp / "rule-decipoint-fraction.pcl",
                                        ESC + b"*c72H" + ESC + b"*c1.5V" +
                                        ESC + b"*c0P" + FF)
        rule_decipoint_fraction_expected = write(
            tmp / "rule-decipoint-fraction-expected.pcl",
            ESC + b"*c31a2b0P" + FF)
        rule_gray = write(tmp / "rule-gray.pcl",
                          ESC + b"*c64a64b50g2P" + FF)
        rule_gray_integer_id = write(tmp / "rule-gray-integer-id.pcl",
                                     ESC + b"*c64a64b2g2P" + FF)
        rule_gray_fractional_id = write(tmp / "rule-gray-fractional-id.pcl",
                                        ESC + b"*c64a64b2.9g2P" + FF)
        rule_gray_fractional_selector = write(
            tmp / "rule-gray-fractional-selector.pcl",
            ESC + b"*c64a64b50g2.9P" + FF)
        rule_pattern = write(tmp / "rule-pattern.pcl",
                             ESC + b"*c64a64b2g3P" + FF)
        rule_neg_gray = write(tmp / "rule-neg-gray.pcl",
                              ESC + b"*c64a64b-50g-2P" + FF)
        rule_neg_pattern = write(tmp / "rule-neg-pattern.pcl",
                                 ESC + b"*c64a64b-2g-3P" + FF)
        rule_landscape_pattern1 = write(
            tmp / "rule-landscape-pattern1.pcl",
            ESC + b"&l1O" + ESC + b"*p0X" + ESC + b"*p0Y" +
            ESC + b"*c16a16b1g3P" + FF)
        rule_landscape_pattern2 = write(
            tmp / "rule-landscape-pattern2.pcl",
            ESC + b"&l1O" + ESC + b"*p0X" + ESC + b"*p0Y" +
            ESC + b"*c16a16b2g3P" + FF)
        rule_landscape_pattern3 = write(
            tmp / "rule-landscape-pattern3.pcl",
            ESC + b"&l1O" + ESC + b"*p0X" + ESC + b"*p0Y" +
            ESC + b"*c16a16b3g3P" + FF)
        rule_landscape_pattern4 = write(
            tmp / "rule-landscape-pattern4.pcl",
            ESC + b"&l1O" + ESC + b"*p0X" + ESC + b"*p0Y" +
            ESC + b"*c16a16b4g3P" + FF)
        rule_off_page_no_publish = write(
            tmp / "rule-off-page-no-publish.pcl",
            ESC + b"*p4000Y" + ESC + b"*c64a64b0P" +
            ESC + b"&l1O" + b"!" + FF)
        rule_solid_pdf = tmp / "rule-solid.pdf"
        rule_dot_integer_pdf = tmp / "rule-dot-integer.pdf"
        rule_dot_fractional_pdf = tmp / "rule-dot-fractional.pdf"
        rule_decipoint_bias_pdf = tmp / "rule-decipoint-bias.pdf"
        rule_decipoint_bias_expected_pdf = \
            tmp / "rule-decipoint-bias-expected.pdf"
        rule_decipoint_fraction_pdf = tmp / "rule-decipoint-fraction.pdf"
        rule_decipoint_fraction_expected_pdf = \
            tmp / "rule-decipoint-fraction-expected.pdf"
        rule_gray_pdf = tmp / "rule-gray.pdf"
        rule_gray_integer_id_pdf = tmp / "rule-gray-integer-id.pdf"
        rule_gray_fractional_id_pdf = tmp / "rule-gray-fractional-id.pdf"
        rule_gray_fractional_selector_pdf = \
            tmp / "rule-gray-fractional-selector.pdf"
        rule_pattern_pdf = tmp / "rule-pattern.pdf"
        rule_neg_gray_pdf = tmp / "rule-neg-gray.pdf"
        rule_neg_pattern_pdf = tmp / "rule-neg-pattern.pdf"
        rule_landscape_pattern1_pdf = tmp / "rule-landscape-pattern1.pdf"
        rule_landscape_pattern2_pdf = tmp / "rule-landscape-pattern2.pdf"
        rule_landscape_pattern3_pdf = tmp / "rule-landscape-pattern3.pdf"
        rule_landscape_pattern4_pdf = tmp / "rule-landscape-pattern4.pdf"
        rule_off_page_no_publish_pdf = tmp / "rule-off-page-no-publish.pdf"
        render(dreamprint, rule_solid, rule_solid_pdf)
        render(dreamprint, rule_dot_integer, rule_dot_integer_pdf)
        render(dreamprint, rule_dot_fractional, rule_dot_fractional_pdf)
        render(dreamprint, rule_decipoint_bias, rule_decipoint_bias_pdf)
        render(dreamprint, rule_decipoint_bias_expected,
               rule_decipoint_bias_expected_pdf)
        render(dreamprint, rule_decipoint_fraction,
               rule_decipoint_fraction_pdf)
        render(dreamprint, rule_decipoint_fraction_expected,
               rule_decipoint_fraction_expected_pdf)
        render(dreamprint, rule_gray, rule_gray_pdf)
        render(dreamprint, rule_gray_integer_id, rule_gray_integer_id_pdf)
        render(dreamprint, rule_gray_fractional_id,
               rule_gray_fractional_id_pdf)
        render(dreamprint, rule_gray_fractional_selector,
               rule_gray_fractional_selector_pdf)
        render(dreamprint, rule_pattern, rule_pattern_pdf)
        render(dreamprint, rule_neg_gray, rule_neg_gray_pdf)
        render(dreamprint, rule_neg_pattern, rule_neg_pattern_pdf)
        render(dreamprint, rule_landscape_pattern1,
               rule_landscape_pattern1_pdf)
        render(dreamprint, rule_landscape_pattern2,
               rule_landscape_pattern2_pdf)
        render(dreamprint, rule_landscape_pattern3,
               rule_landscape_pattern3_pdf)
        render(dreamprint, rule_landscape_pattern4,
               rule_landscape_pattern4_pdf)
        render(dreamprint, rule_off_page_no_publish,
               rule_off_page_no_publish_pdf)
        solid_pixels = ppm_nonwhite(rule_solid_pdf, tmp / "rule-solid",
                                    dpi=300)
        gray_pixels = ppm_nonwhite(rule_gray_pdf, tmp / "rule-gray",
                                   dpi=300)
        pattern_pixels = ppm_nonwhite(rule_pattern_pdf, tmp / "rule-pattern",
                                      dpi=300)
        if not (solid_pixels > gray_pixels > 0):
            raise AssertionError("rule percent fill did not use pattern mask")
        if not (0 < pattern_pixels < solid_pixels):
            raise AssertionError("rule hatch fill did not use pattern mask")
        if ppm_sha256(rule_dot_fractional_pdf,
                      tmp / "rule-dot-fractional", dpi=300) != \
           ppm_sha256(rule_dot_integer_pdf,
                      tmp / "rule-dot-integer", dpi=300):
            raise AssertionError("rectangle dot size used fractional word")
        if ppm_sha256(rule_decipoint_bias_pdf,
                      tmp / "rule-decipoint-bias", dpi=300) != \
           ppm_sha256(rule_decipoint_bias_expected_pdf,
                      tmp / "rule-decipoint-bias-expected", dpi=300):
            raise AssertionError("rectangle decipoint size missed ROM bias")
        if ppm_sha256(rule_decipoint_fraction_pdf,
                      tmp / "rule-decipoint-fraction", dpi=300) != \
           ppm_sha256(rule_decipoint_fraction_expected_pdf,
                      tmp / "rule-decipoint-fraction-expected", dpi=300):
            raise AssertionError("rectangle decipoint size did not round up")
        if ppm_sha256(rule_gray_fractional_id_pdf,
                      tmp / "rule-gray-fractional-id", dpi=300) != \
           ppm_sha256(rule_gray_integer_id_pdf,
                      tmp / "rule-gray-integer-id", dpi=300):
            raise AssertionError("rectangle fill id used fractional word")
        if ppm_sha256(rule_gray_fractional_selector_pdf,
                      tmp / "rule-gray-fractional-selector", dpi=300) != \
           ppm_sha256(rule_gray_pdf, tmp / "rule-gray", dpi=300):
            raise AssertionError("rectangle fill selector used fractional word")
        if ppm_sha256(rule_neg_gray_pdf, tmp / "rule-neg-gray", dpi=300) != \
           ppm_sha256(rule_gray_pdf, tmp / "rule-gray", dpi=300):
            raise AssertionError("negative rule percent fill did not normalize")
        if ppm_sha256(rule_neg_pattern_pdf, tmp / "rule-neg-pattern", dpi=300) != \
           ppm_sha256(rule_pattern_pdf, tmp / "rule-pattern", dpi=300):
            raise AssertionError("negative rule hatch fill did not normalize")
        if not ppm_pixel_dark(rule_landscape_pattern1_pdf,
                              tmp / "rule-landscape-pattern1",
                              7, 50, dpi=300) or \
           ppm_pixel_dark(rule_landscape_pattern1_pdf,
                          tmp / "rule-landscape-pattern1",
                          0, 55, dpi=300):
            raise AssertionError("landscape pattern 1 did not remap to selector 9")
        if not ppm_pixel_dark(rule_landscape_pattern2_pdf,
                              tmp / "rule-landscape-pattern2",
                              0, 55, dpi=300) or \
           ppm_pixel_dark(rule_landscape_pattern2_pdf,
                          tmp / "rule-landscape-pattern2",
                          7, 50, dpi=300):
            raise AssertionError("landscape pattern 2 did not remap to selector 8")
        if not ppm_pixel_dark(rule_landscape_pattern3_pdf,
                              tmp / "rule-landscape-pattern3",
                              1, 50, dpi=300) or \
           ppm_pixel_dark(rule_landscape_pattern3_pdf,
                          tmp / "rule-landscape-pattern3",
                          12, 50, dpi=300):
            raise AssertionError("landscape pattern 3 did not remap to selector 11")
        if not ppm_pixel_dark(rule_landscape_pattern4_pdf,
                              tmp / "rule-landscape-pattern4",
                              12, 50, dpi=300) or \
           ppm_pixel_dark(rule_landscape_pattern4_pdf,
                          tmp / "rule-landscape-pattern4",
                          1, 50, dpi=300):
            raise AssertionError("landscape pattern 4 did not remap to selector 10")
        if pdf_pages(rule_off_page_no_publish_pdf) != 1:
            raise AssertionError("off-page rectangle dirtied an empty page")

        overflow = bytearray(ESC + b"&l2X")
        for i in range(80):
            overflow += f"L{i:02d}\n".encode("ascii")
        overflow_pcl = write(tmp / "overflow.pcl", bytes(overflow))
        overflow_pdf = tmp / "overflow.pdf"
        render(dreamprint, overflow_pcl, overflow_pdf)
        if pdf_pages(overflow_pdf) != 4:
            raise AssertionError("copy-count overflow did not publish copied EOF page")

        perf_lines = bytearray()
        for i in range(60):
            perf_lines += f"P{i:02d}\r\n".encode("ascii")
        perf_tail = bytes(perf_lines) + b"Z" + FF
        perf_default = write(tmp / "perf-default.pcl", perf_tail)
        perf_reset = write(tmp / "perf-reset.pcl",
                           ESC + b"&l0L" + ESC + b"E" + perf_tail)
        perf_enabled = write(tmp / "perf-enabled.pcl",
                             ESC + b"&l1L" + perf_tail)
        perf_preserved = write(tmp / "perf-preserved.pcl",
                               ESC + b"&l1L" + ESC + b"&l-2L" +
                               perf_tail)
        perf_disabled = write(tmp / "perf-disabled.pcl",
                              ESC + b"&l1L" + ESC + b"&l0L" +
                              perf_tail)
        perf_fractional = write(tmp / "perf-fractional.pcl",
                                ESC + b"&l1L" + ESC + b"&l0.9L" +
                                perf_tail)
        perf_default_pdf = tmp / "perf-default.pdf"
        perf_reset_pdf = tmp / "perf-reset.pdf"
        perf_enabled_pdf = tmp / "perf-enabled.pdf"
        perf_preserved_pdf = tmp / "perf-preserved.pdf"
        perf_disabled_pdf = tmp / "perf-disabled.pdf"
        perf_fractional_pdf = tmp / "perf-fractional.pdf"
        render(dreamprint, perf_default, perf_default_pdf)
        render(dreamprint, perf_reset, perf_reset_pdf)
        render(dreamprint, perf_enabled, perf_enabled_pdf)
        render(dreamprint, perf_preserved, perf_preserved_pdf)
        render(dreamprint, perf_disabled, perf_disabled_pdf)
        render(dreamprint, perf_fractional, perf_fractional_pdf)
        if pdf_pages(perf_default_pdf) != pdf_pages(perf_enabled_pdf):
            raise AssertionError("default perforation state was not enabled")
        if pdf_pages(perf_reset_pdf) != pdf_pages(perf_enabled_pdf):
            raise AssertionError("ESC E did not reset perforation state")
        if pdf_pages(perf_preserved_pdf) != pdf_pages(perf_enabled_pdf):
            raise AssertionError("invalid perforation selector did not preserve state")
        if pdf_pages(perf_disabled_pdf) == pdf_pages(perf_enabled_pdf):
            raise AssertionError("perforation regression is not sensitive to disabled state")
        if pdf_pages(perf_fractional_pdf) != pdf_pages(perf_disabled_pdf):
            raise AssertionError("fractional perforation selector rounded")

        paper_zero = write(tmp / "paper-zero.pcl",
                           b"A" + ESC + b"&l0H" + b"B" + FF)
        paper_zero_pdf = tmp / "paper-zero.pdf"
        render(dreamprint, paper_zero, paper_zero_pdf)
        if pdf_pages(paper_zero_pdf) != 2:
            raise AssertionError("paper-source selector zero did not publish")

        page_length_zero = write(tmp / "page-length-zero.pcl",
                                 b"A" + ESC + b"&l0P" + b"B" + FF)
        page_length_zero_fractional = write(
            tmp / "page-length-zero-fractional.pcl",
            b"A" + ESC + b"&l0.9P" + b"B" + FF)
        page_length_zero_pdf = tmp / "page-length-zero.pdf"
        page_length_zero_fractional_pdf = \
            tmp / "page-length-zero-fractional.pdf"
        render(dreamprint, page_length_zero, page_length_zero_pdf)
        render(dreamprint, page_length_zero_fractional,
               page_length_zero_fractional_pdf)
        if pdf_pages(page_length_zero_pdf) != 2:
            raise AssertionError("page-length selector zero did not publish")
        if pdf_pages(page_length_zero_fractional_pdf) != 2:
            raise AssertionError("fractional zero page length did not publish")
        if "".join(pdftotext(page_length_zero_fractional_pdf).split()) != "AB":
            raise AssertionError("fractional zero page length lost text")

        page_length_nonzero = write(tmp / "page-length-nonzero.pcl",
                                    b"A" + ESC + b"&l66P" + b"B" + FF)
        page_length_fractional = write(tmp / "page-length-fractional.pcl",
                                       b"A" + ESC + b"&l66.9P" + b"B" + FF)
        page_length_nonzero_pdf = tmp / "page-length-nonzero.pdf"
        page_length_fractional_pdf = tmp / "page-length-fractional.pdf"
        render(dreamprint, page_length_nonzero, page_length_nonzero_pdf)
        render(dreamprint, page_length_fractional, page_length_fractional_pdf)
        if pdf_pages(page_length_nonzero_pdf) != 2:
            raise AssertionError("nonzero page length did not publish")
        if "AB" not in "".join(pdftotext(page_length_nonzero_pdf).split()):
            raise AssertionError("nonzero page length lost text")
        if pdf_pages(page_length_fractional_pdf) != \
           pdf_pages(page_length_nonzero_pdf):
            raise AssertionError("fractional page length did not match integer word")
        if "AB" not in "".join(pdftotext(page_length_fractional_pdf).split()):
            raise AssertionError("fractional page length lost text")

        page_length_legal = write(tmp / "page-length-legal.pcl",
                                  ESC + b"&l84P" + b"!" + FF)
        page_size_legal = write(tmp / "page-size-legal.pcl",
                                ESC + b"&l3A" + b"!" + FF)
        page_length_legal_pdf = tmp / "page-length-legal.pdf"
        page_size_legal_pdf = tmp / "page-size-legal.pdf"
        render(dreamprint, page_length_legal, page_length_legal_pdf)
        render(dreamprint, page_size_legal, page_size_legal_pdf)
        if ppm_sha256(page_length_legal_pdf, tmp / "page-length-legal",
                      dpi=150) != \
           ppm_sha256(page_size_legal_pdf, tmp / "page-size-legal", dpi=150):
            raise AssertionError("84-line page length did not select legal geometry")
        if pdftotext(page_length_legal_pdf).strip() != "!":
            raise AssertionError("84-line page length lost selectable text")

        page_length_default_from_legal = write(
            tmp / "page-length-default-from-legal.pcl",
            ESC + b"&l3A" + ESC + b"&l0P" + b"!" + FF)
        page_length_default_letter = write(
            tmp / "page-length-default-letter.pcl",
            ESC + b"&l2A" + b"!" + FF)
        page_length_default_from_legal_pdf = \
            tmp / "page-length-default-from-legal.pdf"
        page_length_default_letter_pdf = \
            tmp / "page-length-default-letter.pdf"
        render(dreamprint, page_length_default_from_legal,
               page_length_default_from_legal_pdf)
        render(dreamprint, page_length_default_letter,
               page_length_default_letter_pdf)
        if ppm_sha256(page_length_default_from_legal_pdf,
                      tmp / "page-length-default-from-legal", dpi=150) != \
           ppm_sha256(page_length_default_letter_pdf,
                      tmp / "page-length-default-letter", dpi=150):
            raise AssertionError("zero page length did not restore default letter geometry")
        if pdftotext(page_length_default_from_legal_pdf).strip() != "!":
            raise AssertionError("zero page length default lost selectable text")

        page_length_negative = write(tmp / "page-length-negative.pcl",
                                     b"A" + ESC + b"&l-66P" + b"B" + FF)
        page_length_negative_pdf = tmp / "page-length-negative.pdf"
        render(dreamprint, page_length_negative, page_length_negative_pdf)
        if pdf_pages(page_length_negative_pdf) != pdf_pages(page_length_nonzero_pdf):
            raise AssertionError("negative page length did not match positive selector")
        if "AB" not in "".join(pdftotext(page_length_negative_pdf).split()):
            raise AssertionError("negative page length lost text")

        page_length_hmi_base = write(tmp / "page-length-hmi-base.pcl",
                                     ESC + b"&l66P" + b"!!" + FF)
        page_length_hmi_refresh = write(tmp / "page-length-hmi-refresh.pcl",
                                        ESC + b"&k6H" + ESC + b"&l66P" +
                                        b"!!" + FF)
        page_length_hmi_base_pdf = tmp / "page-length-hmi-base.pdf"
        page_length_hmi_refresh_pdf = tmp / "page-length-hmi-refresh.pdf"
        render(dreamprint, page_length_hmi_base, page_length_hmi_base_pdf)
        render(dreamprint, page_length_hmi_refresh,
               page_length_hmi_refresh_pdf)
        if ppm_sha256(page_length_hmi_base_pdf,
                      tmp / "page-length-hmi-base", dpi=150) != \
           ppm_sha256(page_length_hmi_refresh_pdf,
                      tmp / "page-length-hmi-refresh", dpi=150):
            raise AssertionError("page-length change did not refresh HMI")

        page_length_invalid = write(tmp / "page-length-invalid.pcl",
                                    b"A" + ESC + b"&l999P" + b"B" + FF)
        page_length_invalid_pdf = tmp / "page-length-invalid.pdf"
        render(dreamprint, page_length_invalid, page_length_invalid_pdf)
        if pdf_pages(page_length_invalid_pdf) != 1:
            raise AssertionError("invalid page length published current page")
        if "AB" not in "".join(pdftotext(page_length_invalid_pdf).split()):
            raise AssertionError("invalid page length changed text output")

        page_extent_reject = write(
            tmp / "page-extent-reject.pcl",
            ESC + b"&l20P" + b"A" + ESC + b"*p9999Y" + b"B" +
            ESC + b"&a1R" + b"C" + FF)
        page_extent_reject_pdf = tmp / "page-extent-reject.pdf"
        render(dreamprint, page_extent_reject, page_extent_reject_pdf)
        if "".join(pdftotext(page_extent_reject_pdf).split()) != "AC":
            raise AssertionError("page-extent rejected glyph leaked selectable text")
        bbox = ppm_bbox(page_extent_reject_pdf, tmp / "page-extent-reject",
                        dpi=150)
        if bbox is None or bbox[3] >= 150:
            raise AssertionError("page-extent rejected glyph leaked pixels")

        vmi_positive = write(tmp / "vmi-positive.pcl",
                             ESC + b"&l6C" + b"A\nB" + FF)
        vmi_negative = write(tmp / "vmi-negative.pcl",
                             ESC + b"&l-6C" + b"A\nB" + FF)
        vmi_positive_pdf = tmp / "vmi-positive.pdf"
        vmi_negative_pdf = tmp / "vmi-negative.pdf"
        render(dreamprint, vmi_positive, vmi_positive_pdf)
        render(dreamprint, vmi_negative, vmi_negative_pdf)
        if ppm_sha256(vmi_positive_pdf, tmp / "vmi-positive", dpi=150) != \
           ppm_sha256(vmi_negative_pdf, tmp / "vmi-negative", dpi=150):
            raise AssertionError("negative VMI value did not match positive value")

        vmi_pending = write(tmp / "vmi-pending.pcl",
                            ESC + b"&l24C" + b"A" + FF)
        vmi_explicit = write(tmp / "vmi-explicit.pcl",
                             ESC + b"*p108Y" + b"A" + FF)
        vmi_pending_pdf = tmp / "vmi-pending.pdf"
        vmi_explicit_pdf = tmp / "vmi-explicit.pdf"
        render(dreamprint, vmi_pending, vmi_pending_pdf)
        render(dreamprint, vmi_explicit, vmi_explicit_pdf)
        if ppm_sha256(vmi_pending_pdf, tmp / "vmi-pending", dpi=150) != \
           ppm_sha256(vmi_explicit_pdf, tmp / "vmi-explicit", dpi=150):
            raise AssertionError("pending VMI cursor did not use 18/25 offset")

        top_margin_positive = write(tmp / "top-margin-positive.pcl",
                                    ESC + b"&l3E" + b"!" + FF)
        top_margin_negative = write(tmp / "top-margin-negative.pcl",
                                    ESC + b"&l-3E" + b"!" + FF)
        top_margin_positive_pdf = tmp / "top-margin-positive.pdf"
        top_margin_negative_pdf = tmp / "top-margin-negative.pdf"
        render(dreamprint, top_margin_positive, top_margin_positive_pdf)
        render(dreamprint, top_margin_negative, top_margin_negative_pdf)
        if ppm_sha256(top_margin_positive_pdf, tmp / "top-margin-positive",
                      dpi=150) != \
           ppm_sha256(top_margin_negative_pdf, tmp / "top-margin-negative",
                      dpi=150):
            raise AssertionError("negative top margin did not match positive value")

        text_length_lines = bytearray()
        for i in range(25):
            text_length_lines += f"T{i:02d}\n".encode("ascii")
        text_length_positive = write(tmp / "text-length-positive.pcl",
                                     ESC + b"&l10F" +
                                     bytes(text_length_lines) + FF)
        text_length_negative = write(tmp / "text-length-negative.pcl",
                                     ESC + b"&l-10F" +
                                     bytes(text_length_lines) + FF)
        text_length_positive_pdf = tmp / "text-length-positive.pdf"
        text_length_negative_pdf = tmp / "text-length-negative.pdf"
        render(dreamprint, text_length_positive, text_length_positive_pdf)
        render(dreamprint, text_length_negative, text_length_negative_pdf)
        if pdf_pages(text_length_negative_pdf) != pdf_pages(text_length_positive_pdf):
            raise AssertionError("negative text length did not match positive value")

        perf_text_length_lines = bytearray()
        for i in range(25):
            perf_text_length_lines += f"F{i:02d}\n".encode("ascii")
        perf_text_default = write(tmp / "perf-text-default.pcl",
                                  ESC + b"&l1L" +
                                  bytes(perf_text_length_lines) + FF)
        perf_text_short = write(tmp / "perf-text-short.pcl",
                                ESC + b"&l1L" + ESC + b"&l10F" +
                                bytes(perf_text_length_lines) + FF)
        perf_text_default_pdf = tmp / "perf-text-default.pdf"
        perf_text_short_pdf = tmp / "perf-text-short.pdf"
        render(dreamprint, perf_text_default, perf_text_default_pdf)
        render(dreamprint, perf_text_short, perf_text_short_pdf)
        if pdf_pages(perf_text_short_pdf) <= pdf_pages(perf_text_default_pdf):
            raise AssertionError("text length did not constrain perforation overflow")
        if "F24" not in pdftotext(perf_text_short_pdf):
            raise AssertionError("text length overflow lost selectable text")

        half_feed_text = bytearray()
        for i in range(20):
            half_feed_text += f"H{i:02d}".encode("ascii") + ESC + b"=\r"
        half_feed_default = write(tmp / "half-feed-default.pcl",
                                  ESC + b"&l1L" +
                                  bytes(half_feed_text) + FF)
        half_feed_short = write(tmp / "half-feed-short.pcl",
                                ESC + b"&l1L" + ESC + b"&l4F" +
                                bytes(half_feed_text) + FF)
        half_feed_default_pdf = tmp / "half-feed-default.pdf"
        half_feed_short_pdf = tmp / "half-feed-short.pdf"
        render(dreamprint, half_feed_default, half_feed_default_pdf)
        render(dreamprint, half_feed_short, half_feed_short_pdf)
        if pdf_pages(half_feed_short_pdf) <= pdf_pages(half_feed_default_pdf):
            raise AssertionError("half-line feed did not honor perforation overflow")
        if "H19" not in pdftotext(half_feed_short_pdf):
            raise AssertionError("half-line feed overflow lost selectable text")

        lpi_positive = write(tmp / "lpi-positive.pcl",
                             ESC + b"&l8D" + b"A\nB" + FF)
        lpi_negative = write(tmp / "lpi-negative.pcl",
                             ESC + b"&l-8D" + b"A\nB" + FF)
        lpi_fractional = write(tmp / "lpi-fractional.pcl",
                               ESC + b"&l8.9D" + b"A\nB" + FF)
        lpi_positive_pdf = tmp / "lpi-positive.pdf"
        lpi_negative_pdf = tmp / "lpi-negative.pdf"
        lpi_fractional_pdf = tmp / "lpi-fractional.pdf"
        render(dreamprint, lpi_positive, lpi_positive_pdf)
        render(dreamprint, lpi_negative, lpi_negative_pdf)
        render(dreamprint, lpi_fractional, lpi_fractional_pdf)
        if ppm_sha256(lpi_positive_pdf, tmp / "lpi-positive", dpi=150) != \
           ppm_sha256(lpi_negative_pdf, tmp / "lpi-negative", dpi=150):
            raise AssertionError("negative LPI selector did not match positive selector")
        if ppm_sha256(lpi_positive_pdf, tmp / "lpi-positive", dpi=150) != \
           ppm_sha256(lpi_fractional_pdf, tmp / "lpi-fractional", dpi=150):
            raise AssertionError("fractional LPI selector rounded")

        copies_negative = write(tmp / "copies-negative.pcl",
                                ESC + b"&l-2X" + b"!" + FF)
        copies_fractional = write(tmp / "copies-fractional.pcl",
                                  ESC + b"&l2.9X" + b"!" + FF)
        copies_eof = write(tmp / "copies-eof.pcl",
                           ESC + b"&l2X" + b"!")
        copies_negative_pdf = tmp / "copies-negative.pdf"
        copies_fractional_pdf = tmp / "copies-fractional.pdf"
        copies_eof_pdf = tmp / "copies-eof.pdf"
        render(dreamprint, copies_negative, copies_negative_pdf)
        render(dreamprint, copies_fractional, copies_fractional_pdf)
        render(dreamprint, copies_eof, copies_eof_pdf)
        if pdf_pages(copies_negative_pdf) != 2:
            raise AssertionError("negative copy count was not absolute")
        if pdf_pages(copies_fractional_pdf) != 2:
            raise AssertionError("fractional copy count rounded")
        if pdf_pages(copies_eof_pdf) != 2:
            raise AssertionError("EOF flush did not publish LaserJet copy count")

        vfc_negative = write(tmp / "vfc-negative.pcl",
                             ESC + b"&l-4W" + b"\x00\x00\x00\x02" +
                             b"!" + FF)
        vfc_fractional = write(tmp / "vfc-fractional.pcl",
                               ESC + b"&l4.9W" + b"\x00\x00\x00\x02" +
                               ESC + b"&l2V" + b"!" + FF)
        vfc_integer = write(tmp / "vfc-integer.pcl",
                            ESC + b"&l4W" + b"\x00\x00\x00\x02" +
                            ESC + b"&l2V" + b"!" + FF)
        vfc_lower_negative = write(tmp / "vfc-lower-negative.pcl",
                                   ESC + b"&l-4w4W" + b"\x00\x00\x00\x02" +
                                   b"!" + FF)
        vfc_lower_fractional = write(
            tmp / "vfc-lower-fractional.pcl",
            ESC + b"&l4.9w4W" + b"\x00\x00\x00\x02" +
            ESC + b"&l2V" + b"!" + FF)
        vfc_negative_pdf = tmp / "vfc-negative.pdf"
        vfc_fractional_pdf = tmp / "vfc-fractional.pdf"
        vfc_integer_pdf = tmp / "vfc-integer.pdf"
        vfc_lower_negative_pdf = tmp / "vfc-lower-negative.pdf"
        vfc_lower_fractional_pdf = tmp / "vfc-lower-fractional.pdf"
        render(dreamprint, vfc_negative, vfc_negative_pdf)
        render(dreamprint, vfc_fractional, vfc_fractional_pdf)
        render(dreamprint, vfc_integer, vfc_integer_pdf)
        render(dreamprint, vfc_lower_negative, vfc_lower_negative_pdf)
        render(dreamprint, vfc_lower_fractional, vfc_lower_fractional_pdf)
        if pdftotext(vfc_negative_pdf).strip() != "!":
            raise AssertionError("negative VFC count leaked payload")
        if ppm_sha256(vfc_fractional_pdf, tmp / "vfc-fractional",
                      dpi=150) != \
           ppm_sha256(vfc_integer_pdf, tmp / "vfc-integer", dpi=150):
            raise AssertionError("fractional VFC count rounded")
        if pdftotext(vfc_lower_negative_pdf).strip() != "!":
            raise AssertionError("lowercase negative VFC count leaked payload")
        if ppm_sha256(vfc_lower_fractional_pdf,
                      tmp / "vfc-lower-fractional", dpi=150) != \
           ppm_sha256(vfc_integer_pdf, tmp / "vfc-integer", dpi=150):
            raise AssertionError("lowercase fractional VFC count rounded")

        vfc_jump_positive = write(tmp / "vfc-jump-positive.pcl",
                                  ESC + b"&l4W" + b"\x00\x00\x00\x02" +
                                  ESC + b"&l2V" + b"!" + FF)
        vfc_jump_negative = write(tmp / "vfc-jump-negative.pcl",
                                  ESC + b"&l4W" + b"\x00\x00\x00\x02" +
                                  ESC + b"&l-2V" + b"!" + FF)
        vfc_jump_fractional = write(tmp / "vfc-jump-fractional.pcl",
                                    ESC + b"&l4W" + b"\x00\x00\x00\x02" +
                                    ESC + b"&l2.9V" + b"!" + FF)
        vfc_jump_positive_pdf = tmp / "vfc-jump-positive.pdf"
        vfc_jump_negative_pdf = tmp / "vfc-jump-negative.pdf"
        vfc_jump_fractional_pdf = tmp / "vfc-jump-fractional.pdf"
        render(dreamprint, vfc_jump_positive, vfc_jump_positive_pdf)
        render(dreamprint, vfc_jump_negative, vfc_jump_negative_pdf)
        render(dreamprint, vfc_jump_fractional, vfc_jump_fractional_pdf)
        if ppm_sha256(vfc_jump_positive_pdf, tmp / "vfc-jump-positive",
                      dpi=150) != \
           ppm_sha256(vfc_jump_negative_pdf, tmp / "vfc-jump-negative",
                      dpi=150):
            raise AssertionError("negative VFC channel selector did not match positive")
        if ppm_sha256(vfc_jump_positive_pdf, tmp / "vfc-jump-positive",
                      dpi=150) != \
           ppm_sha256(vfc_jump_fractional_pdf,
                      tmp / "vfc-jump-fractional", dpi=150):
            raise AssertionError("fractional VFC channel selector rounded")

        vfc_limit_lines = bytearray()
        for i in range(6):
            vfc_limit_lines += f"V{i:02d}\n".encode("ascii")
        vfc_limit_default = write(tmp / "vfc-limit-default.pcl",
                                  ESC + b"&l1L" +
                                  bytes(vfc_limit_lines) + FF)
        vfc_limit_custom = write(tmp / "vfc-limit-custom.pcl",
                                 ESC + b"&l1L" +
                                 ESC + b"&l4W" + b"\x00\x00\x00\x02" +
                                 bytes(vfc_limit_lines) + FF)
        vfc_limit_default_pdf = tmp / "vfc-limit-default.pdf"
        vfc_limit_custom_pdf = tmp / "vfc-limit-custom.pdf"
        render(dreamprint, vfc_limit_default, vfc_limit_default_pdf)
        render(dreamprint, vfc_limit_custom, vfc_limit_custom_pdf)
        if pdf_pages(vfc_limit_custom_pdf) <= pdf_pages(vfc_limit_default_pdf):
            raise AssertionError("custom VFC channel-2 limit did not affect overflow")
        if "V05" not in pdftotext(vfc_limit_custom_pdf):
            raise AssertionError("custom VFC overflow lost trailing text")

        vfc_line63_table = (b"\x00\x00" * 63) + b"\x00\x02"
        vfc_bottom_recovery = write(tmp / "vfc-bottom-recovery.pcl",
                                    ESC + b"&l128W" + vfc_line63_table +
                                    ESC + b"&a64R" +
                                    ESC + b"&l2V" + b"!" + FF)
        vfc_bottom_recovery_pdf = tmp / "vfc-bottom-recovery.pdf"
        render(dreamprint, vfc_bottom_recovery, vfc_bottom_recovery_pdf)
        bbox = ppm_bbox(vfc_bottom_recovery_pdf, tmp / "vfc-bottom-recovery",
                        dpi=72)
        if bbox is None or bbox[1] > 120:
            raise AssertionError("VFC line-63 recovery did not return near top")

        vfc_target_after_text = write(tmp / "vfc-target-after-text.pcl",
                                      ESC + b"&l128W" + vfc_line63_table +
                                      b"A" + ESC + b"&a58R" +
                                      ESC + b"&l2V" + b"B" + FF)
        vfc_target_after_text_pdf = tmp / "vfc-target-after-text.pdf"
        render(dreamprint, vfc_target_after_text, vfc_target_after_text_pdf)
        if pdf_pages(vfc_target_after_text_pdf) != 2:
            raise AssertionError("VFC target-after-text did not publish old page")
        if "AB" not in "".join(pdftotext(vfc_target_after_text_pdf).split()):
            raise AssertionError("VFC target-after-text lost selectable text")

        vfc_selector_zero_eject = write(tmp / "vfc-selector-zero-eject.pcl",
                                        b"A" + ESC + b"&l0V" + b"B" + FF)
        vfc_selector_zero_start_after_text = write(
            tmp / "vfc-selector-zero-start-after-text.pcl",
            b"A" + ESC + b"&a64R" + ESC + b"&l0V" + b"B" + FF)
        vfc_selector_zero_eject_pdf = tmp / "vfc-selector-zero-eject.pdf"
        vfc_selector_zero_start_after_text_pdf = \
            tmp / "vfc-selector-zero-start-after-text.pdf"
        render(dreamprint, vfc_selector_zero_eject,
               vfc_selector_zero_eject_pdf)
        render(dreamprint, vfc_selector_zero_start_after_text,
               vfc_selector_zero_start_after_text_pdf)
        if pdf_pages(vfc_selector_zero_eject_pdf) != 2:
            raise AssertionError("VFC selector-zero did not publish old page")
        if pdf_pages(vfc_selector_zero_start_after_text_pdf) != 1:
            raise AssertionError("VFC selector-zero start-after-text published")
        if "AB" not in "".join(pdftotext(vfc_selector_zero_eject_pdf).split()):
            raise AssertionError("VFC selector-zero eject lost selectable text")
        if "AB" not in "".join(
                pdftotext(vfc_selector_zero_start_after_text_pdf).split()):
            raise AssertionError(
                "VFC selector-zero start-after-text lost selectable text")

        vfc_256_table = bytearray(b"\x00\x00" * 128)
        vfc_256_table[2:4] = b"\x00\x02"
        vfc_oversize_store = write(
            tmp / "vfc-oversize-store.pcl",
            ESC + b"&l48D" +
            ESC + b"&l258W" + bytes(vfc_256_table) + b"\xff\xff" +
            ESC + b"&l2V" + b"!" + FF)
        vfc_256_store = write(
            tmp / "vfc-256-store.pcl",
            ESC + b"&l48D" +
            ESC + b"&l256W" + bytes(vfc_256_table) +
            ESC + b"&l2V" + b"!" + FF)
        vfc_oversize_store_pdf = tmp / "vfc-oversize-store.pdf"
        vfc_256_store_pdf = tmp / "vfc-256-store.pdf"
        render(dreamprint, vfc_oversize_store, vfc_oversize_store_pdf)
        render(dreamprint, vfc_256_store, vfc_256_store_pdf)
        if ppm_sha256(vfc_oversize_store_pdf,
                      tmp / "vfc-oversize-store", dpi=150) != \
           ppm_sha256(vfc_256_store_pdf, tmp / "vfc-256-store", dpi=150):
            raise AssertionError("oversized accepted VFC table did not store first 256 bytes")
        if pdftotext(vfc_oversize_store_pdf).strip() != "!":
            raise AssertionError("oversized VFC table lost following text")

        vfc_probe_x = write(tmp / "vfc-probe-x.pcl",
                            ESC + b"&l4W" + b"\x1aX\x00\x00\x02" +
                            ESC + b"&l2V" + b"!" + FF)
        vfc_probe_plain = write(tmp / "vfc-probe-plain.pcl",
                                ESC + b"&l4W" + b"\x00\x00\x00\x02" +
                                ESC + b"&l2V" + b"!" + FF)
        vfc_probe_x_pdf = tmp / "vfc-probe-x.pdf"
        vfc_probe_plain_pdf = tmp / "vfc-probe-plain.pdf"
        render(dreamprint, vfc_probe_x, vfc_probe_x_pdf)
        render(dreamprint, vfc_probe_plain, vfc_probe_plain_pdf)
        if ppm_sha256(vfc_probe_x_pdf, tmp / "vfc-probe-x", dpi=150) != \
           ppm_sha256(vfc_probe_plain_pdf, tmp / "vfc-probe-plain",
                      dpi=150):
            raise AssertionError("VFC 0x1a X did not normalize to zero byte")

        vfc_probe_q = write(tmp / "vfc-probe-q.pcl",
                            ESC + b"&l4W" + b"\x1aQ\x00\x00\x02" +
                            ESC + b"&l2V" + b"!" + FF)
        vfc_probe_q_plain = write(tmp / "vfc-probe-q-plain.pcl",
                                  ESC + b"&l4W" + b"Q\x00\x00\x02" +
                                  ESC + b"&l2V" + b"!" + FF)
        vfc_probe_q_pdf = tmp / "vfc-probe-q.pdf"
        vfc_probe_q_plain_pdf = tmp / "vfc-probe-q-plain.pdf"
        render(dreamprint, vfc_probe_q, vfc_probe_q_pdf)
        render(dreamprint, vfc_probe_q_plain, vfc_probe_q_plain_pdf)
        if ppm_sha256(vfc_probe_q_pdf, tmp / "vfc-probe-q", dpi=150) != \
           ppm_sha256(vfc_probe_q_plain_pdf, tmp / "vfc-probe-q-plain",
                      dpi=150):
            raise AssertionError("VFC 0x1a non-X probe did not keep probe byte")

        macro_execute = write(tmp / "macro-execute.pcl",
                              ESC + b"&f321Y" +
                              ESC + b"&f0X" + b"!\r" +
                              ESC + b"&f1X" +
                              ESC + b"&f2X" + FF)
        macro_call = write(tmp / "macro-call.pcl",
                           ESC + b"&f322Y" +
                           ESC + b"&f0X" + b"!\r" +
                           ESC + b"&f1X" +
                           ESC + b"&f3X" + FF)
        macro_fractional = write(tmp / "macro-fractional.pcl",
                                 ESC + b"&f700.9Y" +
                                 ESC + b"&f0.9X" + b"!\r" +
                                 ESC + b"&f1.9X" +
                                 ESC + b"&f2.9X" + FF)
        macro_execute_pdf = tmp / "macro-execute.pdf"
        macro_call_pdf = tmp / "macro-call.pdf"
        macro_fractional_pdf = tmp / "macro-fractional.pdf"
        render(dreamprint, macro_execute, macro_execute_pdf)
        render(dreamprint, macro_call, macro_call_pdf)
        render(dreamprint, macro_fractional, macro_fractional_pdf)
        if "!" not in pdftotext(macro_execute_pdf):
            raise AssertionError("macro execute did not replay payload")
        if "!" not in pdftotext(macro_call_pdf):
            raise AssertionError("macro call did not replay payload")
        if "!" not in pdftotext(macro_fractional_pdf):
            raise AssertionError("fractional macro selector rounded")

        macro_lower_chain = write(tmp / "macro-lower-chain.pcl",
                                  ESC + b"&f701Y" +
                                  ESC + b"&f0x1X" +
                                  ESC + b"&f2X" + b"Z" + FF)
        macro_lower_payload = write(tmp / "macro-lower-payload.pcl",
                                    ESC + b"&f702Y" +
                                    ESC + b"&f0x" + b"!\r" +
                                    ESC + b"&f1x" +
                                    ESC + b"&f2x" + b"Z" + FF)
        macro_lower_expected = write(tmp / "macro-lower-expected.pcl",
                                     b"Z" + FF)
        macro_lower_payload_expected = write(
            tmp / "macro-lower-payload-expected.pcl", b"\rZ" + FF)
        macro_lower_chain_pdf = tmp / "macro-lower-chain.pdf"
        macro_lower_payload_pdf = tmp / "macro-lower-payload.pdf"
        macro_lower_expected_pdf = tmp / "macro-lower-expected.pdf"
        macro_lower_payload_expected_pdf = \
            tmp / "macro-lower-payload-expected.pdf"
        render(dreamprint, macro_lower_chain, macro_lower_chain_pdf)
        render(dreamprint, macro_lower_payload, macro_lower_payload_pdf)
        render(dreamprint, macro_lower_expected, macro_lower_expected_pdf)
        render(dreamprint, macro_lower_payload_expected,
               macro_lower_payload_expected_pdf)
        expected_lower_hash = ppm_sha256(macro_lower_expected_pdf,
                                         tmp / "macro-lower-expected",
                                         dpi=300)
        expected_lower_payload_hash = ppm_sha256(
            macro_lower_payload_expected_pdf,
            tmp / "macro-lower-payload-expected", dpi=300)
        if ppm_sha256(macro_lower_chain_pdf,
                      tmp / "macro-lower-chain", dpi=300) != \
           expected_lower_hash:
            raise AssertionError("lowercase macro chain did not stop definition")
        if ppm_sha256(macro_lower_payload_pdf,
                      tmp / "macro-lower-payload", dpi=300) != \
           expected_lower_payload_hash:
            raise AssertionError("lowercase macro start did not seed auto-prefix")

        macro_display_control = write(
            tmp / "macro-display-control.pcl",
            ESC + b"&f703Y" +
            ESC + b"&f0X" +
            ESC + b"*p0X" + b"A" + ESC + b"Y" +
            bytes([0x1a, 0x58]) +
            ESC + b"ZB" +
            ESC + b"&f1X" +
            (ESC + b"&f2X") * 256 + FF)
        macro_display_control_pdf = tmp / "macro-display-control.pdf"
        render(dreamprint, macro_display_control,
               macro_display_control_pdf)
        if pdf_pages(macro_display_control_pdf) != 1:
            raise AssertionError("macro display append replayed payload-control side effect")
        macro_display_text = "".join(
            pdftotext(macro_display_control_pdf).split())
        if macro_display_text.count("AZB") < 256:
            raise AssertionError("macro display append lost replayed text")

        macro_control_z_append = write(
            tmp / "macro-control-z-append.pcl",
            ESC + b"&f704Y" +
            ESC + b"&f0X" +
            ESC + b"*p0X" + b"A" + bytes([0x1a, 0x58]) + b"B" +
            ESC + b"&f1X" +
            (ESC + b"&f2X") * 256 + FF)
        macro_control_z_append_pdf = tmp / "macro-control-z-append.pdf"
        render(dreamprint, macro_control_z_append,
               macro_control_z_append_pdf)
        if pdf_pages(macro_control_z_append_pdf) != 1:
            raise AssertionError("macro Control-Z append replayed payload-control side effect")
        macro_control_z_text = "".join(
            pdftotext(macro_control_z_append_pdf).split())
        if macro_control_z_text.count("AB") < 256:
            raise AssertionError("macro Control-Z append lost replayed text")

        macro_transparent_append = write(
            tmp / "macro-transparent-append.pcl",
            ESC + b"&f705Y" +
            ESC + b"&f0X" +
            ESC + b"*p0X" + b"A" +
            ESC + b"&p1X" + bytes([0x1a, 0x58]) + b"B" +
            ESC + b"&f1X" +
            (ESC + b"&f2X") * 256 + FF)
        macro_transparent_append_pdf = tmp / "macro-transparent-append.pdf"
        render(dreamprint, macro_transparent_append,
               macro_transparent_append_pdf)
        if pdf_pages(macro_transparent_append_pdf) != 1:
            raise AssertionError("macro transparent append replayed payload handler")
        macro_transparent_text = "".join(
            pdftotext(macro_transparent_append_pdf).split())
        if macro_transparent_text.count("AB") < 256:
            raise AssertionError("macro transparent append lost replayed text")

        macro_transparent_lower_append = write(
            tmp / "macro-transparent-lower-append.pcl",
            ESC + b"&f707Y" +
            ESC + b"&f0X" +
            ESC + b"*p0X" + b"A" +
            ESC + b"&p1x9X" + b"BC" +
            ESC + b"&f1X" +
            (ESC + b"&f2X") * 256 + FF)
        macro_transparent_lower_append_pdf = \
            tmp / "macro-transparent-lower-append.pdf"
        render(dreamprint, macro_transparent_lower_append,
               macro_transparent_lower_append_pdf)
        macro_transparent_lower_text = "".join(
            pdftotext(macro_transparent_lower_append_pdf).split())
        if macro_transparent_lower_text.count("ABC") < 256:
            raise AssertionError("macro transparent lowercase delayed payload replayed handler")

        macro_raster_lower_append = write(
            tmp / "macro-raster-lower-append.pcl",
            ESC + b"&f708Y" +
            ESC + b"&f0X" +
            ESC + b"*p0X" + b"A" +
            ESC + b"*b1w9W" + b"BC" +
            ESC + b"&f1X" +
            (ESC + b"&f2X") * 256 + FF)
        macro_raster_lower_append_pdf = tmp / "macro-raster-lower-append.pdf"
        render(dreamprint, macro_raster_lower_append,
               macro_raster_lower_append_pdf)
        macro_raster_lower_text = "".join(
            pdftotext(macro_raster_lower_append_pdf).split())
        if macro_raster_lower_text.count("ABC") < 256:
            raise AssertionError("macro raster lowercase delayed payload replayed handler")

        macro_vfc_lower_append = write(
            tmp / "macro-vfc-lower-append.pcl",
            ESC + b"&f709Y" +
            ESC + b"&f0X" +
            ESC + b"*p0X" + b"A" +
            ESC + b"&l1w9W" + b"BC" +
            ESC + b"&f1X" +
            (ESC + b"&f2X") * 256 + FF)
        macro_vfc_lower_append_pdf = tmp / "macro-vfc-lower-append.pdf"
        render(dreamprint, macro_vfc_lower_append,
               macro_vfc_lower_append_pdf)
        macro_vfc_lower_text = "".join(
            pdftotext(macro_vfc_lower_append_pdf).split())
        if macro_vfc_lower_text.count("ABC") < 256:
            raise AssertionError("macro VFC lowercase delayed payload replayed handler")

        macro_font_lower_append = write(
            tmp / "macro-font-lower-append.pcl",
            ESC + b"&f710Y" +
            ESC + b"&f0X" +
            ESC + b"*p0X" + b"A" +
            ESC + b")s1w9W" + b"BC" +
            ESC + b"&f1X" +
            (ESC + b"&f2X") * 256 + FF)
        macro_font_lower_append_pdf = tmp / "macro-font-lower-append.pdf"
        render(dreamprint, macro_font_lower_append,
               macro_font_lower_append_pdf)
        macro_font_lower_text = "".join(
            pdftotext(macro_font_lower_append_pdf).split())
        if macro_font_lower_text.count("ABC") < 256:
            raise AssertionError("macro font lowercase delayed payload replayed handler")

        macro_font_x_payload = write(
            tmp / "macro-font-x-payload.pcl",
            ESC + b"&f706Y" +
            ESC + b"&f0X" +
            ESC + b"(s1X" + b"B" +
            ESC + b"&f1X" +
            b"A" + ESC + b"&f2X" + FF)
        macro_font_x_payload_pdf = tmp / "macro-font-x-payload.pdf"
        render(dreamprint, macro_font_x_payload,
               macro_font_x_payload_pdf)
        if "AB" not in "".join(pdftotext(macro_font_x_payload_pdf).split()):
            raise AssertionError("macro capture treated font final-X as stop")

        macro_call_restore = write(
            tmp / "macro-call-restore.pcl",
            b"A" +
            ESC + b"&f410Y" +
            ESC + b"&f0X" + ESC + b"*p300X" + b"M" +
            ESC + b"&f1X" +
            ESC + b"&f3X" + b"B" + FF)
        macro_execute_no_restore = write(
            tmp / "macro-execute-no-restore.pcl",
            b"A" +
            ESC + b"&f411Y" +
            ESC + b"&f0X" + ESC + b"*p300X" + b"M" +
            ESC + b"&f1X" +
            ESC + b"&f2X" + b"B" + FF)
        macro_call_restore_pdf = tmp / "macro-call-restore.pdf"
        macro_execute_no_restore_pdf = tmp / "macro-execute-no-restore.pdf"
        render(dreamprint, macro_call_restore, macro_call_restore_pdf)
        render(dreamprint, macro_execute_no_restore,
               macro_execute_no_restore_pdf)
        call_cursor_pixels = ppm_rect_nonwhite(
            macro_call_restore_pdf, tmp / "macro-call-restore", 83, 70, 112,
            130, dpi=300)
        execute_cursor_pixels = ppm_rect_nonwhite(
            macro_execute_no_restore_pdf, tmp / "macro-execute-no-restore",
            83, 70, 112, 130, dpi=300)
        execute_macro_pixels = ppm_rect_nonwhite(
            macro_execute_no_restore_pdf, tmp / "macro-execute-no-restore-far",
            295, 70, 365, 130, dpi=300)
        if call_cursor_pixels < 200:
            raise AssertionError("macro call did not restore caller cursor")
        if execute_cursor_pixels > 50 or execute_macro_pixels < 200:
            raise AssertionError("macro execute unexpectedly restored cursor")

        macro_stack_replayed = write(
            tmp / "macro-stack-replayed.pcl",
            ESC + b"&f412Y" +
            ESC + b"&f0X" + ESC + b"&f0S" + ESC + b"*p300X" +
            ESC + b"&f1S" + b"!" + ESC + b"&f1X" +
            ESC + b"&f412Y" + ESC + b"&f2X" + FF)
        macro_stack_replayed_expected = write(
            tmp / "macro-stack-expected.pcl",
            ESC + b"&f0S" + ESC + b"*p300X" +
            ESC + b"&f1S" + b"!" + FF)
        macro_stack_replayed_pdf = tmp / "macro-stack-replayed.pdf"
        macro_stack_replayed_expected_pdf = tmp / "macro-stack-expected.pdf"
        render(dreamprint, macro_stack_replayed, macro_stack_replayed_pdf)
        render(dreamprint, macro_stack_replayed_expected,
               macro_stack_replayed_expected_pdf)
        if ppm_sha256(macro_stack_replayed_pdf,
                      tmp / "macro-stack-replayed", dpi=300) != \
           ppm_sha256(macro_stack_replayed_expected_pdf,
                      tmp / "macro-stack-expected", dpi=300):
            raise AssertionError("macro replay did not execute cursor stack payload")

        macro_id_replayed = write(
            tmp / "macro-id-replayed.pcl",
            ESC + b"&f414Y" +
            ESC + b"&f0X" + ESC + b"&f415Y" + ESC + b"&f2X" +
            ESC + b"&f1X" +
            ESC + b"&f415Y" +
            ESC + b"&f0X" + b"!" + ESC + b"&f1X" +
            ESC + b"&f414Y" + ESC + b"&f2X" + FF)
        macro_id_replayed_expected = write(
            tmp / "macro-id-expected.pcl",
            ESC + b"&f416Y" +
            ESC + b"&f0X" + b"!" + ESC + b"&f1X" +
            ESC + b"&f416Y" + ESC + b"&f2X" + FF)
        macro_id_replayed_pdf = tmp / "macro-id-replayed.pdf"
        macro_id_replayed_expected_pdf = tmp / "macro-id-expected.pdf"
        render(dreamprint, macro_id_replayed, macro_id_replayed_pdf)
        render(dreamprint, macro_id_replayed_expected,
               macro_id_replayed_expected_pdf)
        if ppm_sha256(macro_id_replayed_pdf,
                      tmp / "macro-id-replayed", dpi=300) != \
           ppm_sha256(macro_id_replayed_expected_pdf,
                      tmp / "macro-id-expected", dpi=300):
            raise AssertionError("macro replay did not execute macro-id payload")

        macro_reset_definition = write(
            tmp / "macro-reset-definition.pcl",
            ESC + b"&f417Y" +
            ESC + b"&f0X" + b"A" + ESC + b"E" +
            ESC + b"&f1X" +
            ESC + b"&f417Y" + ESC + b"&f2X" +
            b"!" + FF)
        macro_reset_definition_expected = write(
            tmp / "macro-reset-definition-expected.pcl",
            ESC + b"&f417Y" +
            ESC + b"&f0X" + b"A" + ESC + b"&f1X" +
            ESC + b"E" +
            ESC + b"&f417Y" + ESC + b"&f2X" +
            b"!" + FF)
        macro_reset_definition_pdf = tmp / "macro-reset-definition.pdf"
        macro_reset_definition_expected_pdf = \
            tmp / "macro-reset-definition-expected.pdf"
        render(dreamprint, macro_reset_definition, macro_reset_definition_pdf)
        render(dreamprint, macro_reset_definition_expected,
               macro_reset_definition_expected_pdf)
        if ppm_sha256(macro_reset_definition_pdf,
                      tmp / "macro-reset-definition", dpi=300) != \
           ppm_sha256(macro_reset_definition_expected_pdf,
                      tmp / "macro-reset-definition-expected", dpi=300):
            raise AssertionError(
                "ESC E did not reset as an active macro-definition exception")

        macro_permanent_reset = write(
            tmp / "macro-permanent-reset.pcl",
            ESC + b"&f418Y" +
            ESC + b"&f0X" + b"P" + ESC + b"&f1X" +
            ESC + b"&f10X" +
            ESC + b"E" +
            ESC + b"&f418Y" + ESC + b"&f2X" +
            b"!" + FF)
        macro_temporary_reset = write(
            tmp / "macro-temporary-reset.pcl",
            ESC + b"&f419Y" +
            ESC + b"&f0X" + b"T" + ESC + b"&f1X" +
            ESC + b"E" +
            ESC + b"&f419Y" + ESC + b"&f2X" +
            b"!" + FF)
        macro_permanent_reset_pdf = tmp / "macro-permanent-reset.pdf"
        macro_temporary_reset_pdf = tmp / "macro-temporary-reset.pdf"
        render(dreamprint, macro_permanent_reset, macro_permanent_reset_pdf)
        render(dreamprint, macro_temporary_reset, macro_temporary_reset_pdf)
        if "P!" not in "".join(pdftotext(macro_permanent_reset_pdf).split()):
            raise AssertionError("permanent macro did not survive reset")
        if "".join(pdftotext(macro_temporary_reset_pdf).split()) != "!":
            raise AssertionError("temporary macro survived reset")

        macro_delete_temporary = write(
            tmp / "macro-delete-temporary.pcl",
            ESC + b"&f420Y" +
            ESC + b"&f0X" + b"T" + ESC + b"&f1X" +
            ESC + b"&f421Y" +
            ESC + b"&f0X" + b"P" + ESC + b"&f1X" +
            ESC + b"&f10X" +
            ESC + b"&f7X" +
            ESC + b"&f420Y" + ESC + b"&f2X" +
            ESC + b"&f421Y" + ESC + b"&f2X" +
            b"!" + FF)
        macro_delete_current = write(
            tmp / "macro-delete-current.pcl",
            ESC + b"&f422Y" +
            ESC + b"&f0X" + b"C" + ESC + b"&f1X" +
            ESC + b"&f8X" +
            ESC + b"&f2X" +
            b"!" + FF)
        macro_delete_all = write(
            tmp / "macro-delete-all.pcl",
            ESC + b"&f423Y" +
            ESC + b"&f0X" + b"A" + ESC + b"&f1X" +
            ESC + b"&f10X" +
            ESC + b"&f6X" +
            ESC + b"&f2X" +
            b"!" + FF)
        macro_permanent_to_temporary_delete = write(
            tmp / "macro-permanent-to-temporary-delete.pcl",
            ESC + b"&f424Y" +
            ESC + b"&f0X" + b"R" + ESC + b"&f1X" +
            ESC + b"&f10X" +
            ESC + b"&f9X" +
            ESC + b"&f7X" +
            ESC + b"&f2X" +
            b"!" + FF)
        macro_delete_temporary_pdf = tmp / "macro-delete-temporary.pdf"
        macro_delete_current_pdf = tmp / "macro-delete-current.pdf"
        macro_delete_all_pdf = tmp / "macro-delete-all.pdf"
        macro_permanent_to_temporary_delete_pdf = \
            tmp / "macro-permanent-to-temporary-delete.pdf"
        render(dreamprint, macro_delete_temporary,
               macro_delete_temporary_pdf)
        render(dreamprint, macro_delete_current, macro_delete_current_pdf)
        render(dreamprint, macro_delete_all, macro_delete_all_pdf)
        render(dreamprint, macro_permanent_to_temporary_delete,
               macro_permanent_to_temporary_delete_pdf)
        macro_delete_temporary_text = "".join(
            pdftotext(macro_delete_temporary_pdf).split())
        if macro_delete_temporary_text != "P!":
            raise AssertionError("delete-temporary macro selector removed wrong records")
        if "".join(pdftotext(macro_delete_current_pdf).split()) != "!":
            raise AssertionError("delete-current macro selector left replayable record")
        if "".join(pdftotext(macro_delete_all_pdf).split()) != "!":
            raise AssertionError("delete-all macro selector preserved permanent record")
        if "".join(pdftotext(macro_permanent_to_temporary_delete_pdf).split()) != "!":
            raise AssertionError("make-temporary macro selector did not affect delete-temporary")

        macro_nested = write(tmp / "macro-nested.pcl",
                             ESC + b"&f500Y" +
                             ESC + b"&f0X" + b"N" +
                             ESC + b"&f1X" +
                             ESC + b"&f501Y" +
                             ESC + b"&f0X" +
                             ESC + b"&f500Y" + ESC + b"&f2X" +
                             ESC + b"&f1X" +
                             ESC + b"&f501Y" + ESC + b"&f2X" + FF)
        macro_nested_pdf = tmp / "macro-nested.pdf"
        render(dreamprint, macro_nested, macro_nested_pdf)
        if "N" not in pdftotext(macro_nested_pdf):
            raise AssertionError("nested macro execute did not replay payload")

        macro_replay_guard = write(
            tmp / "macro-replay-guard.pcl",
            ESC + b"&f902Y" +
            ESC + b"&f0X" + b"!" +
            ESC + b"&f1X" +
            ESC + b"&f901Y" +
            ESC + b"&f0X" +
            ESC + b"&f902Y" +
            ESC + b"&f4X" +
            b"A" +
            ESC + b"&f1X" +
            ESC + b"&f901Y" +
            ESC + b"&f2X" +
            b"Live" + FF)
        macro_replay_guard_pdf = tmp / "macro-replay-guard.pdf"
        render(dreamprint, macro_replay_guard, macro_replay_guard_pdf)
        replay_guard_text = pdftotext(macro_replay_guard_pdf)
        if "A" not in replay_guard_text or "Live" not in replay_guard_text:
            raise AssertionError("macro replay guard test lost base output")
        if "!" in replay_guard_text:
            raise AssertionError("macro replay selector armed overlay")

        macro_negative = write(tmp / "macro-negative.pcl",
                               ESC + b"&f-321Y" +
                               ESC + b"&f0X" + b"!" +
                               ESC + b"&f1X" +
                               ESC + b"&f321Y" +
                               ESC + b"&f-2X" + FF)
        macro_negative_pdf = tmp / "macro-negative.pdf"
        render(dreamprint, macro_negative, macro_negative_pdf)
        if "!" not in pdftotext(macro_negative_pdf):
            raise AssertionError("negative macro id/selector did not replay payload")

        overlay = write(tmp / "overlay.pcl",
                        ESC + b"&f123Y" +
                        ESC + b"&f0X" + b"!\r" +
                        ESC + b"&f1X" +
                        ESC + b"&f4X" + b"Live\r" + FF)
        overlay_eof = write(tmp / "overlay-eof.pcl",
                            ESC + b"&f139Y" +
                            ESC + b"&f0X" + b"!\r" +
                            ESC + b"&f1X" +
                            ESC + b"&f4X" + b"Live\r")
        overlay_pdf = tmp / "overlay.pdf"
        overlay_eof_pdf = tmp / "overlay-eof.pdf"
        render(dreamprint, overlay, overlay_pdf)
        render(dreamprint, overlay_eof, overlay_eof_pdf)
        if "Live!" not in pdftotext(overlay_pdf):
            raise AssertionError("macro overlay did not replay at publication")
        if "Live!" not in pdftotext(overlay_eof_pdf):
            raise AssertionError("EOF flush did not replay macro overlay")

        overlay_repeated = write(
            tmp / "overlay-repeated.pcl",
            ESC + b"&f129Y" +
            ESC + b"&f0X" + b"!" +
            ESC + b"&f1X" +
            ESC + b"&f4X" + b"Page1" + FF + b"Page2" + FF)
        overlay_repeated_pdf = tmp / "overlay-repeated.pdf"
        render(dreamprint, overlay_repeated, overlay_repeated_pdf)
        overlay_repeated_text = pdftotext(overlay_repeated_pdf)
        if pdf_pages(overlay_repeated_pdf) != 2 or \
           overlay_repeated_text.count("!") < 2:
            raise AssertionError("macro overlay did not survive page publication")

        overlay_disable = write(
            tmp / "overlay-disable.pcl",
            ESC + b"&f130Y" +
            ESC + b"&f0X" + b"!" +
            ESC + b"&f1X" +
            ESC + b"&f4X" +
            ESC + b"&f5X" +
            b"Live" + FF)
        overlay_disable_pdf = tmp / "overlay-disable.pdf"
        render(dreamprint, overlay_disable, overlay_disable_pdf)
        if "!" in pdftotext(overlay_disable_pdf):
            raise AssertionError("macro overlay disable selector did not clear state")

        overlay_missing_then_defined = write(
            tmp / "overlay-missing-then-defined.pcl",
            ESC + b"&f124Y" +
            ESC + b"&f4X" +
            ESC + b"&f0X" + b"!" +
            ESC + b"&f1X" +
            b"Live" + FF)
        overlay_missing_then_defined_pdf = \
            tmp / "overlay-missing-then-defined.pdf"
        render(dreamprint, overlay_missing_then_defined,
               overlay_missing_then_defined_pdf)
        if "!" in pdftotext(overlay_missing_then_defined_pdf):
            raise AssertionError("missing overlay enable survived later definition")

        overlay_empty = write(
            tmp / "overlay-empty.pcl",
            ESC + b"&f125Y" +
            ESC + b"&f0X" + b"!" +
            ESC + b"&f1X" +
            ESC + b"&f4X" +
            ESC + b"&f0X" +
            ESC + b"&f1X" +
            b"Live" + FF)
        overlay_empty_pdf = tmp / "overlay-empty.pdf"
        render(dreamprint, overlay_empty, overlay_empty_pdf)
        if "!" in pdftotext(overlay_empty_pdf):
            raise AssertionError("empty overlay macro record replayed at publication")

        overlay_transparent = write(
            tmp / "overlay-transparent.pcl",
            ESC + b"&f126Y" +
            ESC + b"&f0X" + ESC + b"&p2X" + b"!!" +
            ESC + b"&f1X" +
            ESC + b"&f4X" + ESC + b"*p400X" + b"Live" + FF)
        overlay_transparent_pdf = tmp / "overlay-transparent.pdf"
        render(dreamprint, overlay_transparent, overlay_transparent_pdf)
        overlay_transparent_text = pdftotext(overlay_transparent_pdf)
        if "Live" not in overlay_transparent_text or \
           overlay_transparent_text.count("!") < 2:
            raise AssertionError("transparent overlay text did not extract")

        overlay_mixed_control = write(
            tmp / "overlay-mixed-control.pcl",
            ESC + b"&f131Y" +
            ESC + b"&f0X" + ESC + b"&k1G" + b"!\r!" +
            ESC + b"&f1X" +
            ESC + b"&f4X" + ESC + b"*p400X" + b"Live" + FF)
        overlay_cursor = write(
            tmp / "overlay-cursor.pcl",
            ESC + b"&f132Y" +
            ESC + b"&f0X" + ESC + b"&a2C" + b"!" +
            ESC + b"&f1X" +
            ESC + b"&f4X" + ESC + b"*p400X" + b"Live" + FF)
        overlay_vertical = write(
            tmp / "overlay-vertical.pcl",
            ESC + b"&f133Y" +
            ESC + b"&f0X" + ESC + b"&a72V" + b"!" +
            ESC + b"&f1X" +
            ESC + b"&f4X" + ESC + b"*p400X" + b"Live" + FF)
        overlay_chained_cursor = write(
            tmp / "overlay-chained-cursor.pcl",
            ESC + b"&f134Y" +
            ESC + b"&f0X" + ESC + b"&a2c+1R" + b"!" +
            ESC + b"&f1X" +
            ESC + b"&f4X" + ESC + b"*p400X" + b"Live" + FF)
        overlay_chained_margin = write(
            tmp / "overlay-chained-margin.pcl",
            ESC + b"&f135Y" +
            ESC + b"&f0X" + ESC + b"&s0C" + ESC + b"&a6l9M" + b"!" +
            ESC + b"&f1X" +
            ESC + b"&f4X" + ESC + b"*p400X" + b"Live" + FF)
        overlay_mixed_control_pdf = tmp / "overlay-mixed-control.pdf"
        overlay_cursor_pdf = tmp / "overlay-cursor.pdf"
        overlay_vertical_pdf = tmp / "overlay-vertical.pdf"
        overlay_chained_cursor_pdf = tmp / "overlay-chained-cursor.pdf"
        overlay_chained_margin_pdf = tmp / "overlay-chained-margin.pdf"
        render(dreamprint, overlay_mixed_control, overlay_mixed_control_pdf)
        render(dreamprint, overlay_cursor, overlay_cursor_pdf)
        render(dreamprint, overlay_vertical, overlay_vertical_pdf)
        render(dreamprint, overlay_chained_cursor,
               overlay_chained_cursor_pdf)
        render(dreamprint, overlay_chained_margin,
               overlay_chained_margin_pdf)
        if pdftotext(overlay_mixed_control_pdf).count("!") < 2:
            raise AssertionError("mixed-control overlay did not replay CR mode")

        overlay_text_only = write(
            tmp / "overlay-text-only.pcl",
            ESC + b"&f127Y" +
            ESC + b"&f0X" + b"!" +
            ESC + b"&f1X" +
            ESC + b"&f4X" + ESC + b"*p400X" + b"Live" + FF)
        overlay_raster = write(
            tmp / "overlay-raster.pcl",
            ESC + b"&f128Y" +
            ESC + b"&f0X" + b"!" +
            ESC + b"*t300R" +
            ESC + b"*r0A" +
            ESC + b"*b2W" + bytes([0xc3, 0x3c]) +
            ESC + b"&f1X" +
            ESC + b"&f4X" + ESC + b"*p400X" + b"Live" + FF)
        overlay_multi_raster = write(
            tmp / "overlay-multi-raster.pcl",
            ESC + b"&f136Y" +
            ESC + b"&f0X" + b"!" +
            ESC + b"*t300R" +
            ESC + b"*r0A" +
            ESC + b"*b2W" + bytes([0xf0, 0x0f]) +
            ESC + b"*b2W" + bytes([0x0f, 0xf0]) +
            ESC + b"&f1X" +
            ESC + b"&f4X" + ESC + b"*p400X" + b"Live" + FF)
        overlay_span_rule = \
            ESC + b"&f0S" + ESC + b"*p400X" + ESC + b"*c64a64b0P" + \
            ESC + b"&f1S"
        overlay_span_plain = write(
            tmp / "overlay-span-plain.pcl",
            ESC + b"&f137Y" +
            ESC + b"&f0X" + b"!" +
            ESC + b"&f1X" +
            ESC + b"&f4X" + overlay_span_rule + FF)
        overlay_span_flush = write(
            tmp / "overlay-span-flush.pcl",
            ESC + b"&f138Y" +
            ESC + b"&f0X" + ESC + b"&a6L" + b"!" +
            ESC + b"&f1X" +
            ESC + b"&f4X" + overlay_span_rule + FF)
        overlay_text_only_pdf = tmp / "overlay-text-only.pdf"
        overlay_raster_pdf = tmp / "overlay-raster.pdf"
        overlay_multi_raster_pdf = tmp / "overlay-multi-raster.pdf"
        overlay_span_plain_pdf = tmp / "overlay-span-plain.pdf"
        overlay_span_flush_pdf = tmp / "overlay-span-flush.pdf"
        render(dreamprint, overlay_text_only, overlay_text_only_pdf)
        render(dreamprint, overlay_raster, overlay_raster_pdf)
        render(dreamprint, overlay_multi_raster, overlay_multi_raster_pdf)
        render(dreamprint, overlay_span_plain, overlay_span_plain_pdf)
        render(dreamprint, overlay_span_flush, overlay_span_flush_pdf)
        text_only_box = ppm_bbox(overlay_text_only_pdf,
                                 tmp / "overlay-text-only", dpi=300)
        cursor_box = ppm_bbox(overlay_cursor_pdf,
                              tmp / "overlay-cursor", dpi=300)
        vertical_box = ppm_bbox(overlay_vertical_pdf,
                                tmp / "overlay-vertical", dpi=300)
        chained_cursor_box = ppm_bbox(overlay_chained_cursor_pdf,
                                      tmp / "overlay-chained-cursor",
                                      dpi=300)
        chained_margin_box = ppm_bbox(overlay_chained_margin_pdf,
                                      tmp / "overlay-chained-margin",
                                      dpi=300)
        span_plain_box = ppm_bbox(overlay_span_plain_pdf,
                                  tmp / "overlay-span-plain", dpi=300)
        span_flush_box = ppm_bbox(overlay_span_flush_pdf,
                                  tmp / "overlay-span-flush", dpi=300)
        if text_only_box is None or cursor_box is None or \
           cursor_box[0] >= text_only_box[0]:
            raise AssertionError("cursor-position overlay did not move glyph")
        if text_only_box is None or vertical_box is None or \
           vertical_box[1] >= text_only_box[1]:
            raise AssertionError("vertical-decipoint overlay did not move glyph")
        if text_only_box is None or chained_cursor_box is None or \
           chained_cursor_box[0] >= text_only_box[0] or \
           chained_cursor_box[3] <= text_only_box[3]:
            raise AssertionError("chained cursor-position overlay did not move glyph")
        if text_only_box is None or chained_margin_box is None or \
           chained_margin_box[0] >= text_only_box[0] or \
           chained_margin_box[3] <= text_only_box[3]:
            raise AssertionError("chained margin overlay did not move glyph")
        if span_plain_box is None or span_flush_box is None or \
           span_flush_box[0] <= span_plain_box[0]:
            raise AssertionError("span-flush overlay did not apply left margin")
        if ppm_nonwhite(overlay_transparent_pdf,
                        tmp / "overlay-transparent", dpi=300) <= \
           ppm_nonwhite(overlay_text_only_pdf,
                        tmp / "overlay-text-only", dpi=300):
            raise AssertionError("transparent overlay payload did not add pixels")
        overlay_raster_text = pdftotext(overlay_raster_pdf)
        if "Live" not in overlay_raster_text or "!" not in overlay_raster_text:
            raise AssertionError("raster overlay lost selectable text")
        if ppm_nonwhite(overlay_raster_pdf, tmp / "overlay-raster",
                        dpi=300) <= \
           ppm_nonwhite(overlay_text_only_pdf,
                        tmp / "overlay-text-only", dpi=300):
            raise AssertionError("raster overlay payload did not add pixels")
        if ppm_nonwhite(overlay_multi_raster_pdf,
                        tmp / "overlay-multi-raster", dpi=300) <= \
           ppm_nonwhite(overlay_raster_pdf, tmp / "overlay-raster",
                        dpi=300):
            raise AssertionError("multi-row raster overlay did not add pixels")

    print("ok: LaserJet II PCL regression checks passed")


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as exc:
        sys.stderr.write(exc.stderr.decode("utf-8", errors="replace"))
        raise
