// license:BSD-3-Clause
// copyright-holders:Stephen Hurd
#include "printer.h"
#include <cstdio>
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
		"\n"
		"Models:\n"
		"  X24E      IBM Proprinter X24E (24-pin)\n"
		"  XIII      IBM Proprinter III (9-pin)\n"
		"  LQ        Epson LQ (24-pin)\n"
		"  FX        Epson FX (9-pin)\n"
		"  BJ10e     Canon BJ-10e (inkjet)\n"
		"  JET       HP LaserJet/DeskJet\n"
		"  WRITER    Apple ImageWriter\n",
		argv0);
}

static PrinterModel parse_model(const char *name)
{
	if (!strcasecmp(name, "X24E"))   return PrinterModel::IbmX24E;
	if (!strcasecmp(name, "XIII"))   return PrinterModel::IbmXIII;
	if (!strcasecmp(name, "LQ"))     return PrinterModel::EpsonLQ;
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

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--model") && i + 1 < argc) {
			model = parse_model(argv[++i]);
		} else if (!strcmp(argv[i], "--config") && i + 1 < argc) {
			config_path = argv[++i];
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

	if (config_path) {
		PrinterConfig cfg = load_printer_config(config_path);
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
