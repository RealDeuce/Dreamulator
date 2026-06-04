// license:BSD-3-Clause
// copyright-holders:Stephen Hurd
#include "printer.h"
#include <cstdio>

class EscpPrinter : public PrinterSim {
public:
	using PrinterSim::PrinterSim;

protected:
	void parse_byte(uint8_t b) override;

	virtual bool parse_esc_extension(uint8_t) { return false; }

	enum class Expect { Normal, Esc, EscParam1, EscParam2 };
	Expect expect_ = Expect::Normal;
	uint8_t esc_cmd_ = 0;
	uint8_t esc_p1_ = 0;

	float denom_A_ = 72.0f;
	float denom_3_ = 216.0f;
	float denom_J_ = 216.0f;
};

class Escp2Printer : public EscpPrinter {
public:
	Escp2Printer(PrinterModel model, PdfWriter &pdf)
		: EscpPrinter(model, pdf)
	{
		denom_A_ = 60.0f;
		denom_3_ = 180.0f;
		denom_J_ = 180.0f;
	}
};

class PpdsPrinter : public EscpPrinter {
public:
	using EscpPrinter::EscpPrinter;
protected:
	bool parse_esc_extension(uint8_t b) override;
};

void EscpPrinter::parse_byte(uint8_t b)
{
	switch (expect_) {
	case Expect::EscParam1:
		expect_ = Expect::Normal;
		switch (esc_cmd_) {
		case '-': st_.underline = (b & 1); break;
		case 'S': st_.superscript = (b == 0); st_.subscript = (b == 1); break;
		case 'W': st_.expanded = (b & 1); break;
		case 'A': st_.line_spacing_in = (float)b / denom_A_; break;
		case '3': st_.line_spacing_in = (float)b / denom_3_; break;
		case 'J':
			new_page_if_needed();
			page_dirty_ = true;
			st_.y_pos += (float)b / denom_J_;
			{
				float bottom = st_.perf_skip_lines > 0
					? st_.page_height_in - (float)st_.perf_skip_lines * st_.line_spacing_in
					: st_.page_height_in - 0.5f;
				if (st_.y_pos >= bottom)
					form_feed();
			}
			break;
		case 'U': st_.unidirectional = (b & 1); break;
		case 'l': st_.left_margin_in = (float)b / (float)st_.pitch_cpi; break;
		case 'Q': st_.right_margin_in = (float)b / (float)st_.pitch_cpi; break;
		default: break;
		}
		return;

	case Expect::Esc:
		expect_ = Expect::Normal;
		if (parse_esc_extension(b)) return;
		switch (b) {
		case '@':
			st_ = PrinterState{};
			apply_config(cfg_);
			break;
		case 'E': st_.bold = true; break;
		case 'F': st_.bold = false; break;
		case 'T': st_.superscript = false; st_.subscript = false; break;
		case 'P': st_.pitch_cpi = 10; break;
		case 'M': st_.pitch_cpi = 12; break;
		case 'g': st_.pitch_cpi = 15; break;
		case '0': st_.line_spacing_in = 1.0f / 8.0f; break;
		case '2': st_.line_spacing_in = 1.0f / 6.0f; break;
		case '1': st_.line_spacing_in = 7.0f / 72.0f; break;
		case '-': case 'S': case 'W': case 'U': case 'A': case '3':
		case 'J': case 'l': case 'Q':
			esc_cmd_ = b;
			expect_ = Expect::EscParam1;
			break;
		default:
			fprintf(stderr, "ESC/P: unknown ESC %02X '%c'\n", b, b >= 0x20 ? b : '.');
			break;
		}
		return;

	case Expect::Normal:
		break;
	case Expect::EscParam2:
		break;
	}

	switch (b) {
	case 0x1B: expect_ = Expect::Esc; return;
	case 0x0D: carriage_return(); return;
	case 0x0A: line_feed(); return;
	case 0x0C: form_feed(); return;
	case 0x0E: st_.expanded_line = true; return;
	case 0x14: st_.expanded_line = false; return;
	case 0x0F: st_.condensed = true; return;
	case 0x12: st_.condensed = false; return;
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

bool PpdsPrinter::parse_esc_extension(uint8_t b)
{
	if (b == ':') {
		st_.pitch_cpi = 12;
		return true;
	}
	return false;
}

std::unique_ptr<PrinterSim> create_pcl_printer(PrinterModel model, PdfWriter &pdf);
std::unique_ptr<PrinterSim> create_imagewriter_printer(PrinterModel model, PdfWriter &pdf);

std::unique_ptr<PrinterSim> create_printer(PrinterModel model, PdfWriter &pdf)
{
	switch (model) {
	case PrinterModel::EpsonFX:
		return std::make_unique<EscpPrinter>(model, pdf);
	case PrinterModel::EpsonLQ:
	case PrinterModel::CanonBJ10e:
		return std::make_unique<Escp2Printer>(model, pdf);
	case PrinterModel::IbmX24E:
	case PrinterModel::IbmXIII:
		return std::make_unique<PpdsPrinter>(model, pdf);
	case PrinterModel::HpJet:
		return create_pcl_printer(model, pdf);
	case PrinterModel::ImageWriter:
		return create_imagewriter_printer(model, pdf);
	}
	return std::make_unique<EscpPrinter>(model, pdf);
}
