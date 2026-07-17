// license:BSD-3-Clause
// copyright-holders:Stephen Hurd
#ifndef FONTLJII_H
#define FONTLJII_H

#include <cstddef>
#include <cstdint>

struct LjiiGlyphInfo {
	bool found = false;
	uint16_t width = 0;
	int16_t x_offset = 0;
	int16_t y_offset = 0;
	uint16_t rows = 0;
	uint16_t span = 0;
	const uint8_t *data = nullptr;
	size_t data_len = 0;
};

struct LjiiCartridgeSlots {
	int slot[2] = { 0, 0 };
};

struct LjiiCartridgeInfo {
	int id;
	const char *key;
	const char *label;
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

size_t ljii_cartridge_count();
const LjiiCartridgeInfo *ljii_cartridge_info(size_t index);
const LjiiCartridgeInfo *find_ljii_cartridge(int id);
bool ljii_valid_cartridge(int id);
int parse_ljii_cartridge(const char *value);

LjiiGlyphInfo get_ljii_glyph(uint32_t context_longword, uint8_t host_byte,
                             LjiiCartridgeSlots cartridges = {});
uint32_t select_ljii_context(const LjiiFontRequest &request, int orientation,
                             LjiiCartridgeSlots cartridges = {});
int ljii_context_pitch(uint32_t context_longword,
                       LjiiCartridgeSlots cartridges = {});
LjiiFontMetrics get_ljii_font_metrics(uint32_t context_longword,
                                      LjiiCartridgeSlots cartridges = {});
uint32_t default_ljii_context_for_pitch(float pitch_cpi, int symbol_set,
                                        int class_id);

#endif
