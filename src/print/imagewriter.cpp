// license:BSD-3-Clause
// copyright-holders:Stephen Hurd
// Apple ImageWriter II command parser for DreamWriter WRITER printer model
#include "printer.h"
#include <cstdio>

class ImageWriterPrinter : public PrinterSim {
public:
	using PrinterSim::PrinterSim;

protected:
	void parse_byte(uint8_t b) override;

private:
	enum class State { Normal, Esc, EscParam1, EscParam2, EscF1, EscF2, EscT1 };
	State state_ = State::Normal;
	uint8_t esc_cmd_ = 0;
	uint8_t esc_p1_ = 0;
};

void ImageWriterPrinter::parse_byte(uint8_t b)
{
	switch (state_) {
	case State::EscParam1:
		state_ = State::Normal;
		switch (esc_cmd_) {
		case 'T':
			esc_p1_ = b;
			state_ = State::EscT1;
			return;
		case 'L':
			st_.left_margin_in = (float)b / (float)st_.pitch_cpi;
			break;
		default: break;
		}
		return;

	case State::EscT1:
		state_ = State::Normal;
		{
			int spacing = (int)esc_p1_ * 256 + (int)b;
			st_.line_spacing_in = (float)spacing / 144.0f;
		}
		return;

	case State::EscF1:
		esc_p1_ = b;
		state_ = State::EscF2;
		return;

	case State::EscF2:
		state_ = State::Normal;
		if (esc_p1_ == 0x01) {
			new_page_if_needed();
			page_dirty_ = true;
			for (int i = 0; i < (int)b; i++)
				line_feed();
		}
		return;

	case State::Esc:
		state_ = State::Normal;
		switch (b) {
		case 'c': st_ = PrinterState{}; apply_config(cfg_); break;
		case '!': st_.bold = true; break;
		case '"': st_.bold = false; break;
		case 'X': st_.underline = true; break;
		case 'Y': st_.underline = false; break;
		case 'x': st_.superscript = true; st_.subscript = false; break;
		case 'y': st_.superscript = false; st_.subscript = true; break;
		case 'z': st_.superscript = false; st_.subscript = false; break;
		case 'N': st_.pitch_cpi = 10; break;
		case 'E': st_.pitch_cpi = 12; break;
		case 'e': st_.expanded = true; break;
		case 'q': st_.pitch_cpi = 17; break;
		case 'n': st_.condensed = true; break;
		case 'T': case 'L':
			esc_cmd_ = b;
			state_ = State::EscParam1;
			break;
		case 'f':
			state_ = State::EscF1;
			break;
		case 'a':
			st_.line_spacing_in = 1.0f / 6.0f;
			break;
		case 'A':
			st_.line_spacing_in = 1.0f / 6.0f;
			break;
		case 'B':
			st_.line_spacing_in = 1.0f / 8.0f;
			break;
		default:
			fprintf(stderr, "ImageWriter: unknown ESC %02X '%c'\n", b, b >= 0x20 ? b : '.');
			break;
		}
		return;

	case State::Normal:
		break;
	case State::EscParam2:
		break;
	}

	switch (b) {
	case 0x1B: state_ = State::Esc; return;
	case 0x0D: carriage_return(); return;
	case 0x0A: line_feed(); return;
	case 0x0C: form_feed(); return;
	case 0x0E: st_.expanded = true; return;
	case 0x0F: st_.expanded = false; return;
	case 0x08:
		st_.x_pos -= 1.0f / (float)st_.pitch_cpi;
		if (st_.x_pos < st_.left_margin_in)
			st_.x_pos = st_.left_margin_in;
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
