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

bool pcl_page_size_selector_valid(int code)
{
	switch (code) {
	case 1:
	case 2:
	case 3:
	case 26:
	case 80:
	case 81:
	case 90:
	case 91:
		return true;
	default:
		return false;
	}
}

int pcl_page_size_for_length_dots(int dots, int orientation)
{
	dots = std::abs(dots);
	if ((orientation & 1) == 0) {
		if (dots <= 3150)
			return 1;
		if (dots <= 3300)
			return 2;
		if (dots <= 3507)
			return 26;
		if (dots <= 4200)
			return 3;
		return 0;
	}

	if (dots <= 2175)
		return 1;
	if (dots <= 2480)
		return 26;
	if (dots <= 2550)
		return 2;
	return 0;
}

float dots_to_in(int dots)
{
	return (float)dots / kDotsPerIn;
}

int pcl_integer_word(double value)
{
	return (int)std::floor(std::abs(value) + 0.000001);
}

int pcl_signed_integer_word(double value)
{
	int word = pcl_integer_word(value);
	return value < 0.0 ? -word : word;
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
	if (term >= '@' && term <= '^')
		return value * 32 + (term - '@');
	if (term >= 'a' && term <= 'z')
		return value * 32 + (term - '`');
	return value;
}

int ljii_default_symbol_word(int slot, int orientation)
{
	if (slot != 0)
		return 0x000e;
	return (orientation & 1) ? 0x0155 : 0x0005;
}

int ljii_default_font_symbol_word()
{
	return 0x000e;
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

bool ljii_symbol_word_has_map(int symbol_set)
{
	if (symbol_set == 0 || symbol_set == kSymbolRoman8 ||
	    symbol_set == 0x0005 || symbol_set == 0x000e ||
	    symbol_set == 0x0015)
		return true;
	for (const auto &patch : kSymbolPatches)
		if (patch.symbol == symbol_set)
			return true;
	return false;
}

int ljii_effective_map_symbol_word(const LjiiFontRequest &req)
{
	if (ljii_symbol_word_has_map(req.symbol_set))
		return req.symbol_set;
	return req.secondary ? 0x000e : kSymbolRoman8;
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

float ljii_rect_decipoints_to_in(double value)
{
	if (value <= 0.0)
		return 0.0f;
	int subunits = (int)std::ceil(value * 5.0 - 0.000001) + 11;
	return (float)subunits / (12.0f * kDotsPerIn);
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
	void flush() override;

protected:
	void parse_byte(uint8_t b) override;

private:
	enum class State {
		Normal,
		Esc,
		EscQuestion,
		SubGroup,
		Parameterized,
		RasterData,
		TransparentData,
		VfcData,
		DrainData,
		DisplayFunctions,
		DownloadData,
		DownloadDescriptorData,
		ControlZ,
		StatusQuery,
	};

	struct Macro {
		std::vector<uint8_t> bytes;
		bool permanent = false;
	};

	enum class MacroReplayMode {
		Execute,
		Call,
		Overlay
	};

	struct MacroPrintEnvironment {
		PrinterState st;
		int orientation = 0;
		int page_size_code = 2;
		float physical_w_in = 8.5f;
		float physical_h_in = 11.0f;
		float logical_x0_in = 50.0f / kDotsPerIn;
		float logical_y0_in = 60.0f / kDotsPerIn;
		float logical_w_in = 8.0f;
		float logical_h_in = 11.0f;
		float hmi_in = 1.0f / 10.0f;
		float vmi_in = 1.0f / 6.0f;
		float text_length_in = 10.0f;
		bool text_length_custom = false;
		float vfc_limit_in = 10.0f;
		LjiiFontRequest font_req[2];
		int active_font_slot = 0;
		std::vector<uint16_t> vfc_table;
		int vfc_last_line = 63;
		int vfc_text_last_line = 62;
		int underline_selector = 0;
		bool pending_cursor_y = false;
		int raster_resolution = 300;
		int raster_mode = 0;
		int raster_scale = 1;
		bool raster_active = false;
		float raster_x_in = 0.0f;
		int raster_row = 0;
		float rect_w_in = 0.0f;
		float rect_h_in = 0.0f;
		int fill_pattern = 0;
		int copy_count = 1;
		bool wrap_enabled = false;
		int selected_soft_font_id[2] = { -1, -1 };
		int download_font_slot = 1;
		std::vector<std::pair<float, float>> cursor_stack;
		bool previous_width_pending = false;
		float previous_text_width_in = 0.0f;
		float previous_text_advance_in = 0.0f;
	};

	struct SoftGlyph {
		uint16_t width = 0;
		uint16_t rows = 0;
		uint16_t span = 0;
		bool split_plane = false;
		bool unresolved_pixels = false;
		std::vector<uint8_t> bitmap;
	};

	struct SoftFont {
		int id = 0;
		bool active = false;
		bool permanent = false;
		bool has_request_metrics = false;
		bool has_pitch_metric = false;
		bool has_height_metric = false;
		bool has_style_metric = false;
		bool has_stroke_metric = false;
		bool has_typeface_metric = false;
		bool continuation_active = false;
		uint8_t continuation_char = 0;
		size_t continuation_offset = 0;
		size_t continuation_remaining = 0;
		bool resource_header_active = false;
		uint8_t resource_type = 0;
		bool resource_extended_chars = false;
		bool fixed_record_extended_chars = false;
		uint16_t resource_first = 0;
		uint16_t resource_last = 0x7f;
		int symbol_set = kSymbolRoman8;
		int spacing = 0;
		int pitch = 1000;
		int height = 1200;
		int style = 0;
		int stroke = 0;
		int typeface = 0;
		std::map<uint8_t, SoftGlyph> glyphs;
	};

	void reset_ljii_state();
	void software_reset();
	void process_normal(uint8_t b);
	void process_control(uint8_t b);
	void process_printable(uint8_t b);
	void process_escape(uint8_t b);
	void process_display_byte(uint8_t b);
	void emit_display_value(uint8_t b);
	bool payload_control_normal_branch();
	void advance_fixed_space();
	bool control_filter_routes_printable() const;
	bool selected_context_routes_parser_printable() const;
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
	void restore_default_text_length();
	void update_vfc_bounds();
	void apply_vfc_payload(const std::vector<uint8_t> &payload);
	void vfc_channel_jump(int selector);
	float vfc_line_y(int line) const;
	float vfc_bottom_recovery_y(int target_line) const;
	SoftFont &current_soft_font();
	SoftFont *selected_soft_font();
	const SoftFont *selected_soft_font() const;
	const SoftFont *selected_soft_font_candidate() const;
	void delete_soft_font(int id);
	void refresh_soft_font_request(const SoftFont &font);
	void release_fixed_record_glyph(SoftFont &font, uint8_t ch);
	size_t soft_glyph_bitmap_index(const SoftGlyph &glyph, uint16_t row,
	                               uint16_t byte_col) const;
	uint8_t soft_glyph_bitmap_byte(const SoftGlyph &glyph, uint16_t row,
	                               uint16_t byte_col) const;
	size_t copy_soft_glyph_host_bytes(SoftGlyph &glyph, size_t host_offset,
	                                  std::vector<uint8_t>::const_iterator first,
	                                  std::vector<uint8_t>::const_iterator last);
	void apply_download_descriptor_payload(const std::vector<uint8_t> &payload);
	void apply_download_payload(const std::vector<uint8_t> &payload);
	void draw_soft_glyph_pixels(const SoftGlyph &glyph);
	bool render_soft_glyph(uint8_t b, float char_w_in);
	bool render_ljii_text(uint8_t b);
	void ensure_text_page();
	void append_ljii_text_glyph(uint16_t cp, float char_w_in);
	bool ljii_text_box_accepts(float top_in, float bottom_in) const;
	bool ljii_nominal_text_vertical_accepts() const;
	bool ljii_resident_glyph_vertical_accepts(const LjiiGlyphInfo &glyph) const;
	bool ljii_soft_glyph_vertical_accepts(const SoftGlyph &glyph) const;
	float ljii_metric_width_in(uint8_t width, float fallback_in) const;
	bool consume_previous_width_adjustment(float current_width_in);
	void finish_text_advance(float width_in, float advance_in, bool had_pending);
	void refresh_pending_cursor_y();
	void clear_pending_cursor_y();
	void start_underline_span();
	void flush_underline_span();
	void restart_underline_span();
	void draw_underline_range(float x0_in, float x1_in, float y_in,
	                          int selector);
	float underline_y_in(float y_in, int selector) const;
	void ljii_carriage_return();
	uint16_t text_unicode(uint8_t b) const;
	uint8_t text_glyph_byte_for(const LjiiFontRequest &req, uint8_t b) const;
	uint8_t text_glyph_byte(uint8_t b) const;
	bool apply_builtin_font_id(int slot, int id);
	LjiiFontRequest &font_request(int slot);
	const LjiiFontRequest &font_request(int slot) const;
	LjiiFontRequest &active_font_request();
	const LjiiFontRequest &active_font_request() const;
	void sync_active_font_state();
	bool ljii_perforation_overflow_check();
	void ljii_line_feed();
	void set_page_size(int code);
	void set_orientation(int orientation);
	void apply_page_geometry();
	void set_page_length(float length_in);
	void ensure_page_root();
	void publish_current_page();
	void start_macro_definition(bool lowercase_final);
	void finish_macro_definition(size_t keep_size);
	void append_macro_definition_byte(uint8_t b);
	void append_macro_display_byte(uint8_t b);
	bool parse_macro_payload_echo_command(const std::vector<uint8_t> &cmd,
	                                      int &count,
	                                      bool &lowercase_final,
	                                      char &expected_final) const;
	bool parse_macro_generic_drain_command(const std::vector<uint8_t> &cmd,
	                                       int &count,
	                                       bool &lowercase_final) const;
	bool capture_macro_payload_chain_byte(uint8_t b);
	bool capture_macro_definition_byte(uint8_t b);
	MacroPrintEnvironment capture_print_environment() const;
	void restore_print_environment(const MacroPrintEnvironment &env);
	void replay_macro(int id, MacroReplayMode mode);

	State state_ = State::Normal;
	State payload_state_ = State::Normal;
	char group_ = 0;
	char subgroup_ = 0;
	char param_buf_[64] = {};
	int param_pos_ = 0;
	bool param_relative_ = false;
	bool current_param_relative_ = false;
	bool current_param_explicit_ = false;
	int payload_remaining_ = 0;
	bool payload_control_pending_ = false;
	int payload_control_counter_ = 0;
	bool download_payload_control_seen_ = false;
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
	bool text_length_custom_ = false;
	float vfc_limit_in_ = 10.0f;
	LjiiFontRequest font_req_[2];
	int active_font_slot_ = 0;
	std::vector<uint16_t> vfc_table_;
	int vfc_last_line_ = 63;
	int vfc_text_last_line_ = 62;
	int pending_vfc_count_ = -1;
	int pending_transparent_count_ = -1;
	int pending_raster_count_ = -1;
	int pending_drain_count_ = -1;
	int pending_download_count_ = -1;
	bool page_root_active_ = false;
	bool underline_span_active_ = false;
	float underline_span_x0_in_ = 0.0f;
	float underline_span_y_in_ = 0.0f;
	int underline_span_selector_ = 0;
	int underline_selector_ = 0;
	bool pending_cursor_y_ = true;

	int raster_resolution_ = 300;
	int raster_mode_ = 0;
	int raster_scale_ = 1;
	bool raster_active_ = false;
	float raster_x_in_ = 0.0f;
	float raster_transfer_y_in_ = 0.0f;
	int raster_row_ = 0;

	float rect_w_in_ = 0.0f;
	float rect_h_in_ = 0.0f;
	int fill_pattern_ = 0;
	int copy_count_ = 1;
	bool wrap_enabled_ = false;

	int macro_id_ = 0;
	int overlay_macro_id_ = 0;
	bool defining_macro_ = false;
	bool replaying_macro_ = false;
	int macro_replay_depth_ = 0;
	bool overlay_enabled_ = false;
	bool macro_chain_active_ = false;
	bool macro_display_capture_ = false;
	bool macro_display_escape_pending_ = false;
	bool macro_display_control_pending_ = false;
	bool macro_control_pending_ = false;
	int macro_payload_echo_remaining_ = 0;
	bool macro_payload_echo_control_pending_ = false;
	int macro_payload_drain_remaining_ = 0;
	bool macro_payload_drain_control_pending_ = false;
	bool macro_payload_chain_active_ = false;
	bool macro_payload_chain_drain_ = false;
	int macro_payload_chain_count_ = -1;
	char macro_payload_chain_final_ = 0;
	std::string macro_payload_chain_param_;
	size_t macro_command_start_ = 0;
	std::vector<uint8_t> macro_stop_buf_;
	std::map<int, Macro> macros_;

	int soft_font_id_ = 0;
	uint8_t soft_char_code_ = 0;
	int selected_soft_font_id_[2] = { -1, -1 };
	int download_font_slot_ = 1;
	std::map<int, SoftFont> soft_fonts_;
	std::vector<std::pair<float, float>> cursor_stack_;
	bool previous_width_pending_ = false;
	float previous_text_width_in_ = 0.0f;
	float previous_text_advance_in_ = 0.0f;
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
	current_param_explicit_ = false;
	payload_remaining_ = 0;
	payload_control_pending_ = false;
	payload_control_counter_ = 0;
	download_payload_control_seen_ = false;
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
	text_length_custom_ = false;
	vfc_limit_in_ = logical_y0_in_ + text_length_in_;
	font_req_[0] = LjiiFontRequest{};
	font_req_[1] = LjiiFontRequest{};
	font_req_[1].secondary = true;
	font_req_[1].symbol_set = 0x000e;
	active_font_slot_ = 0;
	pending_vfc_count_ = -1;
	pending_transparent_count_ = -1;
	pending_raster_count_ = -1;
	pending_drain_count_ = -1;
	pending_download_count_ = -1;
	page_root_active_ = false;
	underline_span_active_ = false;
	underline_span_x0_in_ = 0.0f;
	underline_span_y_in_ = 0.0f;
	underline_span_selector_ = 0;
	underline_selector_ = 0;
	pending_cursor_y_ = true;
	raster_resolution_ = 75;
	raster_mode_ = 3;
	raster_scale_ = 4;
	raster_active_ = false;
	raster_x_in_ = 0.0f;
	raster_transfer_y_in_ = 0.0f;
	raster_row_ = 0;
	rect_w_in_ = 0.0f;
	rect_h_in_ = 0.0f;
	fill_pattern_ = 0;
	copy_count_ = 1;
	wrap_enabled_ = false;
	st_.perf_skip_lines = 6;
	macro_id_ = 0;
	overlay_macro_id_ = 0;
	defining_macro_ = false;
	replaying_macro_ = false;
	macro_replay_depth_ = 0;
	overlay_enabled_ = false;
	macro_chain_active_ = false;
	macro_display_capture_ = false;
	macro_display_escape_pending_ = false;
	macro_display_control_pending_ = false;
	macro_control_pending_ = false;
	macro_payload_echo_remaining_ = 0;
	macro_payload_echo_control_pending_ = false;
	macro_payload_drain_remaining_ = 0;
	macro_payload_drain_control_pending_ = false;
	macro_payload_chain_active_ = false;
	macro_payload_chain_drain_ = false;
	macro_payload_chain_count_ = -1;
	macro_payload_chain_final_ = 0;
	macro_payload_chain_param_.clear();
	macro_command_start_ = 0;
	macro_stop_buf_.clear();
	soft_font_id_ = 0;
	soft_char_code_ = 0;
	selected_soft_font_id_[0] = -1;
	selected_soft_font_id_[1] = -1;
	download_font_slot_ = 1;
	soft_fonts_.clear();
	cursor_stack_.clear();
	previous_width_pending_ = false;
	previous_text_width_in_ = hmi_in_;
	previous_text_advance_in_ = hmi_in_;

	st_.pitch_cpi = 10.0f;
	st_.line_spacing_in = vmi_in_;
	st_.left_margin_in = logical_x0_in_;
	st_.right_margin_in = logical_x0_in_ + logical_w_in_;
	st_.top_margin_in = logical_y0_in_;
	st_.page_width_in = physical_w_in_;
	st_.page_height_in = physical_h_in_;
	st_.x_pos = st_.left_margin_in;
	st_.y_pos = st_.top_margin_in + st_.line_spacing_in;
	pending_cursor_y_ = true;
	sync_active_font_state();
	update_vfc_bounds();
	rebuild_default_vfc_table();
}

void PclPrinter::software_reset()
{
	publish_current_page();
	std::map<int, Macro> permanent_macros;
	for (const auto &entry : macros_)
		if (entry.second.permanent)
			permanent_macros.emplace(entry.first, entry.second);
	std::map<int, SoftFont> permanent_fonts;
	for (const auto &entry : soft_fonts_)
		if (entry.second.permanent)
			permanent_fonts.emplace(entry.first, entry.second);
	PrinterConfig cfg = cfg_;
	reset_printer_state(cfg);
	reset_ljii_state();
	macros_ = std::move(permanent_macros);
	soft_fonts_ = std::move(permanent_fonts);
	state_ = State::Normal;
}

void PclPrinter::parse_byte(uint8_t b)
{
	if (defining_macro_ && !replaying_macro_) {
		if (macro_chain_active_ && state_ == State::Parameterized) {
			bool chain_byte = is_param_byte(b) ||
			                  (b >= '@' && b <= '^') ||
			                  (b >= 'a' && b <= 'z');
			if (chain_byte) {
				process_parameter_byte(b);
				if (!defining_macro_ || state_ != State::Parameterized)
					macro_chain_active_ = false;
				return;
			}
			macro_chain_active_ = false;
		}
		if (capture_macro_definition_byte(b))
			return;
	}

	switch (state_) {
	case State::Normal:
		process_normal(b);
		return;
	case State::Esc:
		process_escape(b);
		return;
	case State::EscQuestion:
		state_ = State::Normal;
		if (b != 0x11)
			process_normal(b);
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
		} else if (b >= '@' && b <= '^') {
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
	case State::DrainData:
	case State::DownloadData:
	case State::DownloadDescriptorData:
		finish_payload_byte(b);
		return;
	case State::DisplayFunctions:
		process_display_byte(b);
		return;
	case State::ControlZ:
		if (b == 0x1A) {
			if (selected_context_routes_parser_printable())
				process_printable(0x1a);
		} else if (b == 0x58) {
			if (!payload_control_normal_branch())
				process_printable(0x7f);
		}
		state_ = State::Normal;
		return;
	case State::StatusQuery:
		state_ = State::Normal;
		return;
	}
}

void PclPrinter::process_normal(uint8_t b)
{
	if (b == 0x1B) {
		state_ = State::Esc;
		return;
	}
	if (b == 0x1A) {
		state_ = State::ControlZ;
		return;
	}
	if ((b & 0x7f) >= 0x20)
		process_printable(b);
	else
		process_control(b);
}

void PclPrinter::process_control(uint8_t b)
{
	switch (b) {
	case 0x00:
	case 0x07:
	case 0x0B:
		break;
	case 0x08:
		flush_underline_span();
	{
		float current_x = st_.x_pos;
		float distance = st_.proportional ? previous_text_width_in_ : hmi_in_;
		float candidate = current_x - distance;
		if (current_x >= st_.left_margin_in && candidate < st_.left_margin_in)
			candidate = st_.left_margin_in;
		if (candidate < 0.0f)
			candidate = 0.0f;
		st_.x_pos = candidate;
		previous_width_pending_ = true;
		clear_pending_cursor_y();
		restart_underline_span();
		break;
	}
	case 0x09:
	{
		if (hmi_in_ <= 0.0f)
			break;
		if (st_.x_pos < st_.left_margin_in) {
			st_.x_pos = st_.left_margin_in;
			clear_pending_cursor_y();
			break;
		}
		float rel = std::max(0.0f, st_.x_pos - st_.left_margin_in);
		float cols = rel / hmi_in_;
		float next = (std::floor(cols / 8.0f) + 1.0f) * 8.0f;
		float limit = (st_.x_pos > st_.right_margin_in) ?
		              st_.page_width_in : st_.right_margin_in;
		st_.x_pos = st_.left_margin_in + next * hmi_in_;
		if (st_.x_pos > limit)
			st_.x_pos = limit;
		clear_pending_cursor_y();
		break;
	}
	case 0x0A:
		if (line_term_ == 2 || line_term_ == 3)
			ljii_carriage_return();
		ljii_line_feed();
		break;
	case 0x0C:
		if (line_term_ == 2 || line_term_ == 3)
			ljii_carriage_return();
		ensure_page_root();
		publish_current_page();
		break;
	case 0x0D:
		ljii_carriage_return();
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
		if (selected_context_routes_parser_printable())
			process_printable(b);
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
		software_reset();
		return;
	}
	if (b == 'Y') {
		display_escape_pending_ = false;
		display_control_pending_ = false;
		state_ = State::DisplayFunctions;
		return;
	}
	if (b == '9') {
		flush_underline_span();
		st_.left_margin_in = 0.0f;
		st_.right_margin_in = st_.page_width_in;
		restart_underline_span();
		state_ = State::Normal;
		return;
	}
	if (b == '=') {
		flush_underline_span();
		new_page_if_needed();
		st_.y_pos += st_.line_spacing_in * 0.5f;
		clear_pending_cursor_y();
		ljii_perforation_overflow_check();
		restart_underline_span();
		state_ = State::Normal;
		return;
	}
	if (b == 'Z' || b == 'z') {
		state_ = State::Normal;
		return;
	}
	if (b == '?') {
		state_ = State::EscQuestion;
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
		if (b == 0x58) {
			payload_control_normal_branch();
			emit_display_value(0x7f);
		} else {
			emit_display_value(b);
		}
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
	bool filtered = b < 0x20 || (b >= 0x80 && b <= 0x9f);
	if (filtered && !control_filter_routes_printable())
		advance_fixed_space();
	else
		process_printable(b);

	if (b == 0x0d) {
		ljii_carriage_return();
		ljii_line_feed();
	}

	if (display_escape_pending_ && b == 'Z') {
		display_escape_pending_ = false;
		display_control_pending_ = false;
		state_ = State::Normal;
		return;
	}
	display_escape_pending_ = (b == 0x1b);
}

bool PclPrinter::control_filter_routes_printable() const
{
	const LjiiFontRequest &req = active_font_request();
	int symbol_set = ljii_effective_map_symbol_word(req);
	return symbol_set != 0 && symbol_set != kSymbolRoman8;
}

bool PclPrinter::selected_context_routes_parser_printable() const
{
	const LjiiFontRequest &req = active_font_request();
	int symbol_set = ljii_effective_map_symbol_word(req);
	return symbol_set != 0 && symbol_set != kSymbolRoman8;
}

bool PclPrinter::payload_control_normal_branch()
{
	new_page_if_needed();
	if (++payload_control_counter_ <= 0xff)
		return false;
	payload_control_counter_ = 0;
	publish_current_page();
	return true;
}

void PclPrinter::advance_fixed_space()
{
	refresh_pending_cursor_y();
	float char_w_in = hmi_in_;
	bool had_pending = consume_previous_width_adjustment(char_w_in);
	if (st_.x_pos + char_w_in > st_.right_margin_in + 0.001f) {
		if (!wrap_enabled_) {
			finish_text_advance(char_w_in, char_w_in, had_pending);
			return;
		}
		ljii_carriage_return();
		ljii_line_feed();
	}
	if (!ljii_nominal_text_vertical_accepts()) {
		finish_text_advance(char_w_in, char_w_in, had_pending);
		return;
	}
	start_underline_span();
	append_ljii_text_glyph(0x20, char_w_in);
	if (st_.underline) {
		new_page_if_needed();
		page_dirty_ = true;
		int dpi = prof_.render_dpi;
		int y = (int)std::lround(underline_y_in(st_.y_pos,
		                                        underline_selector_) *
		                         (float)dpi);
		int x0 = (int)std::lround(st_.x_pos * (float)dpi);
		int x1 = (int)std::lround((st_.x_pos + char_w_in) * (float)dpi);
		for (int x = x0; x < x1; x++) {
			page_->set_pixel(x, y, 0);
			page_->set_pixel(x, y + 1, 0);
		}
	}
	finish_text_advance(char_w_in, char_w_in, had_pending);
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
	current_param_explicit_ = param_pos_ > 0;
	double value = param_pos_ > 0 ? std::atof(param_buf_) : 0.0;
	int ival = static_cast<int>(std::lround(value));
	current_param_relative_ = param_relative_;

	if (b >= '@' && b <= '^') {
		if (b == 'W' &&
		    !((group_ == '&' && subgroup_ == 'l') ||
		      (group_ == '*' && subgroup_ == 'b') ||
		      ((group_ == '(' || group_ == ')') && subgroup_ == 's'))) {
			if (pending_drain_count_ >= 0) {
				ival = pending_drain_count_;
				pending_drain_count_ = -1;
			} else {
				ival = pcl_integer_word(value);
			}
			begin_payload(State::DrainData, ival);
		} else {
			apply_param(group_, subgroup_, value, static_cast<char>(b));
		}
		if (state_ == State::Parameterized)
			state_ = State::Normal;
		param_relative_ = false;
	} else if (b >= 'a' && b <= 'z') {
		if (group_ == '*' && subgroup_ == 'b' && b == 'w') {
			pending_raster_count_ = pcl_integer_word(value);
		} else if (group_ == '*' && subgroup_ == 'b') {
			state_ = State::Normal;
			param_relative_ = false;
			return;
		} else if (group_ == '*' && subgroup_ == 'r' &&
		           b != 'a' && b != 'b') {
			state_ = State::Normal;
			param_relative_ = false;
			return;
		} else if (group_ == '&' && subgroup_ == 'p' && b == 'x') {
			pending_transparent_count_ = pcl_integer_word(value);
		} else if (group_ == '&' && subgroup_ == 'l' && b == 'w') {
			pending_vfc_count_ = pcl_integer_word(value);
		} else if ((group_ == '(' || group_ == ')') &&
		           subgroup_ == 's' && b == 'w') {
			pending_download_count_ = pcl_integer_word(value);
		} else if (b == 'w' &&
		           !(((group_ == '(' || group_ == ')') && subgroup_ == 's'))) {
			pending_drain_count_ = pcl_integer_word(value);
		} else {
			char term = static_cast<char>(std::toupper(b));
			if (group_ == '&' && subgroup_ == 'f' && b == 'x')
				term = 'x';
			apply_param(group_, subgroup_, value, term);
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
		case 'A':
			if (current_param_explicit_)
				set_page_size(pcl_integer_word(value));
			else
				set_page_size(2);
			break;
		case 'C':
			value = std::abs(value);
			if (pcl_integer_word(value) <= 0x150) {
				float new_vmi = (float)value / 48.0f;
				if (new_vmi > st_.page_height_in + 0.0001f)
					break;
				vmi_in_ = new_vmi;
				st_.line_spacing_in = vmi_in_;
				refresh_pending_cursor_y();
				update_vfc_bounds();
				rebuild_default_vfc_table();
			}
			break;
		case 'D':
			ival = pcl_integer_word(value);
			if (ival == 0)
				ival = 12;
			if (ival == 1 || ival == 2 || ival == 3 || ival == 4 ||
			    ival == 6 || ival == 8 || ival == 12 || ival == 16 ||
			    ival == 24 || ival == 48) {
				float new_vmi = 1.0f / (float)ival;
				if (new_vmi > st_.page_height_in + 0.0001f)
					break;
				vmi_in_ = new_vmi;
				st_.line_spacing_in = vmi_in_;
				refresh_pending_cursor_y();
				update_vfc_bounds();
				rebuild_default_vfc_table();
			}
			break;
		case 'E':
			value = std::abs(value);
		{
			float new_top = logical_y0_in_ + (float)value * vmi_in_;
			if (vmi_in_ <= 0.0f || new_top >= st_.page_height_in - 0.0001f)
				break;
			flush_underline_span();
			st_.top_margin_in = new_top;
			if (pending_cursor_y_)
				refresh_pending_cursor_y();
			else
				st_.y_pos = st_.top_margin_in + st_.line_spacing_in;
			restore_default_text_length();
			update_vfc_bounds();
			rebuild_default_vfc_table();
			restart_underline_span();
			break;
		}
		case 'F':
			value = std::abs(value);
			if (value > 0.0) {
				float text_length = (float)value * vmi_in_;
				if (vmi_in_ <= 0.0f ||
				    text_length > st_.page_height_in - st_.top_margin_in +
				                  0.0001f)
					break;
				text_length_in_ = std::max(0.0f, text_length);
				text_length_custom_ = true;
			} else {
				restore_default_text_length();
			}
			update_vfc_bounds();
			rebuild_default_vfc_table();
			break;
		case 'H':
			publish_current_page();
			break;
		case 'L':
			ival = pcl_integer_word(value);
			if (ival == 0)
				st_.perf_skip_lines = 0;
			else if (ival == 1)
				st_.perf_skip_lines = 6;
			break;
		case 'O':
			set_orientation(pcl_integer_word(value));
			break;
		case 'P':
			ival = pcl_integer_word(value);
			if (ival > 0) {
				if (vmi_in_ <= 0.0f)
					break;
				int length_dots = (int)std::lround((float)ival * vmi_in_ *
				                                  kDotsPerIn);
				int code = pcl_page_size_for_length_dots(length_dots,
				                                         orientation_);
				if (code != 0) {
					publish_current_page();
					page_size_code_ = code;
					apply_page_geometry();
					set_page_length((float)length_dots / kDotsPerIn);
				}
			} else {
				set_page_size(2);
			}
			break;
		case 'V':
			vfc_channel_jump(pcl_integer_word(value));
			break;
		case 'W':
			if (pending_vfc_count_ >= 0) {
				ival = pending_vfc_count_;
				pending_vfc_count_ = -1;
			} else {
				ival = pcl_integer_word(value);
			}
			if (ival == 0)
				apply_vfc_payload(std::vector<uint8_t>());
			else
				begin_payload(State::VfcData, ival);
			break;
		case 'X':
			ival = pcl_integer_word(value);
			if (ival > 0)
				copy_count_ = std::min(99, ival);
			break;
		default:
			break;
		}
	} else if (group == '&' && subgroup == 'a') {
		switch (term) {
		case 'C':
			flush_underline_span();
			if (current_param_relative_)
				st_.x_pos += (float)value * hmi_in_;
			else
				st_.x_pos = (float)value * hmi_in_;
			st_.x_pos = std::max(0.0f,
			                     std::min(st_.x_pos, st_.page_width_in));
			clear_pending_cursor_y();
			restart_underline_span();
			break;
		case 'H':
			flush_underline_span();
			if (current_param_relative_)
				st_.x_pos += (float)value / 720.0f;
			else
				st_.x_pos = (float)value / 720.0f;
			st_.x_pos = std::max(0.0f,
			                     std::min(st_.x_pos, st_.page_width_in));
			clear_pending_cursor_y();
			restart_underline_span();
			break;
		case 'L':
			value = std::abs(value);
		{
			float new_left = logical_x0_in_ +
			                 std::max(0.0f, (float)value * hmi_in_);
			if (new_left > st_.right_margin_in - hmi_in_ + 0.0001f)
				break;
			flush_underline_span();
			st_.left_margin_in = new_left;
			st_.x_pos = std::max(st_.x_pos, st_.left_margin_in);
			clear_pending_cursor_y();
			restart_underline_span();
			break;
		}
		case 'M':
			value = std::abs(value);
		{
			float new_right = logical_x0_in_ +
			                  ((float)value + 1.0f) * hmi_in_;
			if (new_right < st_.left_margin_in + hmi_in_ - 0.0001f)
				break;
			flush_underline_span();
			st_.right_margin_in = std::min(new_right,
			                               logical_x0_in_ + logical_w_in_);
			if (st_.x_pos > st_.right_margin_in)
				st_.x_pos = st_.right_margin_in;
			clear_pending_cursor_y();
			restart_underline_span();
			break;
		}
		case 'R':
			flush_underline_span();
			if (current_param_relative_)
				st_.y_pos += (float)value * st_.line_spacing_in;
			else
				st_.y_pos = st_.top_margin_in +
				            ((float)value - 7.0f / 25.0f) *
				            st_.line_spacing_in;
			st_.y_pos = std::max(logical_y0_in_,
			                     std::min(st_.y_pos, st_.page_height_in));
			clear_pending_cursor_y();
			restart_underline_span();
			break;
		case 'V':
			flush_underline_span();
			if (current_param_relative_)
				st_.y_pos += (float)value / 720.0f;
			else
				st_.y_pos = st_.top_margin_in + (float)value / 720.0f;
			st_.y_pos = std::max(logical_y0_in_,
			                     std::min(st_.y_pos, st_.page_height_in));
			clear_pending_cursor_y();
			restart_underline_span();
			break;
		default:
			break;
		}
	} else if (group == '&' && subgroup == 'd') {
		switch (term) {
		case 'D':
			ival = pcl_integer_word(value);
			flush_underline_span();
			st_.underline = true;
			underline_selector_ = (ival == 3) ? 1 : 0;
			start_underline_span();
			break;
		case '@':
			flush_underline_span();
			st_.underline = false;
			underline_selector_ = 0;
			break;
		default:
			break;
		}
	} else if (group == '&' && subgroup == 'k') {
		switch (term) {
		case 'G':
			ival = pcl_integer_word(value);
			if (ival <= 3)
				line_term_ = ival;
			break;
		case 'H':
			value = std::abs(value);
			if (pcl_integer_word(value) <= 0x348) {
				hmi_in_ = (float)value / 120.0f;
				if (hmi_in_ > 0.0f)
					st_.pitch_cpi = 1.0f / hmi_in_;
			}
			break;
		case 'S':
			ival = pcl_integer_word(value);
			if (ival == 0) {
				st_.pitch_cpi = 10.0f;
			} else if (ival == 2) {
				st_.pitch_cpi = 16.66f;
			} else if (ival == 4) {
				st_.pitch_cpi = 12.0f;
			} else {
				break;
			}
			active_font_request().pitch =
				(int)std::lround(st_.pitch_cpi * 100.0f);
			sync_active_font_state();
			break;
		default:
			break;
		}
	} else if ((group == '(' || group == ')') && subgroup == 's') {
		int slot = group == ')' ? 1 : 0;
		LjiiFontRequest &req = font_request(slot);
		switch (term) {
		case 'B':
			req.stroke = std::max(-7, std::min(7,
			                                   pcl_signed_integer_word(value)));
			break;
		case 'H':
			value = std::abs(value);
			req.pitch = (int)std::lround(std::min(655.0, value) * 100.0);
			break;
		case 'P':
			ival = pcl_integer_word(value);
			if (ival < 2)
				req.spacing = ival;
			break;
		case 'S':
			req.style = std::min(255, pcl_integer_word(value));
			break;
		case 'T':
			req.typeface = std::min(255, pcl_integer_word(value));
			break;
		case 'V':
			value = std::abs(value);
			req.height = (int)std::lround(std::min(655.0, value) * 100.0);
			break;
		case 'W':
			download_font_slot_ = slot;
		{
			int final_count = pcl_integer_word(value);
			if (pending_download_count_ >= 0) {
				ival = pending_download_count_;
				pending_download_count_ = -1;
			} else {
				ival = final_count;
			}
			if (final_count == 0) {
				begin_payload(State::DownloadDescriptorData,
				              ival > 0 ? ival : 3);
			} else if (ival > 0x7fff) {
				begin_payload(State::DrainData, 0x7fff);
			} else {
				begin_payload(State::DownloadData, ival);
			}
			break;
		}
		default:
			break;
		}
		if (slot == active_font_slot_)
			sync_active_font_state();
	} else if ((group == '(' || group == ')') && subgroup == 0) {
		int slot = group == ')' ? 1 : 0;
		if (term == '@') {
			ival = pcl_integer_word(value);
			if (ival == 0) {
				font_request(slot).symbol_set =
					ljii_default_symbol_word(slot, orientation_);
				selected_soft_font_id_[slot] = -1;
				if (slot == active_font_slot_)
					sync_active_font_state();
			} else if (ival == 1) {
				font_request(slot).symbol_set =
					ljii_default_symbol_word(0, orientation_);
				selected_soft_font_id_[slot] = -1;
				if (slot == active_font_slot_)
					sync_active_font_state();
			} else if (ival == 2) {
				if (slot != 0) {
					font_request(slot).symbol_set =
						font_request(0).symbol_set;
					selected_soft_font_id_[slot] = -1;
					if (slot == active_font_slot_)
						sync_active_font_state();
				}
			} else if (ival == 3) {
				font_request(slot) = LjiiFontRequest{};
				font_request(slot).secondary = (slot != 0);
				font_request(slot).symbol_set =
					ljii_default_font_symbol_word();
				selected_soft_font_id_[slot] = -1;
				if (slot == active_font_slot_)
					sync_active_font_state();
			}
		} else if (term == 'X') {
			ival = pcl_integer_word(value);
			auto it = soft_fonts_.find(ival);
			if (it != soft_fonts_.end() && it->second.active) {
				selected_soft_font_id_[slot] = ival;
				font_request(slot).symbol_set = it->second.symbol_set;
				if (slot == active_font_slot_)
					sync_active_font_state();
			} else if (apply_builtin_font_id(slot, ival)) {
				selected_soft_font_id_[slot] = -1;
				if (slot == active_font_slot_)
					sync_active_font_state();
			}
		} else if (term > '@' && term <= '^') {
			ival = pcl_integer_word(value);
			if (ival <= 0x07ff) {
				font_request(slot).symbol_set = pcl_symbol_value(ival, term);
				selected_soft_font_id_[slot] = -1;
				if (slot == active_font_slot_)
					sync_active_font_state();
			}
		}
	} else if (group == '&' && subgroup == 'p') {
		if (term == 'X') {
			if (pending_transparent_count_ >= 0) {
				ival = pending_transparent_count_;
				pending_transparent_count_ = -1;
			} else {
				ival = pcl_integer_word(value);
			}
			begin_payload(State::TransparentData, ival);
		}
	} else if (group == '&' && subgroup == 's') {
		if (term == 'C') {
			ival = pcl_integer_word(value);
			if (ival == 0)
				wrap_enabled_ = true;
			else if (ival == 1)
				wrap_enabled_ = false;
		}
	} else if (group == '&' && subgroup == 'f') {
		switch (term) {
		case 'S':
			ival = pcl_integer_word(value);
			if (ival == 0) {
				if (cursor_stack_.size() < 20)
					cursor_stack_.push_back({ st_.x_pos, st_.y_pos });
			} else if (ival == 1 && !cursor_stack_.empty()) {
				flush_underline_span();
				st_.x_pos = cursor_stack_.back().first;
				st_.y_pos = cursor_stack_.back().second;
				cursor_stack_.pop_back();
				const float guard_in = 1.0f / 12.0f;
				float max_x = std::max(0.0f,
				                       st_.page_width_in - guard_in);
				float max_y = std::max(logical_y0_in_,
				                       st_.page_height_in - guard_in);
				st_.x_pos = std::max(0.0f,
				                     std::min(st_.x_pos, max_x));
				st_.y_pos = std::max(logical_y0_in_,
				                     std::min(st_.y_pos, max_y));
				clear_pending_cursor_y();
				restart_underline_span();
			}
			break;
		case 'Y':
			macro_id_ = pcl_integer_word(value);
			break;
		case 'X':
		case 'x':
			ival = pcl_integer_word(value);
			if (defining_macro_ && ival != 1)
				break;
			if (replaying_macro_ && ival != 2 && ival != 3)
				break;
			if (ival == 0) {
				start_macro_definition(term == 'x');
			} else if (ival == 1) {
				if (defining_macro_)
					finish_macro_definition(macro_stop_buf_.empty()
					                        ? macros_[macro_id_].bytes.size()
					                        : macro_command_start_);
				else
					macro_stop_buf_.clear();
			} else if (ival == 2) {
				replay_macro(macro_id_, MacroReplayMode::Execute);
			} else if (ival == 3) {
				replay_macro(macro_id_, MacroReplayMode::Call);
			} else if (ival == 4) {
				auto it = macros_.find(macro_id_);
				if (it != macros_.end() && !it->second.bytes.empty()) {
					overlay_macro_id_ = macro_id_;
					overlay_enabled_ = true;
				} else {
					overlay_enabled_ = false;
				}
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
				auto it = macros_.find(macro_id_);
				if (it != macros_.end() && !it->second.bytes.empty())
					it->second.permanent = (ival == 10);
			}
			break;
		default:
			break;
		}
	} else if (group == '*' && subgroup == 'p') {
		switch (term) {
		case 'X':
		{
			int word = pcl_signed_integer_word(value);
			flush_underline_span();
			if (current_param_relative_)
				st_.x_pos += (float)word / kDotsPerIn;
			else
				st_.x_pos = (float)word / kDotsPerIn;
			st_.x_pos = std::max(0.0f,
			                     std::min(st_.x_pos, st_.page_width_in));
			clear_pending_cursor_y();
			restart_underline_span();
			break;
		}
		case 'Y':
		{
			int word = pcl_signed_integer_word(value);
			flush_underline_span();
			if (current_param_relative_)
				st_.y_pos += (float)word / kDotsPerIn;
			else
				st_.y_pos = st_.top_margin_in + (float)word / kDotsPerIn;
			st_.y_pos = std::max(logical_y0_in_,
			                     std::min(st_.y_pos, st_.page_height_in));
			clear_pending_cursor_y();
			restart_underline_span();
			break;
		}
		default:
			break;
		}
	} else if (group == '*' && subgroup == 't') {
		if (term == 'R')
			set_raster_resolution(pcl_integer_word(value));
	} else if (group == '*' && subgroup == 'r') {
		switch (term) {
		case 'A':
			if (!raster_active_) {
				int start_selector = pcl_integer_word(value);
				raster_x_in_ = (start_selector == 1)
					? ((orientation_ & 1) ? st_.y_pos : st_.x_pos)
					: 0.0f;
				raster_row_ = 0;
				raster_active_ = true;
			}
			break;
		case 'B':
			raster_active_ = false;
			break;
		case 'K':
			state_ = State::StatusQuery;
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
			} else {
				ival = pcl_integer_word(value);
			}
			begin_payload(State::RasterData, ival);
			break;
		default:
			break;
		}
	} else if (group == '*' && subgroup == 's') {
		if (term == '^')
			state_ = State::StatusQuery;
	} else if (group == '*' && subgroup == 'c') {
		switch (term) {
		case 'A':
		{
			int word = pcl_integer_word(value);
			rect_w_in_ = (value > 0.0 && word > 0) ?
			             (float)word / kDotsPerIn : 0.0f;
			break;
		}
		case 'B':
		{
			int word = pcl_integer_word(value);
			rect_h_in_ = (value > 0.0 && word > 0) ?
			             (float)word / kDotsPerIn : 0.0f;
			break;
		}
		case 'D':
			soft_font_id_ = std::min(0x7fff, pcl_integer_word(value));
			current_soft_font();
			break;
		case 'E':
			soft_char_code_ =
				(uint8_t)(std::min(0x7fff, pcl_integer_word(value)) & 0xff);
			break;
		case 'F':
			ival = pcl_signed_integer_word(value);
			if (ival == 0 || ival == 1) {
				std::vector<int> ids;
				for (const auto &entry : soft_fonts_)
					if (ival == 0 || (ival == 1 && !entry.second.permanent))
						ids.push_back(entry.first);
				for (int id : ids)
					delete_soft_font(id);
				sync_active_font_state();
			} else if (ival == 2) {
				delete_soft_font(soft_font_id_);
				sync_active_font_state();
			} else if (ival == 3) {
				auto it = soft_fonts_.find(soft_font_id_);
				if (it != soft_fonts_.end())
					it->second.glyphs.erase(soft_char_code_);
				sync_active_font_state();
			} else if (ival == 4) {
				auto it = soft_fonts_.find(soft_font_id_);
				if (it != soft_fonts_.end())
					it->second.permanent = false;
			} else if (ival == 5) {
				auto it = soft_fonts_.find(soft_font_id_);
				if (it != soft_fonts_.end())
					it->second.permanent = true;
			} else if (ival == 6) {
				auto it = soft_fonts_.find(soft_font_id_);
				if (it != soft_fonts_.end())
					refresh_soft_font_request(it->second);
			}
			break;
		case 'G':
			fill_pattern_ = pcl_integer_word(value);
			break;
		case 'H':
			rect_w_in_ = ljii_rect_decipoints_to_in(value);
			break;
		case 'P': {
			int selector = rule_selector_for_fill_command(pcl_integer_word(value),
			                                              fill_pattern_,
			                                              orientation_);
			if (selector >= 0)
				draw_rule(st_.x_pos, st_.y_pos, rect_w_in_, rect_h_in_,
				          selector);
			break;
		}
		case 'V':
			rect_h_in_ = ljii_rect_decipoints_to_in(value);
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
	download_payload_control_seen_ = false;
	payload_buf_.clear();
	payload_buf_.reserve(static_cast<size_t>(count));
}

void PclPrinter::finish_payload_byte(uint8_t b)
{
	if (payload_state_ == State::TransparentData) {
		if (payload_control_pending_) {
			payload_control_pending_ = false;
			if (b == 0x58) {
				payload_control_normal_branch();
				emit_transparent_byte(0x7f);
			} else {
				emit_transparent_byte(b);
			}
		} else if (b == 0x1a) {
			payload_control_pending_ = true;
			return;
		} else {
			emit_transparent_byte(b);
		}
	} else {
		if ((payload_state_ == State::RasterData ||
		     payload_state_ == State::VfcData ||
		     payload_state_ == State::DownloadData ||
		     payload_state_ == State::DownloadDescriptorData ||
		     payload_state_ == State::DrainData) && payload_control_pending_) {
			payload_control_pending_ = false;
			if (b == 0x58) {
				if (payload_state_ == State::DownloadData ||
				    payload_state_ == State::DownloadDescriptorData)
					download_payload_control_seen_ = true;
				b = 0x00;
			}
		} else if ((payload_state_ == State::RasterData ||
		            payload_state_ == State::VfcData ||
		            payload_state_ == State::DownloadData ||
		            payload_state_ == State::DownloadDescriptorData ||
		            payload_state_ == State::DrainData) && b == 0x1a) {
			payload_control_pending_ = true;
			return;
		}
		if (payload_state_ != State::DrainData)
			payload_buf_.push_back(b);
	}

	if (--payload_remaining_ > 0)
		return;

	if (payload_state_ == State::RasterData)
		draw_raster_row(payload_buf_);
	else if (payload_state_ == State::VfcData)
		apply_vfc_payload(payload_buf_);
	else if (payload_state_ == State::DownloadData) {
		apply_download_payload(payload_buf_);
		if (download_font_slot_ == active_font_slot_)
			sync_active_font_state();
	} else if (payload_state_ == State::DownloadDescriptorData) {
		apply_download_descriptor_payload(payload_buf_);
		if (download_font_slot_ == active_font_slot_)
			sync_active_font_state();
	}

	payload_buf_.clear();
	payload_state_ = State::Normal;
	payload_control_pending_ = false;
	download_payload_control_seen_ = false;
	state_ = State::Normal;
}

void PclPrinter::emit_transparent_byte(uint8_t b)
{
	bool filtered = b < 0x20 || (b >= 0x80 && b <= 0x9f);
	if (filtered && !control_filter_routes_printable())
		advance_fixed_space();
	else
		process_printable(b);
}

void PclPrinter::draw_rule(float x_in, float y_in, float w_in, float h_in,
                           int selector)
{
	if (w_in <= 0.0f || h_in <= 0.0f || selector < 0 || selector > 13)
		return;
	new_page_if_needed();

	int dpi = prof_.render_dpi;
	int x0 = (int)std::floor(x_in * (float)dpi);
	int y0 = (int)std::floor(y_in * (float)dpi);
	int x1 = (int)std::ceil((x_in + w_in) * (float)dpi);
	int y1 = (int)std::ceil((y_in + h_in) * (float)dpi);
	x0 = std::max(0, x0);
	y0 = std::max(0, y0);
	x1 = std::min(page_->width(), x1);
	y1 = std::min(page_->height(), y1);
	if (x1 <= x0 || y1 <= y0)
		return;

	page_dirty_ = true;
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
	raster_transfer_y_in_ = ((orientation_ & 1) ? st_.x_pos : st_.y_pos) +
	                        (float)raster_row_ / kDotsPerIn;
	int start_y_dot = (int)std::floor(raster_transfer_y_in_ * kDotsPerIn);
	int page_w_dot = (int)std::floor(st_.page_width_in * kDotsPerIn);
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
	float base_y = raster_transfer_y_in_ * kDotsPerIn + (float)y_dot;
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
	flush_underline_span();
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
	clear_pending_cursor_y();
	restart_underline_span();
}

void PclPrinter::set_raster_resolution(int dpi)
{
	if (raster_active_)
		return;
	raster_resolution_ = dpi;
	if (dpi > 150) {
		raster_mode_ = 0;
		raster_scale_ = 1;
	} else if (dpi > 100) {
		raster_mode_ = 1;
		raster_scale_ = 2;
	} else if (dpi > 75) {
		raster_mode_ = 2;
		raster_scale_ = 3;
	} else {
		raster_mode_ = 3;
		raster_scale_ = 4;
	}
}

void PclPrinter::rebuild_default_vfc_table()
{
	vfc_limit_in_ = st_.top_margin_in + text_length_in_;
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

void PclPrinter::restore_default_text_length()
{
	text_length_in_ = std::max(0.0f, st_.page_height_in -
	                                 st_.top_margin_in -
	                                 st_.line_spacing_in);
	text_length_custom_ = false;
}

void PclPrinter::update_vfc_bounds()
{
	float line0 = st_.top_margin_in + st_.line_spacing_in;
	float available = std::max(0.0f, st_.page_height_in - line0);
	float vmi = std::max(1.0f / 300.0f, vmi_in_);
	vfc_last_line_ = std::max(0, (int)std::floor(available / vmi));

	float text_available = std::max(0.0f, text_length_in_ - st_.line_spacing_in);
	vfc_text_last_line_ = std::max(0, std::min(vfc_last_line_,
	                                           (int)std::floor(text_available / vmi)));
}

void PclPrinter::apply_vfc_payload(const std::vector<uint8_t> &payload)
{
	if (payload.empty()) {
		if (vmi_in_ <= 0.0f)
			return;
		restore_default_text_length();
		update_vfc_bounds();
		rebuild_default_vfc_table();
		return;
	}
	size_t table_window = (size_t)std::max(0, vfc_last_line_ + 1) * 2;
	if ((payload.size() & 1) || payload.size() > table_window)
		return;

	if (vfc_table_.size() != 128)
		vfc_table_.assign(128, 0);
	std::fill(vfc_table_.begin(), vfc_table_.end(), 0);
	size_t store = std::min<size_t>(payload.size(), 256);
	for (size_t i = 0; i + 1 < store; i += 2) {
		uint16_t word = (uint16_t)(((uint16_t)payload[i] << 8) | payload[i + 1]);
		vfc_table_[i / 2] = word;
	}
	vfc_limit_in_ = st_.page_height_in;
	int text_last = std::max(0, std::min(127, vfc_text_last_line_));
	for (int line = 0; line <= text_last; line++) {
		if (vfc_table_[(size_t)line] & 0x0002u) {
			vfc_limit_in_ = vfc_line_y(line);
			break;
		}
	}
	text_length_in_ = std::max(0.0f, vfc_limit_in_ - st_.top_margin_in);
	text_length_custom_ = true;
}

float PclPrinter::vfc_line_y(int line) const
{
	return st_.top_margin_in +
	       st_.line_spacing_in * ((float)line + 18.0f / 25.0f);
}

float PclPrinter::vfc_bottom_recovery_y(int target_line) const
{
	int last = std::max(0, std::min(127, vfc_last_line_));
	return st_.top_margin_in +
	       st_.line_spacing_in *
	       ((float)(last - target_line + 1) - 18.0f / 25.0f);
}

void PclPrinter::vfc_channel_jump(int selector)
{
	if (vfc_table_.size() != 128)
		rebuild_default_vfc_table();

	float vmi = std::max(1.0f / 300.0f, vmi_in_);
	int current = (int)std::floor((st_.y_pos - vfc_line_y(0)) / vmi + 0.0001f);
	int start = std::max(0, current + 1);
	int last = std::max(0, std::min(127, vfc_last_line_));
	int text_last = std::max(0, std::min(last, vfc_text_last_line_));

	if (selector <= 0) {
		flush_underline_span();
		if (current >= 0 && current <= text_last && page_ && page_dirty_)
			publish_current_page();
		st_.x_pos = st_.left_margin_in;
		st_.y_pos = vfc_line_y(0);
		clear_pending_cursor_y();
		restart_underline_span();
		return;
	}

	uint16_t mask = (selector <= 16) ? (uint16_t)(1u << (selector - 1)) : 0;
	if (mask == 0)
		return;
	flush_underline_span();

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
		if (current >= 0 && current <= text_last && page_ && page_dirty_)
			publish_current_page();
		st_.x_pos = st_.left_margin_in;
		st_.y_pos = (start > text_last + 1) ?
		            vfc_bottom_recovery_y(last + 1) :
		            vfc_line_y(0);
		clear_pending_cursor_y();
		restart_underline_span();
		return;
	}

	st_.x_pos = st_.left_margin_in;
	if (target > text_last) {
		if (!wrapped && current >= 0 && current <= text_last &&
		    page_ && page_dirty_)
			publish_current_page();
		st_.y_pos = vfc_bottom_recovery_y(target);
	} else {
		if (wrapped && current >= 0 && current <= last && page_ && page_dirty_)
			publish_current_page();
		st_.y_pos = vfc_line_y(target);
	}
	clear_pending_cursor_y();
	restart_underline_span();
}

PclPrinter::SoftFont &PclPrinter::current_soft_font()
{
	SoftFont &font = soft_fonts_[soft_font_id_];
	font.id = soft_font_id_;
	return font;
}

PclPrinter::SoftFont *PclPrinter::selected_soft_font()
{
	return const_cast<SoftFont *>(
		static_cast<const PclPrinter *>(this)->selected_soft_font());
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
	return selected_soft_font_candidate();
}

const PclPrinter::SoftFont *PclPrinter::selected_soft_font_candidate() const
{
	std::vector<const SoftFont *> candidates;
	for (const auto &entry : soft_fonts_) {
		if (!entry.second.active)
			continue;
		if (entry.second.glyphs.empty())
			continue;
		candidates.push_back(&entry.second);
	}
	if (candidates.empty())
		return nullptr;

	const LjiiFontRequest &req = active_font_request();
	auto prune = [&candidates](auto predicate) {
		std::vector<const SoftFont *> filtered;
		for (const SoftFont *font : candidates)
			if (predicate(*font))
				filtered.push_back(font);
		if (!filtered.empty())
			candidates = std::move(filtered);
	};
	auto nearest = [&candidates, &prune](auto value_fn, int requested) {
		if (candidates.empty())
			return;
		int best_diff = 0x7fffffff;
		for (const SoftFont *font : candidates)
			best_diff = std::min(best_diff,
			                     std::abs(value_fn(*font) - requested));
		prune([&](const SoftFont &font) {
			return std::abs(value_fn(font) - requested) == best_diff;
		});
	};

	prune([&](const SoftFont &font) { return font.symbol_set == req.symbol_set; });
	if (req.symbol_set != kSymbolRoman8)
		prune([](const SoftFont &font) { return font.symbol_set == kSymbolRoman8; });

	std::vector<const SoftFont *> spacing_matches;
	for (const SoftFont *font : candidates)
		if (font->spacing == req.spacing)
			spacing_matches.push_back(font);
	if (req.spacing == 1) {
		if (!spacing_matches.empty())
			candidates = std::move(spacing_matches);
	} else if (!spacing_matches.empty()) {
		candidates = std::move(spacing_matches);
		nearest([](const SoftFont &font) { return font.pitch; }, req.pitch);
	}

	nearest([](const SoftFont &font) { return font.height; }, req.height);
	prune([&](const SoftFont &font) { return font.style == req.style; });
	if (req.stroke >= 3)
		prune([](const SoftFont &font) { return font.stroke >= 3; });
	else
		prune([&](const SoftFont &font) { return font.stroke == req.stroke; });
	prune([&](const SoftFont &font) { return font.typeface == req.typeface; });

	return candidates.empty() ? nullptr : candidates.front();
}

void PclPrinter::delete_soft_font(int id)
{
	soft_fonts_.erase(id);
	for (int &selected : selected_soft_font_id_)
		if (selected == id)
			selected = -1;
}

void PclPrinter::refresh_soft_font_request(const SoftFont &font)
{
	if (!font.active || !font.has_request_metrics)
		return;
	for (int slot = 0; slot < 2; slot++) {
		if (selected_soft_font_id_[slot] != font.id)
			continue;
		LjiiFontRequest &req = font_request(slot);
		req.symbol_set = font.symbol_set;
		req.spacing = font.spacing;
		if (font.has_pitch_metric)
			req.pitch = font.pitch;
		if (font.has_height_metric)
			req.height = font.height;
		if (font.has_style_metric)
			req.style = font.style;
		if (font.has_stroke_metric)
			req.stroke = font.stroke;
		if (font.has_typeface_metric)
			req.typeface = font.typeface;
		if (slot == active_font_slot_)
			sync_active_font_state();
	}
}

void PclPrinter::release_fixed_record_glyph(SoftFont &font, uint8_t ch)
{
	SoftGlyph replacement;
	replacement.width = 8;
	replacement.rows = 2;
	replacement.span = 1;
	replacement.bitmap = { 0xfa, 0x00 };
	font.glyphs[ch] = std::move(replacement);
	font.continuation_active = false;
	font.continuation_remaining = 0;
}

size_t PclPrinter::soft_glyph_bitmap_index(const SoftGlyph &glyph,
                                           uint16_t row,
                                           uint16_t byte_col) const
{
	if (!glyph.split_plane || glyph.span <= 1)
		return (size_t)row * glyph.span + byte_col;
	uint16_t prefix_span = glyph.span - 1;
	if (byte_col < prefix_span)
		return (size_t)row * prefix_span + byte_col;
	return (size_t)glyph.rows * prefix_span + row;
}

uint8_t PclPrinter::soft_glyph_bitmap_byte(const SoftGlyph &glyph,
                                           uint16_t row,
                                           uint16_t byte_col) const
{
	size_t byte_off = soft_glyph_bitmap_index(glyph, row, byte_col);
	if (byte_off >= glyph.bitmap.size())
		return 0;
	return glyph.bitmap[byte_off];
}

size_t PclPrinter::copy_soft_glyph_host_bytes(
	SoftGlyph &glyph, size_t host_offset,
	std::vector<uint8_t>::const_iterator first,
	std::vector<uint8_t>::const_iterator last)
{
	size_t copied = 0;
	size_t expected = (size_t)glyph.rows * glyph.span;
	for (auto it = first; it != last && host_offset + copied < expected;
	     ++it, ++copied) {
		size_t pos = host_offset + copied;
		uint16_t row = (uint16_t)(pos / glyph.span);
		uint16_t byte_col = (uint16_t)(pos % glyph.span);
		size_t byte_off = soft_glyph_bitmap_index(glyph, row, byte_col);
		if (byte_off < glyph.bitmap.size())
			glyph.bitmap[byte_off] = *it;
	}
	return copied;
}

void PclPrinter::apply_download_descriptor_payload(
	const std::vector<uint8_t> &payload)
{
	if (payload.size() < 3 || payload[0] != 0x04)
		return;

	SoftFont &font = current_soft_font();
	font.active = true;
	if (payload[1] != 0) {
		if (!font.continuation_active)
			return;
		auto it = font.glyphs.find(font.continuation_char);
		if (it == font.glyphs.end()) {
			font.continuation_active = false;
			font.continuation_remaining = 0;
			return;
		}
		SoftGlyph &glyph = it->second;
		auto first = payload.begin() +
		             (std::vector<uint8_t>::difference_type)3;
		size_t available = (size_t)(payload.end() - first);
		if (!glyph.split_plane && available >= 2 &&
		    available < font.continuation_remaining) {
			release_fixed_record_glyph(font, font.continuation_char);
			return;
		}
		auto last = first + (std::vector<uint8_t>::difference_type)
		            std::min(available, font.continuation_remaining);
		size_t copy = copy_soft_glyph_host_bytes(
			glyph, font.continuation_offset, first, last);
		font.continuation_offset += copy;
		font.continuation_remaining -= copy;
		if (font.continuation_remaining == 0)
			font.continuation_active = false;
		return;
	}

	font.continuation_active = false;
	font.continuation_remaining = 0;
	if (payload.size() < 6)
		return;
	if (soft_char_code_ >= 0x80 && soft_char_code_ < 0xa0)
		return;
	if (soft_char_code_ >= 0xa0 && !font.fixed_record_extended_chars)
		return;

	uint16_t span = payload[2];
	uint16_t rows = payload[3];
	if (span == 0 || rows == 0)
		return;

	SoftGlyph glyph;
	glyph.span = span;
	glyph.width = (uint16_t)(span * 8);
	glyph.rows = rows;
	glyph.split_plane = span > 1 && (span & 1);
	size_t expected = (size_t)glyph.rows * glyph.span;
	glyph.bitmap.assign(expected, 0);
	size_t copied = copy_soft_glyph_host_bytes(
		glyph, 0,
		payload.begin() + (std::vector<uint8_t>::difference_type)6,
		payload.end());
	if (copied < expected) {
		font.continuation_active = true;
		font.continuation_char = soft_char_code_;
		font.continuation_offset = copied;
		font.continuation_remaining = expected - copied;
	}

	font.glyphs[soft_char_code_] = std::move(glyph);
}

void PclPrinter::apply_download_payload(const std::vector<uint8_t> &payload)
{
	SoftFont &font = current_soft_font();
	font.active = true;
	if (payload.empty()) {
		return;
	}

	bool descriptor_shaped_payload = payload.size() >= 14 &&
		payload[4] == 0x0c && (payload[5] == 1 || payload[5] == 2);
	if (font.continuation_active &&
	    font.continuation_char == soft_char_code_ &&
	    !descriptor_shaped_payload &&
	    !looks_like_ljii_font_resource_header(payload)) {
		auto it = font.glyphs.find(font.continuation_char);
		if (it == font.glyphs.end()) {
			font.continuation_active = false;
			font.continuation_remaining = 0;
			return;
		}
		SoftGlyph &glyph = it->second;
		auto last = payload.begin() + (std::vector<uint8_t>::difference_type)
		            std::min(payload.size(), font.continuation_remaining);
		size_t copy = copy_soft_glyph_host_bytes(
			glyph, font.continuation_offset, payload.begin(), last);
		font.continuation_offset += copy;
		font.continuation_remaining -= copy;
		if (font.continuation_remaining == 0)
			font.continuation_active = false;
		return;
	}

	if (looks_like_ljii_font_resource_header(payload)) {
		font.continuation_active = false;
		font.continuation_remaining = 0;
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
		font.resource_extended_chars = font_class >= 1;
		font.resource_first = first;
		font.resource_last = font.resource_extended_chars ? 0x00ff : 0x007f;
		font.symbol_set = be16_at(payload, 14);
		if (font.symbol_set == 0)
			font.symbol_set = kSymbolRoman8;
		font.has_request_metrics = true;
		font.spacing = payload[13] ? 1 : 0;
		uint16_t pitch = be16_at(payload, 16);
		uint16_t height = be16_at(payload, 18);
		if (pitch > 0) {
			font.pitch = std::min<int>(pitch, 0x41a0);
			font.has_pitch_metric = true;
		}
		if (height > 0) {
			font.height = std::min<int>(height, 0x2aaa);
			font.has_height_metric = true;
		}
		if (payload.size() > 0x31) {
			font.style = payload[0x2f];
			font.stroke = std::max(-7, std::min(7,
			                                    (int)(int8_t)payload[0x30]));
			font.typeface = payload[0x31];
			font.has_style_metric = true;
			font.has_stroke_metric = true;
			font.has_typeface_metric = true;
		}
		if (download_font_slot_ == 0 || download_font_slot_ == 1) {
			LjiiFontRequest &req = font_request(download_font_slot_);
			req.symbol_set = font.symbol_set;
			req.spacing = font.spacing;
			if (font.has_pitch_metric)
				req.pitch = font.pitch;
			if (font.has_height_metric)
				req.height = font.height;
			if (font.has_style_metric)
				req.style = font.style;
			if (font.has_stroke_metric)
				req.stroke = font.stroke;
			if (font.has_typeface_metric)
				req.typeface = font.typeface;
			if (download_font_slot_ == active_font_slot_)
				sync_active_font_state();
		}
		return;
	}

	if (payload.size() >= 64 && soft_char_code_ <= 0x20) {
		font.continuation_active = false;
		font.continuation_remaining = 0;
		font.fixed_record_extended_chars = payload[0x0e] != 0;
		if (payload.size() > 0x23) {
			int symbol = ((int)payload[0x22] << 8) | payload[0x23];
			if (symbol != 0)
				font.symbol_set = symbol;
		}
		font.has_request_metrics = true;
		if (payload.size() > 0x21)
			font.spacing = payload[0x21] ? 1 : 0;
		if (payload.size() > 0x31) {
			font.style = payload[0x2f];
			font.stroke = std::max(-7, std::min(7,
			                                    (int)(int8_t)payload[0x30]));
			font.typeface = payload[0x31];
			font.has_style_metric = true;
			font.has_stroke_metric = true;
			font.has_typeface_metric = true;
		}
		if (payload.size() > 0x2a) {
			int pitch = ((int)payload[0x24] << 8) | payload[0x25];
			int height = ((int)payload[0x28] << 8) | payload[0x29];
			if (pitch > 0) {
				font.pitch = std::min<int>(pitch, 0x41a0);
				font.has_pitch_metric = true;
			}
			if (height > 0) {
				font.height = std::min<int>(height, 0x2aaa);
				font.has_height_metric = true;
			}
		}
		if (download_font_slot_ == 0 || download_font_slot_ == 1) {
			LjiiFontRequest &req = font_request(download_font_slot_);
			req.symbol_set = font.symbol_set;
			req.spacing = font.spacing;
			if (font.has_style_metric)
				req.style = font.style;
			if (font.has_stroke_metric)
				req.stroke = font.stroke;
			if (font.has_typeface_metric)
				req.typeface = font.typeface;
			if (font.has_pitch_metric)
				req.pitch = font.pitch;
			if (font.has_height_metric)
				req.height = font.height;
			if (download_font_slot_ == active_font_slot_)
				sync_active_font_state();
		}
		return;
	}

	SoftGlyph glyph;
	bool render_payload_control_glyph = false;
	bool preserve_declared_glyph_shape = false;
	bool continuable_descriptor_glyph = false;
	bool descriptor_split_plane_glyph = false;
	size_t descriptor_bitmap_bytes = 0;
	auto fixed_record_char_admitted = [&]() {
		if (soft_char_code_ >= 0x80 && soft_char_code_ < 0xa0)
			return false;
		if (soft_char_code_ >= 0xa0 && !font.fixed_record_extended_chars)
			return false;
		return true;
	};
	if (download_payload_control_seen_ && payload.size() == 18 &&
	    payload[0] == 0x00) {
		glyph.width = 136;
		glyph.span = 17;
		glyph.rows = 1;
		glyph.bitmap.assign(payload.begin(), payload.begin() + 17);
		render_payload_control_glyph = true;
	} else if (payload.size() >= 14 && payload[4] == 0x0c &&
	    (payload[5] == 1 || payload[5] == 2)) {
		font.continuation_active = false;
		font.continuation_remaining = 0;
		glyph.rows = (uint16_t)std::max(1, ((int)payload[6] << 8) | payload[7]);
		glyph.width = (uint16_t)std::max(1, ((int)payload[8] << 8) | payload[9]);
		glyph.span = (uint16_t)std::max(1, (int)((glyph.width + 7) >> 3));
		glyph.bitmap.assign(payload.begin() + 12, payload.end());
		preserve_declared_glyph_shape = true;
		continuable_descriptor_glyph = true;
		descriptor_split_plane_glyph =
			payload[5] == 2 && glyph.span > 1;
		descriptor_bitmap_bytes = glyph.bitmap.size();
		glyph.unresolved_pixels =
			(glyph.span > 0xff && (glyph.span & 0xff) <= 0x10) ||
			(glyph.span == 2 && glyph.rows >= 0x0101 &&
			 glyph.rows <= 0x0103) ||
			(glyph.span == 31 && glyph.rows >= 0x0181);
	} else if (payload.size() >= 6 && payload[4] == 0x0c &&
	           payload[5] != 1 && payload[5] != 2) {
		font.continuation_active = false;
		font.continuation_remaining = 0;
		return;
	} else if (font.resource_header_active && payload.size() == 3) {
		if (soft_char_code_ >= 0x80 && !font.resource_extended_chars)
			return;
		glyph.width = 4;
		glyph.span = 1;
		glyph.rows = 3;
	} else if (payload.size() == 18) {
		if (!fixed_record_char_admitted())
			return;
		glyph.width = 144;
		glyph.span = 18;
		glyph.rows = 1;
	} else if (payload.size() == 32 || payload.size() == 64 ||
	           payload.size() == 128 || payload.size() == 256 ||
	           payload.size() == 258 || payload.size() == 260 ||
	           payload.size() == 516) {
		if (!fixed_record_char_admitted())
			return;
		glyph.width = 16;
		glyph.span = 2;
		glyph.rows = (uint16_t)std::max<size_t>(1, payload.size() / 2);
	} else if (payload.size() == 387 || payload.size() == 2193) {
		if (!fixed_record_char_admitted())
			return;
		glyph.span = payload.size() == 387 ? 3 : 17;
		glyph.width = (uint16_t)(glyph.span * 8);
		glyph.rows = (uint16_t)(payload.size() / glyph.span);
	} else if ((payload.size() & 1) == 0) {
		if (!fixed_record_char_admitted())
			return;
		glyph.width = 16;
		glyph.span = 2;
		glyph.rows = (uint16_t)std::max<size_t>(1, payload.size() / 2);
	} else {
		if (!fixed_record_char_admitted())
			return;
		glyph.width = 8;
		glyph.span = 1;
		glyph.rows = (uint16_t)std::max<size_t>(1, payload.size());
	}
	if (glyph.bitmap.empty())
		glyph.bitmap = payload;

	size_t expected = (size_t)glyph.rows * glyph.span;
	if (descriptor_split_plane_glyph) {
		std::vector<uint8_t> host = std::move(glyph.bitmap);
		glyph.split_plane = true;
		glyph.bitmap.assign(expected, 0);
		descriptor_bitmap_bytes =
			copy_soft_glyph_host_bytes(glyph, 0, host.begin(), host.end());
	} else if ((glyph.span & 1) && glyph.span > 1 &&
	           glyph.bitmap.size() == expected) {
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
	if (preserve_declared_glyph_shape && descriptor_bitmap_bytes < expected) {
		if (continuable_descriptor_glyph) {
			font.continuation_active = true;
			font.continuation_char = soft_char_code_;
			font.continuation_offset = descriptor_bitmap_bytes;
			font.continuation_remaining = expected - descriptor_bitmap_bytes;
		}
		if (glyph.bitmap.size() < expected)
			glyph.bitmap.resize(expected, 0);
	} else if (glyph.bitmap.size() < expected && !glyph.bitmap.empty()) {
		size_t rows = glyph.bitmap.size() / std::max<uint16_t>(1, glyph.span);
		if (rows == 0) {
			glyph.span = 1;
			glyph.width = 8;
			rows = glyph.bitmap.size();
		}
		glyph.rows = (uint16_t)std::min<size_t>(rows, 0xffff);
	}
	if (!(continuable_descriptor_glyph && descriptor_bitmap_bytes < expected)) {
		font.continuation_active = false;
		font.continuation_remaining = 0;
	}
	font.glyphs[soft_char_code_] = std::move(glyph);
	if (render_payload_control_glyph)
		draw_soft_glyph_pixels(font.glyphs[soft_char_code_]);
}

float PclPrinter::ljii_metric_width_in(uint8_t width, float fallback_in) const
{
	if (st_.proportional && width > 0)
		return (float)width / kDotsPerIn;
	return fallback_in;
}

bool PclPrinter::consume_previous_width_adjustment(float current_width_in)
{
	bool had_pending = previous_width_pending_;
	if (had_pending && st_.proportional) {
		float delta = (previous_text_width_in_ - current_width_in) * 0.5f;
		st_.x_pos = std::max(0.0f, st_.x_pos + delta);
	}
	previous_width_pending_ = false;
	return had_pending;
}

void PclPrinter::finish_text_advance(float width_in, float advance_in,
                                     bool had_pending)
{
	st_.x_pos += advance_in;
	if (!had_pending) {
		previous_text_width_in_ = width_in;
		previous_text_advance_in_ = advance_in;
	}
	clear_pending_cursor_y();
}

void PclPrinter::draw_soft_glyph_pixels(const SoftGlyph &glyph)
{
	if (glyph.width == 0 || glyph.rows == 0 || glyph.span == 0)
		return;
	if (glyph.unresolved_pixels)
		return;
	new_page_if_needed();
	page_dirty_ = true;

	int dpi = prof_.render_dpi;
	int base_x = (int)std::lround(st_.x_pos * (float)dpi);
	int base_y = (int)std::lround(st_.y_pos * (float)dpi) - (int)glyph.rows;
	for (uint16_t row = 0; row < glyph.rows; row++) {
		for (uint16_t col = 0; col < glyph.width; col++) {
			uint8_t byte = soft_glyph_bitmap_byte(glyph, row, col >> 3);
			if (byte & (0x80u >> (col & 7)))
				page_->set_pixel(base_x + col, base_y + row, 0);
		}
	}
}

bool PclPrinter::render_soft_glyph(uint8_t b, float char_w_in)
{
	SoftFont *font = selected_soft_font();
	if (!font)
		return false;
	auto it = font->glyphs.find(b);
	if (it == font->glyphs.end())
		return false;
	const SoftGlyph &glyph = it->second;
	float metric_width_in =
		(st_.proportional && glyph.width > 0) ?
		(float)std::min<uint16_t>(glyph.width, 0xff) / kDotsPerIn :
		char_w_in;
	bool had_pending = consume_previous_width_adjustment(metric_width_in);
	char_w_in = metric_width_in;

	if (st_.x_pos + char_w_in > st_.right_margin_in + 0.001f) {
		if (!wrap_enabled_) {
			finish_text_advance(metric_width_in, char_w_in, had_pending);
			return true;
		}
		ljii_carriage_return();
		ljii_line_feed();
	}
	if (!glyph.unresolved_pixels && !ljii_soft_glyph_vertical_accepts(glyph)) {
		finish_text_advance(metric_width_in, char_w_in, had_pending);
		return true;
	}
	start_underline_span();

	if (glyph.unresolved_pixels) {
		uint16_t cp = text_unicode(b);
		if (cp >= 0x20)
			append_ljii_text_glyph(cp, char_w_in);
		finish_text_advance(metric_width_in, char_w_in, had_pending);
		mark_line_output(true);
		return true;
	}

	new_page_if_needed();
	page_dirty_ = true;

	int dpi = prof_.render_dpi;
	int base_x = (int)std::lround(st_.x_pos * (float)dpi);
	int base_y = (int)std::lround(st_.y_pos * (float)dpi) - (int)glyph.rows;
	for (uint16_t row = 0; row < glyph.rows; row++) {
		for (uint16_t col = 0; col < glyph.width; col++) {
			uint8_t byte = soft_glyph_bitmap_byte(glyph, row, col >> 3);
			if (byte & (0x80u >> (col & 7)))
				page_->set_pixel(base_x + col, base_y + row, 0);
		}
	}

	uint16_t cp = text_unicode(b);
	if (cp >= 0x20) {
		append_ljii_text_glyph(cp, char_w_in);
	}
	finish_text_advance(metric_width_in, char_w_in, had_pending);
	mark_line_output(true);
	return true;
}

void PclPrinter::ensure_text_page()
{
	new_page_if_needed();
	page_dirty_ = true;
}

void PclPrinter::append_ljii_text_glyph(uint16_t cp, float char_w_in)
{
	ensure_text_page();
	text_buf_.push_back({
		st_.x_pos, st_.y_pos, cp, char_w_in,
		char_w_in * 72.0f / 0.6f, 0
	});
}

bool PclPrinter::ljii_text_box_accepts(float top_in, float bottom_in) const
{
	return top_in >= -0.0001f &&
	       bottom_in <= st_.page_height_in + 0.0001f;
}

bool PclPrinter::ljii_nominal_text_vertical_accepts() const
{
	float top = st_.y_pos - 31.0f / kDotsPerIn;
	float bottom = top + 32.0f / kDotsPerIn;
	return ljii_text_box_accepts(top, bottom);
}

bool PclPrinter::ljii_resident_glyph_vertical_accepts(
	const LjiiGlyphInfo &glyph) const
{
	float top = st_.y_pos - (float)glyph.y_offset / kDotsPerIn;
	float bottom = top + (float)glyph.rows / kDotsPerIn;
	return ljii_text_box_accepts(top, bottom);
}

bool PclPrinter::ljii_soft_glyph_vertical_accepts(const SoftGlyph &glyph) const
{
	float top = st_.y_pos - (float)glyph.rows / kDotsPerIn;
	return ljii_text_box_accepts(top, st_.y_pos);
}

bool PclPrinter::render_ljii_text(uint8_t b)
{
	refresh_pending_cursor_y();
	const LjiiFontRequest &req = active_font_request();
	float char_w_in = hmi_in_;
	if (b == 0x20) {
		bool had_pending = consume_previous_width_adjustment(char_w_in);
		if (st_.x_pos + char_w_in > st_.right_margin_in + 0.001f) {
			if (!wrap_enabled_) {
				finish_text_advance(char_w_in, char_w_in, had_pending);
				return true;
			}
			ljii_carriage_return();
			ljii_line_feed();
		}
		if (!ljii_nominal_text_vertical_accepts()) {
			finish_text_advance(char_w_in, char_w_in, had_pending);
			return true;
		}
		start_underline_span();
		append_ljii_text_glyph(0x20, char_w_in);
		if (st_.underline) {
			new_page_if_needed();
			page_dirty_ = true;
			int dpi = prof_.render_dpi;
				int y = (int)std::lround(underline_y_in(st_.y_pos,
				                                        underline_selector_) *
				                         (float)dpi);
			int x0 = (int)std::lround(st_.x_pos * (float)dpi);
			int x1 = (int)std::lround((st_.x_pos + char_w_in) * (float)dpi);
			for (int x = x0; x < x1; x++) {
				page_->set_pixel(x, y, 0);
				page_->set_pixel(x, y + 1, 0);
			}
		}
		finish_text_advance(char_w_in, char_w_in, had_pending);
		return true;
	}

	if (render_soft_glyph(b, char_w_in))
		return true;

	const LjiiFontRequest *render_req = &req;
	uint8_t source_byte = b;
	if (active_font_slot_ == 0 && b >= 0x80 && !control_filter_routes_printable()) {
		render_req = &font_request(1);
		source_byte = (uint8_t)(b & 0x7f);
	}
	uint8_t glyph_byte = text_glyph_byte_for(*render_req, source_byte);
	if (glyph_byte == 0)
		return true;
	uint32_t context = select_ljii_context(*render_req, orientation_);
	LjiiGlyphInfo glyph = get_ljii_glyph(context, glyph_byte);
	char_w_in = ljii_metric_width_in(glyph.width, hmi_in_);
	bool had_pending = consume_previous_width_adjustment(char_w_in);
	if (!glyph.found || !glyph.data) {
		if (!ljii_nominal_text_vertical_accepts()) {
			finish_text_advance(char_w_in, char_w_in, had_pending);
			return true;
		}
		start_underline_span();
		uint16_t cp = text_unicode(b);
		if (cp >= 0x20) {
			append_ljii_text_glyph(cp, char_w_in);
		}
		finish_text_advance(char_w_in, char_w_in, had_pending);
		mark_line_output(true);
		return true;
	}

	if (st_.x_pos + char_w_in > st_.right_margin_in + 0.001f) {
		if (!wrap_enabled_) {
			finish_text_advance(char_w_in, char_w_in, had_pending);
			return true;
		}
		ljii_carriage_return();
		ljii_line_feed();
	}
	if (!ljii_resident_glyph_vertical_accepts(glyph)) {
		finish_text_advance(char_w_in, char_w_in, had_pending);
		return true;
	}
	start_underline_span();

	new_page_if_needed();
	page_dirty_ = true;

	int dpi = prof_.render_dpi;
	int base_x = (int)std::lround(st_.x_pos * (float)dpi) + glyph.x_offset;
	int base_y = (int)std::lround(st_.y_pos * (float)dpi) - glyph.y_offset;
	for (uint8_t row = 0; row < glyph.rows; row++) {
		const uint8_t *src = glyph.data + (size_t)row * glyph.span;
		for (uint8_t col = 0; col < glyph.width; col++) {
			uint8_t byte = src[col >> 3];
			if (byte & (0x80u >> (col & 7)))
				page_->set_pixel(base_x + col, base_y + row, 0);
		}
	}
	if (st_.underline) {
			int y = (int)std::lround(underline_y_in(st_.y_pos,
			                                        underline_selector_) *
			                         (float)dpi);
		int x0 = (int)std::lround(st_.x_pos * (float)dpi);
		int x1 = (int)std::lround((st_.x_pos + char_w_in) * (float)dpi);
		for (int x = x0; x < x1; x++) {
			page_->set_pixel(x, y, 0);
			page_->set_pixel(x, y + 1, 0);
		}
	}

	uint16_t cp = text_unicode(b);
	if (cp >= 0x20) {
		append_ljii_text_glyph(cp, char_w_in);
	}
	finish_text_advance(char_w_in, char_w_in, had_pending);
	mark_line_output(true);
	return true;
}

void PclPrinter::refresh_pending_cursor_y()
{
	if (!pending_cursor_y_)
		return;
	st_.y_pos = st_.top_margin_in + st_.line_spacing_in * (18.0f / 25.0f);
}

void PclPrinter::clear_pending_cursor_y()
{
	pending_cursor_y_ = false;
}

void PclPrinter::start_underline_span()
{
	if (!st_.underline || underline_span_active_)
		return;
	refresh_pending_cursor_y();
	underline_span_active_ = true;
	underline_span_x0_in_ = st_.x_pos;
	underline_span_y_in_ = st_.y_pos;
	underline_span_selector_ = underline_selector_;
}

void PclPrinter::flush_underline_span()
{
	previous_width_pending_ = false;
	if (!underline_span_active_)
		return;
	draw_underline_range(underline_span_x0_in_, st_.x_pos,
	                     underline_span_y_in_, underline_span_selector_);
	underline_span_active_ = false;
}

void PclPrinter::restart_underline_span()
{
	underline_span_active_ = false;
	start_underline_span();
}

float PclPrinter::underline_y_in(float y_in, int) const
{
	return y_in + 5.0f / kDotsPerIn;
}

void PclPrinter::draw_underline_range(float x0_in, float x1_in, float y_in,
                                      int selector)
{
	if (x1_in < x0_in)
		std::swap(x0_in, x1_in);
	x0_in = std::max(st_.left_margin_in, x0_in);
	x1_in = std::min(st_.right_margin_in, x1_in);
	if (x1_in <= x0_in + 0.0001f)
		return;

	new_page_if_needed();
	page_dirty_ = true;
	int dpi = prof_.render_dpi;
	int y = (int)std::lround(underline_y_in(y_in, selector) * (float)dpi);
	int x0 = (int)std::lround(x0_in * (float)dpi);
	int x1 = (int)std::lround(x1_in * (float)dpi);
	for (int x = x0; x < x1; x++) {
		page_->set_pixel(x, y, 0);
		page_->set_pixel(x, y + 1, 0);
	}
}

void PclPrinter::ljii_carriage_return()
{
	flush_underline_span();
	carriage_return();
	clear_pending_cursor_y();
	restart_underline_span();
}

uint8_t PclPrinter::text_glyph_byte_for(const LjiiFontRequest &req, uint8_t b) const
{
	int symbol_set = ljii_effective_map_symbol_word(req);
	bool routes_printable = symbol_set != 0 && symbol_set != kSymbolRoman8;
	if (routes_printable && b >= 0x80 && b <= 0x9f) {
		if (symbol_set == 0x000e)
			return (uint8_t)(b - 0x21);
		return (uint8_t)(b - 1);
	}
	if (!routes_printable && b >= 0x80)
		return (uint8_t)(b & 0x7f);
	if (symbol_set == kSymbolRoman8 || symbol_set == 0)
		return b;
	return symbol_glyph_byte(symbol_set, b);
}

uint8_t PclPrinter::text_glyph_byte(uint8_t b) const
{
	return text_glyph_byte_for(active_font_request(), b);
}

uint16_t PclPrinter::text_unicode(uint8_t b) const
{
	if (b == 0x7f)
		return 0;
	if (b < 0x80)
		return b;
	return roman8_to_unicode(b);
}

bool PclPrinter::apply_builtin_font_id(int slot, int id)
{
	LjiiFontRequest &req = font_request(slot);
	if (slot == 0 && id == 7) {
		req.symbol_set = kSymbolRoman8;
		req.pitch = 1000;
		req.height = 1200;
		req.spacing = 0;
		req.style = 0;
		req.stroke = 3;
		req.typeface = 3;
		return true;
	}
	if (slot == 1 && id == 8) {
		req.symbol_set = 0x000e;
		req.pitch = 1666;
		req.height = 850;
		req.spacing = 0;
		req.style = 0;
		req.stroke = 0;
		req.typeface = 0;
		return true;
	}
	return false;
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
	int pitch = req.pitch;
	if (!selected_soft_font()) {
		int context_pitch = ljii_context_pitch(
			select_ljii_context(req, orientation_));
		if (context_pitch > 0)
			pitch = context_pitch;
	}
	st_.pitch_cpi = std::max(1.0f, (float)pitch / 100.0f);
	hmi_in_ = 1.0f / st_.pitch_cpi;
	st_.proportional = (req.spacing != 0);
	st_.italic = (req.style == 1);
	st_.bold = (req.stroke >= 3);
}

bool PclPrinter::ljii_perforation_overflow_check()
{
	if (st_.perf_skip_lines <= 0 || vfc_limit_in_ <= 0.0f)
		return false;
	if (st_.y_pos <= vfc_limit_in_)
		return false;
	publish_current_page();
	return true;
}

void PclPrinter::ljii_line_feed()
{
	flush_underline_span();
	flush_pending_line();
	new_page_if_needed();
	page_dirty_ = true;

	st_.y_pos += st_.line_spacing_in;
	float bottom = st_.page_height_in - 0.5f;
	if (st_.perf_skip_lines > 0) {
		ljii_perforation_overflow_check();
	} else if (bottom > 0.0f && st_.y_pos > bottom) {
		publish_current_page();
	}

	advance_line_direction();
	finish_printed_line();
	clear_pending_cursor_y();
	restart_underline_span();
}

void PclPrinter::set_page_size(int code)
{
	code = std::abs(code);
	if (!pcl_page_size_selector_valid(code))
		return;
	publish_current_page();
	page_size_code_ = code;
	apply_page_geometry();
}

void PclPrinter::set_orientation(int orientation)
{
	orientation = std::abs(orientation);
	if (orientation > 1 || orientation == orientation_)
		return;
	publish_current_page();
	orientation_ = orientation;
	apply_page_geometry();
}

void PclPrinter::apply_page_geometry()
{
	flush_underline_span();
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
	pending_cursor_y_ = true;
	restore_default_text_length();
	update_vfc_bounds();
	rebuild_default_vfc_table();
	restart_underline_span();
	sync_active_font_state();
}

void PclPrinter::set_page_length(float length_in)
{
	flush_underline_span();
	physical_h_in_ = length_in;
	logical_h_in_ = std::max(0.0f, physical_h_in_ - logical_y0_in_);
	st_.page_height_in = physical_h_in_;
	st_.top_margin_in = logical_y0_in_;
	st_.x_pos = st_.left_margin_in;
	st_.y_pos = st_.top_margin_in + st_.line_spacing_in;
	pending_cursor_y_ = true;
	restore_default_text_length();
	update_vfc_bounds();
	rebuild_default_vfc_table();
	restart_underline_span();
	sync_active_font_state();
}

void PclPrinter::ensure_page_root()
{
	new_page_if_needed();
	page_root_active_ = true;
}

void PclPrinter::publish_current_page()
{
	flush_underline_span();
	if (page_ && (page_dirty_ || page_root_active_) &&
	    overlay_enabled_ && !replaying_macro_) {
		auto it = macros_.find(overlay_macro_id_);
		if (it != macros_.end() && !it->second.bytes.empty())
			replay_macro(overlay_macro_id_, MacroReplayMode::Overlay);
	}
	flush_underline_span();
	flush_pending_line();
	if (page_ && (page_dirty_ || page_root_active_)) {
		int copies = std::max(1, copy_count_);
		for (int i = 0; i < copies; i++)
			pdf_.add_page(*page_, prof_.render_dpi, text_buf_);
		text_buf_.clear();
		page_dirty_ = false;
	}
	page_root_active_ = false;
	page_.reset();
	st_.x_pos = st_.left_margin_in;
	st_.y_pos = st_.top_margin_in + st_.line_spacing_in;
	pending_cursor_y_ = true;
}

void PclPrinter::flush()
{
	publish_current_page();
}

void PclPrinter::start_macro_definition(bool lowercase_final)
{
	defining_macro_ = true;
	Macro &macro = macros_[macro_id_];
	macro.bytes.clear();
	macro_command_start_ = 0;
	macro_stop_buf_.clear();
	macro_chain_active_ = lowercase_final;
	macro_display_capture_ = false;
	macro_display_escape_pending_ = false;
	macro_display_control_pending_ = false;
	macro_control_pending_ = false;
	macro_payload_echo_remaining_ = 0;
	macro_payload_echo_control_pending_ = false;
	macro_payload_drain_remaining_ = 0;
	macro_payload_drain_control_pending_ = false;
	macro_payload_chain_active_ = false;
	macro_payload_chain_drain_ = false;
	macro_payload_chain_count_ = -1;
	macro_payload_chain_final_ = 0;
	macro_payload_chain_param_.clear();
	if (lowercase_final) {
		macro.bytes.push_back(0x1B);
		macro.bytes.push_back('&');
		macro.bytes.push_back('f');
	} else {
		macro.bytes.push_back(0);
	}
}

void PclPrinter::finish_macro_definition(size_t keep_size)
{
	Macro &macro = macros_[macro_id_];
	keep_size = std::min(keep_size, macro.bytes.size());
	macro.bytes.resize(keep_size);
	if ((macro.bytes.size() == 1 && macro.bytes[0] == 0) ||
	    (macro.bytes.size() == 3 && macro.bytes[0] == 0x1B &&
	     macro.bytes[1] == '&' && macro.bytes[2] == 'f')) {
		macro.bytes.clear();
	}
	defining_macro_ = false;
	macro_chain_active_ = false;
	macro_display_capture_ = false;
	macro_display_escape_pending_ = false;
	macro_display_control_pending_ = false;
	macro_control_pending_ = false;
	macro_payload_echo_remaining_ = 0;
	macro_payload_echo_control_pending_ = false;
	macro_payload_chain_active_ = false;
	macro_payload_chain_count_ = -1;
	macro_payload_chain_final_ = 0;
	macro_payload_chain_param_.clear();
	macro_stop_buf_.clear();
}

void PclPrinter::append_macro_definition_byte(uint8_t b)
{
	macros_[macro_id_].bytes.push_back(b);
}

void PclPrinter::append_macro_display_byte(uint8_t b)
{
	append_macro_definition_byte(b);
	if (macro_display_escape_pending_ && b == 'Z') {
		macro_display_capture_ = false;
		macro_display_escape_pending_ = false;
		macro_display_control_pending_ = false;
		return;
	}
	macro_display_escape_pending_ = (b == 0x1B);
}

bool PclPrinter::parse_macro_payload_echo_command(const std::vector<uint8_t> &cmd,
                                                  int &count,
                                                  bool &lowercase_final,
                                                  char &expected_final) const
{
	if (cmd.size() < 4 || cmd[0] != 0x1B)
		return false;

	size_t param_start = 0;
	char final = static_cast<char>(cmd.back());
	if (cmd[1] == '&' && cmd.size() >= 4) {
		char subgroup = static_cast<char>(cmd[2]);
		if (subgroup == 'p')
			expected_final = 'X';
		else if (subgroup == 'l')
			expected_final = 'W';
		else
			return false;
		param_start = 3;
	} else if (cmd[1] == '*' && cmd.size() >= 4) {
		if (cmd[2] != 'b')
			return false;
		expected_final = 'W';
		param_start = 3;
	} else if ((cmd[1] == '(' || cmd[1] == ')') && cmd.size() >= 5) {
		if (cmd[2] != 's')
			return false;
		expected_final = 'W';
		param_start = 3;
	} else {
		return false;
	}
	lowercase_final = final == static_cast<char>(std::tolower(expected_final));
	if (final != expected_final && !lowercase_final)
		return false;

	std::string param;
	for (size_t i = param_start; i + 1 < cmd.size(); i++) {
		uint8_t ch = cmd[i];
		if (!((ch >= '0' && ch <= '9') || ch == '.' ||
		      ch == '-' || ch == '+'))
			return false;
		param.push_back(static_cast<char>(ch));
	}
	double value = param.empty() ? 0.0 : std::atof(param.c_str());
	count = pcl_signed_integer_word(value);
	return true;
}

bool PclPrinter::parse_macro_generic_drain_command(
	const std::vector<uint8_t> &cmd, int &count, bool &lowercase_final) const
{
	if (cmd.size() < 4 || cmd[0] != 0x1B)
		return false;

	char final = static_cast<char>(cmd.back());
	lowercase_final = final == 'w';
	if (final != 'W' && !lowercase_final)
		return false;

	if (cmd[1] < 0x21 || cmd[1] > 0x2f)
		return false;

	size_t param_start = 2;
	if (cmd.size() >= 5 && cmd[2] >= 0x60 && cmd[2] <= 0x7e)
		param_start = 3;

	std::string param;
	for (size_t i = param_start; i + 1 < cmd.size(); i++) {
		uint8_t ch = cmd[i];
		if (!((ch >= '0' && ch <= '9') || ch == '.' ||
		      ch == '-' || ch == '+'))
			return false;
		param.push_back(static_cast<char>(ch));
	}
	double value = param.empty() ? 0.0 : std::atof(param.c_str());
	count = lowercase_final ? pcl_integer_word(value)
	                        : std::abs(pcl_signed_integer_word(value));
	return true;
}

bool PclPrinter::capture_macro_payload_chain_byte(uint8_t b)
{
	if (is_param_byte(b)) {
		macro_payload_chain_param_.push_back(static_cast<char>(b));
		return true;
	}

	if (b == static_cast<uint8_t>(std::tolower(macro_payload_chain_final_)) ||
	    b == static_cast<uint8_t>(macro_payload_chain_final_)) {
		double value = macro_payload_chain_param_.empty() ?
		               0.0 : std::atof(macro_payload_chain_param_.c_str());
		int count = pcl_signed_integer_word(value);
		if (b == static_cast<uint8_t>(std::tolower(macro_payload_chain_final_))) {
			macro_payload_chain_count_ = count;
			macro_payload_chain_param_.clear();
			return true;
		}
		int chained_count = macro_payload_chain_count_ >= 0 ?
		                    macro_payload_chain_count_ : count;
		if (macro_payload_chain_drain_) {
			macro_payload_drain_remaining_ = std::max(0, chained_count);
			macro_payload_drain_control_pending_ = false;
		} else {
			macro_payload_echo_remaining_ = std::max(0, chained_count);
			macro_payload_echo_control_pending_ = false;
		}
		macro_payload_chain_active_ = false;
		macro_payload_chain_drain_ = false;
		macro_payload_chain_count_ = -1;
		macro_payload_chain_final_ = 0;
		macro_payload_chain_param_.clear();
		return true;
	}

	macro_payload_chain_active_ = false;
	macro_payload_chain_drain_ = false;
	macro_payload_chain_count_ = -1;
	macro_payload_chain_final_ = 0;
	macro_payload_chain_param_.clear();
	return true;
}

bool PclPrinter::capture_macro_definition_byte(uint8_t b)
{
	if (macro_payload_drain_remaining_ > 0) {
		macro_stop_buf_.clear();
		macro_control_pending_ = false;
		if (macro_payload_drain_control_pending_) {
			macro_payload_drain_control_pending_ = false;
			macro_payload_drain_remaining_--;
		} else if (b == 0x1A) {
			macro_payload_drain_control_pending_ = true;
		} else {
			macro_payload_drain_remaining_--;
		}
		return true;
	}

	if (macro_payload_echo_remaining_ > 0) {
		macro_stop_buf_.clear();
		macro_control_pending_ = false;
		if (macro_payload_echo_control_pending_) {
			macro_payload_echo_control_pending_ = false;
			append_macro_definition_byte(b == 0x58 ? 0x00 : b);
			macro_payload_echo_remaining_--;
		} else if (b == 0x1A) {
			macro_payload_echo_control_pending_ = true;
		} else {
			append_macro_definition_byte(b);
			macro_payload_echo_remaining_--;
		}
		return true;
	}

	if (macro_payload_chain_active_) {
		macro_stop_buf_.clear();
		macro_control_pending_ = false;
		return capture_macro_payload_chain_byte(b);
	}

	if (macro_display_capture_) {
		macro_stop_buf_.clear();
		macro_control_pending_ = false;
		if (macro_display_control_pending_) {
			macro_display_control_pending_ = false;
			append_macro_display_byte(b == 0x58 ? 0x7f : b);
		} else if (b == 0x1A) {
			macro_display_control_pending_ = true;
		} else {
			append_macro_display_byte(b);
		}
		return true;
	}

	if (macro_control_pending_) {
		macro_control_pending_ = false;
		macro_stop_buf_.clear();
		if (b == 0x1A) {
			append_macro_definition_byte(0x1a);
			return true;
		}
		if (b == 0x58) {
			append_macro_definition_byte(0x7f);
			return true;
		}
		append_macro_definition_byte(0x1a);
	}

	if (b == 0x1A) {
		macro_stop_buf_.clear();
		macro_control_pending_ = true;
		return true;
	}

	if (b == 0x1B) {
		macro_command_start_ = macros_[macro_id_].bytes.size();
		macro_stop_buf_.clear();
		macro_stop_buf_.push_back(b);
		append_macro_definition_byte(b);
		return true;
	}

	append_macro_definition_byte(b);
	if (macro_stop_buf_.empty())
		return true;

	macro_stop_buf_.push_back(b);
	size_t len = macro_stop_buf_.size();
	if (len == 2 && macro_stop_buf_[0] == 0x1B && b == 'Y') {
		macro_display_capture_ = true;
		macro_display_escape_pending_ = false;
		macro_display_control_pending_ = false;
		macro_stop_buf_.clear();
		return true;
	}
	int payload_count = 0;
	bool lowercase_payload_final = false;
	char payload_final = 0;
	if (parse_macro_payload_echo_command(macro_stop_buf_, payload_count,
	                                     lowercase_payload_final,
	                                     payload_final)) {
		macros_[macro_id_].bytes.resize(macro_command_start_);
		if (lowercase_payload_final) {
			macro_payload_chain_active_ = true;
			macro_payload_chain_drain_ = false;
			macro_payload_chain_count_ = payload_count;
			macro_payload_chain_final_ = payload_final;
			macro_payload_chain_param_.clear();
		} else {
			macro_payload_echo_remaining_ = std::max(0, payload_count);
			macro_payload_echo_control_pending_ = false;
		}
		macro_stop_buf_.clear();
		return true;
	}
	if (parse_macro_generic_drain_command(macro_stop_buf_, payload_count,
	                                      lowercase_payload_final)) {
		macros_[macro_id_].bytes.resize(macro_command_start_);
		if (lowercase_payload_final) {
			macro_payload_chain_active_ = true;
			macro_payload_chain_drain_ = true;
			macro_payload_chain_count_ = payload_count;
			macro_payload_chain_final_ = 'W';
			macro_payload_chain_param_.clear();
		} else {
			macro_payload_drain_remaining_ = std::max(0, payload_count);
			macro_payload_drain_control_pending_ = false;
		}
		macro_stop_buf_.clear();
		return true;
	}
	if (len == 2 && macro_stop_buf_[0] == 0x1B && b == 'E') {
		if (macros_[macro_id_].bytes.size() >= 2)
			macros_[macro_id_].bytes.resize(macros_[macro_id_].bytes.size() - 2);
		macro_stop_buf_.clear();
		software_reset();
		return true;
	}
	if (len == 2 && b != '&') {
		if (b < 0x21 || b > 0x2f) {
			macro_stop_buf_.clear();
			return true;
		}
	}
	if (len >= 4) {
		if (macro_stop_buf_[1] == '&' && macro_stop_buf_[2] == 'f' &&
		    (b == 'X' || b == 'x')) {
			size_t pos = 3;
			bool negative = false;
			if (pos + 1 < len &&
			    (macro_stop_buf_[pos] == '-' || macro_stop_buf_[pos] == '+')) {
				negative = macro_stop_buf_[pos] == '-';
				pos++;
			}
			int value = 0;
			bool have_digit = false;
			for (; pos + 1 < len; pos++) {
				uint8_t ch = macro_stop_buf_[pos];
				if (ch == '.')
					break;
				if (ch < '0' || ch > '9') {
					macro_stop_buf_.clear();
					return true;
				}
				have_digit = true;
				value = value * 10 + (ch - '0');
			}
			if (pos + 1 < len && macro_stop_buf_[pos] == '.') {
				for (pos++; pos + 1 < len; pos++) {
					uint8_t ch = macro_stop_buf_[pos];
					if (ch < '0' || ch > '9') {
						macro_stop_buf_.clear();
						return true;
					}
				}
			}
			if (pos + 1 != len) {
				macro_stop_buf_.clear();
				return true;
			}
			if (negative)
				value = -value;
			if (have_digit && std::abs(value) == 1) {
				finish_macro_definition(macro_command_start_);
				state_ = State::Normal;
				param_pos_ = 0;
				param_buf_[0] = 0;
				param_relative_ = false;
			}
			macro_stop_buf_.clear();
			return true;
		}
		if (!((b >= '0' && b <= '9') || b == '.' || b == '-' || b == '+'))
			macro_stop_buf_.clear();
	}
	return true;
}

PclPrinter::MacroPrintEnvironment PclPrinter::capture_print_environment() const
{
	MacroPrintEnvironment env;
	env.st = st_;
	env.orientation = orientation_;
	env.page_size_code = page_size_code_;
	env.physical_w_in = physical_w_in_;
	env.physical_h_in = physical_h_in_;
	env.logical_x0_in = logical_x0_in_;
	env.logical_y0_in = logical_y0_in_;
	env.logical_w_in = logical_w_in_;
	env.logical_h_in = logical_h_in_;
	env.hmi_in = hmi_in_;
	env.vmi_in = vmi_in_;
	env.text_length_in = text_length_in_;
	env.text_length_custom = text_length_custom_;
	env.vfc_limit_in = vfc_limit_in_;
	env.font_req[0] = font_req_[0];
	env.font_req[1] = font_req_[1];
	env.active_font_slot = active_font_slot_;
	env.vfc_table = vfc_table_;
	env.vfc_last_line = vfc_last_line_;
	env.vfc_text_last_line = vfc_text_last_line_;
	env.underline_selector = underline_selector_;
	env.pending_cursor_y = pending_cursor_y_;
	env.raster_resolution = raster_resolution_;
	env.raster_mode = raster_mode_;
	env.raster_scale = raster_scale_;
	env.raster_active = raster_active_;
	env.raster_x_in = raster_x_in_;
	env.raster_row = raster_row_;
	env.rect_w_in = rect_w_in_;
	env.rect_h_in = rect_h_in_;
	env.fill_pattern = fill_pattern_;
	env.copy_count = copy_count_;
	env.wrap_enabled = wrap_enabled_;
	env.selected_soft_font_id[0] = selected_soft_font_id_[0];
	env.selected_soft_font_id[1] = selected_soft_font_id_[1];
	env.download_font_slot = download_font_slot_;
	env.cursor_stack = cursor_stack_;
	env.previous_width_pending = previous_width_pending_;
	env.previous_text_width_in = previous_text_width_in_;
	env.previous_text_advance_in = previous_text_advance_in_;
	return env;
}

void PclPrinter::restore_print_environment(const MacroPrintEnvironment &env)
{
	st_ = env.st;
	orientation_ = env.orientation;
	page_size_code_ = env.page_size_code;
	physical_w_in_ = env.physical_w_in;
	physical_h_in_ = env.physical_h_in;
	logical_x0_in_ = env.logical_x0_in;
	logical_y0_in_ = env.logical_y0_in;
	logical_w_in_ = env.logical_w_in;
	logical_h_in_ = env.logical_h_in;
	hmi_in_ = env.hmi_in;
	vmi_in_ = env.vmi_in;
	text_length_in_ = env.text_length_in;
	text_length_custom_ = env.text_length_custom;
	vfc_limit_in_ = env.vfc_limit_in;
	font_req_[0] = env.font_req[0];
	font_req_[1] = env.font_req[1];
	active_font_slot_ = env.active_font_slot;
	vfc_table_ = env.vfc_table;
	vfc_last_line_ = env.vfc_last_line;
	vfc_text_last_line_ = env.vfc_text_last_line;
	underline_selector_ = env.underline_selector;
	pending_cursor_y_ = env.pending_cursor_y;
	raster_resolution_ = env.raster_resolution;
	raster_mode_ = env.raster_mode;
	raster_scale_ = env.raster_scale;
	raster_active_ = env.raster_active;
	raster_x_in_ = env.raster_x_in;
	raster_row_ = env.raster_row;
	rect_w_in_ = env.rect_w_in;
	rect_h_in_ = env.rect_h_in;
	fill_pattern_ = env.fill_pattern;
	copy_count_ = env.copy_count;
	wrap_enabled_ = env.wrap_enabled;
	selected_soft_font_id_[0] = env.selected_soft_font_id[0];
	selected_soft_font_id_[1] = env.selected_soft_font_id[1];
	download_font_slot_ = env.download_font_slot;
	cursor_stack_ = env.cursor_stack;
	previous_width_pending_ = env.previous_width_pending;
	previous_text_width_in_ = env.previous_text_width_in;
	previous_text_advance_in_ = env.previous_text_advance_in;
	restart_underline_span();
}

void PclPrinter::replay_macro(int id, MacroReplayMode mode)
{
	if (macro_replay_depth_ >= 32)
		return;
	auto it = macros_.find(id);
	if (it == macros_.end())
		return;
	MacroPrintEnvironment env;
	const bool restores_environment = (mode != MacroReplayMode::Execute);
	if (restores_environment) {
		flush_underline_span();
		env = capture_print_environment();
	}
	macro_replay_depth_++;
	replaying_macro_ = true;
	const std::vector<uint8_t> bytes = it->second.bytes;
	state_ = State::Normal;
	for (uint8_t byte : bytes)
		parse_byte(byte);
	state_ = State::Normal;
	if (restores_environment) {
		flush_underline_span();
		restore_print_environment(env);
	}
	macro_replay_depth_--;
	replaying_macro_ = macro_replay_depth_ > 0;
}

std::unique_ptr<PrinterSim> create_pcl_printer(PrinterModel model, PdfWriter &pdf)
{
	return std::make_unique<PclPrinter>(model, pdf);
}
