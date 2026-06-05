// license:BSD-3-Clause
// copyright-holders:Stephen Hurd
// Apple ImageWriter II command parser per Technical Reference Manual (1986)
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
	case 'T':
		if (val >= 1 && val <= 99)
			st_.line_spacing_in = static_cast<float>(val) / 144.0f;
		break;
	case 'L':
		st_.left_margin_in = static_cast<float>(val) /
		                     static_cast<float>(st_.pitch_cpi);
		break;
	case 'H':
		if (val >= 1 && val <= 9999)
			st_.page_height_in = static_cast<float>(val) / 144.0f;
		break;
	case 'u': {
		if (st_.tab_count < 32 && val >= 1)
			st_.tab_stops[st_.tab_count++] = val;
		break;
	}
	case 'R':
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
	if (p1 == 0x00 && p2 == 0x20) {
		st_.include_8th_bit = sw_is_z_;
	} else if (p1 == 0x00 && p2 == 0x01) {
		st_.slashed_zero = !sw_is_z_;
	} else if (p1 == 0x00 && p2 == 0x04) {
		st_.perf_skip_lines = sw_is_z_ ? 3 : 0;
	} else if (p1 == 0x40 && p2 == 0x00) {
		// CR-only printing vs CR+LF+FF printing (stored as flag)
	} else if (p1 == 0x80 && p2 == 0x00) {
		st_.auto_lf = sw_is_z_;
	} else if (p1 == 0x20 && p2 == 0x00) {
		st_.lf_when_full = sw_is_z_;
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
		} else {
			state_ = State::Normal;
		}
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

	case State::EscCrIns:
		state_ = State::Normal;
		st_.cr_insertion = (b == '0');
		return;

	case State::MultiLF: {
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
		// Software reset
		case 'c': st_ = PrinterState{}; apply_config(cfg_); break;

		// Bold
		case '!': st_.bold = true; break;
		case '"': st_.bold = false; break;

		// Underline
		case 'X': st_.underline = true; break;
		case 'Y': st_.underline = false; break;

		// Superscript / subscript
		case 'x': st_.superscript = true; st_.subscript = false; break;
		case 'y': st_.superscript = false; st_.subscript = true; break;
		case 'z': st_.superscript = false; st_.subscript = false; break;

		// Half-height
		case 'w': st_.half_height = true; break;
		case 'W': st_.half_height = false; break;

		// Character pitch (Table 4-5)
		case 'n': st_.pitch_cpi = 9; st_.proportional = false; break;
		case 'N': st_.pitch_cpi = 10; st_.proportional = false; break;
		case 'E': st_.pitch_cpi = 12; st_.proportional = false; break;
		case 'e': st_.pitch_cpi = 107.0f / 8.0f; st_.proportional = false; break;
		case 'q': st_.pitch_cpi = 15; st_.proportional = false; break;
		case 'Q': st_.pitch_cpi = 17; st_.proportional = false; break;
		case 'p': st_.proportional = true; st_.prop_dpi = 144; break;
		case 'P': st_.proportional = true; st_.prop_dpi = 160; break;

		// Line spacing
		case 'A': st_.line_spacing_in = 1.0f / 6.0f; break;
		case 'B': st_.line_spacing_in = 1.0f / 8.0f; break;

		// Line feed direction
		case 'f': st_.reverse_lf = false; break;
		case 'r': st_.reverse_lf = true; break;

		// Unidirectional / bidirectional
		case '>': st_.unidirectional = true; break;
		case '<': st_.unidirectional = false; break;

		// TOF set
		case 'v': break;

		// Paper-out sensor
		case 'O': break;
		case 'o': break;

		// Font selection
		case 'm': st_.font_mode = 0; break;
		case 'M': st_.font_mode = 2; break;
		case 'a': state_ = State::EscA; break;

		// MouseText
		case '&': st_.mousetext_mode = true; break;
		case '$': st_.mousetext_mode = false; break;

		// Clear all tabs
		case '0': st_.tab_count = 0; break;

		// Proportional spacing
		case 's': start_digits('s', 1); break;

		// CR insertion
		case 'l': state_ = State::EscCrIns; break;

		// Multi-byte parameter commands
		case 'T': start_digits('T', 2); break;
		case 'L': start_digits('L', 3); break;
		case 'H': start_digits('H', 4); break;
		case 'u': start_digits('u', 3); break;
		case 'R': start_digits('R', 3); break;

		// Software switches
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
	case 0x0D: carriage_return(); return;
	case 0x0A: line_feed(); return;
	case 0x0C: form_feed(); return;
	case 0x0E: st_.expanded = true; return;
	case 0x0F: st_.expanded = false; return;
	case 0x08:
		st_.x_pos -= 1.0f / static_cast<float>(st_.pitch_cpi);
		if (st_.x_pos < st_.left_margin_in)
			st_.x_pos = st_.left_margin_in;
		return;
	case 0x09:
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
	case 0x18:
		st_.x_pos = st_.left_margin_in;
		return;
	case 0x1F:
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
