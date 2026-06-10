// license:BSD-3-Clause
// copyright-holders:Stephen Hurd
// Apple ImageWriter II emulation per Technical Reference Manual (1986)
// Appendix A command summary, pages 141-146.
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
	};
	State state_ = State::Normal;
	uint8_t esc_cmd_ = 0;
	char digit_buf_[5] = {};
	int digit_pos_ = 0;
	int digit_expect_ = 0;
	uint8_t sw_p1_ = 0;
	bool sw_is_z_ = false;

	void start_digits(uint8_t cmd, int count);
	void dispatch_digits(int val);
	void dispatch_sw(uint8_t p1, uint8_t p2);
	int parse_digit_val() const;
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
		if (val >= 1 && val <= 9999)
			st_.page_height_in = static_cast<float>(val) / 144.0f;
		break;
	case 'F': { // Table A-14/A-17: place head nnnn dot columns from left margin
		float dpi = st_.proportional
		    ? static_cast<float>(st_.prop_dpi)
		    : st_.pitch_cpi * 8.0f;
		st_.x_pos = st_.left_margin_in + static_cast<float>(val) / dpi;
		break;
	}
	case 's':  // Table A-11: set proportional dot spacing (0-9)
		st_.prop_spacing = val;
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
	// Table A-16: CR-only vs CR+LF+FF
	else if (p1 == 0x40 && p2 == 0x00) {
		// stored as flag if needed
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

void ImageWriterPrinter::parse_byte(uint8_t b)
{
	switch (state_) {
	case State::CollectDigits:
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

	case State::CollectSw1:
		sw_p1_ = b;
		state_ = State::CollectSw2;
		return;

	case State::CollectSw2:
		state_ = State::Normal;
		dispatch_sw(sw_p1_, b);
		return;

	case State::EscA:
		state_ = State::Normal;
		if (b == '0')      st_.font_mode = 0;
		else if (b == '1') st_.font_mode = 1;
		else if (b == '2') st_.font_mode = 2;
		return;

	case State::EscCrIns:  // Table A-16: ESC l 0/1
		state_ = State::Normal;
		st_.cr_insertion = (b == '0');
		return;

	case State::MultiLF: { // CTRL-_ n: feed 1-15 lines
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

	case State::Esc:
		state_ = State::Normal;
		switch (b) {
		// Table A-19: reset defaults
		case 'c': cancel_pending_line(); st_ = PrinterState{}; apply_config(cfg_); break;

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
		case 'v': break;

		// Table A-15: paper-out sensor
		case 'O': break;
		case 'o': break;

		// Table A-9: user-designed characters
		case '-': break;  // max custom width 8 dots (no-op)
		case '+': break;  // max custom width 16 dots (no-op)
		case '$': st_.mousetext_mode = false; break;  // back to normal font

		// Font selection (IW native)
		case 'm': st_.font_mode = 0; break;
		case 'M': st_.font_mode = 2; break;
		case 'a': state_ = State::EscA; break;

		// Table A-9: MouseText / custom font
		case '&': st_.mousetext_mode = true; break;

		// Table A-18: color select (1 ASCII digit param)
		case 'K': start_digits('K', 1); break;

		// Table A-14: clear all tabs
		case '0': st_.tab_count = 0; break;

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
		// Not yet implemented — would need GfxData state

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

	// Control codes
	switch (b) {
	case 0x1B: state_ = State::Esc; return;
	case 0x0D: carriage_return(); return;           // Table A-14
	case 0x0A: line_feed(); return;                 // Table A-15
	case 0x0C: form_feed(); return;                 // Table A-15
	case 0x0E: st_.expanded = true; return;         // Table A-10/A-12
	case 0x0F: st_.expanded = false; return;        // Table A-10/A-12
	case 0x08:                                      // Table A-14: backspace
		st_.x_pos -= 1.0f / static_cast<float>(st_.pitch_cpi);
		if (st_.x_pos < st_.left_margin_in)
			st_.x_pos = st_.left_margin_in;
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
