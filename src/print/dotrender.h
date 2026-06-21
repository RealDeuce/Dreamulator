// license:BSD-3-Clause
// copyright-holders:Stephen Hurd
#ifndef DOTRENDER_H
#define DOTRENDER_H

#include "pagebuf.h"
#include "printer.h"
#include <cstdint>
#include <memory>
#include <random>
#include <vector>

struct PrinterProfile;
struct PrinterState;

struct Bj10eGlyph {
	const uint64_t *cols;
	uint8_t width;
};

class DotRenderer {
public:
	virtual ~DotRenderer() = default;
	virtual void render_char(PageBitmap &page, const PrinterState &st,
	                         const PrinterProfile &prof, uint8_t ch) = 0;

	void stamp_pin(PageBitmap &page, float x_in, float y_in,
	               int dpi, float radius_mm, float jitter_mm,
	               float intensity, float sharpness,
	               float overprint_gamma = 1.0f,
	               float radius_variance = 0.075f,
	               float intensity_variance = 0.075f,
	               float edge_softness = 0.0f,
	               float x_scale = 1.0f, float y_scale = 1.0f);
	void set_ink_color(float red, float green, float blue);
	void set_ribbon_mask(uint8_t mask);

private:
	float ink_red_ = 0.0f;
	float ink_green_ = 0.0f;
	float ink_blue_ = 0.0f;
	uint8_t ribbon_mask_ = 0;

public:
	std::mt19937 rng_{42};
};

class ImpactDot9 : public DotRenderer {
public:
	void render_char(PageBitmap &page, const PrinterState &st,
	                 const PrinterProfile &prof, uint8_t ch) override;
	float pin_vib_[9] = {};
};

class ImpactDot24 : public DotRenderer {
public:
	void render_char(PageBitmap &page, const PrinterState &st,
	                 const PrinterProfile &prof, uint8_t ch) override;
	float pin_vib_[24] = {};
};

class InkjetDot : public DotRenderer {
public:
	void render_char(PageBitmap &page, const PrinterState &st,
	                 const PrinterProfile &prof, uint8_t ch) override;
};

class TonerDot : public DotRenderer {
public:
	void render_char(PageBitmap &page, const PrinterState &st,
	                 const PrinterProfile &prof, uint8_t ch) override;
};

std::unique_ptr<DotRenderer> create_dot_renderer(const PrinterProfile &prof);

struct Lq500GlyphInfo {
	const uint8_t *data;  // 3 bytes per column
	uint8_t start;        // left padding columns
	uint8_t width;        // glyph body columns
	uint8_t advance;      // right padding columns
};
Lq500GlyphInfo get_lq500_glyph(int font_index, uint8_t ch);
int lq500_font_index(uint8_t family, bool lq, bool elite,
                     bool proportional, bool condensed);
const uint8_t *get_lq500_sec_metrics(int base, uint8_t ch);

const uint16_t *get_9pin_glyph(uint8_t ch);
const uint8_t *get_24pin_glyph(uint8_t ch);
uint8_t get_fx80_prefix(uint8_t ch, bool italic);
uint8_t get_fx80_prop_start(uint8_t ch, bool italic);
uint8_t get_fx80_prop_width(uint8_t ch, bool italic);
const uint16_t *get_fx80_roman_glyph(uint8_t ch);
const uint16_t *get_fx80_italic_glyph(uint8_t ch);
const uint16_t *get_iw2_corr_fw_glyph(uint8_t ch);
const uint16_t *get_iw2_corr_prop_glyph(uint8_t ch);
uint8_t get_iw2_corr_prop_width(uint8_t ch);
const uint16_t *get_iw2_draft_glyph(uint8_t ch);
const uint16_t *get_iw2_nlq_prop_p1_glyph(uint8_t ch);
const uint16_t *get_iw2_nlq_prop_p2_glyph(uint8_t ch);
const uint16_t *get_iw2_nlq_fw_p1_glyph(uint8_t ch);
const uint16_t *get_iw2_nlq_fw_p2_glyph(uint8_t ch);
Bj10eGlyph get_bj10e_glyph(uint8_t ch, bool cp850, bool secondary,
                           bool proportional);
std::vector<uint64_t> build_bj10e_glyph_columns(const PrinterState &st,
                                                uint8_t ch);
void stamp_bj10e_dot(PageBitmap &page, const PrinterProfile &prof,
                     int xdot, int ydot);
void set_ribbon_ink(DotRenderer &dr, uint8_t color);

#endif
