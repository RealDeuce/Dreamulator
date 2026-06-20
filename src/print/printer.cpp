// license:BSD-3-Clause
// copyright-holders:Stephen Hurd
#include "printer.h"
#include "dotrender.h"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstring>

static constexpr PrinterProfile profiles[] = {
	//                                                                                                   radius jitter intens sharp overprint r-var i-var
	{ PrinterModel::IbmX24E,    "IBM X24E",       DotTech::Impact24, 720, 120,180, 360,180, 12,24, 36,  0.14f, 0.03f, 0.90f, 2.0f, 1.0f, 0.075f, 0.075f },
	{ PrinterModel::IbmXIII,    "IBM XIII",        DotTech::Impact9,  720, 120, 72, 240,144, 12, 9, 24, 0.30f, 0.025f, 0.85f, 2.0f, 1.0f, 0.075f, 0.075f },
	{ PrinterModel::EpsonLQ500,    "Epson LQ-500",       DotTech::Impact24, 720, 120,180, 360,180, 12,24, 36,  0.21f, 0.015f, 0.76f, 2.0f, 2.5f, 0.075f, 0.16f },
	{ PrinterModel::EpsonFX,    "Epson FX",       DotTech::Impact9,  720, 120, 72, 240,144, 12, 9, 24,  0.27f, 0.025f, 0.72f, 2.0f, 1.0f, 0.075f, 0.075f },
	{ PrinterModel::CanonBJ10e, "Canon BJ-10e",   DotTech::Inkjet,   360, 360,360, 360,360, 36,48, 30,  0.050f, 0.0f, 0.68f, 1.0f, 1.0f, 0.075f, 0.075f },
	{ PrinterModel::HpJet,      "HP JET",         DotTech::Toner,    600, 300,300, 300,300, 30,50, 30,   0.13f, 0.01f, 0.98f, 0.3f, 1.0f, 0.075f, 0.075f },
	{ PrinterModel::ImageWriter, "ImageWriter",   DotTech::Impact9,  720,  72, 72, 144,144,  8, 8, 16,  0.27f, 0.025f, 0.72f, 2.0f, 1.0f, 0.075f, 0.075f },
};

const PrinterProfile &profile_for(PrinterModel m)
{
	return profiles[static_cast<int>(m)];
}

static int lq500_script_cell_total(const PrinterState &st, uint8_t ch,
                                   const Lq500GlyphInfo &info)
{
	if (st.lq500_lq_mode && (st.superscript || st.subscript)) {
		const uint8_t *sec = get_lq500_sec_metrics(1, ch);
		if (sec && (sec[1] != 0 || sec[2] != 0))
			return info.start + sec[1] + sec[2];
	}
	return info.start + info.width + info.advance;
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
	flush_pending_line();
	if (page_dirty_ && page_) {
		pdf_.add_page(*page_, prof_.render_dpi, text_buf_);
		text_buf_.clear();
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

static uint16_t intl_unicode_fx(uint8_t ch, int charset);
static uint16_t intl_unicode_iw(uint8_t ch, int charset);
static uint8_t intl_substitute_fx(uint8_t ch, int charset);
static uint8_t intl_substitute_iw(uint8_t ch, int charset);

static uint16_t intl_unicode_fx(uint8_t ch, int charset)
{
	static constexpr uint16_t tbl[8][12] = {
	//  #35    $36    @64    [91    \92    ]93    ^94    `96   {123   |124   }125   ~126
	{     0,     0,0x00E0,0x00B0,0x00E7,0x00A7,     0,     0,0x00E9,0x00F9,0x00E8,0x00A8}, // 1 France
	{     0,     0,0x00A7,0x00C4,0x00D6,0x00DC,     0,     0,0x00E4,0x00F6,0x00FC,0x00DF}, // 2 Germany
	{0x00A3,     0,     0,     0,     0,     0,     0,     0,     0,     0,     0,     0}, // 3 UK
	{     0,     0,     0,0x00C6,0x00D8,0x00C5,     0,     0,0x00E6,0x00F8,0x00E5,     0}, // 4 Denmark
	{     0,0x00A4,0x00C9,0x00C4,0x00D6,0x00C5,0x00DC,0x00E9,0x00E4,0x00F6,0x00E5,0x00FC}, // 5 Sweden
	{     0,     0,     0,0x00B0,     0,0x00E9,     0,0x00F9,0x00E0,0x00F2,0x00E8,0x00EC}, // 6 Italy
	{0x20A7,     0,     0,0x00A1,0x00D1,0x00BF,     0,     0,0x00A8,0x00F1,     0,     0}, // 7 Spain
	{     0,     0,     0,     0,0x00A5,     0,     0,     0,     0,     0,     0,     0}, // 8 Japan
	};
	static constexpr uint8_t positions[12] = {
		35, 36, 64, 91, 92, 93, 94, 96, 123, 124, 125, 126
	};

	if (charset < 1 || charset > 8) return ch;
	for (int i = 0; i < 12; i++) {
		if (ch == positions[i]) {
			uint16_t u = tbl[charset - 1][i];
			if (u != 0) return u;
			break;
		}
	}
	return ch;
}

// IW II charset ordering per Table A-3: 1=Italian,2=Danish,3=British,
// 4=German,5=Swedish,6=French,7=Spanish.  Figure 2-2 page 24.
static uint16_t intl_unicode_iw(uint8_t ch, int charset)
{
	static constexpr uint16_t tbl[7][12] = {
	//  #35    $36    @64    [91    \92    ]93    ^94    `96   {123   |124   }125   ~126
	{0x00A3,     0,0x00A7,0x00B0,0x00E7,0x00E9,     0,0x00F9,0x00E0,0x00F2,0x00E8,0x00EC}, // 1 Italian
	{     0,     0,     0,0x00C6,0x00D8,0x00C5,     0,     0,0x00E6,0x00F8,0x00E5,     0}, // 2 Danish
	{0x00A3,     0,     0,     0,     0,     0,     0,     0,     0,     0,     0,     0}, // 3 British
	{     0,     0,0x00A7,0x00C4,0x00D6,0x00DC,     0,     0,0x00E4,0x00F6,0x00FC,0x00DF}, // 4 German
	{     0,0x00A4,     0,0x00C4,0x00D6,0x00C5,0x00DC,     0,0x00E4,0x00F6,0x00E5,     0}, // 5 Swedish
	{0x00A3,     0,0x00E0,0x00B0,0x00E7,0x00A7,     0,     0,0x00E9,0x00F9,0x00E8,0x00A8}, // 6 French
	{0x00A3,     0,0x00A7,0x00A1,0x00D1,0x00BF,     0,     0,0x00B0,0x00F1,0x00E7,     0}, // 7 Spanish
	};
	static constexpr uint8_t positions[12] = {
		35, 36, 64, 91, 92, 93, 94, 96, 123, 124, 125, 126
	};

	if (charset < 1 || charset > 7) return ch;
	for (int i = 0; i < 12; i++) {
		if (ch == positions[i]) {
			uint16_t u = tbl[charset - 1][i];
			if (u != 0) return u;
			break;
		}
	}
	return ch;
}

static uint8_t intl_substitute_fx(uint8_t ch, int charset)
{
	static constexpr int8_t tbl[8][12] = {
	//  #35  $36  @64  [91  \92  ]93  ^94  `96 {123 |124 }125 ~126
	{   -1,  -1,0x00,0x05,0x0F,0x10,  -1,  -1,0x1E,0x02,0x01,0x16}, // 1 France
	{   -1,  -1,0x10,0x17,0x18,0x19,  -1,  -1,0x1A,0x1B,0x1C,0x11}, // 2 Germany
	{ 0x06,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1}, // 3 UK
	{   -1,  -1,  -1,0x12,0x14,0x0D,  -1,  -1,0x13,0x15,0x0E,  -1}, // 4 Denmark
	{   -1,0x0B,0x1D,0x17,0x18,0x0D,0x19,0x1E,0x1A,0x1B,0x0E,0x1C}, // 5 Sweden
	{   -1,  -1,  -1,0x05,  -1,0x1E,  -1,0x02,0x00,0x03,0x01,0x04}, // 6 Italy
	{ 0x0C,  -1,  -1,0x07,0x09,0x08,  -1,  -1,0x16,0x0A,  -1,  -1}, // 7 Spain
	{   -1,  -1,  -1,  -1,0x1F,  -1,  -1,  -1,  -1,  -1,  -1,  -1}, // 8 Japan
	};
	static constexpr uint8_t positions[12] = {
		35, 36, 64, 91, 92, 93, 94, 96, 123, 124, 125, 126
	};

	for (int i = 0; i < 12; i++) {
		if (ch == positions[i]) {
			int8_t sub = tbl[charset - 1][i];
			if (sub >= 0) return (uint8_t)sub;
			break;
		}
	}
	return ch;
}

// IW II glyph substitution: maps the 12 substitutable ASCII positions to
// IW2 ROM glyph indices (0x00-0x1F) per the IW charset ordering.
static uint8_t intl_substitute_iw(uint8_t ch, int charset)
{
	static constexpr int8_t tbl[7][12] = {
	//  #35  $36  @64  [91  \92  ]93  ^94  `96 {123 |124 }125 ~126
	{0x08,  -1,0x09,0x1A,0x14,0x0D,  -1,0x1D,0x12,0x1E,0x15,0x1F}, // 1 Italian
	{  -1,  -1,  -1,0x04,0x07,0x0C,  -1,  -1,0x05,0x06,0x0F,  -1}, // 2 Danish
	{0x08,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1}, // 3 British
	{  -1,  -1,0x09,0x0A,0x1B,0x1C,  -1,  -1,0x0E,0x0B,0x10,0x11}, // 4 German
	{  -1,  -1,  -1,0x0A,0x1B,0x0C,0x1C,  -1,0x0E,0x0B,0x0F,  -1}, // 5 Swedish
	{0x08,  -1,0x12,0x1A,0x14,0x09,  -1,  -1,0x0D,0x1D,0x15,0x13}, // 6 French
	{0x08,  -1,0x09,0x16,0x19,0x17,  -1,  -1,0x1A,0x18,0x14,  -1}, // 7 Spanish
	};
	static constexpr uint8_t positions[12] = {
		35, 36, 64, 91, 92, 93, 94, 96, 123, 124, 125, 126
	};

	if (charset < 1 || charset > 7) return ch;
	for (int i = 0; i < 12; i++) {
		if (ch == positions[i]) {
			int8_t sub = tbl[charset - 1][i];
			if (sub >= 0) return (uint8_t)sub;
			break;
		}
	}
	return ch;
}

static uint16_t try_compose(uint16_t a, uint16_t b)
{
	for (int pass = 0; pass < 2; pass++) {
		uint16_t base = pass ? b : a;
		uint16_t acc  = pass ? a : b;
		uint16_t r = 0;
		switch (acc) {
		case 0x27: // apostrophe → acute
			switch (base) {
			case 'a': r=0xE1; break; case 'e': r=0xE9; break;
			case 'i': r=0xED; break; case 'o': r=0xF3; break;
			case 'u': r=0xFA; break; case 'y': r=0xFD; break;
			case 'A': r=0xC1; break; case 'E': r=0xC9; break;
			case 'I': r=0xCD; break; case 'O': r=0xD3; break;
			case 'U': r=0xDA; break; case 'Y': r=0xDD; break;
			} break;
		case 0x60: // backtick → grave
			switch (base) {
			case 'a': r=0xE0; break; case 'e': r=0xE8; break;
			case 'i': r=0xEC; break; case 'o': r=0xF2; break;
			case 'u': r=0xF9; break;
			case 'A': r=0xC0; break; case 'E': r=0xC8; break;
			case 'I': r=0xCC; break; case 'O': r=0xD2; break;
			case 'U': r=0xD9; break;
			} break;
		case 0x5E: // caret → circumflex
			switch (base) {
			case 'a': r=0xE2; break; case 'e': r=0xEA; break;
			case 'i': r=0xEE; break; case 'o': r=0xF4; break;
			case 'u': r=0xFB; break;
			case 'A': r=0xC2; break; case 'E': r=0xCA; break;
			case 'I': r=0xCE; break; case 'O': r=0xD4; break;
			case 'U': r=0xDB; break;
			} break;
		case 0x7E: // tilde
			switch (base) {
			case 'n': r=0xF1; break; case 'N': r=0xD1; break;
			case 'a': r=0xE3; break; case 'o': r=0xF5; break;
			case 'A': r=0xC3; break; case 'O': r=0xD5; break;
			} break;
		case 0xA8: // ¨ diaeresis
			switch (base) {
			case 'a': r=0xE4; break; case 'e': r=0xEB; break;
			case 'i': r=0xEF; break; case 'o': r=0xF6; break;
			case 'u': r=0xFC; break; case 'y': r=0xFF; break;
			case 'A': r=0xC4; break; case 'E': r=0xCB; break;
			case 'I': r=0xCF; break; case 'O': r=0xD6; break;
			case 'U': r=0xDC; break;
			} break;
		case 0x2C: // comma → cedilla
			switch (base) {
			case 'c': r=0xE7; break; case 'C': r=0xC7; break;
			} break;
		case 0x2F: // slash → stroke
			switch (base) {
			case 'o': r=0xF8; break; case 'O': r=0xD8; break;
			} break;
		}
		if (r) return r;
	}
	return 0;
}

static void append_text_glyph(std::vector<TextGlyph> &text_buf,
                              const TextGlyph &glyph)
{
	if (!text_buf.empty()) {
		auto &prev = text_buf.back();
		if (fabsf(prev.x_in - glyph.x_in) < 0.01f &&
		    fabsf(prev.y_in - glyph.y_in) < 0.01f) {
			uint16_t c = try_compose(prev.codepoint, glyph.codepoint);
			if (c) {
				prev.codepoint = c;
				return;
			}
		}
	}
	text_buf.push_back(glyph);
}

static uint64_t bj10e_dot_key(int xdot, int ydot)
{
	return ((uint64_t)(uint32_t)xdot << 32) | (uint32_t)ydot;
}

static int bj10e_key_x(uint64_t key)
{
	return (int32_t)(uint32_t)(key >> 32);
}

static int bj10e_key_y(uint64_t key)
{
	return (int32_t)(uint32_t)key;
}

size_t PrinterSim::queue_bj10e_text_dots(uint8_t ch,
                                         const PrinterState &state)
{
	size_t start = bj10e_pending_dots_.size();
	std::vector<uint64_t> cols = build_bj10e_glyph_columns(state, ch);
	if (cols.empty())
		return 0;

	float bidi_offset = state.line_dir_ltr ? 0.0f : (0.20f / 360.0f);
	int base_xdot = (int)std::lround((state.x_pos + bidi_offset) * 360.0f);
	int base_ydot = (int)std::lround((state.y_pos - 48.0f / 360.0f) * 360.0f);
	bool omit = state.font_mode == 1;

	for (size_t xdot = 0; xdot < cols.size(); xdot++) {
		uint64_t column = cols[xdot];
		for (int ydot = 0; ydot < 48; ydot++) {
			if (!(column & (1ULL << ydot)))
				continue;
			int row = state.double_high ? ydot * 2 : ydot;
			bj10e_pending_dots_.push_back({ base_xdot + (int)xdot,
			                                base_ydot + row, omit });
			if (state.double_high)
				bj10e_pending_dots_.push_back({ base_xdot + (int)xdot,
				                                base_ydot + row + 1, omit });
		}
	}
	return bj10e_pending_dots_.size() - start;
}

void PrinterSim::queue_bj10e_graphics_dot(float x_in, float y_in, bool omit)
{
	int xdot = (int)std::lround(x_in * 360.0f);
	int ydot = (int)std::lround(y_in * 360.0f);
	bj10e_pending_dots_.push_back({ xdot, ydot, omit });
}

void PrinterSim::flush_bj10e_pending_dots()
{
	if (bj10e_pending_dots_.empty())
		return;

	struct Dot {
		uint64_t key;
		bool omit;
	};
	std::vector<Dot> dots;
	dots.reserve(bj10e_pending_dots_.size());
	for (const Bj10ePendingDot &dot : bj10e_pending_dots_)
		dots.push_back({ bj10e_dot_key(dot.xdot, dot.ydot), dot.omit });
	std::sort(dots.begin(), dots.end(),
	          [](const Dot &a, const Dot &b) { return a.key < b.key; });

	for (size_t i = 0; i < dots.size();) {
		uint64_t key = dots[i].key;
		bool omit = true;
		do {
			omit = omit && dots[i].omit;
			i++;
		} while (i < dots.size() && dots[i].key == key);

		int xdot = bj10e_key_x(key);
		int ydot = bj10e_key_y(key);
		if (omit && ((xdot + ydot) & 1))
			continue;
		stamp_bj10e_dot(*page_, prof_, xdot, ydot);
	}
	bj10e_pending_dots_.clear();
}

void PrinterSim::emit_char(uint8_t ch)
{
	new_page_if_needed();

	if (!st_.include_8th_bit && prof_.model != PrinterModel::EpsonFX &&
	    prof_.model != PrinterModel::EpsonLQ500)
		ch = static_cast<uint8_t>(ch & 0x7F);

	if (st_.mousetext_mode && ch >= 0x40 && ch <= 0x5F)
		ch = static_cast<uint8_t>(ch + 0x80);

	uint8_t render_ch = ch;
	if (prof_.model == PrinterModel::ImageWriter && st_.use_user_chars &&
	    st_.use_high_user_chars && render_ch >= 0x20 && render_ch <= 0x6F)
		render_ch = static_cast<uint8_t>(render_ch | 0x80);
	if (st_.slashed_zero) {
		if (render_ch == 0x30) render_ch = prof_.model == PrinterModel::CanonBJ10e ? 0x00 : 0x7F;
		else if (render_ch == 0xB0) render_ch = 0xFF;
	}
	if (st_.charset > 0 && st_.charset <= 8)
		render_ch = (prof_.model == PrinterModel::ImageWriter)
		    ? intl_substitute_iw(render_ch, st_.charset)
		    : intl_substitute_fx(render_ch, st_.charset);

	float char_w_in = 1.0f / static_cast<float>(st_.pitch_cpi);
	if (st_.condensed && st_.pitch_cpi <= 10 &&
	    prof_.model != PrinterModel::EpsonLQ500)
		char_w_in = 1.0f / 17.16f;
	if (prof_.model == PrinterModel::CanonBJ10e && st_.condensed && !st_.proportional)
		char_w_in = 21.0f / 360.0f;
	if (st_.proportional && prof_.model == PrinterModel::ImageWriter) {
		uint8_t gw = (st_.use_user_chars && st_.user_char_defined[render_ch])
		    ? static_cast<uint8_t>(st_.user_char_prefix[render_ch] & 0x1F)
		    : get_iw2_corr_prop_width(ch);
		if (gw > 0)
			char_w_in = static_cast<float>(gw + st_.prop_spacing) /
			            static_cast<float>(st_.prop_dpi);
	} else if (st_.proportional && prof_.model == PrinterModel::CanonBJ10e) {
		if (st_.bj10e_use_downloaded_font && st_.bj10e_user_font &&
		    st_.bj10e_user_font->defined[ch]) {
			uint8_t width = st_.bj10e_user_font->width[ch];
			if (width > 0)
				char_w_in = static_cast<float>(width) / 360.0f;
		} else {
			Bj10eGlyph glyph = get_bj10e_glyph(ch, st_.codepage_850, false, true);
			if (glyph.width > 0)
				char_w_in = static_cast<float>(glyph.width) / 360.0f;
		}
	} else if (prof_.model == PrinterModel::EpsonLQ500) {
		// LQ-500 character width always comes from font glyph metrics.
		// The font selection (via lq500_font_index) picks the correct
		// font variant for the current pitch/quality/condensed state,
		// and start + width + advance at the font's DPI gives the
		// correct cell width.
		// VV:A6 bit 4 = VV:23 bit 4 (super/subscript active only).
		// SI/DC2 condensed does not select a different font.
		bool script = st_.superscript || st_.subscript;
		int idx = lq500_font_index(st_.lq500_family, st_.lq500_lq_mode,
		                            st_.pitch_cpi == 12, st_.proportional,
		                            script);
		auto info = get_lq500_glyph(idx, render_ch);
		int total = lq500_script_cell_total(st_, render_ch, info);
		if (total > 0) {
			float hres = st_.lq500_lq_mode
			    ? static_cast<float>(prof_.lq_hres)
			    : static_cast<float>(prof_.draft_hres);
			char_w_in = static_cast<float>(total) / hres;
		}
	} else if (st_.proportional && prof_.model == PrinterModel::EpsonFX) {
		bool italic = st_.italic || render_ch >= 0x80;
		if (st_.use_user_chars && st_.user_char_defined[render_ch]) {
			uint8_t attr = st_.user_char_prefix[render_ch];
			uint8_t start = (uint8_t)((attr >> 4) & 0x07);
			uint8_t end = (uint8_t)(attr & 0x0F);
			char_w_in = static_cast<float>(end >= start ? end - start + 1 : 1) / 120.0f;
		} else {
			char_w_in = static_cast<float>(get_fx80_prop_width(render_ch, italic)) / 120.0f;
		}
	}
	if (st_.lq500_interchar > 0 && prof_.model == PrinterModel::EpsonLQ500) {
		float denom = st_.lq500_lq_mode ? 180.0f : 120.0f;
		char_w_in += static_cast<float>(st_.lq500_interchar) / denom;
	}
	if (st_.expanded || st_.expanded_line)
		char_w_in *= 2.0f;

	if (!pending_line_.empty() &&
	    st_.x_pos + char_w_in > st_.right_margin_in + 0.001f) {
		bool feed_when_full =
		    prof_.model != PrinterModel::ImageWriter || st_.lf_when_full;
		wrap_full_line(feed_when_full);
	}

	{
		size_t bj10e_dot_count = 0;
		if (prof_.model == PrinterModel::CanonBJ10e)
			bj10e_dot_count = queue_bj10e_text_dots(render_ch, st_);

		uint16_t cp = ch;
		if (st_.mousetext_mode && ch >= 0xC0 && ch <= 0xDF) {
			static constexpr uint16_t mt_unicode[32] = {
				0xF8FF,0xF8FF, // 0x40-41: closed/open apple (PUA)
				0x2191,0x2193,0x2190,0x2192, // arrows
				0x2588,0x2592,0x2590,0x258C, // blocks
				0x2580,0x2584,0x2502,0x2500, // half blocks + lines
				0x253C,0x2534,0x252C,0x251C, // box drawing
				0x2524,0x250C,0x2510,0x2514, // corners
				0x2518,0x2588,0x25A0,0x25C6, // box + diamond
				0x2573,0x2592,0x2591,0x2593, // patterns
			};
			cp = mt_unicode[ch - 0xC0];
		} else if (st_.charset > 0 && st_.charset <= 8 && ch >= 0x20) {
			cp = (prof_.model == PrinterModel::ImageWriter)
			    ? intl_unicode_iw(ch, st_.charset)
			    : intl_unicode_fx(ch, st_.charset);
		}
		if (cp >= 0x20) {
			float sz = char_w_in * 72.0f / 0.6f;
			uint8_t sty = 0;
			if (st_.bold || (prof_.model == PrinterModel::EpsonFX && st_.proportional))
				sty |= TextGlyph::BOLD;
			if (st_.underline) sty |= TextGlyph::UNDERLINE;
			if (st_.superscript) sty |= TextGlyph::SUPER;
			if (st_.subscript) sty |= TextGlyph::SUB;
			pending_line_.push_back({
				render_ch, st_, char_w_in, true,
				{st_.x_pos, st_.y_pos, cp, char_w_in, sz, sty},
				bj10e_dot_count
			});
		} else {
			pending_line_.push_back({render_ch, st_, char_w_in, false, {},
			                         bj10e_dot_count});
		}
	}

	st_.x_pos += char_w_in;
}

void PrinterSim::flush_pending_line()
{
	if (pending_line_.empty() && bj10e_pending_dots_.empty())
		return;

	new_page_if_needed();
	page_dirty_ = true;
	if (prof_.model == PrinterModel::CanonBJ10e) {
		for (const auto &entry : pending_line_) {
			if (entry.has_text)
				append_text_glyph(text_buf_, entry.text);
		}
		pending_line_.clear();
		flush_bj10e_pending_dots();
		mark_line_output();
		return;
	}
	// LQ-500 justification: shift pending characters for center/right
	if (st_.lq500_justify > 0 && !pending_line_.empty()) {
		float line_end = pending_line_.back().state.x_pos +
		                 pending_line_.back().advance_in;
		float slack = st_.right_margin_in - line_end;
		if (slack > 0.001f) {
			float shift = 0.0f;
			if (st_.lq500_justify == 1)       // center
				shift = slack * 0.5f;
			else if (st_.lq500_justify == 2)  // right
				shift = slack;
			if (shift > 0.0f) {
				for (auto &entry : pending_line_) {
					entry.state.x_pos += shift;
					if (entry.has_text)
						entry.text.x_in += shift;
				}
			}
		}
	}
	for (const auto &entry : pending_line_) {
		if (entry.has_text)
			append_text_glyph(text_buf_, entry.text);
		PrinterState render_state = entry.state;
		bool force_ltr = st_.unidirectional || st_.unidirectional_line ||
		                 line_force_ltr_;
		render_state.line_dir_ltr = force_ltr ? true : st_.line_dir_ltr;
		dots_->render_char(*page_, render_state, prof_, entry.ch);
	}
	pending_line_.clear();
	mark_line_output();
}

void PrinterSim::cancel_pending_line()
{
	pending_line_.clear();
	bj10e_pending_dots_.clear();
}

void PrinterSim::delete_pending_char()
{
	if (pending_line_.empty())
		return;
	st_.x_pos = pending_line_.back().state.x_pos;
	size_t dot_count = pending_line_.back().bj10e_dot_count;
	while (dot_count-- > 0 && !bj10e_pending_dots_.empty())
		bj10e_pending_dots_.pop_back();
	pending_line_.pop_back();
}

void PrinterSim::reset_printer_state(const PrinterConfig &cfg)
{
	bool preserve_position = page_ != nullptr;
	float x_pos = st_.x_pos;
	float y_pos = st_.y_pos;

	st_ = PrinterState{};
	apply_config(cfg);

	if (preserve_position) {
		st_.x_pos = x_pos;
		st_.y_pos = y_pos;
	}
	line_output_ = false;
	line_force_ltr_ = false;
}

void PrinterSim::wrap_full_line(bool feed)
{
	flush_pending_line();
	st_.x_pos = st_.left_margin_in;
	st_.expanded_line = false;
	if (feed) {
		line_feed();
	} else {
		advance_line_direction();
		finish_printed_line();
	}
}

void PrinterSim::mark_line_output(bool force_ltr)
{
	line_output_ = true;
	if (force_ltr) {
		line_force_ltr_ = true;
		st_.line_dir_ltr = true;
	}
}

void PrinterSim::advance_line_direction()
{
	if (!line_output_)
		return;

	bool force_ltr = st_.unidirectional || st_.unidirectional_line || line_force_ltr_;
	if (force_ltr)
		st_.line_dir_ltr = true;
	else
		st_.line_dir_ltr = !st_.line_dir_ltr;
}

void PrinterSim::finish_printed_line()
{
	if (!line_output_)
		return;
	st_.unidirectional_line = false;
	line_force_ltr_ = false;
	line_output_ = false;
}

void PrinterSim::apply_config(const PrinterConfig &cfg)
{
	cfg_ = cfg;
	if (prof_.model == PrinterModel::ImageWriter) {
		st_.pitch_cpi = cfg.pitch_cpi;
		st_.font_mode = cfg.font_mode;
	} else {
		if (cfg.pitch_cpi == 17) {
			st_.pitch_cpi = 10;
			st_.condensed = true;
		} else {
			st_.pitch_cpi = cfg.pitch_cpi;
			st_.condensed = false;
		}
		st_.bold = cfg.emphasized;
	}
	if (prof_.model == PrinterModel::EpsonLQ500) {
		st_.include_8th_bit = true;
		st_.lq500_lq_mode = (cfg.font_mode != 0);
		// DIP switch interpretation for LQ-500
		if (cfg.dip_switches) {
			int sw1 = cfg.dip_switches & 0xFF;
			int sw2 = (cfg.dip_switches >> 8) & 0xFF;
			// SW1-1..1-3: international charset
			st_.charset = sw1 & 0x07;
			// SW1-4..1-5: font selection
			int font_sel = (sw1 >> 3) & 0x03;
			switch (font_sel) {
			case 0: st_.lq500_lq_mode = false; st_.lq500_family = 0; break; // Draft
			case 1: st_.lq500_lq_mode = true; st_.lq500_family = 0; break;  // Roman
			case 2: st_.lq500_lq_mode = true; st_.lq500_family = 1; break;  // SansSerif
			default: break;
			}
			// SW1-6: condensed
			if (sw1 & 0x20) st_.condensed = true;
			// SW1-7: char table
			st_.lq500_char_table = (sw1 & 0x40) ? 1 : 0;
			// SW2-1: page length
			if (sw2 & 0x01)
				st_.page_height_in = 12.0f;
			// SW2-3: skip-over-perforation
			if (sw2 & 0x04)
				st_.perf_skip_lines = 6;
			// SW2-4: auto LF
			st_.auto_lf = !!(sw2 & 0x08);
			// SW2-7..2-8: pitch
			int pitch_sel = (sw2 >> 6) & 0x03;
			switch (pitch_sel) {
			case 0: st_.pitch_cpi = 10; break;
			case 1: st_.pitch_cpi = 12; break;
			case 2: st_.pitch_cpi = 15; break;
			default: st_.pitch_cpi = 10; break;
			}
		}
	}
	if (prof_.model == PrinterModel::CanonBJ10e) {
		st_.include_8th_bit = true;
		st_.expanded = cfg.double_width;
		st_.double_high = cfg.double_high;
		st_.double_high_motion = cfg.double_high;
		st_.proportional = cfg.proportional;
		st_.font_mode = cfg.font_mode;
	}
	st_.auto_lf = cfg.auto_lf;
	st_.perf_skip_lines = cfg.perf_skip;
	st_.charset = cfg.charset;
	st_.slashed_zero = cfg.slashed_zero;
	if (cfg.page_length_lines > 0)
		st_.page_height_in = static_cast<float>(cfg.page_length_lines) *
		                     st_.line_spacing_in;
}

void PrinterSim::carriage_return()
{
	flush_pending_line();
	advance_line_direction();
	st_.x_pos = st_.left_margin_in;
	st_.expanded_line = false;
	finish_printed_line();
	if (st_.auto_lf)
		line_feed();
}

void PrinterSim::line_feed()
{
	flush_pending_line();
	new_page_if_needed();
	page_dirty_ = true;

	float delta = st_.reverse_lf ? -st_.line_spacing_in : st_.line_spacing_in;
	if (prof_.model == PrinterModel::CanonBJ10e && st_.double_high_motion)
		delta *= 2.0f;
	st_.y_pos += delta;

	if (st_.reverse_lf) {
		if (st_.y_pos < st_.top_margin_in)
			st_.y_pos = st_.top_margin_in;
	} else {
		float bottom = st_.perf_skip_lines > 0
			? st_.page_height_in -
			  static_cast<float>(st_.perf_skip_lines) * st_.line_spacing_in
			: st_.page_height_in - 0.5f;
		if (st_.y_pos >= bottom)
			form_feed();
	}
	advance_line_direction();
	finish_printed_line();
}

void PrinterSim::form_feed()
{
	flush_pending_line();
	advance_line_direction();
	finish_printed_line();
	if (page_ && page_dirty_) {
		pdf_.add_page(*page_, prof_.render_dpi, text_buf_);
		text_buf_.clear();
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

static float match_iw_pitch(const char *val)
{
	if (strcasecmp(val, "extended") == 0)        return 9;
	if (strcasecmp(val, "pica") == 0)            return 10;
	if (strcasecmp(val, "elite") == 0)           return 12;
	if (strcasecmp(val, "semicondensed") == 0)   return 107.0f / 8.0f;
	if (strcasecmp(val, "condensed") == 0)       return 15;
	if (strcasecmp(val, "ultracondensed") == 0)  return 17;
	return 12;
}

PrinterConfig default_config_for(PrinterModel model)
{
	PrinterConfig cfg;
	if (model == PrinterModel::ImageWriter) {
		cfg.pitch_cpi = 12;
		cfg.font_mode = 2;  // ROM 0x39DB initializes AA70/AA71 to NLQ
		cfg.page_length_lines = 66;
	} else if (model == PrinterModel::EpsonLQ500) {
		cfg.pitch_cpi = 10;
		cfg.charset = 0;       // USA
		cfg.auto_lf = false;
		cfg.page_length_lines = 66;  // 11" at 6 lpi
		cfg.font_mode = 0;    // Draft
	} else if (model == PrinterModel::CanonBJ10e) {
		cfg.auto_lf = false;
		cfg.page_length_lines = 66;
		cfg.dip_switches = 0;
		cfg.font_mode = 0;
	}
	return cfg;
}

float parse_pitch(const char *val, PrinterModel model)
{
	if (model == PrinterModel::ImageWriter)
		return match_iw_pitch(val);
	if (model == PrinterModel::CanonBJ10e) {
		if (strcasecmp(val, "elite") == 0 || strcasecmp(val, "12") == 0)
			return 12;
		if (strcasecmp(val, "compressed") == 0 || strcasecmp(val, "condensed") == 0 ||
		    strcasecmp(val, "17") == 0)
			return 17;
	}
	if (strcasecmp(val, "compressed") == 0) return 17;
	return 10;
}

PrinterConfig load_printer_config(const char *path, PrinterModel model)
{
	PrinterConfig cfg = default_config_for(model);
	FILE *f = fopen(path, "r");
	if (!f) { perror(path); return cfg; }

	bool is_iw = (model == PrinterModel::ImageWriter);

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
			if (is_iw) {
				cfg.pitch_cpi = match_iw_pitch(val);
			} else if (model == PrinterModel::CanonBJ10e) {
				cfg.pitch_cpi = parse_pitch(val, model);
			} else {
				if (strcasecmp(val, "compressed") == 0) cfg.pitch_cpi = 17;
				else cfg.pitch_cpi = 10;
			}
		} else if (strcasecmp(key, "zero_style") == 0) {
			cfg.slashed_zero = (strcasecmp(val, "slashed") == 0);
		} else if (strcasecmp(key, "print_weight") == 0) {
			if (!is_iw)
				cfg.emphasized = (strcasecmp(val, "emphasized") == 0);
		} else if (strcasecmp(key, "charset") == 0) {
			cfg.charset = match_charset(val);
		} else if (strcasecmp(key, "auto_lf") == 0) {
			cfg.auto_lf = (strcasecmp(val, "on") == 0);
		} else if (strcasecmp(key, "perf_skip") == 0) {
			if (strcasecmp(val, "on") == 0) cfg.perf_skip = 6;
			else if (strcasecmp(val, "off") == 0) cfg.perf_skip = 0;
			else cfg.perf_skip = atoi(val);
		} else if (strcasecmp(key, "font") == 0) {
			if (is_iw) {
				if (strcasecmp(val, "draft") == 0)          cfg.font_mode = 2;
				else if (strcasecmp(val, "standard") == 0)  cfg.font_mode = 0;
				else if (strcasecmp(val, "nlq") == 0)       cfg.font_mode = 1;
			} else if (model == PrinterModel::CanonBJ10e) {
				if (strcasecmp(val, "economy") == 0)        cfg.font_mode = 1;
				else if (strcasecmp(val, "hq") == 0 ||
				         strcasecmp(val, "high_quality") == 0)
					cfg.font_mode = 0;
			}
		} else if (strcasecmp(key, "page_length") == 0) {
			if (strcasecmp(val, "11") == 0) cfg.page_length_lines = 66;
			else if (strcasecmp(val, "12") == 0) cfg.page_length_lines = 72;
			else cfg.page_length_lines = atoi(val);
		} else if (strcasecmp(key, "quality") == 0 &&
		           model == PrinterModel::EpsonLQ500) {
			if (strcasecmp(val, "draft") == 0) cfg.font_mode = 0;
			else if (strcasecmp(val, "lq") == 0) cfg.font_mode = 1;
		} else if (strcasecmp(key, "pitch") == 0 &&
		           model == PrinterModel::EpsonLQ500) {
			if (strcasecmp(val, "pica") == 0 || strcasecmp(val, "10") == 0)
				cfg.pitch_cpi = 10;
			else if (strcasecmp(val, "elite") == 0 || strcasecmp(val, "12") == 0)
				cfg.pitch_cpi = 12;
			else if (strcasecmp(val, "condensed") == 0 || strcasecmp(val, "15") == 0)
				cfg.pitch_cpi = 15;
		} else if (strcasecmp(key, "dip_switches") == 0) {
			if (model == PrinterModel::CanonBJ10e ||
			    model == PrinterModel::EpsonLQ500)
				cfg.dip_switches = atoi(val);
		} else if (strcasecmp(key, "control_mode") == 0) {
			if (model == PrinterModel::CanonBJ10e) {
				if (strcasecmp(val, "mode1") == 0) cfg.dip_switches &= ~(1 << 9);
				else if (strcasecmp(val, "mode2") == 0) cfg.dip_switches |= (1 << 9);
			}
		} else if (strcasecmp(key, "double_width") == 0) {
			if (model == PrinterModel::CanonBJ10e)
				cfg.double_width = (strcasecmp(val, "on") == 0);
		} else if (strcasecmp(key, "double_high") == 0) {
			if (model == PrinterModel::CanonBJ10e)
				cfg.double_high = (strcasecmp(val, "on") == 0);
		} else if (strcasecmp(key, "proportional") == 0) {
			if (model == PrinterModel::CanonBJ10e)
				cfg.proportional = (strcasecmp(val, "on") == 0);
		} else if (strcasecmp(key, "quality") == 0) {
			if (model == PrinterModel::CanonBJ10e) {
				if (strcasecmp(val, "economy") == 0) cfg.font_mode = 1;
				else if (strcasecmp(val, "hq") == 0 ||
				         strcasecmp(val, "high_quality") == 0)
					cfg.font_mode = 0;
			}
		} else {
			fprintf(stderr, "printer config: unknown key '%s'\n", key);
		}
	}
	fclose(f);
	return cfg;
}
