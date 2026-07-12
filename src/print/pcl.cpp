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
#include <vector>

namespace {

static constexpr float kDotsPerIn = 300.0f;
static constexpr int kSymbolRoman8 = 0x0115;

struct PageSize {
	float w;
	float h;
};

PageSize pcl_page_size(int code)
{
	switch (code) {
	case 1:  return { 8.5f, 11.0f };      // executive is model/region dependent; keep printable.
	case 2:  return { 8.5f, 11.0f };      // letter
	case 3:  return { 8.5f, 14.0f };      // legal
	case 6:  return { 7.25f, 10.5f };     // statement
	case 26: return { 8.27f, 11.69f };    // A4
	case 80: return { 3.875f, 7.5f };     // Monarch envelope
	case 81: return { 4.125f, 9.5f };     // COM10 envelope
	case 90: return { 4.33f, 8.66f };     // DL envelope
	case 91: return { 6.38f, 9.02f };     // C5 envelope
	default: return { 8.5f, 11.0f };
	}
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
	{0x0073,0x40,0xcd},{0x0073,0x5b,0xd8},{0x0073,0x5c,0xda},{0x0073,0x5d,0xdb},
	{0x0073,0x5e,0xaa},{0x0073,0x60,0xc8},{0x0073,0x7b,0xcc},{0x0073,0x7c,0xce},
	{0x0073,0x7d,0xcf},{0x0073,0x7e,0xb0},
	{0x0013,0x23,0xbb},{0x0013,0x40,0xcd},{0x0013,0x5b,0xd8},{0x0013,0x5c,0xda},
	{0x0013,0x5d,0xdb},{0x0013,0x5e,0xaa},{0x0013,0x60,0xc8},{0x0013,0x7b,0xcc},
	{0x0013,0x7c,0xce},{0x0013,0x7d,0xcf},{0x0013,0x7e,0xb0},
	{0x0033,0x40,0xbd},{0x0033,0x5b,0xa1},{0x0033,0x5c,0xbb},{0x0033,0x5d,0xbf},
	{0x0033,0x5e,0xaa},{0x0033,0x60,0xa9},{0x0033,0x7e,0xb0},
	{0x0053,0x23,0xbb},{0x0053,0x40,0xbd},{0x0053,0x5b,0xa1},{0x0053,0x5c,0xbb},
	{0x0053,0x5d,0xbf},{0x0053,0x5e,0xaa},{0x0053,0x60,0xa9},{0x0053,0x7b,0xc8},
	{0x0053,0x7c,0xc7},{0x0053,0x7d,0xc9},{0x0053,0x7e,0xb0},
};

uint8_t symbol_glyph_byte(int symbol_set, uint8_t ch)
{
	if (symbol_set == 0x0015 && ch >= 0x80)
		return 0;
	for (const auto &patch : kSymbolPatches)
		if (patch.symbol == symbol_set && patch.dst == ch)
			return patch.src;
	return ch;
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
		DisplayFunctions,
		DownloadData,
	};

	struct Macro {
		std::vector<uint8_t> bytes;
		bool permanent = false;
	};

	void reset_ljii_state();
	void process_normal(uint8_t b);
	void process_control(uint8_t b);
	void process_printable(uint8_t b);
	void process_escape(uint8_t b);
	void process_parameter_byte(uint8_t b);
	void apply_param(char group, char subgroup, double value, char term);
	void begin_payload(State state, int count);
	void finish_payload_byte(uint8_t b);
	void emit_transparent_byte(uint8_t b);
	void draw_rule(float x_in, float y_in, float w_in, float h_in, uint8_t gray);
	void draw_raster_row(const std::vector<uint8_t> &row);
	bool render_ljii_text(uint8_t b);
	uint16_t text_unicode(uint8_t b) const;
	uint8_t text_glyph_byte(uint8_t b) const;
	void set_page_size(int code);
	void set_orientation(int orientation);
	void publish_current_page();
	void maybe_record_macro_byte(uint8_t b);
	void replay_macro(int id);

	State state_ = State::Normal;
	State payload_state_ = State::Normal;
	char group_ = 0;
	char subgroup_ = 0;
	char param_buf_[64] = {};
	int param_pos_ = 0;
	int payload_remaining_ = 0;
	std::vector<uint8_t> payload_buf_;

	int line_term_ = 0;
	int orientation_ = 0;
	float physical_w_in_ = 8.5f;
	float physical_h_in_ = 11.0f;
	float hmi_in_ = 1.0f / 10.0f;
	float vmi_in_ = 1.0f / 6.0f;
	float text_length_in_ = 10.0f;
	int symbol_set_ = kSymbolRoman8;

	int raster_resolution_ = 300;
	int raster_compression_ = 0;
	float raster_x_in_ = 0.0f;
	float raster_y_in_ = 0.0f;
	int raster_row_ = 0;
	int raster_plane_ = 0;

	float rect_w_in_ = 0.0f;
	float rect_h_in_ = 0.0f;
	int fill_pattern_ = 0;

	int macro_id_ = 0;
	bool defining_macro_ = false;
	bool replaying_macro_ = false;
	std::map<int, Macro> macros_;
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
	payload_remaining_ = 0;
	payload_buf_.clear();
	line_term_ = 0;
	orientation_ = 0;
	physical_w_in_ = 8.5f;
	physical_h_in_ = 11.0f;
	hmi_in_ = 1.0f / 10.0f;
	vmi_in_ = 1.0f / 6.0f;
	text_length_in_ = 10.0f;
	symbol_set_ = kSymbolRoman8;
	raster_resolution_ = 300;
	raster_compression_ = 0;
	raster_x_in_ = 0.0f;
	raster_y_in_ = 0.0f;
	raster_row_ = 0;
	raster_plane_ = 0;
	rect_w_in_ = 0.0f;
	rect_h_in_ = 0.0f;
	fill_pattern_ = 0;
	macro_id_ = 0;
	defining_macro_ = false;
	replaying_macro_ = false;

	st_.pitch_cpi = 10.0f;
	st_.line_spacing_in = vmi_in_;
	st_.left_margin_in = 0.25f;
	st_.right_margin_in = 8.0f;
	st_.top_margin_in = 0.5f;
	st_.page_width_in = physical_w_in_;
	st_.page_height_in = physical_h_in_;
	st_.x_pos = st_.left_margin_in;
	st_.y_pos = st_.top_margin_in + st_.line_spacing_in;
}

void PclPrinter::parse_byte(uint8_t b)
{
	if (defining_macro_ && state_ == State::Normal && b != 0x1B)
		macros_[macro_id_].bytes.push_back(b);

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
			state_ = State::Parameterized;
		} else if (is_param_byte(b)) {
			subgroup_ = 0;
			param_pos_ = 0;
			param_buf_[0] = 0;
			state_ = State::Parameterized;
			process_parameter_byte(b);
		} else if ((b >= 'A' && b <= 'Z') || b == '@') {
			subgroup_ = 0;
			param_pos_ = 0;
			param_buf_[0] = 0;
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
	case State::DownloadData:
		finish_payload_byte(b);
		return;
	case State::DisplayFunctions:
		if (b == 0x1B) {
			state_ = State::Esc;
		} else {
			emit_transparent_byte(b);
		}
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
		st_.x_pos += hmi_in_ * 8.0f;
		if (st_.x_pos > st_.right_margin_in)
			st_.x_pos = st_.right_margin_in;
		break;
	case 0x0A:
		if (line_term_ == 2 || line_term_ == 3)
			carriage_return();
		line_feed();
		break;
	case 0x0C:
		if (line_term_ == 2 || line_term_ == 3)
			carriage_return();
		form_feed();
		break;
	case 0x0D:
		carriage_return();
		if (line_term_ == 1 || line_term_ == 3)
			line_feed();
		break;
	case 0x0E:
		break;
	case 0x0F:
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
	maybe_record_macro_byte(0x1B);
	maybe_record_macro_byte(b);

	if (b == 'E') {
		publish_current_page();
		PrinterConfig cfg = cfg_;
		reset_printer_state(cfg);
		reset_ljii_state();
		state_ = State::Normal;
		return;
	}
	if (b == 'Y') {
		state_ = State::DisplayFunctions;
		return;
	}
	if (b == 'Z' || b == 'z' || b == '9' || b == '=') {
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

void PclPrinter::process_parameter_byte(uint8_t b)
{
	if (is_param_byte(b)) {
		if (param_pos_ < static_cast<int>(sizeof(param_buf_)) - 1)
			param_buf_[param_pos_++] = static_cast<char>(b);
		return;
	}

	param_buf_[param_pos_] = 0;
	double value = param_pos_ > 0 ? std::atof(param_buf_) : 0.0;

	if ((b >= 'A' && b <= 'Z') || b == '@') {
		apply_param(group_, subgroup_, value, static_cast<char>(b));
		if (state_ == State::Parameterized)
			state_ = State::Normal;
	} else if (b >= 'a' && b <= 'z') {
		apply_param(group_, subgroup_, value,
		            static_cast<char>(std::toupper(b)));
		if (state_ == State::Parameterized) {
			param_pos_ = 0;
			param_buf_[0] = 0;
		}
	} else {
		state_ = State::Normal;
	}
}

void PclPrinter::apply_param(char group, char subgroup, double value, char term)
{
	int ival = static_cast<int>(std::lround(value));

	if (group == '&' && subgroup == 'l') {
		switch (term) {
		case 'A': set_page_size(ival); break;
		case 'D':
			if (ival > 0) {
				vmi_in_ = 1.0f / static_cast<float>(ival);
				st_.line_spacing_in = vmi_in_;
			}
			break;
		case 'E':
			st_.top_margin_in = std::max(0.0f, (float)value / 6.0f);
			break;
		case 'F':
			text_length_in_ = std::max(0.0f, (float)value / 6.0f);
			break;
		case 'H':
			break;
		case 'L':
			st_.perf_skip_lines = std::max(0, ival);
			break;
		case 'O':
			set_orientation(ival);
			break;
		case 'X':
			break;
		default:
			break;
		}
	} else if (group == '&' && subgroup == 'a') {
		switch (term) {
		case 'C':
			st_.x_pos = st_.left_margin_in + (float)value / st_.pitch_cpi;
			break;
		case 'H':
			st_.x_pos = std::max(0.0f, (float)value / 720.0f);
			break;
		case 'L':
			st_.left_margin_in = std::max(0.0f, (float)value / st_.pitch_cpi);
			st_.x_pos = std::max(st_.x_pos, st_.left_margin_in);
			break;
		case 'M':
			st_.right_margin_in = std::max(st_.left_margin_in,
			                               (float)value / st_.pitch_cpi);
			break;
		case 'R':
			st_.y_pos = st_.top_margin_in + (float)value * st_.line_spacing_in;
			break;
		case 'V':
			st_.y_pos = std::max(0.0f, (float)value / 720.0f);
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
			}
			break;
		default:
			break;
		}
	} else if ((group == '(' || group == ')') && subgroup == 's') {
		switch (term) {
		case 'B':
			st_.bold = (ival >= 3);
			break;
		case 'H':
			if (value > 0.0)
				st_.pitch_cpi = (float)value;
			break;
		case 'P':
			st_.proportional = (ival != 0);
			break;
		case 'S':
			st_.italic = (ival == 1);
			break;
		case 'T':
			break;
		case 'V':
			break;
		case 'W':
			begin_payload(State::DownloadData, std::max(0, ival));
			break;
		default:
			break;
		}
	} else if ((group == '(' || group == ')') && subgroup == 0) {
		if (term >= 'A' && term <= 'Z')
			symbol_set_ = pcl_symbol_value(ival, term);
	} else if (group == '&' && subgroup == 'p') {
		if (term == 'X')
			begin_payload(State::TransparentData, std::max(0, ival));
	} else if (group == '&' && subgroup == 'f') {
		switch (term) {
		case 'Y':
			macro_id_ = ival;
			break;
		case 'X':
			if (ival == 0) {
				defining_macro_ = true;
				macros_[macro_id_].bytes.clear();
			} else if (ival == 1) {
				defining_macro_ = false;
			} else if (ival == 2 || ival == 3) {
				replay_macro(macro_id_);
			} else if (ival == 6) {
				macros_.clear();
			} else if (ival == 8) {
				macros_.erase(macro_id_);
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
			st_.x_pos = std::max(0.0f, (float)value / kDotsPerIn);
			break;
		case 'Y':
			st_.y_pos = std::max(0.0f, (float)value / kDotsPerIn);
			break;
		default:
			break;
		}
	} else if (group == '*' && subgroup == 't') {
		if (term == 'R' && ival > 0)
			raster_resolution_ = ival;
	} else if (group == '*' && subgroup == 'r') {
		switch (term) {
		case 'A':
			raster_x_in_ = st_.x_pos;
			raster_y_in_ = st_.y_pos;
			raster_row_ = 0;
			raster_plane_ = std::max(0, ival);
			break;
		case 'B':
			raster_row_ = 0;
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
			raster_compression_ = std::max(0, std::min(3, ival));
			break;
		case 'W':
			begin_payload(State::RasterData, std::max(0, ival));
			break;
		case 'Y':
			raster_row_ += std::max(0, ival);
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
		case 'G':
			fill_pattern_ = ival;
			break;
		case 'H':
			rect_w_in_ = std::max(0.0f, (float)value / 720.0f);
			break;
		case 'P':
			draw_rule(st_.x_pos, st_.y_pos, rect_w_in_, rect_h_in_,
			          fill_pattern_ == 0 ? 0 : 64);
			break;
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
	payload_buf_.clear();
	payload_buf_.reserve(static_cast<size_t>(count));
}

void PclPrinter::finish_payload_byte(uint8_t b)
{
	if (payload_state_ == State::TransparentData) {
		emit_transparent_byte(b);
	} else {
		payload_buf_.push_back(b);
	}

	if (--payload_remaining_ > 0)
		return;

	if (payload_state_ == State::RasterData)
		draw_raster_row(payload_buf_);

	payload_buf_.clear();
	payload_state_ = State::Normal;
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
                           uint8_t gray)
{
	if (w_in <= 0.0f || h_in <= 0.0f)
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

	for (int y = y0; y < y1; y++) {
		for (int x = x0; x < x1; x++)
			page_->set_pixel(x, y, gray);
	}
}

void PclPrinter::draw_raster_row(const std::vector<uint8_t> &row)
{
	if (row.empty())
		return;
	new_page_if_needed();
	page_dirty_ = true;

	std::vector<uint8_t> decoded;
	if (raster_compression_ == 0) {
		decoded = row;
	} else if (raster_compression_ == 1) {
		for (size_t i = 0; i + 1 < row.size();) {
			uint8_t count = row[i++];
			uint8_t value = row[i++];
			decoded.insert(decoded.end(), (size_t)count + 1, value);
		}
	} else {
		decoded = row;
	}

	float scale = kDotsPerIn / (float)std::max(1, raster_resolution_);
	int dpi = prof_.render_dpi;
	int y0 = (int)std::lround((raster_y_in_ * kDotsPerIn +
	                          (float)raster_row_ * scale) *
	                         (float)dpi / kDotsPerIn);
	int row_px = std::max(1, (int)std::ceil(scale * (float)dpi / kDotsPerIn));
	int col_px = row_px;
	int bit_index = 0;
	for (uint8_t byte : decoded) {
		for (int bit = 7; bit >= 0; bit--, bit_index++) {
			if (!(byte & (1 << bit)))
				continue;
			int x0 = (int)std::lround((raster_x_in_ * kDotsPerIn +
			                          (float)bit_index * scale) *
			                         (float)dpi / kDotsPerIn);
			for (int yy = 0; yy < row_px; yy++)
				for (int xx = 0; xx < col_px; xx++)
					page_->set_pixel(x0 + xx, y0 + yy, 0);
		}
	}
	raster_row_++;
}

bool PclPrinter::render_ljii_text(uint8_t b)
{
	float char_w_in = 1.0f / std::max(1.0f, st_.pitch_cpi);
	if (b == 0x20) {
		if (st_.x_pos + char_w_in > st_.right_margin_in + 0.001f) {
			carriage_return();
			line_feed();
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

	uint8_t glyph_byte = text_glyph_byte(b);
	if (glyph_byte == 0)
		return true;
	uint32_t context = default_ljii_context_for_pitch(st_.pitch_cpi, st_.bold);
	LjiiGlyphInfo glyph = get_ljii_glyph(context, glyph_byte);
	if (!glyph.found || !glyph.data)
		return false;

	if (st_.x_pos + char_w_in > st_.right_margin_in + 0.001f) {
		carriage_return();
		line_feed();
	}

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
	if (symbol_set_ == kSymbolRoman8 || symbol_set_ == 0)
		return b;
	return symbol_glyph_byte(symbol_set_, b);
}

uint16_t PclPrinter::text_unicode(uint8_t b) const
{
	if (symbol_set_ == 0x0015)
		return b < 0x80 ? b : 0;
	uint8_t glyph_byte = text_glyph_byte(b);
	if (glyph_byte == 0)
		return 0;
	return roman8_to_unicode(glyph_byte);
}

void PclPrinter::set_page_size(int code)
{
	publish_current_page();
	PageSize sz = pcl_page_size(code);
	physical_w_in_ = sz.w;
	physical_h_in_ = sz.h;
	set_orientation(orientation_);
}

void PclPrinter::set_orientation(int orientation)
{
	publish_current_page();
	orientation_ = orientation & 1;
	if (orientation_ == 0) {
		st_.page_width_in = physical_w_in_;
		st_.page_height_in = physical_h_in_;
	} else {
		st_.page_width_in = physical_h_in_;
		st_.page_height_in = physical_w_in_;
	}
	st_.left_margin_in = 0.25f;
	st_.right_margin_in = std::max(0.25f, st_.page_width_in - 0.25f);
	st_.x_pos = st_.left_margin_in;
	st_.y_pos = st_.top_margin_in + st_.line_spacing_in;
}

void PclPrinter::publish_current_page()
{
	flush_pending_line();
	if (page_ && page_dirty_)
		form_feed();
}

void PclPrinter::maybe_record_macro_byte(uint8_t b)
{
	if (defining_macro_)
		macros_[macro_id_].bytes.push_back(b);
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
	for (uint8_t byte : bytes)
		parse_byte(byte);
	replaying_macro_ = false;
}

std::unique_ptr<PrinterSim> create_pcl_printer(PrinterModel model, PdfWriter &pdf)
{
	return std::make_unique<PclPrinter>(model, pdf);
}
