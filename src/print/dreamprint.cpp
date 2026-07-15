// license:BSD-3-Clause
// copyright-holders:Stephen Hurd
#include "printer.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static void usage(const char *argv0)
{
	fprintf(stderr,
		"Usage: %s [options] input.bin output.pdf\n"
		"\n"
		"Options:\n"
		"  --model MODEL    Printer model (default: FX)\n"
		"  --config PATH    Printer config file\n"
		"  --font MODE      Initial font (JET: courier, courier-bold, line-printer)\n"
		"  --pitch PITCH    Initial pitch (e.g. pica, elite, condensed)\n"
		"  --orientation O  JET default orientation: portrait or landscape\n"
		"  --symbol-set SET JET default symbol set (e.g. 8U, 10U, 0N)\n"
		"  --copies N       JET default copies (1-99)\n"
		"  --form-lines N   JET default form length (5-128 lines)\n"
		"\n"
		"Models:\n"
		"  X24E      IBM Proprinter X24E (24-pin)\n"
		"  XIII      IBM Proprinter III (9-pin)\n"
		"  LQ500     Epson LQ-500 (24-pin)\n"
		"  FX        Epson FX (9-pin)\n"
		"  BJ10e     Canon BJ-10e (inkjet)\n"
		"  JET       HP LaserJet II\n"
		"  WRITER    Apple ImageWriter\n",
		argv0);
}

static PrinterModel parse_model(const char *name)
{
	if (!strcasecmp(name, "X24E"))   return PrinterModel::IbmX24E;
	if (!strcasecmp(name, "XIII"))   return PrinterModel::IbmXIII;
	if (!strcasecmp(name, "LQ500"))  return PrinterModel::EpsonLQ500;
	if (!strcasecmp(name, "LQ"))     return PrinterModel::EpsonLQ500;
	if (!strcasecmp(name, "FX"))     return PrinterModel::EpsonFX;
	if (!strcasecmp(name, "BJ10e"))  return PrinterModel::CanonBJ10e;
	if (!strcasecmp(name, "JET"))    return PrinterModel::HpJet;
	if (!strcasecmp(name, "WRITER")) return PrinterModel::ImageWriter;
	fprintf(stderr, "Unknown model: %s\n", name);
	return PrinterModel::EpsonFX;
}

int main(int argc, char *argv[])
{
	PrinterModel model = PrinterModel::EpsonFX;
	const char *input = nullptr;
	const char *output = nullptr;
	const char *config_path = nullptr;
	const char *font_arg = nullptr;
	const char *pitch_arg = nullptr;
	const char *orientation_arg = nullptr;
	const char *symbol_arg = nullptr;
	int copies_arg = -1;
	int form_lines_arg = -1;

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--model") && i + 1 < argc) {
			model = parse_model(argv[++i]);
		} else if (!strcmp(argv[i], "--config") && i + 1 < argc) {
			config_path = argv[++i];
		} else if (!strcmp(argv[i], "--font") && i + 1 < argc) {
			font_arg = argv[++i];
		} else if (!strcmp(argv[i], "--pitch") && i + 1 < argc) {
			pitch_arg = argv[++i];
		} else if (!strcmp(argv[i], "--orientation") && i + 1 < argc) {
			orientation_arg = argv[++i];
		} else if (!strcmp(argv[i], "--symbol-set") && i + 1 < argc) {
			symbol_arg = argv[++i];
		} else if (!strcmp(argv[i], "--copies") && i + 1 < argc) {
			copies_arg = atoi(argv[++i]);
		} else if (!strcmp(argv[i], "--form-lines") && i + 1 < argc) {
			form_lines_arg = atoi(argv[++i]);
		} else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
			usage(argv[0]);
			return 0;
		} else if (!input) {
			input = argv[i];
		} else if (!output) {
			output = argv[i];
		}
	}

	if (!input || !output) {
		usage(argv[0]);
		return 1;
	}

	FILE *fp = fopen(input, "rb");
	if (!fp) {
		fprintf(stderr, "Cannot open %s\n", input);
		return 1;
	}

	const PrinterProfile &prof = profile_for(model);
	fprintf(stderr, "Model: %s (%s)\n", prof.name,
		prof.tech == DotTech::Impact9 ? "9-pin impact" :
		prof.tech == DotTech::Impact24 ? "24-pin impact" :
		prof.tech == DotTech::Inkjet ? "inkjet" : "laser");

	PdfWriter pdf(output);
	auto printer = create_printer(model, pdf);

	{
		PrinterConfig cfg = config_path
		    ? load_printer_config(config_path, model)
		    : default_config_for(model);

		if (font_arg) {
			if (model == PrinterModel::HpJet) {
				int font = parse_pcl_font(font_arg);
				if (font < 0) {
					fprintf(stderr, "Invalid JET font: %s\n", font_arg);
					return 1;
				}
				cfg.pcl_font = font;
			} else if (!strcasecmp(font_arg, "draft"))  cfg.font_mode = 2;
			else if (!strcasecmp(font_arg, "standard")) cfg.font_mode = 0;
			else if (!strcasecmp(font_arg, "nlq"))      cfg.font_mode = 1;
		}
		if (pitch_arg)
			cfg.pitch_cpi = parse_pitch(pitch_arg, model);
		if (orientation_arg) {
			if (!strcasecmp(orientation_arg, "portrait"))
				cfg.pcl_orientation = 0;
			else if (!strcasecmp(orientation_arg, "landscape"))
				cfg.pcl_orientation = 1;
			else {
				fprintf(stderr, "Invalid orientation: %s\n", orientation_arg);
				return 1;
			}
		}
		if (symbol_arg) {
			int symbol = parse_pcl_symbol_set(symbol_arg);
			if (symbol < 0) {
				fprintf(stderr, "Invalid symbol set: %s\n", symbol_arg);
				return 1;
			}
			cfg.pcl_symbol_set = symbol;
		}
		if (copies_arg >= 0)
			cfg.copies = std::max(1, std::min(99, copies_arg));
		if (form_lines_arg >= 0)
			cfg.page_length_lines = std::max(5, std::min(128, form_lines_arg));

		printer->apply_config(cfg);
	}

	uint8_t buf[4096];
	size_t n;
	size_t total = 0;
	while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
		printer->feed(buf, n);
		total += n;
	}
	fclose(fp);

	printer->flush();
	pdf.finish();

	fprintf(stderr, "Processed %zu bytes -> %s\n", total, output);
	return 0;
}
