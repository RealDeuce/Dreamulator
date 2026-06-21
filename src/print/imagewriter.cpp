// license:BSD-3-Clause
// copyright-holders:Stephen Hurd
// Apple ImageWriter II emulation per Technical Reference Manual (1986)
// Appendix A command summary, pages 141-146.
#include "dotrender.h"
#include "printer.h"
#include <cstdio>
#include <cstring>

class ImageWriterPrinter : public PrinterSim {
public:
	using PrinterSim::PrinterSim;

protected:
	void parse_byte(uint8_t b) override;

private:
	enum class State {
		Normal, Esc,
		CollectDigits,
		CollectRepChar,
		CollectSw1, CollectSw2,
		EscA,
		EscCrIns,
		MultiLF,
		BackspaceChar,
		CustomLoadChar,
		CustomLoadPrefix,
		CustomLoadColumns,
		GfxData,
		GfxRepeatByte,
		TabListDigits,
		TabListDelim,
		CtrlBracketFirstValue,
		CtrlBracketFirstAt,
		CtrlBracketBodyValue,
		CtrlBracketBodyAt,
		CtrlBracketTrailer,
	};
	State state_ = State::Normal;
	uint8_t esc_cmd_ = 0;
	char digit_buf_[5] = {};
	int digit_pos_ = 0;
	int digit_expect_ = 0;
	uint8_t sw_p1_ = 0;
	bool sw_is_z_ = false;
	int gfx_remaining_ = 0;
	bool ctrl_bracket_end_ = false;
	bool ctrl_bracket_return_ = false;
	bool software_select_response_ = false;
	bool lf_ff_print_trigger_ = true;
	int custom_load_width_ = 8;
	uint8_t custom_load_char_ = 0;
	int custom_load_pos_ = 0;
	int custom_load_remaining_ = 0;
	bool custom_load_bottom_wires_ = false;
	bool tab_list_is_clear_ = false;
	int tab_list_prev_ = 0;
	int tab_list_count_ = 0;

	void start_digits(uint8_t cmd, int count);
	void dispatch_digits(int val);
	void dispatch_sw(uint8_t p1, uint8_t p2);
	int parse_digit_val() const;
	uint8_t command_byte(uint8_t b) const;
	float char_advance(uint8_t ch) const;
	float graphics_hdpi() const;
	float graphics_dot_width() const;
	void emit_gfx_col(uint8_t data);
	void clear_custom_chars();
	bool begin_custom_char(uint8_t ch);
	bool begin_custom_columns(uint8_t width_code);
	void store_custom_col(uint8_t data);
	void begin_ctrl_bracket();
	void consume_ctrl_bracket_value(uint8_t b);
	void start_tab_list(bool is_clear);
	void dispatch_tab_value(int val);
	void compact_tab_table();
	void feed_without_print(float delta);
	static uint8_t ribbon_command_color(int val);
};

void ImageWriterPrinter::start_digits(uint8_t cmd, int count)
{
	esc_cmd_ = cmd;
	digit_pos_ = 0;
	digit_expect_ = count;
	memset(digit_buf_, 0, sizeof(digit_buf_));
	state_ = State::CollectDigits;
}

int ImageWriterPrinter::parse_digit_val() const
{
	int val = 0;
	for (int i = 0; i < digit_expect_; i++) {
		char c = digit_buf_[i];
		int d = (c >= '0' && c <= '9') ? c - '0' : 0;
		val = val * 10 + d;
	}
	return val;
}

uint8_t ImageWriterPrinter::command_byte(uint8_t b) const
{
	return st_.include_8th_bit ? b : static_cast<uint8_t>(b & 0x7F);
}

float ImageWriterPrinter::char_advance(uint8_t ch) const
{
	ch = command_byte(ch);
	if (st_.mousetext_mode && ch >= 0x40 && ch <= 0x5F)
		ch = static_cast<uint8_t>(ch + 0x80);
	if (st_.use_user_chars && st_.use_high_user_chars && ch >= 0x20 && ch <= 0x6F)
		ch = static_cast<uint8_t>(ch | 0x80);

	float char_w_in = 1.0f / static_cast<float>(st_.pitch_cpi);
	if (st_.proportional) {
		uint8_t gw = (st_.use_user_chars && st_.user_char_defined[ch])
		    ? static_cast<uint8_t>(st_.user_char_prefix[ch] & 0x1F)
		    : get_iw2_corr_prop_width(ch);
		if (gw > 0)
			char_w_in = static_cast<float>(gw + st_.prop_spacing) /
			            static_cast<float>(st_.prop_dpi);
	}
	if (st_.expanded || st_.expanded_line)
		char_w_in *= 2.0f;
	return char_w_in;
}

float ImageWriterPrinter::graphics_hdpi() const
{
	return st_.proportional
	    ? static_cast<float>(st_.prop_dpi)
	    : st_.pitch_cpi * 8.0f;
}

float ImageWriterPrinter::graphics_dot_width() const
{
	float dpi = graphics_hdpi();
	return 1.0f / dpi;
}

uint8_t ImageWriterPrinter::ribbon_command_color(int val)
{
	if (val == 0)
		return 0x08;
	if (val == 3)
		return 0x04;
	if (val == 4)
		return 0x03;
	return static_cast<uint8_t>(val & 0x0F);
}

void ImageWriterPrinter::emit_gfx_col(uint8_t data)
{
	float dot_w = graphics_dot_width();
	bool expanded = st_.expanded || st_.expanded_line;
	float advance = expanded ? dot_w * 2.0f : dot_w;
	if (data == 0) {
		st_.x_pos += advance;
		return;
	}

	flush_pending_line();
	new_page_if_needed();
	mark_line_output();
	page_dirty_ = true;

	constexpr float pin_h = 1.0f / 72.0f;
	float bold_off = (1.0f / 80.0f) * 0.5f;
	bool force_ltr = st_.unidirectional || st_.unidirectional_line || line_force_ltr_;
	float bidi_offset = (force_ltr || st_.line_dir_ltr) ? 0.0f : (1.0f / 120.0f);
	set_ribbon_ink(*dots_, st_.ribbon_color);

	auto stamp_graphics_dot = [&](float gx, float gy) {
		dots_->stamp_pin(*page_, gx, gy, prof_.render_dpi,
		                 prof_.dot_radius_mm, prof_.jitter_mm,
		                 prof_.dot_intensity, prof_.dot_sharpness,
		                 prof_.overprint_gamma, prof_.radius_variance,
		                 prof_.intensity_variance, prof_.dot_edge_softness,
		                 prof_.dot_x_scale, prof_.dot_y_scale);
	};

	for (int pin = 0; pin < 8; pin++) {
		if (!(data & (1U << pin)))
			continue;

		float x = st_.x_pos + bidi_offset;
		float y = st_.y_pos + static_cast<float>(pin) * pin_h - 9.0f * pin_h;
		stamp_graphics_dot(x, y);
		if (expanded) {
			stamp_graphics_dot(x + dot_w, y);
		}
		if (st_.bold) {
			stamp_graphics_dot(x + bold_off, y);
			if (expanded) {
				stamp_graphics_dot(x + dot_w + bold_off, y);
			}
		}
	}

	st_.x_pos += advance;
}

void ImageWriterPrinter::clear_custom_chars()
{
	std::memset(st_.user_char_defined, 0, sizeof(st_.user_char_defined));
	std::memset(st_.user_char_prefix, 0, sizeof(st_.user_char_prefix));
	std::memset(st_.user_char_glyph, 0, sizeof(st_.user_char_glyph));
}

bool ImageWriterPrinter::begin_custom_char(uint8_t ch)
{
	ch = command_byte(ch);

	if (ch == 0x04)
		return false;

	if (ch >= 0x20 && ch <= 0x7E) {
		custom_load_char_ = ch;
	} else if (custom_load_width_ <= 8 && ch >= 0xA0 && ch <= 0xEF) {
		custom_load_char_ = ch;
	} else {
		state_ = State::Normal;
		return false;
	}

	custom_load_pos_ = 0;
	custom_load_remaining_ = 0;
	custom_load_bottom_wires_ = false;
	std::memset(st_.user_char_glyph[custom_load_char_], 0,
	            sizeof(st_.user_char_glyph[custom_load_char_]));
	st_.user_char_prefix[custom_load_char_] = 0;
	st_.user_char_defined[custom_load_char_] = false;
	state_ = State::CustomLoadPrefix;
	return true;
}

bool ImageWriterPrinter::begin_custom_columns(uint8_t width_code)
{
	uint8_t width = static_cast<uint8_t>(width_code & 0x1F);
	bool top = width_code >= 'A' && width_code <= 'P';
	bool bottom = width_code >= 'a' && width_code <= 'p';
	if ((!top && !bottom) || width == 0 || width > custom_load_width_) {
		state_ = State::Normal;
		return false;
	}

	st_.user_char_prefix[custom_load_char_] = width_code;
	custom_load_bottom_wires_ = bottom;
	custom_load_pos_ = 0;
	custom_load_remaining_ = width;
	state_ = State::CustomLoadColumns;
	return true;
}

void ImageWriterPrinter::store_custom_col(uint8_t data)
{
	if (custom_load_remaining_ <= 0 || custom_load_pos_ >= 16)
		return;

	uint16_t col = 0;
	for (int pin = 0; pin < 8; pin++) {
		if (data & (1U << pin))
			col |= static_cast<uint16_t>(1U << pin);
	}
	if (custom_load_bottom_wires_)
		col = static_cast<uint16_t>((col << 1) & 0x1FF);
	st_.user_char_glyph[custom_load_char_][custom_load_pos_++] = col;
	if (--custom_load_remaining_ == 0) {
		st_.user_char_defined[custom_load_char_] = true;
		state_ = State::CustomLoadChar;
	}
}

void ImageWriterPrinter::start_tab_list(bool is_clear)
{
	tab_list_is_clear_ = is_clear;
	tab_list_prev_ = 0;
	tab_list_count_ = 0;
	if (!is_clear)
		st_.tab_count = 0;
	digit_pos_ = 0;
	digit_expect_ = 3;
	memset(digit_buf_, 0, sizeof(digit_buf_));
	state_ = State::TabListDigits;
}

void ImageWriterPrinter::dispatch_tab_value(int val)
{
	if (val < 1 || val >= 140) {
		st_.tab_count = 0;
		state_ = State::Normal;
		return;
	}
	if (tab_list_is_clear_) {
		// Scan and zero matching entries
		for (int i = 0; i < st_.tab_count; i++) {
			if (st_.tab_stops[i] == val)
				st_.tab_stops[i] = 0;
		}
	} else {
		// Set: values must be strictly increasing
		if (val <= tab_list_prev_ || st_.tab_count >= 32) {
			st_.tab_count = 0;
			state_ = State::Normal;
			return;
		}
		st_.tab_stops[st_.tab_count++] = val;
		tab_list_prev_ = val;
	}
	state_ = State::TabListDelim;
}

void ImageWriterPrinter::compact_tab_table()
{
	int dst = 0;
	for (int src = 0; src < st_.tab_count; src++) {
		if (st_.tab_stops[src] != 0)
			st_.tab_stops[dst++] = st_.tab_stops[src];
	}
	st_.tab_count = dst;
}

void ImageWriterPrinter::dispatch_digits(int val)
{
	switch (esc_cmd_) {
	case 'T':  // Table A-15: line spacing nn/144 inch (nn = 01..99)
		if (val >= 1 && val <= 99)
			st_.line_spacing_in = static_cast<float>(val) / 144.0f;
		break;
	case 'L':  // Table A-13: left margin at column nnn
		st_.left_margin_in = static_cast<float>(val) /
		                     static_cast<float>(st_.pitch_cpi);
		break;
	case 'H':  // Table A-13: page length nnnn/144 inch
		if (val >= 1 && val <= 9999 &&
		    (st_.perf_skip_lines == 0 || val > 0x0090))
			st_.page_height_in = static_cast<float>(val) / 144.0f;
		break;
	case 'F': { // Table A-14/A-17: place head nnnn dot columns from left margin
		float dpi = graphics_hdpi();
		int max_col = static_cast<int>(dpi * 8.0f + 0.5f) - 1;
		float target = st_.left_margin_in + static_cast<float>(val) / dpi;
		if (val <= max_col && target > st_.x_pos + 0.00001f)
			st_.x_pos = target;
		break;
	}
	case 's':  // Table A-11: set proportional dot spacing (0-9)
		st_.prop_spacing = val;
		break;
	case 'K':  // Table A-18: color select
		if (val >= 0 && val <= 6)
			st_.ribbon_color = ribbon_command_color(val);
		break;
	case 'u': { // Table A-14: add one tab stop at column nnn
		if (st_.tab_count < 32 && val >= 1)
			st_.tab_stops[st_.tab_count++] = val;
		break;
	}
	case 'R':  // Table A-19: repeat char — switch to collect the char byte
		esc_cmd_ = 'R';
		digit_buf_[0] = static_cast<char>(val & 0xFF);
		digit_buf_[1] = static_cast<char>((val >> 8) & 0xFF);
		state_ = State::CollectRepChar;
		return;
	case 'G':
	case 'S':
		gfx_remaining_ = val;
		state_ = gfx_remaining_ > 0 ? State::GfxData : State::Normal;
		return;
	case 'g':
		gfx_remaining_ = val * 8;
		state_ = gfx_remaining_ > 0 ? State::GfxData : State::Normal;
		return;
	case 'V':
		gfx_remaining_ = val;
		state_ = State::GfxRepeatByte;
		return;
	default:
		break;
	}
	state_ = State::Normal;
}

void ImageWriterPrinter::dispatch_sw(uint8_t p1, uint8_t p2)
{
	// Table A-12: slashed zeros
	if (p1 == 0x00 && p2 == 0x01) {
		st_.slashed_zero = !sw_is_z_;
	}
	// Table A-15: perforation skip
	else if (p1 == 0x00 && p2 == 0x04) {
		st_.perf_skip_lines = sw_is_z_ ? 3 : 0;
	}
	// Table A-19: 8th data bit
	else if (p1 == 0x00 && p2 == 0x20) {
		st_.include_8th_bit = sw_is_z_;
	}
	// Table A-19: software select response
	else if (p1 == 0x10 && p2 == 0x00) {
		software_select_response_ = sw_is_z_;
	}
	// Table A-16: CR-only vs CR+LF+FF
	else if (p1 == 0x40 && p2 == 0x00) {
		lf_ff_print_trigger_ = !sw_is_z_;
	}
	// Table A-16: auto LF after CR
	// ESC D = adds auto LF, ESC Z = no auto LF
	else if (p1 == 0x80 && p2 == 0x00) {
		st_.auto_lf = !sw_is_z_;
	}
	// Table A-16: LF when line is full
	// ESC D = adds LF, ESC Z = no LF
	else if (p1 == 0x20 && p2 == 0x00) {
		st_.lf_when_full = !sw_is_z_;
	}
	// International charset bits (p1 = bitmask 0x01..0x07, p2 = 0x00)
	// Table A-5: ESC Z = open (clear bits), ESC D = closed (set bits)
	else if (p2 == 0x00 && p1 >= 0x01 && p1 <= 0x07) {
		if (sw_is_z_)
			st_.charset &= ~p1;
		else
			st_.charset |= p1;
	}
}

void ImageWriterPrinter::begin_ctrl_bracket()
{
	ctrl_bracket_end_ = false;
	ctrl_bracket_return_ = false;
	state_ = State::CtrlBracketFirstValue;
}

void ImageWriterPrinter::consume_ctrl_bracket_value(uint8_t b)
{
	uint8_t v = static_cast<uint8_t>(b & 0x7F);
	ctrl_bracket_end_ = (v == 'A');
	ctrl_bracket_return_ = (v == 'C') || (v != 'A' && (v & 1) != 0);
	state_ = State::CtrlBracketBodyAt;
}

void ImageWriterPrinter::feed_without_print(float delta)
{
	new_page_if_needed();
	page_dirty_ = true;
	st_.y_pos += st_.reverse_lf ? -delta : delta;
	if (st_.y_pos < st_.top_margin_in)
		st_.y_pos = st_.top_margin_in;
}

void ImageWriterPrinter::parse_byte(uint8_t b)
{
	switch (state_) {
	case State::CollectDigits:
		b = command_byte(b);
		if ((b >= '0' && b <= '9') || b == ' ') {
			digit_buf_[digit_pos_++] = (b == ' ') ? '0' : static_cast<char>(b);
			if (digit_pos_ >= digit_expect_) {
				int val = parse_digit_val();
				dispatch_digits(val);
			}
			return;
		}
		state_ = State::Normal;
		parse_byte(b);
		return;

	case State::CollectRepChar:
		state_ = State::Normal;
		{
			int count = static_cast<int>(
			    static_cast<uint8_t>(digit_buf_[0]) |
			    (static_cast<uint8_t>(digit_buf_[1]) << 8));
			for (int i = 0; i < count; i++)
				emit_char(b);
		}
		return;

	case State::TabListDigits:
		b = command_byte(b);
		if ((b >= '0' && b <= '9') || b == ' ') {
			digit_buf_[digit_pos_++] = (b == ' ') ? '0' : static_cast<char>(b);
			if (digit_pos_ >= digit_expect_) {
				int val = parse_digit_val();
				dispatch_tab_value(val);
			}
			return;
		}
		// Non-digit before completing a value: clear table
		st_.tab_count = 0;
		state_ = State::Normal;
		return;

	case State::TabListDelim:
		b = command_byte(b);
		if (b == ',') {
			if (++tab_list_count_ >= 32) {
				// Max entries reached; treat as done
				if (tab_list_is_clear_)
					compact_tab_table();
				state_ = State::Normal;
				return;
			}
			digit_pos_ = 0;
			memset(digit_buf_, 0, sizeof(digit_buf_));
			state_ = State::TabListDigits;
			return;
		}
		if (b == '.') {
			if (tab_list_is_clear_)
				compact_tab_table();
			state_ = State::Normal;
			return;
		}
		// Unrecognized terminator: clear table
		st_.tab_count = 0;
		state_ = State::Normal;
		return;

	case State::CollectSw1:
		sw_p1_ = b;
		state_ = State::CollectSw2;
		return;

	case State::CollectSw2:
		state_ = State::Normal;
		dispatch_sw(sw_p1_, b);
		return;

	case State::EscA:
		b = command_byte(b);
		state_ = State::Normal;
		if (b == '0')      st_.font_mode = 0;  // correspondence
		else if (b == '1') st_.font_mode = 2;  // draft (ROM AA70 value 2)
		else if (b == '2') st_.font_mode = 1;  // NLQ (ROM AA70 value 1)
		return;

	case State::EscCrIns:  // Table A-16: ESC l 0/1
		b = command_byte(b);
		state_ = State::Normal;
		st_.cr_insertion = (b == '0');
		return;

	case State::MultiLF: { // CTRL-_ n: feed 1-15 lines
		b = command_byte(b);
		state_ = State::Normal;
		int n = 0;
		if (b >= '1' && b <= '9')
			n = b - '0';
		else if (b >= ':' && b <= '?')
			n = b - '0';
		new_page_if_needed();
		page_dirty_ = true;
		for (int i = 0; i < n; i++)
			line_feed();
		return;
	}

	case State::BackspaceChar: {
		b = command_byte(b);
		state_ = State::Normal;
		st_.x_pos -= char_advance(b);
		if (st_.x_pos < st_.left_margin_in)
			st_.x_pos = st_.left_margin_in;
		emit_char(b);
		return;
	}

	case State::CustomLoadChar:
		if (command_byte(b) == 0x04) {
			state_ = State::Normal;
			return;
		}
		begin_custom_char(b);
		return;

	case State::CustomLoadPrefix:
		begin_custom_columns(command_byte(b));
		return;

	case State::CustomLoadColumns:
		store_custom_col(b);
		return;

	case State::GfxRepeatByte:
		for (int i = 0; i < gfx_remaining_; i++)
			emit_gfx_col(b);
		gfx_remaining_ = 0;
		state_ = State::Normal;
		return;

	case State::GfxData:
		emit_gfx_col(b);
		if (gfx_remaining_ > 0)
			gfx_remaining_--;
		if (gfx_remaining_ <= 0)
			state_ = State::Normal;
		return;

	case State::CtrlBracketFirstValue:
		if ((b & 0x7F) == 'A')
			state_ = State::CtrlBracketFirstAt;
		else
			state_ = State::Normal;
		return;

	case State::CtrlBracketFirstAt:
		state_ = ((b & 0x7F) == '@') ? State::CtrlBracketBodyValue
		                             : State::Normal;
		return;

	case State::CtrlBracketBodyValue:
		consume_ctrl_bracket_value(b);
		return;

	case State::CtrlBracketBodyAt:
		if ((b & 0x7F) != '@') {
			state_ = State::Normal;
			return;
		}
		if (ctrl_bracket_return_) {
			state_ = State::Normal;
			return;
		}
		state_ = ctrl_bracket_end_ ? State::CtrlBracketTrailer
		                          : State::CtrlBracketBodyValue;
		return;

	case State::CtrlBracketTrailer:
		state_ = State::Normal;
		if ((b & 0x7F) == 0x1E)
			fprintf(stderr, "ImageWriter: CTRL-] trailer 1E would hang real firmware\n");
		return;

	case State::Esc:
		b = command_byte(b);
		state_ = State::Normal;
		switch (b) {
		// Table A-19: reset defaults
		case 'c': flush_pending_line(); reset_printer_state(cfg_); break;

		// Table A-12: boldface
		case '!': st_.bold = true; break;
		case '"': st_.bold = false; break;

		// Table A-12: underline
		case 'X': st_.underline = true; break;
		case 'Y': st_.underline = false; break;

		// Table A-12: superscript / subscript
		case 'x': st_.superscript = true; st_.subscript = false; break;
		case 'y': st_.superscript = false; st_.subscript = true; break;
		case 'z': st_.superscript = false; st_.subscript = false; break;

		// Table A-12: half-height
		case 'w': st_.half_height = true; break;
		case 'W': st_.half_height = false; break;

		// Table A-10: character pitch
		case 'n': st_.pitch_cpi = 9; st_.proportional = false; break;
		case 'N': st_.pitch_cpi = 10; st_.proportional = false; break;
		case 'E': st_.pitch_cpi = 12; st_.proportional = false; break;
		case 'e': st_.pitch_cpi = 107.0f / 8.0f; st_.proportional = false; break;
		case 'q': st_.pitch_cpi = 15; st_.proportional = false; break;
		case 'Q': st_.pitch_cpi = 17; st_.proportional = false; break;
		case 'p': st_.proportional = true; st_.prop_dpi = 144; break;
		case 'P': st_.proportional = true; st_.prop_dpi = 160; break;

		// Table A-15: line spacing
		case 'A': st_.line_spacing_in = 1.0f / 6.0f; break;
		case 'B': st_.line_spacing_in = 1.0f / 8.0f; break;

		// Table A-15: line feed direction
		case 'f': st_.reverse_lf = false; break;
		case 'r': st_.reverse_lf = true; break;

		// Table A-14: unidirectional / bidirectional
		case '>': st_.unidirectional = true; break;
		case '<': st_.unidirectional = false; break;

		// Table A-15: TOF set
		case 'v': st_.top_margin_in = st_.y_pos - st_.line_spacing_in; break;

		// Table A-15: paper-out sensor
		case 'O': break;
		case 'o': break;

		// Table A-9: user-designed characters
		case '-':
			custom_load_width_ = 8;
			st_.include_8th_bit = true;
			clear_custom_chars();
			break;
		case '+':
			custom_load_width_ = 16;
			st_.include_8th_bit = true;
			clear_custom_chars();
			break;
		case '$':
			st_.mousetext_mode = false;
			st_.use_user_chars = false;
			st_.use_high_user_chars = false;
			break;
		case '\'':
			st_.mousetext_mode = false;
			st_.use_user_chars = true;
			st_.use_high_user_chars = false;
			break;
		case '*':
			st_.mousetext_mode = false;
			st_.use_user_chars = true;
			st_.use_high_user_chars = true;
			break;
		case 'I':
			state_ = State::CustomLoadChar;
			break;

		// Font selection (IW native)
		case 'm': st_.font_mode = 0; break;  // correspondence
		case 'M': st_.font_mode = 1; break;  // NLQ (ROM AA70 value 1)
		case 'a': state_ = State::EscA; break;

		// Table A-9: MouseText / custom font
		case '&':
			st_.mousetext_mode = true;
			st_.use_user_chars = false;
			st_.use_high_user_chars = false;
			break;

		// Table A-18: color select (1 ASCII digit param)
		case 'K': start_digits('K', 1); break;

		// Table A-14: tab set/clear lists
		case '(': start_tab_list(false); break;
		case ')': start_tab_list(true); break;

		// Table A-14: clear all tabs
		case '0': st_.tab_count = 0; break;

		// Table A-11: extra proportional dot spacing (only in proportional pitch)
		case '1': case '2': case '3': case '4': case '5': case '6':
			if (st_.proportional)
				st_.x_pos += static_cast<float>(b - '0') /
				             static_cast<float>(st_.prop_dpi);
			break;

		// Table A-11: proportional dot spacing
		case 's': start_digits('s', 1); break;

		// Table A-16: CR insertion mode
		case 'l': state_ = State::EscCrIns; break;

		// Table A-14/A-17: print head position (4 ASCII digits)
		case 'F': start_digits('F', 4); break;

		// Table A-15: line spacing (2 ASCII digits)
		case 'T': start_digits('T', 2); break;

		// Table A-13: left margin (3 ASCII digits)
		case 'L': start_digits('L', 3); break;

		// Table A-13: page length (4 ASCII digits)
		case 'H': start_digits('H', 4); break;

		// Table A-14: add tab stop (3 ASCII digits)
		case 'u': start_digits('u', 3); break;

		// Table A-19: repeat character (3 ASCII digits + 1 char)
		case 'R': start_digits('R', 3); break;

		// Table A-17: graphics — ESC G nnnn (4 ASCII digits + data)
		// Table A-17: graphics — ESC S nnnn (same as ESC G)
		// Table A-17: graphics — ESC g nnn (3 ASCII digits, nnn*8 data)
		// Table A-17: graphics — ESC V nnnn c (4 digits + 1 byte)
		case 'G': start_digits('G', 4); break;
		case 'S': start_digits('S', 4); break;
		case 'g': start_digits('g', 3); break;
		case 'V': start_digits('V', 4); break;

		// Table A-16/A-19: software switches
		case 'Z':
			sw_is_z_ = true;
			state_ = State::CollectSw1;
			break;
		case 'D':
			sw_is_z_ = false;
			state_ = State::CollectSw1;
			break;

		default:
			fprintf(stderr, "ImageWriter: unknown ESC %02X '%c'\n",
			        b, b >= 0x20 ? b : '.');
			break;
		}
		return;

	case State::Normal:
		break;
	}

	b = command_byte(b);

	if (!st_.selected && b != 0x11)
		return;

	// Control codes
	switch (b) {
	case 0x1B: state_ = State::Esc; return;
	case 0x0D: carriage_return(); return;           // Table A-14
	case 0x0A:                                      // Table A-15
		if (st_.cr_insertion)
			st_.x_pos = st_.left_margin_in;
		if (lf_ff_print_trigger_)
			line_feed();
		else
			feed_without_print(st_.line_spacing_in);
		return;
	case 0x0B:                                      // ROM-observed vertical feed
		if (lf_ff_print_trigger_)
			flush_pending_line();
		feed_without_print(2.0f / 144.0f);
		st_.x_pos = st_.left_margin_in;
		st_.expanded_line = false;
		advance_line_direction();
		finish_printed_line();
		return;
	case 0x0C:                                      // Table A-15
		if (st_.cr_insertion)
			st_.x_pos = st_.left_margin_in;
		if (lf_ff_print_trigger_)
			form_feed();
		else {
			if (page_ && page_dirty_) {
				pdf_.add_page(*page_, prof_.render_dpi, text_buf_);
				text_buf_.clear();
				page_dirty_ = false;
			}
			page_.reset();
		}
		return;
	case 0x0E: st_.expanded = true; return;         // Table A-10/A-12
	case 0x0F: st_.expanded = false; return;        // Table A-10/A-12
	case 0x11:
		if (software_select_response_)
			st_.selected = true;
		return;
	case 0x13:
		if (software_select_response_)
			st_.selected = false;
		return;
	case 0x08:                                      // Table A-14: backspace
		state_ = State::BackspaceChar;
		return;
	case 0x09:                                      // Table A-14: tab
		for (int i = 0; i < st_.tab_count; i++) {
			float tab_pos = st_.left_margin_in +
			    static_cast<float>(st_.tab_stops[i]) /
			    static_cast<float>(st_.pitch_cpi);
			if (tab_pos > st_.x_pos) {
				st_.x_pos = tab_pos;
				return;
			}
		}
		return;
	case 0x18:                                      // Table A-19: CAN
		cancel_pending_line();
		st_.x_pos = st_.left_margin_in;
		return;
	case 0x1D:
		begin_ctrl_bracket();
		return;
	case 0x1F:                                      // Table A-15: multi-LF
		state_ = State::MultiLF;
		return;
	default: break;
	}

	if (b >= 0x20)
		emit_char(b);
}

std::unique_ptr<PrinterSim> create_imagewriter_printer(PrinterModel model, PdfWriter &pdf)
{
	return std::make_unique<ImageWriterPrinter>(model, pdf);
}
