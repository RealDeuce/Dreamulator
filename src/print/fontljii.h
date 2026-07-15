// license:BSD-3-Clause
// copyright-holders:Stephen Hurd
#ifndef FONTLJII_H
#define FONTLJII_H

#include <cstddef>
#include <cstdint>

struct LjiiGlyphInfo {
	bool found = false;
	uint8_t width = 0;
	int8_t x_offset = 0;
	int8_t y_offset = 0;
	uint8_t rows = 0;
	uint8_t span = 0;
	const uint8_t *data = nullptr;
	size_t data_len = 0;
};

struct LjiiFontRequest {
	int symbol_set = 0x0115;
	int pitch = 1000;
	int height = 1200;
	int spacing = 0;
	int style = 0;
	int stroke = 0;
	int typeface = 3;
	bool secondary = false;
};

struct LjiiFontMetrics {
	bool found = false;
	int orientation = 0;
	int symbol_set = 0;
	int pitch = 0;
	int height = 0;
	int spacing = 0;
	int style = 0;
	int stroke = 0;
	int typeface = 0;
	int underline_distance = 0;
};

LjiiGlyphInfo get_ljii_glyph(uint32_t context_longword, uint8_t host_byte);
uint32_t select_ljii_context(const LjiiFontRequest &request, int orientation);
int ljii_context_pitch(uint32_t context_longword);
LjiiFontMetrics get_ljii_font_metrics(uint32_t context_longword);
uint32_t default_ljii_context_for_pitch(float pitch_cpi, int symbol_set,
                                        int class_id);

#endif
