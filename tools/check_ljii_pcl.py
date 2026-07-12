#!/usr/bin/env python3
import argparse
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


def ppm_nonwhite(pdf, stem, dpi=72):
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
    return sum(1 for i in range(0, len(pixels), 3)
               if pixels[i:i + 3] != b"\xff\xff\xff")


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

        soft = write(tmp / "soft.pcl",
                     ESC + b"*c4660D" +
                     ESC + b"*c41E" +
                     ESC + b")s18W" +
                     bytes.fromhex(
                         "f0 0f aa 55 3c c3 81 7e ff 00 18 e7 "
                         "24 db 42 bd 66 99") +
                     ESC + b"(4660X" + b")" + FF)
        soft_pdf = tmp / "soft.pdf"
        render(dreamprint, soft, soft_pdf)
        if ")" not in pdftotext(soft_pdf):
            raise AssertionError("downloaded glyph text did not extract")
        if ppm_nonwhite(soft_pdf, tmp / "soft") < 5:
            raise AssertionError("downloaded glyph render looks blank")

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

        rule_solid = write(tmp / "rule-solid.pcl",
                           ESC + b"*c64a64b0P" + FF)
        rule_gray = write(tmp / "rule-gray.pcl",
                          ESC + b"*c64a64b50g2P" + FF)
        rule_pattern = write(tmp / "rule-pattern.pcl",
                             ESC + b"*c64a64b2g3P" + FF)
        rule_solid_pdf = tmp / "rule-solid.pdf"
        rule_gray_pdf = tmp / "rule-gray.pdf"
        rule_pattern_pdf = tmp / "rule-pattern.pdf"
        render(dreamprint, rule_solid, rule_solid_pdf)
        render(dreamprint, rule_gray, rule_gray_pdf)
        render(dreamprint, rule_pattern, rule_pattern_pdf)
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

        overflow = bytearray(ESC + b"&l2X")
        for i in range(80):
            overflow += f"L{i:02d}\n".encode("ascii")
        overflow_pcl = write(tmp / "overflow.pcl", bytes(overflow))
        overflow_pdf = tmp / "overflow.pdf"
        render(dreamprint, overflow_pcl, overflow_pdf)
        if pdf_pages(overflow_pdf) != 3:
            raise AssertionError("copy-count overflow did not publish 3 pages")

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
