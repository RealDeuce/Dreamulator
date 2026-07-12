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
	if (ch < 0x80)
		return ch;

	static constexpr uint16_t table[128] = {
		0x00C7,0x00FC,0x00E9,0x00E2,0x00E4,0x00E0,0x00E5,0x00E7,
		0x00EA,0x00EB,0x00E8,0x00EF,0x00EE,0x00EC,0x00C4,0x00C5,
		0x00C9,0x00E6,0x00C6,0x00F4,0x00F6,0x00F2,0x00FB,0x00F9,
		0x00FF,0x00D6,0x00DC,0x00A2,0x00A3,0x00A5,0x20A7,0x0192,
		0x00E1,0x00ED,0x00F3,0x00FA,0x00F1,0x00D1,0x00AA,0x00BA,
		0x00BF,0x2310,0x00AC,0x00BD,0x00BC,0x00A1,0x00AB,0x00BB,
		0x2591,0x2592,0x2593,0x2502,0x2524,0x2561,0x2562,0x2556,
		0x2555,0x2563,0x2551,0x2557,0x255D,0x255C,0x255B,0x2510,
		0x2514,0x2534,0x252C,0x251C,0x2500,0x253C,0x255E,0x255F,
		0x255A,0x2554,0x2569,0x2566,0x2560,0x2550,0x256C,0x2567,
		0x2568,0x2564,0x2565,0x2559,0x2558,0x2552,0x2553,0x256B,
		0x256A,0x2518,0x250C,0x2588,0x2584,0x258C,0x2590,0x2580,
		0x03B1,0x00DF,0x0393,0x03C0,0x03A3,0x03C3,0x00B5,0x03C4,
		0x03A6,0x0398,0x03A9,0x03B4,0x221E,0x03C6,0x03B5,0x2229,
		0x2261,0x00B1,0x2265,0x2264,0x2320,0x2321,0x00F7,0x2248,
		0x00B0,0x2219,0x00B7,0x221A,0x207F,0x00B2,0x25A0,0x00A0,
	};
	return table[ch - 0x80];
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
	int symbol_set_ = 0;

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
	symbol_set_ = 0;
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
	if ((b >= '0' && b <= '9') || b == '.' || b == '-' || b == '+') {
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
	} else if ((group == '(' || group == ')') && subgroup == 'U') {
		symbol_set_ = ival;
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

	uint32_t context = default_ljii_context_for_pitch(st_.pitch_cpi, st_.bold);
	LjiiGlyphInfo glyph = get_ljii_glyph(context, b);
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

	uint16_t cp = roman8_to_unicode(b);
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
