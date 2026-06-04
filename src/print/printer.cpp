// license:BSD-3-Clause
// copyright-holders:Stephen Hurd
#include "printer.h"
#include "dotrender.h"
#include <cctype>
#include <cstdio>
#include <cstring>

static constexpr PrinterProfile profiles[] = {
	//                                                                                                   radius jitter intens sharp
	{ PrinterModel::IbmX24E,    "IBM X24E",       DotTech::Impact24, 720, 120,180, 360,180, 12,24, 36,  0.14f, 0.03f, 0.90f, 2.0f },
	{ PrinterModel::IbmXIII,    "IBM XIII",        DotTech::Impact9,  720, 120, 72, 240,144, 12, 9, 24, 0.30f, 0.025f, 0.85f, 2.0f },
	{ PrinterModel::EpsonLQ,    "Epson LQ",       DotTech::Impact24, 720, 120,180, 360,180, 12,24, 36,  0.14f, 0.015f, 0.90f, 2.0f },
	{ PrinterModel::EpsonFX,    "Epson FX",       DotTech::Impact9,  720, 120, 72, 240,144, 12, 9, 24,  0.30f, 0.025f, 0.85f, 2.0f },
	{ PrinterModel::CanonBJ10e, "Canon BJ-10e",   DotTech::Inkjet,   720, 360,360, 360,360, 36,24, 36,  0.12f, 0.04f, 0.95f, 1.2f },
	{ PrinterModel::HpJet,      "HP JET",         DotTech::Toner,    600, 300,300, 300,300, 30,50, 30,   0.13f, 0.01f, 0.98f, 0.3f },
	{ PrinterModel::ImageWriter, "ImageWriter",   DotTech::Impact9,  720,  72, 72, 144,144,  8, 8, 16,  0.30f, 0.025f, 0.85f, 2.0f },
};

const PrinterProfile &profile_for(PrinterModel m)
{
	return profiles[static_cast<int>(m)];
}

PrinterSim::PrinterSim(PrinterModel model, PdfWriter &pdf)
	: prof_(profile_for(model)), pdf_(pdf)
{
	dots_ = create_dot_renderer(prof_);
}

PrinterSim::~PrinterSim()
{
	flush();
}

void PrinterSim::feed(const uint8_t *data, size_t len)
{
	for (size_t i = 0; i < len; i++)
		parse_byte(data[i]);
}

void PrinterSim::flush()
{
	if (page_dirty_ && page_) {
		pdf_.add_page(*page_, prof_.render_dpi);
		page_dirty_ = false;
	}
}

void PrinterSim::new_page_if_needed()
{
	if (!page_) {
		page_ = std::make_unique<PageBitmap>(
			PageBitmap::letter_at_dpi(prof_.render_dpi));
		page_dirty_ = false;
		st_.x_pos = st_.left_margin_in;
		st_.y_pos = st_.top_margin_in + st_.line_spacing_in;
	}
}

void PrinterSim::emit_char(uint8_t ch)
{
	new_page_if_needed();
	page_dirty_ = true;

	float char_w_in = 1.0f / (float)st_.pitch_cpi;
	if (st_.condensed && st_.pitch_cpi <= 10)
		char_w_in = 1.0f / 17.16f;
	if (st_.expanded || st_.expanded_line)
		char_w_in *= 2.0f;

	dots_->render_char(*page_, st_, prof_, ch);

	st_.x_pos += char_w_in;

	if (st_.x_pos >= st_.right_margin_in) {
		carriage_return();
		line_feed();
	}
}

void PrinterSim::apply_config(const PrinterConfig &cfg)
{
	cfg_ = cfg;
	st_.pitch_cpi = cfg.pitch_cpi;
	st_.bold = cfg.emphasized;
	st_.auto_lf = cfg.auto_lf;
	st_.perf_skip_lines = cfg.perf_skip;
	st_.charset = cfg.charset;
	st_.slashed_zero = cfg.slashed_zero;
}

void PrinterSim::carriage_return()
{
	if (!st_.unidirectional && st_.x_pos > st_.left_margin_in)
		st_.line_dir_ltr = !st_.line_dir_ltr;
	st_.x_pos = st_.left_margin_in;
	st_.expanded_line = false;
	if (st_.auto_lf)
		line_feed();
}

void PrinterSim::line_feed()
{
	new_page_if_needed();
	page_dirty_ = true;
	st_.y_pos += st_.line_spacing_in;

	float bottom = st_.perf_skip_lines > 0
		? st_.page_height_in - (float)st_.perf_skip_lines * st_.line_spacing_in
		: st_.page_height_in - 0.5f;
	if (st_.y_pos >= bottom)
		form_feed();
}

void PrinterSim::form_feed()
{
	if (page_ && page_dirty_) {
		pdf_.add_page(*page_, prof_.render_dpi);
		page_dirty_ = false;
	}
	page_.reset();
}

static char *trim(char *s)
{
	while (isspace((unsigned char)*s)) s++;
	char *e = s + strlen(s);
	while (e > s && isspace((unsigned char)e[-1])) e--;
	*e = '\0';
	return s;
}

static int match_charset(const char *s)
{
	static const char *names[] = {
		"usa", "france", "germany", "uk", "denmark",
		"sweden", "italy", "spain", "japan"
	};
	for (int i = 0; i < 9; i++)
		if (strcasecmp(s, names[i]) == 0) return i;
	return 0;
}

PrinterConfig load_printer_config(const char *path)
{
	PrinterConfig cfg;
	FILE *f = fopen(path, "r");
	if (!f) { perror(path); return cfg; }

	char line[256];
	while (fgets(line, sizeof(line), f)) {
		char *p = strchr(line, '#');
		if (p) *p = '\0';
		p = strchr(line, '=');
		if (!p) continue;
		*p = '\0';
		char *key = trim(line);
		char *val = trim(p + 1);
		if (!*key || !*val) continue;

		if (strcasecmp(key, "pitch") == 0) {
			if (strcasecmp(val, "compressed") == 0) cfg.pitch_cpi = 17;
			else cfg.pitch_cpi = 10;
		} else if (strcasecmp(key, "zero_style") == 0) {
			cfg.slashed_zero = (strcasecmp(val, "slashed") == 0);
		} else if (strcasecmp(key, "print_weight") == 0) {
			cfg.emphasized = (strcasecmp(val, "emphasized") == 0);
		} else if (strcasecmp(key, "charset") == 0) {
			cfg.charset = match_charset(val);
		} else if (strcasecmp(key, "auto_lf") == 0) {
			cfg.auto_lf = (strcasecmp(val, "on") == 0);
		} else if (strcasecmp(key, "perf_skip") == 0) {
			if (strcasecmp(val, "on") == 0) cfg.perf_skip = 6;
			else if (strcasecmp(val, "off") == 0) cfg.perf_skip = 0;
			else cfg.perf_skip = atoi(val);
		} else {
			fprintf(stderr, "printer config: unknown key '%s'\n", key);
		}
	}
	fclose(f);
	return cfg;
}
