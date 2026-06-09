// license:BSD-3-Clause
// copyright-holders:Stephen Hurd
#ifndef DOTRENDER_H
#define DOTRENDER_H

#include "pagebuf.h"
#include "printer.h"
#include <cstdint>
#include <memory>
#include <random>

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
	               float intensity, float sharpness);

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

const uint16_t *get_9pin_glyph(uint8_t ch);
const uint8_t *get_24pin_glyph(uint8_t ch);
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

#endif
