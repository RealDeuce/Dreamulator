// license:BSD-3-Clause
// copyright-holders:Stephen Hurd
// HP LaserJet II / PCL Level IV printer model.
#include "printer.h"
#include "fontljii.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace {

static constexpr float kDotsPerIn = 300.0f;
static constexpr int kSymbolRoman8 = 0x0115;

struct PageGeometry {
	int physical_w = 2550;
	int physical_h = 3300;
	int logical_w = 2400;
	int logical_h = 3300;
	int left = 50;
	int right = 100;
	int top = 60;
	int bottom = 60;
};

PageGeometry pcl_page_geometry(int code, int orientation)
{
	PageGeometry portrait;
	switch (code) {
	case 1:  portrait = { 2175, 3150, 2025, 3150, 50, 100, 60, 60 }; break;
	case 2:  portrait = { 2550, 3300, 2400, 3300, 50, 100, 60, 60 }; break;
	case 3:  portrait = { 2550, 4200, 2400, 4200, 50, 100, 60, 60 }; break;
	case 26: portrait = { 2480, 3507, 2338, 3507, 50,  92, 60, 58 }; break;
	case 80: portrait = { 1162, 2250, 1012, 2250, 50, 100, 60, 60 }; break;
	case 81: portrait = { 1237, 2850, 1087, 2850, 50, 100, 60, 60 }; break;
	case 90: portrait = { 1299, 2598, 1157, 2598, 50,  92, 60, 58 }; break;
	case 91: portrait = { 1913, 2704, 1771, 2704, 50,  92, 60, 58 }; break;
	default: portrait = { 2550, 3300, 2400, 3300, 50, 100, 60, 60 }; break;
	}
	if ((orientation & 1) == 0)
		return portrait;

	PageGeometry landscape;
	landscape.physical_w = portrait.physical_h;
	landscape.physical_h = portrait.physical_w;
	landscape.logical_w = portrait.logical_h - portrait.top - portrait.bottom;
	landscape.logical_h = portrait.physical_w;
	landscape.left = portrait.top;
	landscape.right = portrait.bottom;
	landscape.top = portrait.left;
	landscape.bottom = portrait.right;
	return landscape;
}

float dots_to_in(int dots)
{
	return (float)dots / kDotsPerIn;
}

uint16_t roman8_to_unicode(uint8_t ch)
{
	static constexpr uint16_t table[256] = {
		0x0000,0x0001,0x0002,0x0003,0x0004,0x0005,0x0006,0x0007,
		0x0008,0x0009,0x000a,0x000b,0x000c,0x000d,0x000e,0x000f,
		0x0010,0x0011,0x0012,0x0013,0x0014,0x0015,0x0016,0x0017,
		0x0018,0x0019,0x001a,0x001b,0x001c,0x001d,0x001e,0x001f,
		0x0020,0x0021,0x0022,0x0023,0x0024,0x0025,0x0026,0x0027,
		0x0028,0x0029,0x002a,0x002b,0x002c,0x002d,0x002e,0x002f,
		0x0030,0x0031,0x0032,0x0033,0x0034,0x0035,0x0036,0x0037,
		0x0038,0x0039,0x003a,0x003b,0x003c,0x003d,0x003e,0x003f,
		0x0040,0x0041,0x0042,0x0043,0x0044,0x0045,0x0046,0x0047,
		0x0048,0x0049,0x004a,0x004b,0x004c,0x004d,0x004e,0x004f,
		0x0050,0x0051,0x0052,0x0053,0x0054,0x0055,0x0056,0x0057,
		0x0058,0x0059,0x005a,0x005b,0x005c,0x005d,0x005e,0x005f,
		0x0060,0x0061,0x0062,0x0063,0x0064,0x0065,0x0066,0x0067,
		0x0068,0x0069,0x006a,0x006b,0x006c,0x006d,0x006e,0x006f,
		0x0070,0x0071,0x0072,0x0073,0x0074,0x0075,0x0076,0x0077,
		0x0078,0x0079,0x007a,0x007b,0x007c,0x007d,0x007e,0x007f,
		0x0080,0x0081,0x0082,0x0083,0x0084,0x0085,0x0086,0x0087,
		0x0088,0x0089,0x008a,0x008b,0x008c,0x008d,0x008e,0x008f,
		0x0090,0x0091,0x0092,0x0093,0x0094,0x0095,0x0096,0x0097,
		0x0098,0x0099,0x009a,0x009b,0x009c,0x009d,0x009e,0x009f,
		0x00a0,0x00c0,0x00c2,0x00c8,0x00ca,0x00cb,0x00ce,0x00cf,
		0x00b4,0x02cb,0x02c6,0x00a8,0x02dc,0x00d9,0x00db,0x20a4,
		0x00af,0x00dd,0x00fd,0x00b0,0x00c7,0x00e7,0x00d1,0x00f1,
		0x00a1,0x00bf,0x00a4,0x00a3,0x00a5,0x00a7,0x0192,0x00a2,
		0x00e2,0x00ea,0x00f4,0x00fb,0x00e1,0x00e9,0x00f3,0x00fa,
		0x00e0,0x00e8,0x00f2,0x00f9,0x00e4,0x00eb,0x00f6,0x00fc,
		0x00c5,0x00ee,0x00d8,0x00c6,0x00e5,0x00ed,0x00f8,0x00e6,
		0x00c4,0x00ec,0x00d6,0x00dc,0x00c9,0x00ef,0x00df,0x00d4,
		0x00c1,0x00c3,0x00e3,0x00d0,0x00f0,0x00cd,0x00cc,0x00d3,
		0x00d2,0x00d5,0x00f5,0x0160,0x0161,0x00da,0x0178,0x00ff,
		0x00de,0x00fe,0x00b7,0x00b5,0x00b6,0x00be,0x2014,0x00bc,
		0x00bd,0x00aa,0x00ba,0x00ab,0x25a0,0x00bb,0x00b1,0xfffd,
	};
	return table[ch];
}

bool is_param_byte(uint8_t b)
{
	return (b >= '0' && b <= '9') || b == '.' || b == '-' || b == '+';
}

int pcl_symbol_value(int value, char term)
{
	if (term >= 'A' && term <= 'Z')
		return value * 32 + (term - '@');
	if (term >= 'a' && term <= 'z')
		return value * 32 + (term - '`');
	return value;
}

struct SymbolPatch {
	int symbol;
	uint8_t dst;
	uint8_t src;
};

static constexpr SymbolPatch kSymbolPatches[] = {
	{0x0055,0x24,0xba},{0x0055,0x5e,0xaa},{0x0055,0x60,0xa9},{0x0055,0x7e,0xb0},
	{0x0025,0x23,0xbb},{0x0025,0x5e,0xaa},{0x0025,0x60,0xa9},{0x0025,0x7e,0xb0},
	{0x0006,0x23,0xbb},{0x0006,0x40,0xc8},{0x0006,0x5b,0xb3},{0x0006,0x5c,0xb5},
	{0x0006,0x5d,0xbd},{0x0006,0x5e,0xaa},{0x0006,0x60,0xa9},{0x0006,0x7b,0xc5},
	{0x0006,0x7c,0xcb},{0x0006,0x7d,0xc9},{0x0006,0x7e,0xab},
	{0x0026,0x23,0xbb},{0x0026,0x40,0xc8},{0x0026,0x5b,0xb3},{0x0026,0x5c,0xb5},
	{0x0026,0x5d,0xbd},{0x0026,0x5e,0xaa},{0x0026,0x60,0xf3},{0x0026,0x7b,0xc5},
	{0x0026,0x7c,0xcb},{0x0026,0x7d,0xc9},{0x0026,0x7e,0xab},
	{0x0007,0x23,0xbb},{0x0007,0x40,0xbd},{0x0007,0x5b,0xd8},{0x0007,0x5c,0xda},
	{0x0007,0x5d,0xdb},{0x0007,0x5e,0xaa},{0x0007,0x60,0xa9},{0x0007,0x7b,0xcc},
	{0x0007,0x7c,0xce},{0x0007,0x7d,0xcf},{0x0007,0x7e,0xde},
	{0x0027,0x40,0xbd},{0x0027,0x5b,0xd8},{0x0027,0x5c,0xda},{0x0027,0x5d,0xdb},
	{0x0027,0x5e,0xaa},{0x0027,0x60,0xa9},{0x0027,0x7b,0xcc},{0x0027,0x7c,0xce},
	{0x0027,0x7d,0xcf},{0x0027,0x7e,0xde},
	{0x0009,0x23,0xbb},{0x0009,0x40,0xbd},{0x0009,0x5b,0xb3},{0x0009,0x5c,0xb5},
	{0x0009,0x5d,0xc5},{0x0009,0x5e,0xaa},{0x0009,0x60,0xcb},{0x0009,0x7b,0xc8},
	{0x0009,0x7c,0xca},{0x0009,0x7d,0xc9},{0x0009,0x7e,0xd9},
	{0x000b,0x5c,0xbc},{0x000b,0x5e,0xaa},{0x000b,0x60,0xa9},{0x000b,0x7e,0xb0},
	{0x004b,0x24,0xbc},{0x004b,0x5e,0xaa},{0x004b,0x60,0xa9},{0x004b,0x7e,0xb0},
	{0x0073,0x24,0xba},{0x0073,0x5b,0xd8},{0x0073,0x5c,0xda},{0x0073,0x5d,0xd0},
	{0x0073,0x5e,0xaa},{0x0073,0x60,0xa9},{0x0073,0x7b,0xcc},{0x0073,0x7c,0xce},
	{0x0073,0x7d,0xd4},{0x0073,0x7e,0xb0},
	{0x0013,0x24,0xba},{0x0013,0x40,0xdc},{0x0013,0x5b,0xd8},{0x0013,0x5c,0xda},
	{0x0013,0x5d,0xd0},{0x0013,0x5e,0xdb},{0x0013,0x60,0xc5},{0x0013,0x7b,0xcc},
	{0x0013,0x7c,0xce},{0x0013,0x7d,0xd4},{0x0013,0x7e,0xcf},
	{0x0033,0x5b,0xb8},{0x0033,0x5c,0xb6},{0x0033,0x5d,0xb9},{0x0033,0x5e,0xb3},
	{0x0033,0x60,0xa9},{0x0033,0x7c,0xb7},{0x0033,0x7e,0xac},
	{0x0053,0x23,0xbb},{0x0053,0x40,0xbd},{0x0053,0x5b,0xb8},{0x0053,0x5c,0xb6},
	{0x0053,0x5d,0xb9},{0x0053,0x5e,0xaa},{0x0053,0x60,0xa9},{0x0053,0x7b,0xb3},
	{0x0053,0x7c,0xb7},{0x0053,0x7d,0xb5},{0x0053,0x7e,0xac},
	{0x00d3,0x40,0xf2},{0x00d3,0x5b,0xb8},{0x00d3,0x5c,0xb6},{0x00d3,0x5d,0xb4},
	{0x00d3,0x5e,0xb9},{0x00d3,0x60,0xa9},{0x00d3,0x7b,0xa8},{0x00d3,0x7c,0xb7},
	{0x00d3,0x7d,0xb5},{0x00d3,0x7e,0xab},
	{0x0093,0x40,0xbd},{0x0093,0x5b,0xe1},{0x0093,0x5c,0xb4},{0x0093,0x5d,0xe9},
	{0x0093,0x5e,0xaa},{0x0093,0x60,0xa9},{0x0093,0x7b,0xe2},{0x0093,0x7c,0xb5},
	{0x0093,0x7d,0xea},{0x0093,0x7e,0xb3},
	{0x00b3,0x40,0xa8},{0x00b3,0x5b,0xe1},{0x00b3,0x5c,0xb4},{0x00b3,0x5d,0xe9},
	{0x00b3,0x5e,0xaa},{0x00b3,0x60,0xa9},{0x00b3,0x7b,0xe2},{0x00b3,0x7c,0xb5},
	{0x00b3,0x7d,0xea},{0x00b3,0x7e,0xac},
	{0x0004,0x5b,0xd3},{0x0004,0x5c,0xd2},{0x0004,0x5d,0xd0},{0x0004,0x5e,0xaa},
	{0x0004,0x60,0xa9},{0x0004,0x7b,0xd7},{0x0004,0x7c,0xd6},{0x0004,0x7d,0xd4},
	{0x0004,0x7e,0xb0},
	{0x0024,0x23,0xbd},{0x0024,0x5b,0xd3},{0x0024,0x5c,0xd2},{0x0024,0x5d,0xd0},
	{0x0024,0x5e,0xaa},{0x0024,0x60,0xa9},{0x0024,0x7b,0xd7},{0x0024,0x7e,0x7c},
	{0x0024,0x7d,0xd4},{0x0024,0x7c,0xd6},
};

uint8_t symbol_glyph_byte(int symbol_set, uint8_t ch)
{
	if (symbol_set == 0x0005)
		return ch < 0x80 ? (uint8_t)(ch | 0x80) : 0;
	if (symbol_set == 0x0015 && ch >= 0x80)
		return 0;
	for (const auto &patch : kSymbolPatches)
		if (patch.symbol == symbol_set && patch.dst == ch)
			return patch.src;
	return ch;
}

uint16_t expand_raster_2x(uint8_t byte)
{
	uint16_t out = 0;
	for (int bit = 7; bit >= 0; bit--) {
		out <<= 2;
		if (byte & (1 << bit))
			out |= 0x0003;
	}
	return out;
}

uint32_t expand_raster_3x(uint8_t byte)
{
	uint32_t out = 0;
	for (int bit = 7; bit >= 0; bit--) {
		out <<= 3;
		if (byte & (1 << bit))
			out |= 0x0007;
	}
	return out << 8;
}

uint32_t expand_raster_4x(uint8_t byte)
{
	uint32_t out = 0;
	for (int bit = 7; bit >= 0; bit--) {
		out <<= 4;
		if (byte & (1 << bit))
			out |= 0x0000000f;
	}
	return out;
}

static constexpr uint16_t kRulePatterns[14][16] = {
	{0x8080,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0808,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000},
	{0x8080,0x0000,0x0000,0x0000,0x0808,0x0000,0x0000,0x0000,0x8080,0x0000,0x0000,0x0000,0x0808,0x0000,0x0000,0x0000},
	{0xc0c0,0xc0c0,0x0000,0x0000,0x0c0c,0x0c0c,0x0000,0x0000,0xc0c0,0xc0c0,0x0000,0x0000,0x0c0c,0x0c0c,0x0000,0x0000},
	{0xc1c1,0xc1c1,0x8080,0x0808,0x1c1c,0x1c1c,0x0808,0x8080,0xc1c1,0xc1c1,0x8080,0x0808,0x1c1c,0x1c1c,0x0808,0x8080},
	{0xc1c1,0xebeb,0xc1c1,0x8888,0x1c1c,0xbebe,0x1c1c,0x8888,0xc1c1,0xebeb,0xc1c1,0x8888,0x1c1c,0xbebe,0x1c1c,0x8888},
	{0xe3e3,0xe3e3,0xe3e3,0xdddd,0x3e3e,0x3e3e,0x3e3e,0xdddd,0xe3e3,0xe3e3,0xe3e3,0xdddd,0x3e3e,0x3e3e,0x3e3e,0xdddd},
	{0xf7f7,0xe3e3,0xf7f7,0xffff,0x7f7f,0x3e3e,0x7f7f,0xffff,0xf7f7,0xe3e3,0xf7f7,0xffff,0x7f7f,0x3e3e,0x7f7f,0xffff},
	{0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff},
	{0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0xffff,0xffff,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000},
	{0x0180,0x0180,0x0180,0x0180,0x0180,0x0180,0x0180,0x0180,0x0180,0x0180,0x0180,0x0180,0x0180,0x0180,0x0180,0x0180},
	{0x8003,0x0007,0x000e,0x001c,0x0038,0x0070,0x00e0,0x01c0,0x0380,0x0700,0x0e00,0x1c00,0x3800,0x7000,0xe000,0xc001},
	{0xc001,0xe000,0x7000,0x3800,0x1c00,0x0e00,0x0700,0x0380,0x01c0,0x00e0,0x0070,0x0038,0x001c,0x000e,0x0007,0x8003},
	{0x0180,0x0180,0x0180,0x0180,0x0180,0x0180,0x0180,0xffff,0xffff,0x0180,0x0180,0x0180,0x0180,0x0180,0x0180,0x0180},
	{0xc003,0xe007,0x700e,0x381c,0x1c38,0x0e70,0x07e0,0x03c0,0x03c0,0x07e0,0x0e70,0x1c38,0x381c,0x700e,0xe007,0xc003},
};

int rule_selector_for_fill_command(int mode, int fill_pattern, int orientation)
{
	if (mode == 0)
		return 7;

	if (mode == 2) {
		if (fill_pattern >= 1 && fill_pattern <= 2)
			return 0;
		if (fill_pattern >= 3 && fill_pattern <= 10)
			return 1;
		if (fill_pattern >= 11 && fill_pattern <= 20)
			return 2;
		if (fill_pattern >= 21 && fill_pattern <= 35)
			return 3;
		if (fill_pattern >= 36 && fill_pattern <= 55)
			return 4;
		if (fill_pattern >= 56 && fill_pattern <= 80)
			return 5;
		if (fill_pattern >= 81 && fill_pattern <= 99)
			return 6;
		if (fill_pattern == 100)
			return 7;
		return -1;
	}

	if (mode != 3 || fill_pattern < 1 || fill_pattern > 6)
		return -1;

	if (orientation & 1) {
		switch (fill_pattern) {
		case 1: return 9;
		case 2: return 8;
		case 3: return 11;
		case 4: return 10;
		default: break;
		}
	}
	return 7 + fill_pattern;
}

bool looks_like_ljii_font_resource_header(const std::vector<uint8_t> &payload)
{
	if (payload.size() < 8)
		return false;
	if (payload[0] != 0x00 || payload[1] != 0x01)
		return false;

	/*
	 * Nonzero ESC )s#W resource/header payloads are not raw character
	 * bitmaps. Invalid headers must drain without installing a candidate;
	 * otherwise the current character code can be polluted with header bytes.
	 */
	return true;
}

uint16_t be16_at(const std::vector<uint8_t> &payload, size_t off)
{
	if (off + 1 >= payload.size())
		return 0;
	return (uint16_t)(((uint16_t)payload[off] << 8) | payload[off + 1]);
}

} // namespace

class PclPrinter : public PrinterSim {
public:
	using PrinterSim::PrinterSim;

	void apply_config(const PrinterConfig &cfg) override;

protected:
	void parse_byte(uint8_t b) override;

private:
	enum class State {
		Normal,
		Esc,
		SubGroup,
		Parameterized,
		RasterData,
		TransparentData,
		VfcData,
		DisplayFunctions,
		DownloadData,
	};

	struct Macro {
		std::vector<uint8_t> bytes;
		bool permanent = false;
	};

	struct SoftGlyph {
		uint16_t width = 0;
		uint16_t rows = 0;
		uint16_t span = 0;
		std::vector<uint8_t> bitmap;
	};

	struct SoftFont {
		int id = 0;
		bool active = false;
		bool permanent = false;
		bool resource_header_active = false;
		uint8_t resource_type = 0;
		uint16_t resource_first = 0;
		uint16_t resource_last = 0x7f;
		int symbol_set = kSymbolRoman8;
		std::map<uint8_t, SoftGlyph> glyphs;
	};

	void reset_ljii_state();
	void process_normal(uint8_t b);
	void process_control(uint8_t b);
	void process_printable(uint8_t b);
	void process_escape(uint8_t b);
	void process_display_byte(uint8_t b);
	void emit_display_value(uint8_t b);
	void advance_fixed_space();
	void process_parameter_byte(uint8_t b);
	void apply_param(char group, char subgroup, double value, char term);
	void begin_payload(State state, int count);
	void finish_payload_byte(uint8_t b);
	void emit_transparent_byte(uint8_t b);
	void draw_rule(float x_in, float y_in, float w_in, float h_in, int selector);
	void draw_raster_row(const std::vector<uint8_t> &row);
	void draw_raster_bits(uint32_t bits, int bit_count, int x_dot, int y_dot,
	                      int row_count);
	void draw_raster_dot(int x_dot, int y_dot);
	void advance_raster_cursor_after_transfer();
	void set_raster_resolution(int dpi);
	void rebuild_default_vfc_table();
	void update_vfc_bounds();
	void apply_vfc_payload(const std::vector<uint8_t> &payload);
	void vfc_channel_jump(int selector);
	float vfc_line_y(int line) const;
	SoftFont &current_soft_font();
	SoftFont *selected_soft_font();
	const SoftFont *selected_soft_font() const;
	void delete_soft_font(int id);
	void apply_download_payload(const std::vector<uint8_t> &payload);
	bool render_soft_glyph(uint8_t b, float char_w_in);
	bool render_ljii_text(uint8_t b);
	uint16_t text_unicode(uint8_t b) const;
	uint8_t text_glyph_byte(uint8_t b) const;
	LjiiFontRequest &font_request(int slot);
	const LjiiFontRequest &font_request(int slot) const;
	LjiiFontRequest &active_font_request();
	const LjiiFontRequest &active_font_request() const;
	void sync_active_font_state();
	void ljii_line_feed();
	void set_page_size(int code);
	void set_orientation(int orientation);
	void apply_page_geometry();
	void publish_current_page();
	bool capture_macro_definition_byte(uint8_t b);
	void replay_macro(int id);

	State state_ = State::Normal;
	State payload_state_ = State::Normal;
	char group_ = 0;
	char subgroup_ = 0;
	char param_buf_[64] = {};
	int param_pos_ = 0;
	bool param_relative_ = false;
	bool current_param_relative_ = false;
	int payload_remaining_ = 0;
	bool payload_control_pending_ = false;
	bool display_escape_pending_ = false;
	bool display_control_pending_ = false;
	std::vector<uint8_t> payload_buf_;

	int line_term_ = 0;
	int orientation_ = 0;
	int page_size_code_ = 2;
	float physical_w_in_ = 8.5f;
	float physical_h_in_ = 11.0f;
	float logical_x0_in_ = 50.0f / kDotsPerIn;
	float logical_y0_in_ = 60.0f / kDotsPerIn;
	float logical_w_in_ = 8.0f;
	float logical_h_in_ = 11.0f;
	float hmi_in_ = 1.0f / 10.0f;
	float vmi_in_ = 1.0f / 6.0f;
	float text_length_in_ = 10.0f;
	LjiiFontRequest font_req_[2];
	int active_font_slot_ = 0;
	std::vector<uint16_t> vfc_table_;
	int vfc_last_line_ = 63;
	int vfc_text_last_line_ = 62;
	int pending_vfc_count_ = -1;
	int pending_raster_count_ = -1;

	int raster_resolution_ = 300;
	int raster_mode_ = 0;
	int raster_scale_ = 1;
	bool raster_active_ = false;
	float raster_x_in_ = 0.0f;
	float raster_y_in_ = 0.0f;
	int raster_row_ = 0;

	float rect_w_in_ = 0.0f;
	float rect_h_in_ = 0.0f;
	int fill_pattern_ = 0;
	int copy_count_ = 1;
	bool wrap_enabled_ = true;

	int macro_id_ = 0;
	int overlay_macro_id_ = 0;
	bool defining_macro_ = false;
	bool replaying_macro_ = false;
	bool overlay_enabled_ = false;
	size_t macro_command_start_ = 0;
	std::vector<uint8_t> macro_stop_buf_;
	std::map<int, Macro> macros_;

	int soft_font_id_ = 0;
	uint8_t soft_char_code_ = 0;
	int selected_soft_font_id_[2] = { -1, -1 };
	int download_font_slot_ = 1;
	std::map<int, SoftFont> soft_fonts_;
	std::vector<std::pair<float, float>> cursor_stack_;
};

void PclPrinter::apply_config(const PrinterConfig &cfg)
{
	PrinterSim::apply_config(cfg);
	reset_ljii_state();
}

void PclPrinter::reset_ljii_state()
{
	state_ = State::Normal;
	payload_state_ = State::Normal;
	group_ = 0;
	subgroup_ = 0;
	param_pos_ = 0;
	param_buf_[0] = 0;
	param_relative_ = false;
	current_param_relative_ = false;
	payload_remaining_ = 0;
	payload_control_pending_ = false;
	display_escape_pending_ = false;
	display_control_pending_ = false;
	payload_buf_.clear();
	line_term_ = 0;
	orientation_ = 0;
	page_size_code_ = 2;
	physical_w_in_ = 8.5f;
	physical_h_in_ = 11.0f;
	logical_x0_in_ = 50.0f / kDotsPerIn;
	logical_y0_in_ = 60.0f / kDotsPerIn;
	logical_w_in_ = 8.0f;
	logical_h_in_ = 11.0f;
	hmi_in_ = 1.0f / 10.0f;
	vmi_in_ = 1.0f / 6.0f;
	text_length_in_ = 10.0f;
	font_req_[0] = LjiiFontRequest{};
	font_req_[1] = LjiiFontRequest{};
	font_req_[1].secondary = true;
	font_req_[1].symbol_set = 0x000e;
	active_font_slot_ = 0;
	pending_vfc_count_ = -1;
	pending_raster_count_ = -1;
	raster_resolution_ = 300;
	raster_mode_ = 0;
	raster_scale_ = 1;
	raster_active_ = false;
	raster_x_in_ = 0.0f;
	raster_y_in_ = 0.0f;
	raster_row_ = 0;
	rect_w_in_ = 0.0f;
	rect_h_in_ = 0.0f;
	fill_pattern_ = 0;
	copy_count_ = 1;
	wrap_enabled_ = true;
	macro_id_ = 0;
	overlay_macro_id_ = 0;
	defining_macro_ = false;
	replaying_macro_ = false;
	overlay_enabled_ = false;
	macro_command_start_ = 0;
	macro_stop_buf_.clear();
	soft_font_id_ = 0;
	soft_char_code_ = 0;
	selected_soft_font_id_[0] = -1;
	selected_soft_font_id_[1] = -1;
	download_font_slot_ = 1;
	soft_fonts_.clear();
	cursor_stack_.clear();

	st_.pitch_cpi = 10.0f;
	st_.line_spacing_in = vmi_in_;
	st_.left_margin_in = logical_x0_in_;
	st_.right_margin_in = logical_x0_in_ + logical_w_in_;
	st_.top_margin_in = logical_y0_in_;
	st_.page_width_in = physical_w_in_;
	st_.page_height_in = physical_h_in_;
	st_.x_pos = st_.left_margin_in;
	st_.y_pos = st_.top_margin_in + st_.line_spacing_in;
	sync_active_font_state();
	update_vfc_bounds();
	rebuild_default_vfc_table();
}

void PclPrinter::parse_byte(uint8_t b)
{
	if (defining_macro_ && !replaying_macro_ && capture_macro_definition_byte(b))
		return;

	switch (state_) {
	case State::Normal:
		process_normal(b);
		return;
	case State::Esc:
		process_escape(b);
		return;
	case State::SubGroup:
		if (b >= 0x60 && b <= 0x7E) {
			subgroup_ = static_cast<char>(b);
			param_pos_ = 0;
			param_buf_[0] = 0;
			param_relative_ = false;
			state_ = State::Parameterized;
		} else if (is_param_byte(b)) {
			subgroup_ = 0;
			param_pos_ = 0;
			param_buf_[0] = 0;
			param_relative_ = false;
			state_ = State::Parameterized;
			process_parameter_byte(b);
		} else if ((b >= 'A' && b <= 'Z') || b == '@') {
			subgroup_ = 0;
			param_pos_ = 0;
			param_buf_[0] = 0;
			param_relative_ = false;
			state_ = State::Parameterized;
			process_parameter_byte(b);
		} else {
			state_ = State::Normal;
		}
		return;
	case State::Parameterized:
		process_parameter_byte(b);
		return;
	case State::RasterData:
	case State::TransparentData:
	case State::VfcData:
	case State::DownloadData:
		finish_payload_byte(b);
		return;
	case State::DisplayFunctions:
		process_display_byte(b);
		return;
	}
}

void PclPrinter::process_normal(uint8_t b)
{
	if (b == 0x1B) {
		state_ = State::Esc;
		return;
	}
	if (b < 0x20 || b == 0x7F)
		process_control(b);
	else
		process_printable(b);
}

void PclPrinter::process_control(uint8_t b)
{
	switch (b) {
	case 0x08:
		st_.x_pos = std::max(st_.left_margin_in, st_.x_pos - hmi_in_);
		break;
	case 0x09:
	{
		float rel = std::max(0.0f, st_.x_pos - st_.left_margin_in);
		float cols = rel / std::max(0.0001f, hmi_in_);
		float next = (std::floor(cols / 8.0f) + 1.0f) * 8.0f;
		st_.x_pos = st_.left_margin_in + next * hmi_in_;
		if (st_.x_pos > st_.right_margin_in)
			st_.x_pos = st_.right_margin_in;
		break;
	}
	case 0x0A:
		if (line_term_ == 2 || line_term_ == 3)
			carriage_return();
		ljii_line_feed();
		break;
	case 0x0C:
		if (line_term_ == 2 || line_term_ == 3)
			carriage_return();
		publish_current_page();
		break;
	case 0x0D:
		carriage_return();
		if (line_term_ == 1 || line_term_ == 3)
			ljii_line_feed();
		break;
	case 0x0E:
		active_font_slot_ = 1;
		sync_active_font_state();
		break;
	case 0x0F:
		active_font_slot_ = 0;
		sync_active_font_state();
		break;
	default:
		break;
	}
}

void PclPrinter::process_printable(uint8_t b)
{
	if (render_ljii_text(b))
		return;
	emit_char(b);
}

void PclPrinter::process_escape(uint8_t b)
{
	if (b == 'E') {
		publish_current_page();
		PrinterConfig cfg = cfg_;
		reset_printer_state(cfg);
		reset_ljii_state();
		state_ = State::Normal;
		return;
	}
	if (b == 'Y') {
		display_escape_pending_ = false;
		display_control_pending_ = false;
		state_ = State::DisplayFunctions;
		return;
	}
	if (b == '9') {
		st_.left_margin_in = logical_x0_in_;
		st_.right_margin_in = logical_x0_in_ + logical_w_in_;
		st_.x_pos = st_.left_margin_in;
		state_ = State::Normal;
		return;
	}
	if (b == '=') {
		new_page_if_needed();
		st_.y_pos += st_.line_spacing_in * 0.5f;
		if (st_.y_pos >= st_.page_height_in)
			publish_current_page();
		state_ = State::Normal;
		return;
	}
	if (b == 'Z' || b == 'z') {
		state_ = State::Normal;
		return;
	}
	if (b >= 0x21 && b <= 0x2F) {
		group_ = static_cast<char>(b);
		state_ = State::SubGroup;
		return;
	}
	state_ = State::Normal;
}

void PclPrinter::process_display_byte(uint8_t b)
{
	if (display_control_pending_) {
		display_control_pending_ = false;
		emit_display_value(b == 0x58 ? 0x7f : b);
		return;
	}
	if (b == 0x1a) {
		display_control_pending_ = true;
		return;
	}
	emit_display_value(b);
}

void PclPrinter::emit_display_value(uint8_t b)
{
	if (b < 0x20 || (b >= 0x80 && b <= 0x9f))
		advance_fixed_space();
	else
		process_printable(b);

	if (display_escape_pending_ && b == 'Z') {
		display_escape_pending_ = false;
		display_control_pending_ = false;
		state_ = State::Normal;
		return;
	}
	display_escape_pending_ = (b == 0x1b);
}

void PclPrinter::advance_fixed_space()
{
	const LjiiFontRequest &req = active_font_request();
	float pitch_cpi = std::max(1.0f, (float)req.pitch / 100.0f);
	float char_w_in = 1.0f / pitch_cpi;
	if (st_.x_pos + char_w_in > st_.right_margin_in + 0.001f) {
		if (!wrap_enabled_)
			return;
		carriage_return();
		ljii_line_feed();
	}
	st_.x_pos += char_w_in;
}

void PclPrinter::process_parameter_byte(uint8_t b)
{
	if (is_param_byte(b)) {
		if (param_pos_ == 0 && (b == '+' || b == '-'))
			param_relative_ = true;
		if (param_pos_ < static_cast<int>(sizeof(param_buf_)) - 1)
			param_buf_[param_pos_++] = static_cast<char>(b);
		return;
	}

	param_buf_[param_pos_] = 0;
	double value = param_pos_ > 0 ? std::atof(param_buf_) : 0.0;
	current_param_relative_ = param_relative_;

	if ((b >= 'A' && b <= 'Z') || b == '@') {
		apply_param(group_, subgroup_, value, static_cast<char>(b));
		if (state_ == State::Parameterized)
			state_ = State::Normal;
		param_relative_ = false;
	} else if (b >= 'a' && b <= 'z') {
		int lower_value = static_cast<int>(std::lround(value));
		if (group_ == '*' && subgroup_ == 'b' && b == 'w') {
			pending_raster_count_ = std::max(0, lower_value);
		} else if (group_ == '&' && subgroup_ == 'l' && b == 'w') {
			pending_vfc_count_ = std::max(0, lower_value);
		} else {
			apply_param(group_, subgroup_, value,
			            static_cast<char>(std::toupper(b)));
		}
		if (state_ == State::Parameterized) {
			param_pos_ = 0;
			param_buf_[0] = 0;
			param_relative_ = false;
		}
	} else {
		state_ = State::Normal;
		param_relative_ = false;
	}
}

void PclPrinter::apply_param(char group, char subgroup, double value, char term)
{
	int ival = static_cast<int>(std::lround(value));

	if (group == '&' && subgroup == 'l') {
		switch (term) {
		case 'A': set_page_size(ival); break;
		case 'C':
			if (value > 0.0) {
				vmi_in_ = (float)value / 48.0f;
				st_.line_spacing_in = vmi_in_;
				update_vfc_bounds();
				rebuild_default_vfc_table();
			}
			break;
		case 'D':
			if (ival == 0)
				ival = 12;
			if (ival == 1 || ival == 2 || ival == 3 || ival == 4 ||
			    ival == 6 || ival == 8 || ival == 12 || ival == 16 ||
			    ival == 24 || ival == 48) {
				vmi_in_ = 1.0f / (float)ival;
				st_.line_spacing_in = vmi_in_;
				update_vfc_bounds();
				rebuild_default_vfc_table();
			}
			break;
		case 'E':
			st_.top_margin_in = logical_y0_in_ +
			                    std::max(0.0f, (float)value * vmi_in_);
			st_.y_pos = st_.top_margin_in + st_.line_spacing_in;
			update_vfc_bounds();
			rebuild_default_vfc_table();
			break;
		case 'F':
			if (value > 0.0)
				text_length_in_ = std::max(0.0f, (float)value * vmi_in_);
			else
				text_length_in_ = std::max(0.0f, st_.page_height_in -
				                                  st_.top_margin_in -
				                                  st_.line_spacing_in);
			update_vfc_bounds();
			rebuild_default_vfc_table();
			break;
		case 'H':
			publish_current_page();
			break;
		case 'L':
			st_.perf_skip_lines = (ival == 1) ? 6 : 0;
			break;
		case 'O':
			set_orientation(ival);
			break;
		case 'P':
			if (value > 0.0) {
				physical_h_in_ = std::max(1.0f, (float)value * vmi_in_);
				set_orientation(orientation_);
				update_vfc_bounds();
				rebuild_default_vfc_table();
			} else if (ival == 0) {
				set_page_size(page_size_code_);
			}
			break;
		case 'V':
			vfc_channel_jump(ival);
			break;
		case 'W':
			if (pending_vfc_count_ >= 0) {
				ival = pending_vfc_count_;
				pending_vfc_count_ = -1;
			}
			if (ival == 0)
				rebuild_default_vfc_table();
			else
				begin_payload(State::VfcData, std::max(0, ival));
			break;
		case 'X':
			if (ival < 1)
				ival = 1;
			copy_count_ = std::min(99, ival);
			break;
		default:
			break;
		}
	} else if (group == '&' && subgroup == 'a') {
		switch (term) {
		case 'C':
			if (current_param_relative_)
				st_.x_pos += (float)value / st_.pitch_cpi;
			else
				st_.x_pos = logical_x0_in_ + (float)value / st_.pitch_cpi;
			st_.x_pos = std::max(logical_x0_in_,
			                     std::min(st_.x_pos, logical_x0_in_ + logical_w_in_));
			break;
		case 'H':
			if (current_param_relative_)
				st_.x_pos += (float)value / 720.0f;
			else
				st_.x_pos = logical_x0_in_ + (float)value / 720.0f;
			st_.x_pos = std::max(logical_x0_in_,
			                     std::min(st_.x_pos, logical_x0_in_ + logical_w_in_));
			break;
		case 'L':
			st_.left_margin_in = logical_x0_in_ +
			                     std::max(0.0f, (float)value / st_.pitch_cpi);
			st_.x_pos = std::max(st_.x_pos, st_.left_margin_in);
			break;
		case 'M':
			st_.right_margin_in = std::max(st_.left_margin_in,
			                               logical_x0_in_ +
			                               ((float)value + 1.0f) / st_.pitch_cpi);
			st_.right_margin_in = std::min(st_.right_margin_in,
			                               logical_x0_in_ + logical_w_in_);
			break;
		case 'R':
			if (current_param_relative_)
				st_.y_pos += (float)value * st_.line_spacing_in;
			else
				st_.y_pos = st_.top_margin_in +
				            (float)value * st_.line_spacing_in;
			st_.y_pos = std::max(logical_y0_in_,
			                     std::min(st_.y_pos, st_.page_height_in));
			break;
		case 'V':
			if (current_param_relative_)
				st_.y_pos += (float)value / 720.0f;
			else
				st_.y_pos = st_.top_margin_in + (float)value / 720.0f;
			st_.y_pos = std::max(logical_y0_in_,
			                     std::min(st_.y_pos, st_.page_height_in));
			break;
		default:
			break;
		}
	} else if (group == '&' && subgroup == 'd') {
		switch (term) {
		case 'D':
			st_.underline = true;
			break;
		case '@':
			st_.underline = false;
			break;
		default:
			break;
		}
	} else if (group == '&' && subgroup == 'k') {
		switch (term) {
		case 'G':
			line_term_ = std::max(0, std::min(3, ival));
			break;
		case 'H':
			if (value > 0.0) {
				hmi_in_ = (float)value / 120.0f;
				st_.pitch_cpi = 1.0f / hmi_in_;
				active_font_request().pitch =
					(int)std::lround(st_.pitch_cpi * 100.0f);
			}
			break;
		case 'S':
			if (ival == 0) {
				st_.pitch_cpi = 10.0f;
			} else if (ival == 2) {
				st_.pitch_cpi = 16.66f;
			} else if (ival == 4) {
				st_.pitch_cpi = 12.0f;
			}
			active_font_request().pitch =
				(int)std::lround(st_.pitch_cpi * 100.0f);
			hmi_in_ = 1.0f / std::max(1.0f, st_.pitch_cpi);
			break;
		default:
			break;
		}
	} else if ((group == '(' || group == ')') && subgroup == 's') {
		int slot = group == ')' ? 1 : 0;
		LjiiFontRequest &req = font_request(slot);
		switch (term) {
		case 'B':
			req.stroke = ival;
			break;
		case 'H':
			if (value > 0.0)
				req.pitch = (int)std::lround(value * 100.0);
			break;
		case 'P':
			req.spacing = ival;
			break;
		case 'S':
			req.style = ival;
			break;
		case 'T':
			req.typeface = ival;
			break;
		case 'V':
			if (value > 0.0)
				req.height = (int)std::lround(value * 100.0);
			break;
		case 'W':
			download_font_slot_ = slot;
			if (ival <= 0) {
				payload_buf_.clear();
				apply_download_payload(payload_buf_);
			} else {
				begin_payload(State::DownloadData, ival);
			}
			break;
		default:
			break;
		}
		if (slot == active_font_slot_)
			sync_active_font_state();
	} else if ((group == '(' || group == ')') && subgroup == 0) {
		int slot = group == ')' ? 1 : 0;
		if (term == '@') {
			font_request(slot) = LjiiFontRequest{};
			font_request(slot).secondary = (slot != 0);
			font_request(slot).symbol_set = slot == 0 ? kSymbolRoman8 : 0x000e;
			selected_soft_font_id_[slot] = -1;
			if (slot == active_font_slot_)
				sync_active_font_state();
		} else if (term == 'X') {
			auto it = soft_fonts_.find(ival);
			if (it != soft_fonts_.end() && it->second.active) {
				selected_soft_font_id_[slot] = ival;
				font_request(slot).symbol_set = it->second.symbol_set;
				if (slot == active_font_slot_)
					sync_active_font_state();
			}
		} else if (term >= 'A' && term <= 'Z') {
			font_request(slot).symbol_set = pcl_symbol_value(ival, term);
			selected_soft_font_id_[slot] = -1;
		}
	} else if (group == '&' && subgroup == 'p') {
		if (term == 'X')
			begin_payload(State::TransparentData, std::max(0, ival));
	} else if (group == '&' && subgroup == 's') {
		if (term == 'C') {
			if (ival == 0)
				wrap_enabled_ = true;
			else if (ival == 1)
				wrap_enabled_ = false;
		}
	} else if (group == '&' && subgroup == 'f') {
		switch (term) {
		case 'S':
			if (ival == 0) {
				if (cursor_stack_.size() < 20)
					cursor_stack_.push_back({ st_.x_pos, st_.y_pos });
			} else if (ival == 1 && !cursor_stack_.empty()) {
				st_.x_pos = cursor_stack_.back().first;
				st_.y_pos = cursor_stack_.back().second;
				cursor_stack_.pop_back();
			}
			break;
		case 'Y':
			macro_id_ = ival;
			break;
		case 'X':
			if (ival == 0) {
				defining_macro_ = true;
				macros_[macro_id_].bytes.clear();
				macro_command_start_ = 0;
				macro_stop_buf_.clear();
			} else if (ival == 1) {
				if (defining_macro_)
					macros_[macro_id_].bytes.resize(macro_command_start_);
				defining_macro_ = false;
				macro_stop_buf_.clear();
			} else if (ival == 2 || ival == 3) {
				replay_macro(macro_id_);
			} else if (ival == 4) {
				overlay_macro_id_ = macro_id_;
				overlay_enabled_ = true;
			} else if (ival == 5) {
				overlay_enabled_ = false;
			} else if (ival == 6) {
				macros_.clear();
				overlay_enabled_ = false;
			} else if (ival == 7) {
				std::vector<int> ids;
				for (const auto &entry : macros_)
					if (!entry.second.permanent)
						ids.push_back(entry.first);
				for (int id : ids)
					macros_.erase(id);
				if (macros_.find(overlay_macro_id_) == macros_.end())
					overlay_enabled_ = false;
			} else if (ival == 8) {
				macros_.erase(macro_id_);
				if (overlay_macro_id_ == macro_id_)
					overlay_enabled_ = false;
			} else if (ival == 9 || ival == 10) {
				macros_[macro_id_].permanent = (ival == 10);
			}
			break;
		default:
			break;
		}
	} else if (group == '*' && subgroup == 'p') {
		switch (term) {
		case 'X':
			if (current_param_relative_)
				st_.x_pos += (float)value / kDotsPerIn;
			else
				st_.x_pos = logical_x0_in_ + (float)value / kDotsPerIn;
			st_.x_pos = std::max(logical_x0_in_,
			                     std::min(st_.x_pos, logical_x0_in_ + logical_w_in_));
			break;
		case 'Y':
			if (current_param_relative_)
				st_.y_pos += (float)value / kDotsPerIn;
			else
				st_.y_pos = st_.top_margin_in + (float)value / kDotsPerIn;
			st_.y_pos = std::max(logical_y0_in_,
			                     std::min(st_.y_pos, st_.page_height_in));
			break;
		default:
			break;
		}
	} else if (group == '*' && subgroup == 't') {
		if (term == 'R' && ival > 0)
			set_raster_resolution(ival);
	} else if (group == '*' && subgroup == 'r') {
		switch (term) {
		case 'A':
			if (!raster_active_) {
				raster_x_in_ = (ival == 1)
					? ((orientation_ & 1) ? st_.y_pos : st_.x_pos)
					: 0.0f;
				raster_y_in_ = (orientation_ & 1) ? st_.x_pos : st_.y_pos;
				raster_row_ = 0;
				raster_active_ = true;
			}
			break;
		case 'B':
			raster_active_ = false;
			break;
		case 'S':
			break;
		case 'T':
			break;
		default:
			break;
		}
	} else if (group == '*' && subgroup == 'b') {
		switch (term) {
		case 'M':
			break;
		case 'W':
			if (pending_raster_count_ >= 0) {
				ival = pending_raster_count_;
				pending_raster_count_ = -1;
			}
			begin_payload(State::RasterData, std::max(0, ival));
			break;
		case 'Y':
			raster_row_ += std::max(0, ival) * raster_scale_;
			break;
		default:
			break;
		}
	} else if (group == '*' && subgroup == 'c') {
		switch (term) {
		case 'A':
			rect_w_in_ = std::max(0.0f, (float)value / kDotsPerIn);
			break;
		case 'B':
			rect_h_in_ = std::max(0.0f, (float)value / kDotsPerIn);
			break;
		case 'D':
			soft_font_id_ = ival;
			current_soft_font();
			break;
		case 'E':
			soft_char_code_ = (uint8_t)(ival & 0xff);
			break;
		case 'F':
			if (ival == 0 || ival == 1 || ival == 3 || ival == 6) {
				std::vector<int> ids;
				for (const auto &entry : soft_fonts_)
					if (ival == 0 || ival == 3 || ival == 6 ||
					    (ival == 1 && !entry.second.permanent))
						ids.push_back(entry.first);
				for (int id : ids)
					delete_soft_font(id);
			} else if (ival == 2) {
				delete_soft_font(soft_font_id_);
			} else if (ival == 4) {
				current_soft_font().permanent = false;
			} else if (ival == 5) {
				current_soft_font().permanent = true;
			}
			break;
		case 'G':
			fill_pattern_ = ival;
			break;
		case 'H':
			rect_w_in_ = std::max(0.0f, (float)value / 720.0f);
			break;
		case 'P': {
			int selector = rule_selector_for_fill_command(ival, fill_pattern_,
			                                              orientation_);
			if (selector >= 0)
				draw_rule(st_.x_pos, st_.y_pos, rect_w_in_, rect_h_in_,
				          selector);
			break;
		}
		case 'V':
			rect_h_in_ = std::max(0.0f, (float)value / 720.0f);
			break;
		default:
			break;
		}
	}
}

void PclPrinter::begin_payload(State state, int count)
{
	if (count <= 0) {
		state_ = State::Normal;
		return;
	}
	payload_state_ = state;
	state_ = state;
	payload_remaining_ = count;
	payload_control_pending_ = false;
	payload_buf_.clear();
	payload_buf_.reserve(static_cast<size_t>(count));
}

void PclPrinter::finish_payload_byte(uint8_t b)
{
	if (payload_state_ == State::TransparentData) {
		emit_transparent_byte(b);
	} else {
		if ((payload_state_ == State::RasterData ||
		     payload_state_ == State::DownloadData) && payload_control_pending_) {
			payload_control_pending_ = false;
			if (b == 0x58)
				b = 0x00;
		} else if ((payload_state_ == State::RasterData ||
		            payload_state_ == State::DownloadData) && b == 0x1a) {
			payload_control_pending_ = true;
			return;
		}
		payload_buf_.push_back(b);
	}

	if (--payload_remaining_ > 0)
		return;

	if (payload_state_ == State::RasterData)
		draw_raster_row(payload_buf_);
	else if (payload_state_ == State::VfcData)
		apply_vfc_payload(payload_buf_);
	else if (payload_state_ == State::DownloadData)
		apply_download_payload(payload_buf_);

	payload_buf_.clear();
	payload_state_ = State::Normal;
	payload_control_pending_ = false;
	state_ = State::Normal;
}

void PclPrinter::emit_transparent_byte(uint8_t b)
{
	if (b >= 0x20 && b != 0x7F)
		process_printable(b);
	else
		process_control(b);
}

void PclPrinter::draw_rule(float x_in, float y_in, float w_in, float h_in,
                           int selector)
{
	if (w_in <= 0.0f || h_in <= 0.0f || selector < 0 || selector > 13)
		return;
	new_page_if_needed();
	page_dirty_ = true;

	int dpi = prof_.render_dpi;
	int x0 = (int)std::floor(x_in * (float)dpi);
	int y0 = (int)std::floor(y_in * (float)dpi);
	int x1 = (int)std::ceil((x_in + w_in) * (float)dpi);
	int y1 = (int)std::ceil((y_in + h_in) * (float)dpi);
	x0 = std::max(0, x0);
	y0 = std::max(0, y0);
	x1 = std::min(page_->width(), x1);
	y1 = std::min(page_->height(), y1);

	float pattern_scale = kDotsPerIn / (float)dpi;
	for (int y = y0; y < y1; y++) {
		int pattern_y = (int)std::floor((float)y * pattern_scale) & 15;
		uint16_t row = kRulePatterns[selector][pattern_y];
		for (int x = x0; x < x1; x++) {
			int pattern_x = (int)std::floor((float)x * pattern_scale) & 15;
			if (row & (uint16_t)(0x8000u >> pattern_x))
				page_->set_pixel(x, y, 0);
		}
	}
}

void PclPrinter::draw_raster_row(const std::vector<uint8_t> &row)
{
	if (row.empty() || !raster_active_)
		return;

	int start_x_dot = (int)std::floor(raster_x_in_ * kDotsPerIn);
	int start_y_dot = (int)std::floor(raster_y_in_ * kDotsPerIn) + raster_row_;
	int page_w_dot = (int)std::floor(st_.right_margin_in * kDotsPerIn);
	int page_h_dot = (int)std::floor(st_.page_height_in * kDotsPerIn);

	if (raster_row_ < 0) {
		raster_row_ += raster_scale_;
		advance_raster_cursor_after_transfer();
		return;
	}
	if (start_y_dot > page_h_dot)
		return;

	int dots_per_byte = 8 * std::max(1, raster_scale_);
	int remaining_dots = page_w_dot - start_x_dot;
	int accepted = remaining_dots > 0
		? (remaining_dots + dots_per_byte - 1) / dots_per_byte
		: 0;
	accepted = std::max(0, std::min(accepted, (int)row.size()));
	if (accepted <= 0) {
		raster_row_ += raster_scale_;
		advance_raster_cursor_after_transfer();
		return;
	}

	std::vector<uint8_t> accepted_row(row.begin(), row.begin() + accepted);
	new_page_if_needed();
	page_dirty_ = true;

	if (raster_mode_ == 0) {
		int x_dot = 0;
		for (uint8_t byte : accepted_row) {
			draw_raster_bits(byte, 8, x_dot, 0, 1);
			x_dot += 8;
		}
	} else if (raster_mode_ == 1) {
		int x_dot = 0;
		for (uint8_t byte : accepted_row) {
			draw_raster_bits(expand_raster_2x(byte), 16, x_dot, 0, 2);
			x_dot += 16;
		}
	} else if (raster_mode_ == 2) {
		for (size_t i = 0; i < accepted_row.size(); i += 2)
			draw_raster_bits(expand_raster_3x(accepted_row[i]), 32,
			                 (int)(i / 2) * 48, 0, 3);
		for (size_t i = 1; i < accepted_row.size(); i += 2)
			draw_raster_bits(expand_raster_3x(accepted_row[i]), 32,
			                 (int)(i / 2) * 48 + 16, 0, 3);
	} else {
		int x_dot = 0;
		for (uint8_t byte : accepted_row) {
			draw_raster_bits(expand_raster_4x(byte), 32, x_dot, 0, 4);
			x_dot += 32;
		}
	}
	raster_row_ += raster_scale_;
	advance_raster_cursor_after_transfer();
}

void PclPrinter::draw_raster_bits(uint32_t bits, int bit_count, int x_dot,
                                  int y_dot, int row_count)
{
	for (int bit = bit_count - 1; bit >= 0; bit--, x_dot++) {
		if (!(bits & (1u << bit)))
			continue;
		for (int row = 0; row < row_count; row++)
			draw_raster_dot(x_dot, y_dot + row);
	}
}

void PclPrinter::draw_raster_dot(int x_dot, int y_dot)
{
	int dpi = prof_.render_dpi;
	float base_x = raster_x_in_ * kDotsPerIn + (float)x_dot;
	float base_y = raster_y_in_ * kDotsPerIn + (float)raster_row_ + (float)y_dot;
	int x0 = (int)std::floor(base_x * (float)dpi / kDotsPerIn);
	int y0 = (int)std::floor(base_y * (float)dpi / kDotsPerIn);
	int x1 = (int)std::ceil((base_x + 1.0f) * (float)dpi / kDotsPerIn);
	int y1 = (int)std::ceil((base_y + 1.0f) * (float)dpi / kDotsPerIn);
	x1 = std::max(x1, x0 + 1);
	y1 = std::max(y1, y0 + 1);
	for (int y = y0; y < y1; y++)
		for (int x = x0; x < x1; x++)
			page_->set_pixel(x, y, 0);
}

void PclPrinter::advance_raster_cursor_after_transfer()
{
	float step = (float)raster_scale_ / kDotsPerIn;
	if (orientation_ & 1) {
		st_.x_pos = std::max(0.0f, st_.x_pos - step);
		st_.y_pos = std::max(logical_y0_in_,
		                     std::min(raster_x_in_, st_.page_height_in));
	} else {
		st_.x_pos = raster_x_in_;
		st_.y_pos = std::max(logical_y0_in_,
		                     std::min(st_.y_pos + step, st_.page_height_in));
	}
}

void PclPrinter::set_raster_resolution(int dpi)
{
	if (raster_active_)
		return;
	raster_resolution_ = dpi;
	if (dpi >= 300) {
		raster_mode_ = 0;
		raster_scale_ = 1;
	} else if (dpi >= 150) {
		raster_mode_ = 1;
		raster_scale_ = 2;
	} else if (dpi >= 100) {
		raster_mode_ = 2;
		raster_scale_ = 3;
	} else {
		raster_mode_ = 3;
		raster_scale_ = 4;
	}
}

void PclPrinter::rebuild_default_vfc_table()
{
	vfc_table_.assign(128, 0);
	int text_last = std::max(0, std::min(127, vfc_text_last_line_));
	int last = std::max(text_last, std::min(127, vfc_last_line_));

	auto set_channel = [this](int line, int channel) {
		if (line < 0 || line >= 128 || channel <= 0 || channel > 16)
			return;
		vfc_table_[(size_t)line] |= (uint16_t)(1u << (channel - 1));
	};

	set_channel(0, 1);
	set_channel(std::max(0, text_last - 1), 2);
	set_channel(text_last, 2);
	for (int line = 0; line <= text_last; line++)
		set_channel(line, 3);
	set_channel(last, 3);
	for (int line = 0; line <= last; line++) {
		if ((line % 2) == 0) set_channel(line, 4);
		if ((line % 3) == 0) set_channel(line, 5);
		if ((line % 10) == 0) set_channel(line, 8);
		if ((line % 7) == 0) set_channel(line, 13);
		if ((line % 6) == 0) set_channel(line, 14);
		if ((line % 5) == 0) set_channel(line, 15);
		if ((line % 4) == 0) set_channel(line, 16);
	}
	set_channel(0, 6);
	set_channel(text_last / 2, 6);
	set_channel(0, 7);
	set_channel(text_last / 4, 7);
	set_channel(text_last / 2, 7);
	set_channel((text_last * 3) / 4, 7);
	set_channel(text_last, 9);
	set_channel(0, 12);
}

void PclPrinter::update_vfc_bounds()
{
	float line0 = st_.top_margin_in + st_.line_spacing_in;
	float available = std::max(0.0f, st_.page_height_in - line0);
	float vmi = std::max(1.0f / 300.0f, vmi_in_);
	vfc_last_line_ = std::max(0, std::min(127, (int)std::floor(available / vmi)));

	float text_available = std::max(0.0f, text_length_in_ - st_.line_spacing_in);
	vfc_text_last_line_ = std::max(0, std::min(vfc_last_line_,
	                                           (int)std::floor(text_available / vmi)));
}

void PclPrinter::apply_vfc_payload(const std::vector<uint8_t> &payload)
{
	if (payload.empty()) {
		rebuild_default_vfc_table();
		return;
	}
	if ((payload.size() & 1) || payload.size() > 256 ||
	    payload.size() > (size_t)(vfc_last_line_ + 1) * 2)
		return;

	if (vfc_table_.size() != 128)
		vfc_table_.assign(128, 0);
	std::fill(vfc_table_.begin(), vfc_table_.end(), 0);
	for (size_t i = 0; i + 1 < payload.size(); i += 2) {
		uint16_t word = (uint16_t)(((uint16_t)payload[i] << 8) | payload[i + 1]);
		vfc_table_[i / 2] = word;
	}
}

float PclPrinter::vfc_line_y(int line) const
{
	return st_.top_margin_in + st_.line_spacing_in * (float)(line + 1);
}

void PclPrinter::vfc_channel_jump(int selector)
{
	if (vfc_table_.size() != 128)
		rebuild_default_vfc_table();

	float vmi = std::max(1.0f / 300.0f, vmi_in_);
	int current = (int)std::floor((st_.y_pos - vfc_line_y(0)) / vmi + 0.0001f);
	int start = std::max(0, current + 1);

	if (selector <= 0) {
		if (current > 0 && page_ && page_dirty_)
			publish_current_page();
		st_.x_pos = st_.left_margin_in;
		st_.y_pos = vfc_line_y(0);
		return;
	}

	uint16_t mask = (selector <= 16) ? (uint16_t)(1u << (selector - 1)) : 0;
	if (mask == 0)
		return;

	int last = std::max(0, std::min(127, vfc_last_line_));
	int target = -1;
	bool wrapped = false;
	for (int line = start; line <= last; line++) {
		if (vfc_table_[(size_t)line] & mask) {
			target = line;
			break;
		}
	}
	if (target < 0) {
		for (int line = 0; line < std::min(start, last + 1); line++) {
			if (vfc_table_[(size_t)line] & mask) {
				target = line;
				wrapped = true;
				break;
			}
		}
	}

	if (target < 0) {
		if (current >= 0 && current <= last && page_ && page_dirty_)
			publish_current_page();
		target = 0;
		wrapped = true;
	}

	if (wrapped && current >= 0 && current <= last && page_ && page_dirty_)
		publish_current_page();
	st_.x_pos = st_.left_margin_in;
	st_.y_pos = vfc_line_y(target);
}

PclPrinter::SoftFont &PclPrinter::current_soft_font()
{
	SoftFont &font = soft_fonts_[soft_font_id_];
	font.id = soft_font_id_;
	return font;
}

PclPrinter::SoftFont *PclPrinter::selected_soft_font()
{
	int id = selected_soft_font_id_[active_font_slot_ ? 1 : 0];
	if (id >= 0) {
		auto it = soft_fonts_.find(id);
		if (it != soft_fonts_.end() && it->second.active)
			return &it->second;
		return nullptr;
	}

	SoftFont *fallback = nullptr;
	for (auto &entry : soft_fonts_) {
		if (!entry.second.active)
			continue;
		if (entry.second.glyphs.empty())
			continue;
		if (fallback)
			return nullptr;
		fallback = &entry.second;
	}
	return fallback;
}

const PclPrinter::SoftFont *PclPrinter::selected_soft_font() const
{
	int id = selected_soft_font_id_[active_font_slot_ ? 1 : 0];
	if (id >= 0) {
		auto it = soft_fonts_.find(id);
		if (it != soft_fonts_.end() && it->second.active)
			return &it->second;
		return nullptr;
	}

	const SoftFont *fallback = nullptr;
	for (const auto &entry : soft_fonts_) {
		if (!entry.second.active)
			continue;
		if (entry.second.glyphs.empty())
			continue;
		if (fallback)
			return nullptr;
		fallback = &entry.second;
	}
	return fallback;
}

void PclPrinter::delete_soft_font(int id)
{
	soft_fonts_.erase(id);
	for (int &selected : selected_soft_font_id_)
		if (selected == id)
			selected = -1;
}

void PclPrinter::apply_download_payload(const std::vector<uint8_t> &payload)
{
	SoftFont &font = current_soft_font();
	font.active = true;
	if (payload.empty()) {
		if (selected_soft_font_id_[download_font_slot_ ? 1 : 0] < 0)
			selected_soft_font_id_[download_font_slot_ ? 1 : 0] = font.id;
		return;
	}

	if (looks_like_ljii_font_resource_header(payload)) {
		if (payload.size() < 64)
			return;
		uint8_t resource_type = payload[3];
		uint16_t first = be16_at(payload, 6);
		uint16_t line_count = be16_at(payload, 8);
		uint16_t last = be16_at(payload, 10);
		uint8_t font_class = payload[12];
		if (resource_type > 2 || first > 0x1067 ||
		    line_count == 0 || line_count > 0x1068 ||
		    last == 0 || last > 0x1068 || first > (uint16_t)(last - 1) ||
		    font_class > 1)
			return;

		font.resource_header_active = true;
		font.resource_type = resource_type;
		font.resource_first = first;
		font.resource_last = resource_type == 0 ? 0x007f : 0x00ff;
		font.symbol_set = be16_at(payload, 14);
		if (font.symbol_set == 0)
			font.symbol_set = kSymbolRoman8;
		if (download_font_slot_ == 0 || download_font_slot_ == 1) {
			LjiiFontRequest &req = font_request(download_font_slot_);
			req.symbol_set = font.symbol_set;
			req.spacing = payload[13] ? 1 : 0;
			uint16_t pitch = be16_at(payload, 16);
			uint16_t height = be16_at(payload, 18);
			if (pitch > 0)
				req.pitch = std::min<int>(pitch, 0x41a0);
			if (height > 0)
				req.height = std::min<int>(height, 0x2aaa);
		}
		if (selected_soft_font_id_[download_font_slot_ ? 1 : 0] < 0)
			selected_soft_font_id_[download_font_slot_ ? 1 : 0] = font.id;
		return;
	}

	if (payload.size() >= 64 && soft_char_code_ <= 0x20) {
		if (payload.size() > 0x23) {
			int symbol = ((int)payload[0x22] << 8) | payload[0x23];
			if (symbol != 0)
				font.symbol_set = symbol;
		}
		if (download_font_slot_ == 0 || download_font_slot_ == 1) {
			LjiiFontRequest &req = font_request(download_font_slot_);
			req.symbol_set = font.symbol_set;
			if (payload.size() > 0x21)
				req.spacing = payload[0x21] ? 1 : 0;
			if (payload.size() > 0x31)
				req.stroke = (int)(int8_t)payload[0x30];
			if (payload.size() > 0x2a) {
				int pitch = ((int)payload[0x24] << 8) | payload[0x25];
				int height = ((int)payload[0x28] << 8) | payload[0x29];
				if (pitch > 0)
					req.pitch = pitch;
				if (height > 0)
					req.height = height;
			}
		}
		return;
	}

	SoftGlyph glyph;
	if (payload.size() >= 14 && payload[4] == 0x0c &&
	    (payload[5] == 1 || payload[5] == 2)) {
		glyph.rows = (uint16_t)std::max(1, ((int)payload[6] << 8) | payload[7]);
		glyph.width = (uint16_t)std::max(1, ((int)payload[8] << 8) | payload[9]);
		glyph.span = (uint16_t)std::max(1, (int)((glyph.width + 7) >> 3));
		glyph.bitmap.assign(payload.begin() + 12, payload.end());
	} else if (payload.size() >= 6 && payload[4] == 0x0c &&
	           payload[5] != 1 && payload[5] != 2) {
		return;
	} else if (font.resource_header_active && payload.size() == 3) {
		glyph.width = 4;
		glyph.span = 1;
		glyph.rows = 3;
	} else if (payload.size() == 18) {
		glyph.width = 144;
		glyph.span = 18;
		glyph.rows = 1;
	} else if (payload.size() == 32 || payload.size() == 64 ||
	           payload.size() == 128 || payload.size() == 256 ||
	           payload.size() == 258 || payload.size() == 260 ||
	           payload.size() == 516) {
		glyph.width = 16;
		glyph.span = 2;
		glyph.rows = (uint16_t)std::max<size_t>(1, payload.size() / 2);
	} else if (payload.size() == 387 || payload.size() == 2193) {
		glyph.span = payload.size() == 387 ? 3 : 17;
		glyph.width = (uint16_t)(glyph.span * 8);
		glyph.rows = (uint16_t)(payload.size() / glyph.span);
	} else if ((payload.size() & 1) == 0) {
		glyph.width = 16;
		glyph.span = 2;
		glyph.rows = (uint16_t)std::max<size_t>(1, payload.size() / 2);
	} else {
		glyph.width = 8;
		glyph.span = 1;
		glyph.rows = (uint16_t)std::max<size_t>(1, payload.size());
	}
	if (glyph.bitmap.empty())
		glyph.bitmap = payload;

	size_t expected = (size_t)glyph.rows * glyph.span;
	if ((glyph.span & 1) && glyph.span > 1 && glyph.bitmap.size() == expected) {
		uint16_t prefix_span = glyph.span - 1;
		size_t trailing_base = (size_t)glyph.rows * prefix_span;
		std::vector<uint8_t> interleaved(expected);
		for (uint16_t row = 0; row < glyph.rows; row++) {
			size_t src = (size_t)row * prefix_span;
			size_t dst = (size_t)row * glyph.span;
			std::copy_n(glyph.bitmap.begin() +
			            (std::vector<uint8_t>::difference_type)src,
			            prefix_span,
			            interleaved.begin() +
			            (std::vector<uint8_t>::difference_type)dst);
			interleaved[dst + prefix_span] =
				glyph.bitmap[trailing_base + row];
		}
		glyph.bitmap = std::move(interleaved);
	}
	if (glyph.bitmap.size() < expected && !glyph.bitmap.empty()) {
		size_t rows = glyph.bitmap.size() / std::max<uint16_t>(1, glyph.span);
		if (rows == 0) {
			glyph.span = 1;
			glyph.width = 8;
			rows = glyph.bitmap.size();
		}
		glyph.rows = (uint16_t)std::min<size_t>(rows, 0xffff);
	}
	font.glyphs[soft_char_code_] = std::move(glyph);
	if (selected_soft_font_id_[download_font_slot_ ? 1 : 0] < 0)
		selected_soft_font_id_[download_font_slot_ ? 1 : 0] = font.id;
}

bool PclPrinter::render_soft_glyph(uint8_t b, float char_w_in)
{
	SoftFont *font = selected_soft_font();
	if (!font)
		return false;
	auto it = font->glyphs.find(b);
	if (it == font->glyphs.end())
		return false;

	if (st_.x_pos + char_w_in > st_.right_margin_in + 0.001f) {
		if (!wrap_enabled_)
			return true;
		carriage_return();
		ljii_line_feed();
	}

	new_page_if_needed();
	page_dirty_ = true;

	const SoftGlyph &glyph = it->second;
	int dpi = prof_.render_dpi;
	int base_x = (int)std::lround(st_.x_pos * (float)dpi);
	int base_y = (int)std::lround(st_.y_pos * (float)dpi) - (int)glyph.rows;
	for (uint16_t row = 0; row < glyph.rows; row++) {
		size_t row_off = (size_t)row * glyph.span;
		if (row_off >= glyph.bitmap.size())
			break;
		int italic_shift = st_.italic ? (int)(glyph.rows - 1 - row) / 6 : 0;
		for (uint16_t col = 0; col < glyph.width; col++) {
			size_t byte_off = row_off + (col >> 3);
			if (byte_off >= glyph.bitmap.size())
				continue;
			uint8_t byte = glyph.bitmap[byte_off];
			if (byte & (0x80u >> (col & 7)))
				page_->set_pixel(base_x + col + italic_shift, base_y + row, 0);
		}
	}

	uint16_t cp = text_unicode(b);
	if (cp >= 0x20) {
		uint8_t sty = 0;
		if (st_.bold) sty |= TextGlyph::BOLD;
		if (st_.underline) sty |= TextGlyph::UNDERLINE;
		text_buf_.push_back({
			st_.x_pos, st_.y_pos, cp, char_w_in,
			char_w_in * 72.0f / 0.6f, sty
		});
	}
	st_.x_pos += char_w_in;
	mark_line_output(true);
	return true;
}

bool PclPrinter::render_ljii_text(uint8_t b)
{
	const LjiiFontRequest &req = active_font_request();
	float pitch_cpi = std::max(1.0f, (float)req.pitch / 100.0f);
	float char_w_in = 1.0f / pitch_cpi;
	if (b == 0x20) {
		if (st_.x_pos + char_w_in > st_.right_margin_in + 0.001f) {
			if (!wrap_enabled_)
				return true;
			carriage_return();
			ljii_line_feed();
		}
		text_buf_.push_back({
			st_.x_pos, st_.y_pos, 0x20, char_w_in,
			char_w_in * 72.0f / 0.6f, 0
		});
		if (st_.underline) {
			new_page_if_needed();
			page_dirty_ = true;
			int dpi = prof_.render_dpi;
			int y = (int)std::lround((st_.y_pos + 1.0f / 72.0f) * (float)dpi);
			int x0 = (int)std::lround(st_.x_pos * (float)dpi);
			int x1 = (int)std::lround((st_.x_pos + char_w_in) * (float)dpi);
			for (int x = x0; x < x1; x++) {
				page_->set_pixel(x, y, 0);
				page_->set_pixel(x, y + 1, 0);
			}
		}
		st_.x_pos += char_w_in;
		return true;
	}

	if (render_soft_glyph(b, char_w_in))
		return true;

	uint8_t glyph_byte = text_glyph_byte(b);
	if (glyph_byte == 0)
		return true;
	uint32_t context = select_ljii_context(req);
	LjiiGlyphInfo glyph = get_ljii_glyph(context, glyph_byte);
	if (!glyph.found || !glyph.data) {
		uint16_t cp = text_unicode(b);
		if (cp >= 0x20) {
			uint8_t sty = 0;
			if (st_.bold) sty |= TextGlyph::BOLD;
			if (st_.underline) sty |= TextGlyph::UNDERLINE;
			text_buf_.push_back({
				st_.x_pos, st_.y_pos, cp, char_w_in,
				char_w_in * 72.0f / 0.6f, sty
			});
		}
		st_.x_pos += char_w_in;
		mark_line_output(true);
		return true;
	}

	if (st_.x_pos + char_w_in > st_.right_margin_in + 0.001f) {
		if (!wrap_enabled_)
			return true;
		carriage_return();
		ljii_line_feed();
	}

	new_page_if_needed();
	page_dirty_ = true;

	int dpi = prof_.render_dpi;
	int base_x = (int)std::lround(st_.x_pos * (float)dpi) + glyph.x_offset;
	int base_y = (int)std::lround(st_.y_pos * (float)dpi) - glyph.y_offset;
	for (uint8_t row = 0; row < glyph.rows; row++) {
		const uint8_t *src = glyph.data + (size_t)row * glyph.span;
		int italic_shift = st_.italic ? (int)(glyph.rows - 1 - row) / 6 : 0;
		for (uint8_t col = 0; col < glyph.width; col++) {
			uint8_t byte = src[col >> 3];
			if (byte & (0x80u >> (col & 7)))
				page_->set_pixel(base_x + col + italic_shift, base_y + row, 0);
		}
	}
	if (st_.underline) {
		int y = (int)std::lround((st_.y_pos + 1.0f / 72.0f) * (float)dpi);
		int x0 = (int)std::lround(st_.x_pos * (float)dpi);
		int x1 = (int)std::lround((st_.x_pos + char_w_in) * (float)dpi);
		for (int x = x0; x < x1; x++) {
			page_->set_pixel(x, y, 0);
			page_->set_pixel(x, y + 1, 0);
		}
	}

	uint16_t cp = text_unicode(b);
	if (cp >= 0x20) {
		uint8_t sty = 0;
		if (st_.bold) sty |= TextGlyph::BOLD;
		if (st_.underline) sty |= TextGlyph::UNDERLINE;
		text_buf_.push_back({
			st_.x_pos, st_.y_pos, cp, char_w_in,
			char_w_in * 72.0f / 0.6f, sty
		});
	}
	st_.x_pos += char_w_in;
	mark_line_output(true);
	return true;
}

uint8_t PclPrinter::text_glyph_byte(uint8_t b) const
{
	int symbol_set = active_font_request().symbol_set;
	if (symbol_set == kSymbolRoman8 || symbol_set == 0)
		return b;
	return symbol_glyph_byte(symbol_set, b);
}

uint16_t PclPrinter::text_unicode(uint8_t b) const
{
	if (b == 0x7f)
		return 0;
	if (b < 0x80)
		return b;
	return roman8_to_unicode(b);
}

LjiiFontRequest &PclPrinter::font_request(int slot)
{
	return font_req_[slot ? 1 : 0];
}

const LjiiFontRequest &PclPrinter::font_request(int slot) const
{
	return font_req_[slot ? 1 : 0];
}

LjiiFontRequest &PclPrinter::active_font_request()
{
	return font_request(active_font_slot_);
}

const LjiiFontRequest &PclPrinter::active_font_request() const
{
	return font_request(active_font_slot_);
}

void PclPrinter::sync_active_font_state()
{
	const LjiiFontRequest &req = active_font_request();
	st_.pitch_cpi = std::max(1.0f, (float)req.pitch / 100.0f);
	hmi_in_ = 1.0f / st_.pitch_cpi;
	st_.proportional = (req.spacing != 0);
	st_.italic = (req.style == 1);
	st_.bold = (req.stroke >= 3);
}

void PclPrinter::ljii_line_feed()
{
	flush_pending_line();
	new_page_if_needed();
	page_dirty_ = true;

	st_.y_pos += st_.line_spacing_in;
	float bottom = st_.perf_skip_lines > 0
		? st_.page_height_in -
		  static_cast<float>(st_.perf_skip_lines) * st_.line_spacing_in
		: st_.page_height_in - 0.5f;
	if (st_.y_pos >= bottom)
		publish_current_page();

	advance_line_direction();
	finish_printed_line();
}

void PclPrinter::set_page_size(int code)
{
	publish_current_page();
	page_size_code_ = code;
	apply_page_geometry();
}

void PclPrinter::set_orientation(int orientation)
{
	if (orientation < 0 || orientation > 1 || orientation == orientation_)
		return;
	publish_current_page();
	orientation_ = orientation;
	apply_page_geometry();
}

void PclPrinter::apply_page_geometry()
{
	PageGeometry geom = pcl_page_geometry(page_size_code_, orientation_);
	physical_w_in_ = dots_to_in(geom.physical_w);
	physical_h_in_ = dots_to_in(geom.physical_h);
	logical_x0_in_ = dots_to_in(geom.left);
	logical_y0_in_ = dots_to_in(geom.top);
	logical_w_in_ = dots_to_in(geom.logical_w);
	logical_h_in_ = dots_to_in(geom.logical_h);
	st_.page_width_in = physical_w_in_;
	st_.page_height_in = physical_h_in_;
	st_.left_margin_in = logical_x0_in_;
	st_.right_margin_in = logical_x0_in_ + logical_w_in_;
	st_.top_margin_in = logical_y0_in_;
	st_.x_pos = st_.left_margin_in;
	st_.y_pos = st_.top_margin_in + st_.line_spacing_in;
	update_vfc_bounds();
	rebuild_default_vfc_table();
}

void PclPrinter::publish_current_page()
{
	if (overlay_enabled_ && !replaying_macro_ &&
	    macros_.find(overlay_macro_id_) != macros_.end())
		replay_macro(overlay_macro_id_);
	flush_pending_line();
	if (page_ && page_dirty_) {
		int copies = std::max(1, copy_count_);
		for (int i = 0; i < copies; i++)
			pdf_.add_page(*page_, prof_.render_dpi, text_buf_);
		text_buf_.clear();
		page_dirty_ = false;
	}
	page_.reset();
}

bool PclPrinter::capture_macro_definition_byte(uint8_t b)
{
	if (b == 0x1B) {
		macro_command_start_ = macros_[macro_id_].bytes.size();
		macro_stop_buf_.clear();
		macro_stop_buf_.push_back(b);
		macros_[macro_id_].bytes.push_back(b);
		return true;
	}

	macros_[macro_id_].bytes.push_back(b);
	if (macro_stop_buf_.empty())
		return true;

	macro_stop_buf_.push_back(b);
	size_t len = macro_stop_buf_.size();
	if (len == 2 && b != '&') {
		macro_stop_buf_.clear();
		return true;
	}
	if (len == 3 && b != 'f') {
		macro_stop_buf_.clear();
		return true;
	}
	if (len >= 4) {
		if (b == 'X') {
			int value = 0;
			bool have_digit = false;
			for (size_t i = 3; i + 1 < len; i++) {
				uint8_t ch = macro_stop_buf_[i];
				if (ch < '0' || ch > '9') {
					macro_stop_buf_.clear();
					return true;
				}
				have_digit = true;
				value = value * 10 + (ch - '0');
			}
			if (have_digit && value == 1) {
				macros_[macro_id_].bytes.resize(macro_command_start_);
				defining_macro_ = false;
			}
			macro_stop_buf_.clear();
			return true;
		}
		if (b < '0' || b > '9')
			macro_stop_buf_.clear();
	}
	return true;
}

void PclPrinter::replay_macro(int id)
{
	if (replaying_macro_)
		return;
	auto it = macros_.find(id);
	if (it == macros_.end())
		return;
	replaying_macro_ = true;
	const std::vector<uint8_t> bytes = it->second.bytes;
	state_ = State::Normal;
	for (uint8_t byte : bytes)
		parse_byte(byte);
	state_ = State::Normal;
	replaying_macro_ = false;
}

std::unique_ptr<PrinterSim> create_pcl_printer(PrinterModel model, PdfWriter &pdf)
{
	return std::make_unique<PclPrinter>(model, pdf);
}
