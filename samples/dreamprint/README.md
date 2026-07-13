# dreamprint Samples

These files are printer input streams for the `dreamprint` tool. They exercise
the rendered text attribute combinations and font/pitch modes currently
implemented by `dreamprint` for the supported printer models:

- `fx80-text-attributes.bin` for `--model FX`
- `bj10e-text-attributes.bin` for `--model BJ10e`
- `imagewriter-ii-text-attributes.bin` for `--model WRITER`
- `lq500-text-attributes.bin` for `--model LQ500`
- `laserjet-ii-text-attributes.bin` for `--model JET`
- `laserjet-ii-pcl4-smoke.bin` for `--model JET`

The ImageWriter II sample includes a separate six-line ribbon colour section for
the non-black colours. Colour is intentionally not included as an axis in the
attribute matrix.

Each entry is formatted as a readable label line, the rendered sample text on
the next line, and one empty separator line.

Implemented font and pitch modes covered by the samples:

- FX-80: draft; 10 cpi, 12 cpi, 15 cpi, condensed, proportional
- Canon BJ-10e: high-quality and economy; 10 cpi, 12 cpi, condensed, and
  high-quality proportional; normal, superscript, and subscript
- ImageWriter II: standard, draft, NLQ; 9 cpi, 10 cpi, 12 cpi, 13.4 cpi,
  15 cpi, 17 cpi, 144 dpi proportional, 160 dpi proportional
- Epson LQ-500: draft Roman, LQ Roman, LQ Sans Serif; 10 cpi, 12 cpi,
  15 cpi, condensed, proportional
- HP LaserJet II attributes: 10 cpi, 12 cpi request/fallback behavior,
  line-printer pitch, bold, style-selection requests, and underline
- HP LaserJet II smoke sample: PCL4 text, underline, cursor positioning,
  rectangle/rule graphics, raster rows, transparent print data, page size, and
  orientation

ImageWriter II labels are printed in NLQ for readability. Its draft and NLQ
sections include only the text attributes those fonts support in the emulator.

The ImageWriter II matrix enables `ESC D SPACE NUL` so long double-width rows
use the printer's LF-on-full-line behavior instead of the hard-reset default,
which returns the carriage to the left edge of the same physical row.

The matrices cover text effects and pitch modes that are rendered by the
emulator and controllable from the printer input stream. They are not intended
to document every command the physical printers support. They do not include
colour or printer configuration settings as matrix axes.

Generate PDFs from the repository root:

```sh
./build/dreamprint --model FX samples/dreamprint/fx80-text-attributes.bin fx80-text-attributes.pdf
./build/dreamprint --model BJ10e samples/dreamprint/bj10e-text-attributes.bin bj10e-text-attributes.pdf
./build/dreamprint --model WRITER samples/dreamprint/imagewriter-ii-text-attributes.bin imagewriter-ii-text-attributes.pdf
./build/dreamprint --model LQ500 samples/dreamprint/lq500-text-attributes.bin lq500-text-attributes.pdf
./build/dreamprint --model JET samples/dreamprint/laserjet-ii-text-attributes.bin laserjet-ii-text-attributes.pdf
./build/dreamprint --model JET samples/dreamprint/laserjet-ii-pcl4-smoke.bin laserjet-ii-pcl4-smoke.pdf
```

Regenerate the input streams after editing the matrix definitions:

```sh
python3 samples/dreamprint/generate.py
```
