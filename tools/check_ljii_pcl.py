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
            if pixels[off:off + 3] == b"\xff\xff\xff":
                continue
            min_x = min(min_x, x)
            min_y = min(min_y, y)
            max_x = max(max_x, x)
            max_y = max(max_y, y)
    if max_x < 0:
        return None
    return min_x, min_y, max_x, max_y


def ppm_pixel(pdf, stem, x, y, dpi=72):
    width, height, pixels = ppm_image(pdf, stem, dpi)
    if x < 0 or y < 0 or x >= width or y >= height:
        raise AssertionError("pixel sample outside rendered image")
    off = (y * width + x) * 3
    return pixels[off:off + 3]


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
            "LaserJet II text capability sample",
            "# \\ ^ ~",
            "Transparent payload",
            "Downloaded glyph selected by font id",
            ")",
        ):
            if needle not in text:
                raise AssertionError(f"sample text missing {needle!r}")
        if ppm_nonwhite(sample_pdf, tmp / "sample") < 100:
            raise AssertionError("sample render looks blank")

        display = write(tmp / "display.pcl",
                        ESC + b"Y!\x05!" + ESC + b"Z" + FF)
        display_pdf = tmp / "display.pdf"
        render(dreamprint, display, display_pdf)
        display_text = "".join(pdftotext(display_pdf).split())
        if "!!Z" not in display_text:
            raise AssertionError("display-functions terminator did not route")

        display_esc = write(tmp / "display-esc.pcl",
                            ESC + b"YA" + ESC + b"EB" + ESC + b"Z" + FF)
        display_esc_pdf = tmp / "display-esc.pdf"
        render(dreamprint, display_esc, display_esc_pdf)
        display_esc_text = "".join(pdftotext(display_esc_pdf).split())
        if "AEBZ" not in display_esc_text:
            raise AssertionError("display-functions embedded ESC became command")

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

        tabbed = write(tmp / "tabbed.pcl", b"A\tB" + FF)
        explicit_tab = write(tmp / "explicit-tab.pcl",
                             b"A" + ESC + b"&a8C" + b"B" + FF)
        tabbed_pdf = tmp / "tabbed.pdf"
        explicit_tab_pdf = tmp / "explicit-tab.pdf"
        render(dreamprint, tabbed, tabbed_pdf)
        render(dreamprint, explicit_tab, explicit_tab_pdf)
        if "AB" not in "".join(pdftotext(tabbed_pdf).split()):
            raise AssertionError("tabbed text did not extract")
        if ppm_sha256(tabbed_pdf, tmp / "tabbed", dpi=150) != \
           ppm_sha256(explicit_tab_pdf, tmp / "explicit-tab", dpi=150):
            raise AssertionError("horizontal tab did not use next tab stop")

        dot_position = write(tmp / "dot-position.pcl",
                             ESC + b"*p300X" + b"A" + FF)
        column_position = write(tmp / "column-position.pcl",
                                ESC + b"&a10C" + b"A" + FF)
        default_position = write(tmp / "default-position.pcl", b"A" + FF)
        dot_position_pdf = tmp / "dot-position.pdf"
        column_position_pdf = tmp / "column-position.pdf"
        default_position_pdf = tmp / "default-position.pdf"
        render(dreamprint, dot_position, dot_position_pdf)
        render(dreamprint, column_position, column_position_pdf)
        render(dreamprint, default_position, default_position_pdf)
        if ppm_sha256(dot_position_pdf, tmp / "dot-position", dpi=150) != \
           ppm_sha256(column_position_pdf, tmp / "column-position", dpi=150):
            raise AssertionError("dot and column horizontal positions diverged")
        if ppm_sha256(dot_position_pdf, tmp / "dot-position", dpi=150) == \
           ppm_sha256(default_position_pdf, tmp / "default-position", dpi=150):
            raise AssertionError("absolute horizontal position did not move pixels")

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

        cursor_pop_positive = write(tmp / "cursor-pop-positive.pcl",
                                    ESC + b"&a20C" + ESC + b"&f0S" +
                                    ESC + b"&a30C" + ESC + b"&f1S" +
                                    b"!" + FF)
        cursor_pop_negative = write(tmp / "cursor-pop-negative.pcl",
                                    ESC + b"&a20C" + ESC + b"&f0S" +
                                    ESC + b"&a30C" + ESC + b"&f-1S" +
                                    b"!" + FF)
        cursor_pop_positive_pdf = tmp / "cursor-pop-positive.pdf"
        cursor_pop_negative_pdf = tmp / "cursor-pop-negative.pdf"
        render(dreamprint, cursor_pop_positive, cursor_pop_positive_pdf)
        render(dreamprint, cursor_pop_negative, cursor_pop_negative_pdf)
        if ppm_sha256(cursor_pop_positive_pdf, tmp / "cursor-pop-positive",
                      dpi=150) != \
           ppm_sha256(cursor_pop_negative_pdf, tmp / "cursor-pop-negative",
                      dpi=150):
            raise AssertionError("negative cursor-stack selector did not pop")

        line_term_positive = write(tmp / "line-term-positive.pcl",
                                   ESC + b"&k2G" + b"A\nB" + FF)
        line_term_negative = write(tmp / "line-term-negative.pcl",
                                   ESC + b"&k-2G" + b"A\nB" + FF)
        line_term_positive_pdf = tmp / "line-term-positive.pdf"
        line_term_negative_pdf = tmp / "line-term-negative.pdf"
        render(dreamprint, line_term_positive, line_term_positive_pdf)
        render(dreamprint, line_term_negative, line_term_negative_pdf)
        if ppm_sha256(line_term_positive_pdf, tmp / "line-term-positive",
                      dpi=150) != \
           ppm_sha256(line_term_negative_pdf, tmp / "line-term-negative",
                      dpi=150):
            raise AssertionError("negative line termination selector was not absolute")

        wrap_positive = write(tmp / "wrap-positive.pcl",
                              b"X" + ESC + b"&s1C" + ESC + b"&a80C" +
                              b"A" + FF)
        wrap_negative = write(tmp / "wrap-negative.pcl",
                              b"X" + ESC + b"&s-1C" + ESC + b"&a80C" +
                              b"A" + FF)
        wrap_positive_pdf = tmp / "wrap-positive.pdf"
        wrap_negative_pdf = tmp / "wrap-negative.pdf"
        render(dreamprint, wrap_positive, wrap_positive_pdf)
        render(dreamprint, wrap_negative, wrap_negative_pdf)
        if "".join(pdftotext(wrap_negative_pdf).split()) != "X":
            raise AssertionError("negative wrap-disable selector allowed overflow text")
        if ppm_sha256(wrap_positive_pdf, tmp / "wrap-positive", dpi=150) != \
           ppm_sha256(wrap_negative_pdf, tmp / "wrap-negative", dpi=150):
            raise AssertionError("negative wrap-disable selector did not match positive")

        control_z = write(tmp / "control-z.pcl",
                          b"A" + bytes([0x1a, 0x58]) + b"B" + FF)
        control_z_pdf = tmp / "control-z.pdf"
        render(dreamprint, control_z, control_z_pdf)
        if "".join(pdftotext(control_z_pdf).split()) != "AB":
            raise AssertionError("normal Control-Z X leaked printable text")

        raster_query = write(tmp / "raster-query.pcl",
                             ESC + b"*r1K" + b"QAB" + FF)
        model_query = write(tmp / "model-query.pcl",
                            ESC + b"*s1^" + b"QAB" + FF)
        raster_query_pdf = tmp / "raster-query.pdf"
        model_query_pdf = tmp / "model-query.pdf"
        render(dreamprint, raster_query, raster_query_pdf)
        render(dreamprint, model_query, model_query_pdf)
        if "".join(pdftotext(raster_query_pdf).split()) != "AB":
            raise AssertionError("raster query byte leaked printable text")
        if "".join(pdftotext(model_query_pdf).split()) != "AB":
            raise AssertionError("model query byte leaked printable text")

        symbol_positive = write(tmp / "symbol-positive.pcl",
                                ESC + b"(2S" + b"@#[]" + FF)
        symbol_negative = write(tmp / "symbol-negative.pcl",
                                ESC + b"(-2S" + b"@#[]" + FF)
        symbol_positive_pdf = tmp / "symbol-positive.pdf"
        symbol_negative_pdf = tmp / "symbol-negative.pdf"
        render(dreamprint, symbol_positive, symbol_positive_pdf)
        render(dreamprint, symbol_negative, symbol_negative_pdf)
        if ppm_sha256(symbol_positive_pdf, tmp / "symbol-positive",
                      dpi=150) != \
           ppm_sha256(symbol_negative_pdf, tmp / "symbol-negative",
                      dpi=150):
            raise AssertionError("negative symbol-set parameter was not absolute")

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
        orientation_positive_pdf = tmp / "orientation-positive.pdf"
        orientation_negative_pdf = tmp / "orientation-negative.pdf"
        render(dreamprint, orientation_positive, orientation_positive_pdf)
        render(dreamprint, orientation_negative, orientation_negative_pdf)
        if pdf_pages(orientation_negative_pdf) != pdf_pages(orientation_positive_pdf):
            raise AssertionError("negative orientation selector was not absolute")

        page_size_positive = write(tmp / "page-size-positive.pcl",
                                   ESC + b"&l3A" + b"!" + FF)
        page_size_negative = write(tmp / "page-size-negative.pcl",
                                   ESC + b"&l-3A" + b"!" + FF)
        page_size_invalid = write(tmp / "page-size-invalid.pcl",
                                  ESC + b"&l3A" + ESC + b"&l999A" +
                                  b"!" + FF)
        page_size_positive_pdf = tmp / "page-size-positive.pdf"
        page_size_negative_pdf = tmp / "page-size-negative.pdf"
        page_size_invalid_pdf = tmp / "page-size-invalid.pdf"
        render(dreamprint, page_size_positive, page_size_positive_pdf)
        render(dreamprint, page_size_negative, page_size_negative_pdf)
        render(dreamprint, page_size_invalid, page_size_invalid_pdf)
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

        upright = write(tmp / "upright.pcl",
                        ESC + b"(s0p10h0s0b3TItalic sample" + FF)
        italic = write(tmp / "italic.pcl",
                       ESC + b"(s0p10h1s0b3TItalic sample" + FF)
        upright_pdf = tmp / "upright.pdf"
        italic_pdf = tmp / "italic.pdf"
        render(dreamprint, upright, upright_pdf)
        render(dreamprint, italic, italic_pdf)
        if ppm_sha256(upright_pdf, tmp / "upright", dpi=150) == \
           ppm_sha256(italic_pdf, tmp / "italic", dpi=150):
            raise AssertionError("italic font request did not affect pixels")

        medium = write(tmp / "medium.pcl",
                       ESC + b"(s0p10h12v0s0b3TStroke sample" + FF)
        bold = write(tmp / "bold.pcl",
                     ESC + b"(s0p10h12v0s3b3TStroke sample" + FF)
        medium_pdf = tmp / "medium.pdf"
        bold_pdf = tmp / "bold.pdf"
        render(dreamprint, medium, medium_pdf)
        render(dreamprint, bold, bold_pdf)
        if ppm_sha256(medium_pdf, tmp / "medium", dpi=150) == \
           ppm_sha256(bold_pdf, tmp / "bold", dpi=150):
            raise AssertionError("bold stroke request selected medium glyph pixels")
        if ppm_nonwhite(bold_pdf, tmp / "bold", dpi=150) <= \
           ppm_nonwhite(medium_pdf, tmp / "medium", dpi=150):
            raise AssertionError("bold stroke request did not increase ink")

        invalid_default = write(tmp / "invalid-default.pcl",
                                ESC + b"(s0p10h12v0s3b3T" +
                                ESC + b"(99@" + b"Stroke sample" + FF)
        explicit_bold = write(tmp / "explicit-bold.pcl",
                              ESC + b"(s0p10h12v0s3b3T" +
                              b"Stroke sample" + FF)
        default_font = write(tmp / "default-font.pcl",
                             ESC + b"(s0p10h12v0s3b3T" +
                             ESC + b"(3@" + b"Stroke sample" + FF)
        explicit_medium = write(tmp / "explicit-medium.pcl",
                                ESC + b"(s0p10h12v0s0b3T" +
                                b"Stroke sample" + FF)
        invalid_default_pdf = tmp / "invalid-default.pdf"
        explicit_bold_pdf = tmp / "explicit-bold.pdf"
        default_font_pdf = tmp / "default-font.pdf"
        explicit_medium_pdf = tmp / "explicit-medium.pdf"
        render(dreamprint, invalid_default, invalid_default_pdf)
        render(dreamprint, explicit_bold, explicit_bold_pdf)
        render(dreamprint, default_font, default_font_pdf)
        render(dreamprint, explicit_medium, explicit_medium_pdf)
        if ppm_sha256(invalid_default_pdf, tmp / "invalid-default",
                      dpi=150) != \
           ppm_sha256(explicit_bold_pdf, tmp / "explicit-bold", dpi=150):
            raise AssertionError("invalid final-@ reset the selected font")
        if ppm_sha256(default_font_pdf, tmp / "default-font", dpi=150) != \
           ppm_sha256(explicit_medium_pdf, tmp / "explicit-medium", dpi=150):
            raise AssertionError("final-3@ did not reset to default font")

        font_id_bold = write(tmp / "font-id-bold.pcl",
                             ESC + b"(7X" + b"Stroke sample" + FF)
        font_id_bold_pdf = tmp / "font-id-bold.pdf"
        render(dreamprint, font_id_bold, font_id_bold_pdf)
        if ppm_sha256(font_id_bold_pdf, tmp / "font-id-bold", dpi=150) != \
           ppm_sha256(explicit_bold_pdf, tmp / "explicit-bold", dpi=150):
            raise AssertionError("built-in primary font ID 7 did not select bold Courier")

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

        typeface_low_priority = write(
            tmp / "typeface-low-priority.pcl",
            ESC + b"(s0p10h12v0s0b0T" + b"Stroke sample" + FF,
        )
        typeface_low_priority_pdf = tmp / "typeface-low-priority.pdf"
        render(dreamprint, typeface_low_priority, typeface_low_priority_pdf)
        if ppm_sha256(typeface_low_priority_pdf,
                      tmp / "typeface-low-priority", dpi=150) != \
           ppm_sha256(explicit_medium_pdf, tmp / "explicit-medium", dpi=150):
            raise AssertionError("typeface request overrode higher-priority resident font filters")

        pitch_positive = write(tmp / "pitch-positive.pcl",
                               ESC + b"(s10H" + b"Pitch sample" + FF)
        pitch_negative = write(tmp / "pitch-negative.pcl",
                               ESC + b"(s-10H" + b"Pitch sample" + FF)
        style_positive = write(tmp / "style-positive.pcl",
                               ESC + b"(s1S" + b"Italic sample" + FF)
        style_negative = write(tmp / "style-negative.pcl",
                               ESC + b"(s-1S" + b"Italic sample" + FF)
        pitch_positive_pdf = tmp / "pitch-positive.pdf"
        pitch_negative_pdf = tmp / "pitch-negative.pdf"
        style_positive_pdf = tmp / "style-positive.pdf"
        style_negative_pdf = tmp / "style-negative.pdf"
        render(dreamprint, pitch_positive, pitch_positive_pdf)
        render(dreamprint, pitch_negative, pitch_negative_pdf)
        render(dreamprint, style_positive, style_positive_pdf)
        render(dreamprint, style_negative, style_negative_pdf)
        if ppm_sha256(pitch_positive_pdf, tmp / "pitch-positive", dpi=150) != \
           ppm_sha256(pitch_negative_pdf, tmp / "pitch-negative", dpi=150):
            raise AssertionError("negative pitch request did not match positive pitch")
        if ppm_sha256(style_positive_pdf, tmp / "style-positive", dpi=150) != \
           ppm_sha256(style_negative_pdf, tmp / "style-negative", dpi=150):
            raise AssertionError("negative style request did not match positive style")

        underline_fixed = write(tmp / "underline-fixed.pcl",
                                ESC + b"&d0D" + b"A\tB" +
                                ESC + b"&d@" + FF)
        underline_span = write(tmp / "underline-span.pcl",
                               ESC + b"&d3D" + b"A\tB" +
                               ESC + b"&d@" + FF)
        underline_fixed_pdf = tmp / "underline-fixed.pdf"
        underline_span_pdf = tmp / "underline-span.pdf"
        render(dreamprint, underline_fixed, underline_fixed_pdf)
        render(dreamprint, underline_span, underline_span_pdf)
        if "AB" not in "".join(pdftotext(underline_fixed_pdf).split()):
            raise AssertionError("fixed underline span text did not extract")
        if "AB" not in "".join(pdftotext(underline_span_pdf).split()):
            raise AssertionError("floating underline span text did not extract")
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
        underline_negative_pdf = tmp / "underline-negative.pdf"
        render(dreamprint, underline_negative, underline_negative_pdf)
        if ppm_sha256(underline_span_pdf, tmp / "underline-span",
                      dpi=150) != \
           ppm_sha256(underline_negative_pdf, tmp / "underline-negative",
                      dpi=150):
            raise AssertionError("negative underline selector did not match positive")

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
        soft_pdf = tmp / "soft.pdf"
        soft_negative_pdf = tmp / "soft-negative.pdf"
        render(dreamprint, soft, soft_pdf)
        render(dreamprint, soft_negative, soft_negative_pdf)
        if ")" not in pdftotext(soft_pdf):
            raise AssertionError("downloaded glyph text did not extract")
        if ppm_nonwhite(soft_pdf, tmp / "soft") < 5:
            raise AssertionError("downloaded glyph render looks blank")
        if ppm_sha256(soft_pdf, tmp / "soft", dpi=150) != \
           ppm_sha256(soft_negative_pdf, tmp / "soft-negative", dpi=150):
            raise AssertionError("negative downloaded font id/code did not normalize")

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
        soft_keep_char = write(tmp / "soft-keep-char.pcl",
                               soft_two_glyphs + b"(" + FF)
        soft_housekeeping = write(tmp / "soft-housekeeping.pcl",
                                  soft_two_glyphs + ESC + b"*c6F" + b"(" + FF)
        soft_delete_char_pdf = tmp / "soft-delete-char.pdf"
        soft_keep_char_pdf = tmp / "soft-keep-char.pdf"
        soft_housekeeping_pdf = tmp / "soft-housekeeping.pdf"
        render(dreamprint, soft_delete_char, soft_delete_char_pdf)
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
        negative_download_pdf = tmp / "negative-download.pdf"
        render(dreamprint, negative_download, negative_download_pdf)
        if pdftotext(negative_download_pdf).strip() != "!":
            raise AssertionError("negative downloaded-font count leaked payload")

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
        raster_negative_pdf = tmp / "raster-negative.pdf"
        render(dreamprint, raster_negative, raster_negative_pdf)
        if pdftotext(raster_negative_pdf).strip() != "!":
            raise AssertionError("negative raster count leaked payload")
        if ppm_nonwhite(raster_negative_pdf, tmp / "raster-negative") < 5:
            raise AssertionError("negative raster payload did not render")

        raster_lower_negative = write(tmp / "raster-lower-negative.pcl",
                                      ESC + b"*t300R" +
                                      ESC + b"*r0A" +
                                      ESC + b"*b-2w2W" +
                                      b"QZ" + b"!" + FF)
        raster_lower_negative_pdf = tmp / "raster-lower-negative.pdf"
        render(dreamprint, raster_lower_negative, raster_lower_negative_pdf)
        if pdftotext(raster_lower_negative_pdf).strip() != "!":
            raise AssertionError("lowercase negative raster count leaked payload")

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
        raster_full_pdf = tmp / "raster-full.pdf"
        raster_cap_pdf = tmp / "raster-cap.pdf"
        render(dreamprint, raster_full, raster_full_pdf)
        render(dreamprint, raster_cap, raster_cap_pdf)
        full_pixels = ppm_nonwhite(raster_full_pdf, tmp / "raster-full",
                                   dpi=150)
        cap_pixels = ppm_nonwhite(raster_cap_pdf, tmp / "raster-cap",
                                  dpi=150)
        if not (0 < cap_pixels < full_pixels):
            raise AssertionError("raster transfer cap did not reduce row")

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
        raster_150_pdf = tmp / "raster-150.pdf"
        raster_neg_150_pdf = tmp / "raster-neg-150.pdf"
        render(dreamprint, raster_150, raster_150_pdf)
        render(dreamprint, raster_neg_150, raster_neg_150_pdf)
        if ppm_sha256(raster_150_pdf, tmp / "raster-150", dpi=300) != \
           ppm_sha256(raster_neg_150_pdf, tmp / "raster-neg-150", dpi=300):
            raise AssertionError("negative raster resolution did not match positive")

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
        rule_gray = write(tmp / "rule-gray.pcl",
                          ESC + b"*c64a64b50g2P" + FF)
        rule_pattern = write(tmp / "rule-pattern.pcl",
                             ESC + b"*c64a64b2g3P" + FF)
        rule_neg_gray = write(tmp / "rule-neg-gray.pcl",
                              ESC + b"*c64a64b-50g-2P" + FF)
        rule_neg_pattern = write(tmp / "rule-neg-pattern.pcl",
                                 ESC + b"*c64a64b-2g-3P" + FF)
        rule_solid_pdf = tmp / "rule-solid.pdf"
        rule_gray_pdf = tmp / "rule-gray.pdf"
        rule_pattern_pdf = tmp / "rule-pattern.pdf"
        rule_neg_gray_pdf = tmp / "rule-neg-gray.pdf"
        rule_neg_pattern_pdf = tmp / "rule-neg-pattern.pdf"
        render(dreamprint, rule_solid, rule_solid_pdf)
        render(dreamprint, rule_gray, rule_gray_pdf)
        render(dreamprint, rule_pattern, rule_pattern_pdf)
        render(dreamprint, rule_neg_gray, rule_neg_gray_pdf)
        render(dreamprint, rule_neg_pattern, rule_neg_pattern_pdf)
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
        if ppm_sha256(rule_neg_gray_pdf, tmp / "rule-neg-gray", dpi=300) != \
           ppm_sha256(rule_gray_pdf, tmp / "rule-gray", dpi=300):
            raise AssertionError("negative rule percent fill did not normalize")
        if ppm_sha256(rule_neg_pattern_pdf, tmp / "rule-neg-pattern", dpi=300) != \
           ppm_sha256(rule_pattern_pdf, tmp / "rule-pattern", dpi=300):
            raise AssertionError("negative rule hatch fill did not normalize")

        overflow = bytearray(ESC + b"&l2X")
        for i in range(80):
            overflow += f"L{i:02d}\n".encode("ascii")
        overflow_pcl = write(tmp / "overflow.pcl", bytes(overflow))
        overflow_pdf = tmp / "overflow.pdf"
        render(dreamprint, overflow_pcl, overflow_pdf)
        if pdf_pages(overflow_pdf) != 3:
            raise AssertionError("copy-count overflow did not publish 3 pages")

        perf_lines = bytearray()
        for i in range(59):
            perf_lines += f"P{i:02d}\n".encode("ascii")
        perf_enabled = write(tmp / "perf-enabled.pcl",
                             ESC + b"&l1L" + bytes(perf_lines) + FF)
        perf_preserved = write(tmp / "perf-preserved.pcl",
                               ESC + b"&l1L" + ESC + b"&l-2L" +
                               bytes(perf_lines) + FF)
        perf_disabled = write(tmp / "perf-disabled.pcl",
                              ESC + b"&l1L" + ESC + b"&l0L" +
                              bytes(perf_lines) + FF)
        perf_enabled_pdf = tmp / "perf-enabled.pdf"
        perf_preserved_pdf = tmp / "perf-preserved.pdf"
        perf_disabled_pdf = tmp / "perf-disabled.pdf"
        render(dreamprint, perf_enabled, perf_enabled_pdf)
        render(dreamprint, perf_preserved, perf_preserved_pdf)
        render(dreamprint, perf_disabled, perf_disabled_pdf)
        if pdf_pages(perf_preserved_pdf) != pdf_pages(perf_enabled_pdf):
            raise AssertionError("invalid perforation selector did not preserve state")
        if pdf_pages(perf_disabled_pdf) == pdf_pages(perf_enabled_pdf):
            raise AssertionError("perforation regression is not sensitive to disabled state")

        paper_zero = write(tmp / "paper-zero.pcl",
                           b"A" + ESC + b"&l0H" + b"B" + FF)
        paper_zero_pdf = tmp / "paper-zero.pdf"
        render(dreamprint, paper_zero, paper_zero_pdf)
        if pdf_pages(paper_zero_pdf) != 2:
            raise AssertionError("paper-source selector zero did not publish")

        page_length_zero = write(tmp / "page-length-zero.pcl",
                                 b"A" + ESC + b"&l0P" + b"B" + FF)
        page_length_zero_pdf = tmp / "page-length-zero.pdf"
        render(dreamprint, page_length_zero, page_length_zero_pdf)
        if pdf_pages(page_length_zero_pdf) != 2:
            raise AssertionError("page-length selector zero did not publish")

        page_length_nonzero = write(tmp / "page-length-nonzero.pcl",
                                    b"A" + ESC + b"&l66P" + b"B" + FF)
        page_length_nonzero_pdf = tmp / "page-length-nonzero.pdf"
        render(dreamprint, page_length_nonzero, page_length_nonzero_pdf)
        if pdf_pages(page_length_nonzero_pdf) != 2:
            raise AssertionError("nonzero page length did not publish")
        if "AB" not in "".join(pdftotext(page_length_nonzero_pdf).split()):
            raise AssertionError("nonzero page length lost text")

        page_length_negative = write(tmp / "page-length-negative.pcl",
                                     b"A" + ESC + b"&l-66P" + b"B" + FF)
        page_length_negative_pdf = tmp / "page-length-negative.pdf"
        render(dreamprint, page_length_negative, page_length_negative_pdf)
        if pdf_pages(page_length_negative_pdf) != pdf_pages(page_length_nonzero_pdf):
            raise AssertionError("negative page length did not match positive selector")
        if "AB" not in "".join(pdftotext(page_length_negative_pdf).split()):
            raise AssertionError("negative page length lost text")

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

        lpi_positive = write(tmp / "lpi-positive.pcl",
                             ESC + b"&l8D" + b"A\nB" + FF)
        lpi_negative = write(tmp / "lpi-negative.pcl",
                             ESC + b"&l-8D" + b"A\nB" + FF)
        lpi_positive_pdf = tmp / "lpi-positive.pdf"
        lpi_negative_pdf = tmp / "lpi-negative.pdf"
        render(dreamprint, lpi_positive, lpi_positive_pdf)
        render(dreamprint, lpi_negative, lpi_negative_pdf)
        if ppm_sha256(lpi_positive_pdf, tmp / "lpi-positive", dpi=150) != \
           ppm_sha256(lpi_negative_pdf, tmp / "lpi-negative", dpi=150):
            raise AssertionError("negative LPI selector did not match positive selector")

        copies_negative = write(tmp / "copies-negative.pcl",
                                ESC + b"&l-2X" + b"!" + FF)
        copies_negative_pdf = tmp / "copies-negative.pdf"
        render(dreamprint, copies_negative, copies_negative_pdf)
        if pdf_pages(copies_negative_pdf) != 2:
            raise AssertionError("negative copy count was not absolute")

        vfc_negative = write(tmp / "vfc-negative.pcl",
                             ESC + b"&l-4W" + b"\x00\x00\x00\x02" +
                             b"!" + FF)
        vfc_lower_negative = write(tmp / "vfc-lower-negative.pcl",
                                   ESC + b"&l-4w4W" + b"\x00\x00\x00\x02" +
                                   b"!" + FF)
        vfc_negative_pdf = tmp / "vfc-negative.pdf"
        vfc_lower_negative_pdf = tmp / "vfc-lower-negative.pdf"
        render(dreamprint, vfc_negative, vfc_negative_pdf)
        render(dreamprint, vfc_lower_negative, vfc_lower_negative_pdf)
        if pdftotext(vfc_negative_pdf).strip() != "!":
            raise AssertionError("negative VFC count leaked payload")
        if pdftotext(vfc_lower_negative_pdf).strip() != "!":
            raise AssertionError("lowercase negative VFC count leaked payload")

        vfc_jump_positive = write(tmp / "vfc-jump-positive.pcl",
                                  ESC + b"&l4W" + b"\x00\x00\x00\x02" +
                                  ESC + b"&l2V" + b"!" + FF)
        vfc_jump_negative = write(tmp / "vfc-jump-negative.pcl",
                                  ESC + b"&l4W" + b"\x00\x00\x00\x02" +
                                  ESC + b"&l-2V" + b"!" + FF)
        vfc_jump_positive_pdf = tmp / "vfc-jump-positive.pdf"
        vfc_jump_negative_pdf = tmp / "vfc-jump-negative.pdf"
        render(dreamprint, vfc_jump_positive, vfc_jump_positive_pdf)
        render(dreamprint, vfc_jump_negative, vfc_jump_negative_pdf)
        if ppm_sha256(vfc_jump_positive_pdf, tmp / "vfc-jump-positive",
                      dpi=150) != \
           ppm_sha256(vfc_jump_negative_pdf, tmp / "vfc-jump-negative",
                      dpi=150):
            raise AssertionError("negative VFC channel selector did not match positive")

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
        macro_execute_pdf = tmp / "macro-execute.pdf"
        macro_call_pdf = tmp / "macro-call.pdf"
        render(dreamprint, macro_execute, macro_execute_pdf)
        render(dreamprint, macro_call, macro_call_pdf)
        if "!" not in pdftotext(macro_execute_pdf):
            raise AssertionError("macro execute did not replay payload")
        if "!" not in pdftotext(macro_call_pdf):
            raise AssertionError("macro call did not replay payload")

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
            macro_call_restore_pdf, tmp / "macro-call-restore", 78, 70, 112,
            130, dpi=300)
        execute_cursor_pixels = ppm_rect_nonwhite(
            macro_execute_no_restore_pdf, tmp / "macro-execute-no-restore",
            78, 70, 112, 130, dpi=300)
        execute_macro_pixels = ppm_rect_nonwhite(
            macro_execute_no_restore_pdf, tmp / "macro-execute-no-restore-far",
            375, 70, 420, 130, dpi=300)
        if call_cursor_pixels < 200:
            raise AssertionError("macro call did not restore caller cursor")
        if execute_cursor_pixels > 50 or execute_macro_pixels < 200:
            raise AssertionError("macro execute unexpectedly restored cursor")

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
        overlay_pdf = tmp / "overlay.pdf"
        render(dreamprint, overlay, overlay_pdf)
        if "Live!" not in pdftotext(overlay_pdf):
            raise AssertionError("macro overlay did not replay at publication")

    print("ok: LaserJet II PCL regression checks passed")


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as exc:
        sys.stderr.write(exc.stderr.decode("utf-8", errors="replace"))
        raise
