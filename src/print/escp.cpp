// license:BSD-3-Clause
// copyright-holders:Stephen Hurd
// Epson ESC/P parser per FX Series User's Manual Vol.2 Reference (1984)
// Appendix C: Control Codes by Function, pages 283-286
// Appendix D: Control Code Chart, pages 287-289
#include "printer.h"
#include "dotrender.h"
#include <cstdio>

class EscpPrinter : public PrinterSim {
public:
	using PrinterSim::PrinterSim;

protected:
	void parse_byte(uint8_t b) override;

	virtual bool parse_esc_extension(uint8_t) { return false; }

	enum class Expect {
		Normal, Esc,
		Bin1,
		GfxLo, GfxHi, GfxData,
		GfxData9Hi,
		TabStops,
		MasterSel,
		FormLenInch,
		EscStar1, EscStar2,
	};
	Expect expect_ = Expect::Normal;
	uint8_t esc_cmd_ = 0;
	uint8_t esc_p1_ = 0;
	int gfx_remain_ = 0;
	float gfx_dot_w_ = 0;
	bool gfx_9pin_ = false;
	uint8_t gfx_hi_byte_ = 0;

	void emit_gfx_col(uint8_t data);
	void emit_gfx_col_9pin(uint8_t lo, uint8_t hi);

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

// Render one 8-pin graphics column at the current position.
// Bit 0 = pin 1 (top), bit 7 = pin 8 (bottom).
// Pin vertical spacing is 1/72".  (FX manual, Ch.10)
void EscpPrinter::emit_gfx_col(uint8_t data)
{
	if (data == 0) {
		st_.x_pos += gfx_dot_w_;
		return;
	}

	new_page_if_needed();
	page_dirty_ = true;

	constexpr float pin_h = 1.0f / 72.0f;
	for (int pin = 0; pin < 8; pin++) {
		if (!(data & (1 << pin))) continue;
		float x = st_.x_pos;
		float y = st_.y_pos + (float)pin * pin_h - 9.0f * pin_h;
		dots_->stamp_pin(*page_, x, y, prof_.render_dpi,
		                 prof_.dot_radius_mm, prof_.jitter_mm,
		                 prof_.dot_intensity, prof_.dot_sharpness);
	}
	st_.x_pos += gfx_dot_w_;
}

// Render one 9-pin graphics column.
// lo: pins 1-8 (bit 0 = pin 1), hi: bit 0 = pin 9.
void EscpPrinter::emit_gfx_col_9pin(uint8_t lo, uint8_t hi)
{
	uint16_t data = static_cast<uint16_t>((uint16_t)lo | ((uint16_t)(hi & 1) << 8));
	if (data == 0) {
		st_.x_pos += gfx_dot_w_;
		return;
	}

	new_page_if_needed();
	page_dirty_ = true;

	constexpr float pin_h = 1.0f / 72.0f;
	for (int pin = 0; pin < 9; pin++) {
		if (!(data & (1 << pin))) continue;
		float x = st_.x_pos;
		float y = st_.y_pos + (float)pin * pin_h - 9.0f * pin_h;
		dots_->stamp_pin(*page_, x, y, prof_.render_dpi,
		                 prof_.dot_radius_mm, prof_.jitter_mm,
		                 prof_.dot_intensity, prof_.dot_sharpness);
	}
	st_.x_pos += gfx_dot_w_;
}

void EscpPrinter::parse_byte(uint8_t b)
{
	switch (expect_) {

	// --- 1-byte binary parameter ---
	case Expect::Bin1:
		expect_ = Expect::Normal;
		switch (esc_cmd_) {
		case '-': st_.underline = (b & 1); break;           // p283
		case 'S':                                            // p283
			st_.superscript = (b == 0);
			st_.subscript = (b == 1);
			break;
		case 'W': st_.expanded = (b & 1); break;            // p283
		case 'A': st_.line_spacing_in = (float)b / denom_A_; break; // p285
		case '3': st_.line_spacing_in = (float)b / denom_3_; break; // p285
		case 'J':                                            // p285
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
		case 'U': st_.unidirectional = (b & 1); break;      // p285
		case 'l': st_.left_margin_in = (float)b / (float)st_.pitch_cpi; break;  // p286
		case 'Q': st_.right_margin_in = (float)b / (float)st_.pitch_cpi; break; // p286
		case 'N': st_.perf_skip_lines = b; break;            // p285
		case 'p': st_.proportional = (b & 1); break;         // p283
		case 'C':                                             // p285
			if (b == 0) {
				expect_ = Expect::FormLenInch;
				return;
			}
			if (b >= 1)
				st_.page_height_in = (float)b * st_.line_spacing_in;
			break;
		case 'R': st_.charset = b; break;                    // p284
		default: break;
		}
		return;

	// --- ESC C 0 n: form length in inches ---
	case Expect::FormLenInch:
		expect_ = Expect::Normal;
		if (b >= 1)
			st_.page_height_in = (float)b;
		return;

	// --- Master Select: ESC ! n ---
	case Expect::MasterSel:
		expect_ = Expect::Normal;
		st_.pitch_cpi = (b & 0x01) ? 12 : 10;               // p284
		st_.proportional = !!(b & 0x02);
		st_.condensed = !!(b & 0x04);
		st_.bold = !!(b & 0x08);
		st_.expanded = !!(b & 0x20);
		st_.underline = !!(b & 0x80);
		return;

	// --- Graphics: nL ---
	case Expect::GfxLo:
		esc_p1_ = b;
		expect_ = Expect::GfxHi;
		return;

	// --- Graphics: nH ---
	case Expect::GfxHi:
		gfx_remain_ = (int)esc_p1_ | ((int)b << 8);
		if (gfx_9pin_)
			gfx_remain_ *= 2;
		expect_ = (gfx_remain_ > 0)
			? (gfx_9pin_ ? Expect::GfxData9Hi : Expect::GfxData)
			: Expect::Normal;
		return;

	// --- Graphics: 8-pin data ---
	case Expect::GfxData:
		if (gfx_9pin_) {
			emit_gfx_col_9pin(gfx_hi_byte_, b);
			if ((gfx_remain_ -= 2) > 0)
				expect_ = Expect::GfxData9Hi;
			else
				expect_ = Expect::Normal;
		} else {
			emit_gfx_col(b);
			if (--gfx_remain_ <= 0)
				expect_ = Expect::Normal;
		}
		return;

	// --- Graphics: 9-pin first byte (pins 1-8), second byte follows ---
	case Expect::GfxData9Hi:
		gfx_hi_byte_ = b;
		expect_ = Expect::GfxData;
		return;

	// --- ESC * mode byte ---
	case Expect::EscStar1:
		esc_p1_ = b;
		if (esc_cmd_ != '^')
			gfx_9pin_ = false;
		switch (b) {                                         // Table 11-1, p150
		case 0: gfx_dot_w_ = 1.0f / 60.0f; break;
		case 1: gfx_dot_w_ = 1.0f / 120.0f; break;
		case 2: gfx_dot_w_ = 1.0f / 120.0f; break;
		case 3: gfx_dot_w_ = 1.0f / 240.0f; break;
		case 4: gfx_dot_w_ = 1.0f / 80.0f; break;
		case 5: gfx_dot_w_ = 1.0f / 72.0f; break;
		case 6: gfx_dot_w_ = 1.0f / 90.0f; break;
		default: gfx_dot_w_ = 1.0f / 60.0f; break;
		}
		expect_ = Expect::EscStar2;
		return;

	// --- ESC * nL ---
	case Expect::EscStar2:
		esc_p1_ = b;
		expect_ = Expect::GfxHi;
		return;

	// --- Horizontal tab stops: values until NUL ---
	case Expect::TabStops:
		if (b == 0) {
			expect_ = Expect::Normal;
		} else {
			if (st_.tab_count < 32)
				st_.tab_stops[st_.tab_count++] = b;
		}
		return;

	// --- ESC byte ---
	case Expect::Esc:
		expect_ = Expect::Normal;
		if (parse_esc_extension(b)) return;
		switch (b) {
		// Reset — p284
		case '@':
			st_ = PrinterState{};
			apply_config(cfg_);
			break;

		// Emphasized (bold) — p283
		case 'E': st_.bold = true; break;
		case 'F': st_.bold = false; break;

		// Double-strike — p283 (no rendering difference)
		case 'G': break;
		case 'H': break;

		// Script mode off — p283
		case 'T': st_.superscript = false; st_.subscript = false; break;

		// Pitch — p283
		case 'P': st_.pitch_cpi = 10; st_.proportional = false; break;
		case 'M': st_.pitch_cpi = 12; break;
		case 'g': st_.pitch_cpi = 15; break;

		// Line spacing — p285
		case '0': st_.line_spacing_in = 1.0f / 8.0f; break;
		case '2': st_.line_spacing_in = 1.0f / 6.0f; break;
		case '1': st_.line_spacing_in = 7.0f / 72.0f; break;

		// Skip-over-perforation OFF — p285
		case 'O': st_.perf_skip_lines = 0; break;

		// Character set — p284, p291
		case '6': break;
		case '7': break;

		// One-line unidirectional — p285
		case '<': st_.unidirectional = true; break;

		// MSB control — p284
		case '#': break;
		case '=': break;
		case '>': break;

		// 1-byte binary parameter commands
		case '-': case 'S': case 'W': case 'U': case 'A': case '3':
		case 'J': case 'l': case 'Q': case 'N': case 'p': case 'C':
		case 'R':
			esc_cmd_ = b;
			expect_ = Expect::Bin1;
			break;

		// Master select — p284
		case '!':
			expect_ = Expect::MasterSel;
			break;

		// Horizontal tab setting — p286
		case 'D':
			st_.tab_count = 0;
			expect_ = Expect::TabStops;
			break;

		// 8-pin graphics — p286
		case 'K': gfx_dot_w_ = 1.0f / 60.0f;  gfx_9pin_ = false;
			esc_cmd_ = b; expect_ = Expect::GfxLo; break;
		case 'L': gfx_dot_w_ = 1.0f / 120.0f; gfx_9pin_ = false;
			esc_cmd_ = b; expect_ = Expect::GfxLo; break;
		case 'Y': gfx_dot_w_ = 1.0f / 120.0f; gfx_9pin_ = false;
			esc_cmd_ = b; expect_ = Expect::GfxLo; break;
		case 'Z': gfx_dot_w_ = 1.0f / 240.0f; gfx_9pin_ = false;
			esc_cmd_ = b; expect_ = Expect::GfxLo; break;

		// Select dot graphics — p286
		case '*':
			expect_ = Expect::EscStar1;
			break;

		// Nine-pin graphics — p286
		case '^':
			esc_cmd_ = '^';
			gfx_9pin_ = true;
			expect_ = Expect::EscStar1;
			break;

		default:
			fprintf(stderr, "ESC/P: unknown ESC %02X '%c'\n",
			        b, b >= 0x20 ? b : '.');
			break;
		}
		return;

	case Expect::Normal:
		break;
	}

	// Control codes
	switch (b) {
	case 0x1B: expect_ = Expect::Esc; return;
	case 0x0D: carriage_return(); return;              // p285
	case 0x0A: line_feed(); return;                    // p285
	case 0x0C: form_feed(); return;                    // p285
	case 0x0E: st_.expanded_line = true; return;       // p283
	case 0x14: st_.expanded_line = false; return;      // p283
	case 0x0F: st_.condensed = true; return;           // p283
	case 0x12: st_.condensed = false; return;          // p283
	case 0x18:                                         // p284: CAN
		st_.x_pos = st_.left_margin_in;
		return;
	case 0x08:                                         // p284: BS
		st_.x_pos -= 1.0f / (float)st_.pitch_cpi;
		if (st_.x_pos < st_.left_margin_in)
			st_.x_pos = st_.left_margin_in;
		return;
	case 0x09:                                         // p286: HT
		for (int i = 0; i < st_.tab_count; i++) {
			float tab_pos = st_.left_margin_in +
			    (float)st_.tab_stops[i] / (float)st_.pitch_cpi;
			if (tab_pos > st_.x_pos) {
				st_.x_pos = tab_pos;
				return;
			}
		}
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
