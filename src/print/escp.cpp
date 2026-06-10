// license:BSD-3-Clause
// copyright-holders:Stephen Hurd
// Epson ESC/P parser per FX Series User's Manual Vol.2 Reference (1984)
// Appendix C: Control Codes by Function, pages 283-286
// Appendix D: Control Code Chart, pages 287-289
#include "printer.h"
#include "dotrender.h"
#include <cstring>
#include <cstdio>
#include <cmath>
#include <vector>

class EscpPrinter : public PrinterSim {
public:
	EscpPrinter(PrinterModel model, PdfWriter &pdf)
		: PrinterSim(model, pdf)
	{
		set_default_horizontal_tabs();
		set_default_vertical_tabs();
	}

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
		EscQuestionCmd, EscQuestionMode,
		EscPercent1, EscPercent2,
		EscI, EscBChannel, VTabStops,
		EscAmpNull, EscAmpFirst, EscAmpLast, EscAmpFxData,
		EscAmpLqFirst, EscAmpLqLast, EscAmpLqD0, EscAmpLqD1, EscAmpLqD2, EscAmpLqData,
		EscColon1, EscColon2, EscColon3,
	};
	Expect expect_ = Expect::Normal;
	uint8_t esc_cmd_ = 0;
	uint8_t esc_p1_ = 0;
	int gfx_remain_ = 0;
	float gfx_dot_w_ = 0;
	bool gfx_9pin_ = false;
	bool gfx_adjacent_suppression_ = false;
	uint8_t gfx_hi_byte_ = 0;
	uint16_t gfx_prev_col_ = 0;
	uint8_t gfx_reassign_[4] = { 0, 1, 2, 3 };
	uint8_t user_first_ = 0;
	uint8_t user_last_ = 0;
	uint8_t user_cur_ = 0;
	int user_data_pos_ = 0;
	int user_lq_data_remain_ = 0;
	int vtab_pending_channel_ = 0;
	int vtab_last_stop_ = 0;
	int htab_last_stop_ = 0;
	uint8_t colon_p1_ = 0;
	uint8_t colon_p2_ = 0;

	void emit_gfx_col(uint8_t data);
	void emit_gfx_col_9pin(uint8_t lo, uint8_t hi);
	void emit_gfx_col_24pin(uint8_t top, uint8_t mid, uint8_t bot,
	                        float pin_h = 1.0f / 180.0f);
	void emit_gfx_col_bytes(const uint8_t *bytes, int byte_count,
	                        float pin_h);
	bool graphics_dot_allowed(int pin) const;
	void set_default_horizontal_tabs();
	void reset_graphics_reassignments();
	bool set_graphics_mode(uint8_t mode);
	void set_9pin_graphics_density(uint8_t density);
	bool begin_reassigned_graphics(uint8_t cmd);
	void set_default_vertical_tabs();
	void clear_vertical_tabs();
	void copy_fx_rom_to_user_chars();
	bool low_control_printable(uint8_t b) const;
	void begin_vertical_tabs(int channel);
	void add_vertical_tab(uint8_t stop);
	float current_pitch_width_in() const;
	float printable_left_edge_in() const;
	void set_line_spacing_72(uint8_t b);
	void set_form_length_lines(uint8_t b);
	void set_form_length_inches(uint8_t b);
	void set_skip_over_perforation(uint8_t b);
	void set_left_margin(uint8_t b);
	void set_right_margin(uint8_t b);
	void select_vertical_tab_channel(uint8_t b);
	void select_international_set(uint8_t b);

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

class CanonBj10ePrinter : public PpdsPrinter {
public:
	CanonBj10ePrinter(PrinterModel model, PdfWriter &pdf)
		: PpdsPrinter(model, pdf)
	{
		denom_3_ = 180.0f;
		denom_J_ = 180.0f;
		set_default_horizontal_tabs();
	}

	void apply_config(const PrinterConfig &cfg) override;

protected:
	void parse_byte(uint8_t b) override;
	bool parse_esc_extension(uint8_t b) override;

private:
	enum class CanonExpect {
		Normal,
		Esc5,
		EscI,
		EscP,
		EscXLeft,
		EscXRight,
		EscDLo,
		EscDHi,
		EscEqLo,
		EscEqHi,
		EscEqData,
		EscBracketSub,
		EscBracketLo,
		EscBracketHi,
		EscBracketData,
		AllCharsLo,
		AllCharsHi,
		AllCharsOne,
		AllCharsData,
		FsPrefix,
		FsSub,
		FsCJMode,
		FsCJAmount,
		FsCB1,
		FsCB2,
		MasterSel,
		EscLowerP,
		Sink1,
		EscStarMode,
		EscStarLo,
		EscStarHi,
		EscStarData,
	};

	void begin_counted_sequence(uint8_t subcmd);
	void handle_counted_byte(uint8_t b);
	void finish_counted_sequence();
	void apply_print_mode(uint8_t mode);
	void derive_print_mode();
	bool printable_c0(uint8_t b) const;
	void consume_bytes(int count);
	void start_download(int count);
	void finish_download();
	void reset_downloaded_fonts();
	void parse_downloaded_font_payload();
	void select_downloaded_font(bool selected);

	CanonExpect canon_expect_ = CanonExpect::Normal;
	uint8_t canon_cmd_ = 0;
	uint8_t canon_p1_ = 0;
	uint16_t canon_count_ = 0;
	uint16_t canon_seen_ = 0;
	uint8_t canon_payload_[8] = {};
	uint8_t canon_payload_len_ = 0;
	bool charset1_controls_ = true;
	bool mode2_ = false;
	bool alternate_graphics_mode_ = false;
	bool downloaded_font_present_ = false;
	bool downloaded_font_selected_ = false;
	bool cr_after_lf_ = true;
	int consume_remain_ = 0;
	std::vector<uint8_t> download_payload_;
	int download_remain_ = 0;
	bool counted_graphics_ = false;
	int counted_graphics_bytes_per_col_ = 0;
	float counted_graphics_dot_w_ = 1.0f / 60.0f;
	float counted_graphics_pin_h_ = 1.0f / 180.0f;
	uint8_t counted_graphics_buf_[8] = {};
	uint8_t counted_graphics_buf_len_ = 0;
	uint16_t canon_star_columns_ = 0;
	int canon_star_remain_ = 0;
	bool bj_raw_12_ = false;
	bool bj_raw_proportional_ = false;
	bool bj_raw_17_ = false;
	bool bj_raw_emphasized_ = false;
	Bj10eUserFont bj_user_font_;
};

static int graphics_reassign_index(uint8_t cmd)
{
	switch (cmd) {
	case 'K': return 0;
	case 'L': return 1;
	case 'Y': return 2;
	case 'Z': return 3;
	default:  return -1;
	}
}

void EscpPrinter::reset_graphics_reassignments()
{
	gfx_reassign_[0] = 0;
	gfx_reassign_[1] = 1;
	gfx_reassign_[2] = 2;
	gfx_reassign_[3] = 3;
}

bool EscpPrinter::set_graphics_mode(uint8_t mode)
{
	gfx_9pin_ = false;
	gfx_adjacent_suppression_ = false;
	switch (mode) {                                         // FX Table 11-1 plus ROM mode 7
	case 0: gfx_dot_w_ = 1.0f / 60.0f;  break;
	case 1: gfx_dot_w_ = 1.0f / 120.0f; break;
	case 2: gfx_dot_w_ = 1.0f / 120.0f; gfx_adjacent_suppression_ = true; break;
	case 3: gfx_dot_w_ = 1.0f / 240.0f; gfx_adjacent_suppression_ = true; break;
	case 4: gfx_dot_w_ = 1.0f / 80.0f;  break;
	case 5: gfx_dot_w_ = 1.0f / 72.0f;  break;
	case 6: gfx_dot_w_ = 1.0f / 90.0f;  break;
	case 7: gfx_dot_w_ = 1.0f / 144.0f; break;
	default:
		gfx_dot_w_ = 1.0f / 60.0f;
		return false;
	}
	return true;
}

void EscpPrinter::set_9pin_graphics_density(uint8_t density)
{
	gfx_9pin_ = true;
	gfx_adjacent_suppression_ = false;
	gfx_dot_w_ = density == 1 ? 1.0f / 120.0f : 1.0f / 60.0f;
}

bool EscpPrinter::begin_reassigned_graphics(uint8_t cmd)
{
	int idx = graphics_reassign_index(cmd);
	if (idx < 0)
		return false;
	set_graphics_mode(gfx_reassign_[idx]);
	esc_cmd_ = cmd;
	expect_ = Expect::GfxLo;
	return true;
}

bool EscpPrinter::graphics_dot_allowed(int pin) const
{
	if (prof_.model != PrinterModel::CanonBJ10e ||
	    !st_.bj10e_graphics_density_omit)
		return true;
	int xdot = (int)std::lround(st_.x_pos * 360.0f);
	return ((xdot + pin) & 1) == 0;
}

// Render one 8-pin graphics column at the current position.
// Bit 7 = pin 1 (top), bit 0 = pin 8 (bottom).
// Pin vertical spacing is 1/72".  (FX manual, Ch.10)
void EscpPrinter::emit_gfx_col(uint8_t data)
{
	flush_pending_line();

	uint8_t printable = data;
	if (gfx_adjacent_suppression_)
		printable = static_cast<uint8_t>(printable & ~gfx_prev_col_);
	gfx_prev_col_ = data;

	if (printable == 0) {
		st_.x_pos += gfx_dot_w_;
		return;
	}

	new_page_if_needed();
	page_dirty_ = true;
	mark_line_output(true);

	constexpr float pin_h = 1.0f / 72.0f;
	for (int pin = 0; pin < 8; pin++) {
		if (!(printable & (0x80 >> pin))) continue;
		if (!graphics_dot_allowed(pin)) continue;
		float x = st_.x_pos;
		float y = st_.y_pos + (float)pin * pin_h - 9.0f * pin_h;
		dots_->stamp_pin(*page_, x, y, prof_.render_dpi,
		                 prof_.dot_radius_mm, prof_.jitter_mm,
		                 prof_.dot_intensity, prof_.dot_sharpness);
	}
	st_.x_pos += gfx_dot_w_;
}

// Render one 9-pin graphics column.
// lo: pins 1-8 (bit 7 = pin 1), hi: bit 7 = pin 9.
void EscpPrinter::emit_gfx_col_9pin(uint8_t lo, uint8_t hi)
{
	flush_pending_line();

	uint16_t data = 0;
	for (int pin = 0; pin < 8; pin++) {
		if (lo & (0x80 >> pin))
			data |= static_cast<uint16_t>(1U << pin);
	}
	if (hi & 0x80)
		data |= static_cast<uint16_t>(1U << 8);
	if (data == 0) {
		st_.x_pos += gfx_dot_w_;
		return;
	}

	new_page_if_needed();
	page_dirty_ = true;
	mark_line_output(true);

	constexpr float pin_h = 1.0f / 72.0f;
	for (int pin = 0; pin < 9; pin++) {
		if (!(data & (1 << pin))) continue;
		if (!graphics_dot_allowed(pin)) continue;
		float x = st_.x_pos;
		float y = st_.y_pos + (float)pin * pin_h - 9.0f * pin_h;
		dots_->stamp_pin(*page_, x, y, prof_.render_dpi,
		                 prof_.dot_radius_mm, prof_.jitter_mm,
		                 prof_.dot_intensity, prof_.dot_sharpness);
	}
	st_.x_pos += gfx_dot_w_;
}

void EscpPrinter::emit_gfx_col_24pin(uint8_t top, uint8_t mid, uint8_t bot,
                                     float pin_h)
{
	flush_pending_line();

	uint32_t data = ((uint32_t)top << 16) | ((uint32_t)mid << 8) | (uint32_t)bot;
	if (data == 0) {
		st_.x_pos += gfx_dot_w_;
		return;
	}

	new_page_if_needed();
	page_dirty_ = true;
	mark_line_output(true);

	for (int pin = 0; pin < 24; pin++) {
		if (!(data & (1U << (23 - pin)))) continue;
		if (!graphics_dot_allowed(pin)) continue;
		float x = st_.x_pos;
		float y = st_.y_pos + (float)pin * pin_h - 24.0f * pin_h;
		dots_->stamp_pin(*page_, x, y, prof_.render_dpi,
		                 prof_.dot_radius_mm, prof_.jitter_mm,
		                 prof_.dot_intensity, prof_.dot_sharpness);
	}
	st_.x_pos += gfx_dot_w_;
}

void EscpPrinter::emit_gfx_col_bytes(const uint8_t *bytes, int byte_count,
                                     float pin_h)
{
	flush_pending_line();

	if (byte_count <= 0)
		return;

	bool any = false;
	for (int i = 0; i < byte_count; i++)
		any = any || bytes[i] != 0;
	if (!any) {
		st_.x_pos += gfx_dot_w_;
		return;
	}

	new_page_if_needed();
	page_dirty_ = true;
	mark_line_output(true);

	float origin = 9.0f / 72.0f;
	for (int pin = 0; pin < byte_count * 8; pin++) {
		uint8_t b = bytes[pin / 8];
		if (!(b & (0x80 >> (pin & 7)))) continue;
		if (!graphics_dot_allowed(pin)) continue;
		float x = st_.x_pos;
		float y = st_.y_pos + (float)pin * pin_h - origin;
		dots_->stamp_pin(*page_, x, y, prof_.render_dpi,
		                 prof_.dot_radius_mm, prof_.jitter_mm,
		                 prof_.dot_intensity, prof_.dot_sharpness);
	}
	st_.x_pos += gfx_dot_w_;
}

void EscpPrinter::set_default_horizontal_tabs()
{
	st_.tab_count = 0;
	for (int col = 8; col <= 80 && st_.tab_count < 32; col += 8) {
		st_.tab_stops[st_.tab_count] = col;
		st_.tab_stops_in[st_.tab_count] =
		    st_.left_margin_in + static_cast<float>(col) * current_pitch_width_in();
		st_.tab_count++;
	}
}

void EscpPrinter::set_default_vertical_tabs()
{
	for (int ch = 0; ch < 8; ch++) {
		st_.vtab_count[ch] = 0;
		for (int line = 2; line <= 66 && st_.vtab_count[ch] < 16; line += 2)
			st_.vtab_stops[ch][st_.vtab_count[ch]++] =
			    static_cast<float>(line) * st_.line_spacing_in;
	}
	st_.vtab_channel = 0;
}

void EscpPrinter::clear_vertical_tabs()
{
	for (int ch = 0; ch < 8; ch++)
		st_.vtab_count[ch] = 0;
	st_.vtab_channel = 0;
}

void EscpPrinter::copy_fx_rom_to_user_chars()
{
	for (int ch = 0; ch < 256; ch++) {
		bool italic = ch >= 128;
		const uint16_t *glyph = italic ? get_fx80_italic_glyph((uint8_t)ch)
		                               : get_fx80_roman_glyph((uint8_t)ch);
		if (!glyph)
			continue;
		st_.user_char_defined[ch] = true;
		st_.user_char_prefix[ch] = get_fx80_prefix((uint8_t)ch, italic);
		std::memcpy(st_.user_char_glyph[ch], glyph, sizeof(st_.user_char_glyph[ch]));
	}
}

bool EscpPrinter::low_control_printable(uint8_t b) const
{
	if (!st_.printable_low_controls)
		return false;
	return (b <= 6) || b == 16 || (b >= 21 && b <= 23) || (b >= 25 && b <= 31);
}

void EscpPrinter::begin_vertical_tabs(int channel)
{
	if (channel < 0 || channel > 7)
		channel = 0;
	vtab_pending_channel_ = channel;
	vtab_last_stop_ = 0;
	st_.vtab_count[channel] = 0;
	expect_ = Expect::VTabStops;
}

void EscpPrinter::add_vertical_tab(uint8_t stop)
{
	if (stop == 0 || stop <= vtab_last_stop_) {
		expect_ = Expect::Normal;
		return;
	}
	float pos = static_cast<float>(stop) * st_.line_spacing_in;
	if (st_.vtab_count[vtab_pending_channel_] >= 16 || pos >= st_.page_height_in) {
		expect_ = Expect::Normal;
		return;
	}
	st_.vtab_stops[vtab_pending_channel_][st_.vtab_count[vtab_pending_channel_]++] = pos;
	vtab_last_stop_ = stop;
}

float EscpPrinter::current_pitch_width_in() const
{
	if (st_.condensed && st_.pitch_cpi <= 10.0f)
		return 1.0f / 17.16f;
	return 1.0f / static_cast<float>(st_.pitch_cpi);
}

float EscpPrinter::printable_left_edge_in() const
{
	return 0.25f;
}

void EscpPrinter::set_line_spacing_72(uint8_t b)
{
	if (prof_.model == PrinterModel::EpsonFX) {
		uint8_t v = static_cast<uint8_t>(b & 0x7F);
		if (v < 0x56)
			st_.line_spacing_in = static_cast<float>(v) / 72.0f;
	} else {
		st_.line_spacing_in = static_cast<float>(b) / denom_A_;
	}
}

void EscpPrinter::set_form_length_lines(uint8_t b)
{
	uint8_t v = prof_.model == PrinterModel::EpsonFX
	    ? static_cast<uint8_t>(b & 0x7F)
	    : b;
	clear_vertical_tabs();
	if (v == 0) {
		expect_ = Expect::FormLenInch;
		return;
	}
	st_.page_height_in = static_cast<float>(v) * st_.line_spacing_in;
	st_.perf_skip_lines = 0;
}

void EscpPrinter::set_form_length_inches(uint8_t b)
{
	uint8_t v = prof_.model == PrinterModel::EpsonFX
	    ? static_cast<uint8_t>(b & 0x7F)
	    : b;
	if (prof_.model == PrinterModel::EpsonFX) {
		if (v == 0 || v >= 23)
			return;
		float height = static_cast<float>(v);
		if (height < st_.line_spacing_in)
			return;
		st_.page_height_in = height;
	} else if (v >= 1) {
		st_.page_height_in = static_cast<float>(v);
	}
	st_.perf_skip_lines = 0;
}

void EscpPrinter::set_skip_over_perforation(uint8_t b)
{
	uint8_t v = prof_.model == PrinterModel::EpsonFX
	    ? static_cast<uint8_t>(b & 0x7F)
	    : b;
	if (v == 0)
		return;
	float skip = static_cast<float>(v) * st_.line_spacing_in;
	if (skip < st_.page_height_in)
		st_.perf_skip_lines = v;
}

void EscpPrinter::set_left_margin(uint8_t b)
{
	float candidate = printable_left_edge_in() + static_cast<float>(b) * current_pitch_width_in();
	if (candidate > st_.right_margin_in - 0.2f)
		return;
	cancel_pending_line();
	st_.left_margin_in = candidate;
	st_.x_pos = st_.left_margin_in;
}

void EscpPrinter::set_right_margin(uint8_t b)
{
	float candidate = printable_left_edge_in() + static_cast<float>(b) * current_pitch_width_in();
	float max_right = printable_left_edge_in() + 8.0f + (1.0f / 720.0f);
	if (candidate >= max_right || candidate < st_.left_margin_in + 0.2f)
		return;
	cancel_pending_line();
	st_.right_margin_in = candidate;
	st_.x_pos = st_.left_margin_in;
}

void EscpPrinter::select_vertical_tab_channel(uint8_t b)
{
	uint8_t v = static_cast<uint8_t>(b & 0x7F);
	if (v < 8)
		st_.vtab_channel = v;
}

void EscpPrinter::select_international_set(uint8_t b)
{
	uint8_t v = prof_.model == PrinterModel::EpsonFX
	    ? static_cast<uint8_t>(b & 0x7F)
	    : b;
	if (prof_.model != PrinterModel::EpsonFX || v <= 10)
		st_.charset = v;
}

void EscpPrinter::parse_byte(uint8_t b)
{
	if (expect_ == Expect::Normal) {
		if (b != 0x1B) {
			if (st_.msb_mode < 0)
				b = static_cast<uint8_t>(b & 0x7F);
			else if (st_.msb_mode > 0)
				b = static_cast<uint8_t>(b | 0x80);
		}
		if (b >= 0x80 && b <= 0x9F && !st_.printable_high_controls)
			b = static_cast<uint8_t>(b & 0x7F);
		else if (b == 0xFF && !st_.printable_high_controls)
			b = 0x7F;

		if (!st_.selected) {
			if (b == 0x11)
				st_.selected = true;
			return;
		}
	}

	switch (expect_) {

	// --- 1-byte binary parameter ---
	case Expect::Bin1:
		expect_ = Expect::Normal;
		switch (esc_cmd_) {
		case '-': st_.underline = (b & 1); break;           // p283
		case '_': st_.overline = (b & 1); break;
		case 'S':                                            // p283
			st_.superscript = (b == 0);
			st_.subscript = (b == 1);
			st_.proportional = false;
			break;
		case 'W': st_.expanded = (b & 1); break;            // p283
		case 'A': set_line_spacing_72(b); break;            // p285
		case '3': st_.line_spacing_in = (float)b / denom_3_; break; // p285
		case 'J':                                            // p285
			flush_pending_line();
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
			finish_printed_line();
			break;
		case 'U':                                           // p285
			st_.unidirectional = (b & 1);
			if (st_.unidirectional)
				st_.line_dir_ltr = true;
			break;
		case 'I':
			st_.printable_low_controls = (b & 1);
			break;
		case '/': select_vertical_tab_channel(b); break;
		case '%':
			st_.use_user_chars = (b & 1);
			break;
		case 'i':
		case 's':
			break;
		case 'j':
			flush_pending_line();
			new_page_if_needed();
			page_dirty_ = true;
			st_.y_pos -= (float)b / denom_J_;
			if (st_.y_pos < st_.top_margin_in)
				st_.y_pos = st_.top_margin_in;
			finish_printed_line();
			break;
		case 'l': set_left_margin(b); break;                // p286
		case 'Q': set_right_margin(b); break;               // p286
		case 'N': set_skip_over_perforation(b); break;      // p285
		case 'p':                                            // p283
			st_.proportional = (b & 1);
			if (st_.proportional) {
				st_.pitch_cpi = 10;
				st_.condensed = false;
				st_.superscript = false;
				st_.subscript = false;
				st_.double_strike = false;
			}
			break;
		case 'C': set_form_length_lines(b); break;          // p285
		case 'R': select_international_set(b); break;       // p284
		default: break;
		}
		return;

	// --- ESC C 0 n: form length in inches ---
	case Expect::FormLenInch:
		expect_ = Expect::Normal;
		set_form_length_inches(b);
		return;

	// --- Master Select: ESC ! n ---
	case Expect::MasterSel:
		expect_ = Expect::Normal;
		st_.pitch_cpi = (b & 0x01) ? 12 : 10;               // p284
		st_.proportional = prof_.model == PrinterModel::EpsonFX
		    ? false
		    : !!(b & 0x02);
		st_.condensed = !!(b & 0x04);
		st_.bold = !!(b & 0x08);
		st_.double_strike = !!(b & 0x10);
		st_.expanded = !!(b & 0x20);
		if (prof_.model != PrinterModel::EpsonFX)
			st_.italic = !!(b & 0x40);
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
		gfx_prev_col_ = 0;
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
		if (esc_cmd_ == '^')
			set_9pin_graphics_density(b);
		else
			set_graphics_mode(b);
		expect_ = Expect::EscStar2;
		return;

	// --- ESC * nL ---
	case Expect::EscStar2:
		esc_p1_ = b;
		expect_ = Expect::GfxHi;
		return;

	// --- Horizontal tab stops: values until NUL ---
	case Expect::TabStops:
		if (b == 0 || b <= htab_last_stop_) {
			expect_ = Expect::Normal;
		} else {
			float pos = st_.left_margin_in +
			    static_cast<float>(b) * current_pitch_width_in();
			if (st_.tab_count >= 32 || pos > st_.right_margin_in) {
				expect_ = Expect::Normal;
				return;
			}
			st_.tab_stops[st_.tab_count] = b;
			st_.tab_stops_in[st_.tab_count] = pos;
			st_.tab_count++;
			htab_last_stop_ = b;
		}
		return;

	case Expect::EscPercent1:
		esc_p1_ = b;
		expect_ = Expect::EscPercent2;
		return;

	case Expect::EscPercent2:
		expect_ = Expect::Normal;
		st_.use_user_chars = (esc_p1_ == 1 && b == 0);
		return;

	case Expect::EscI:
		expect_ = Expect::Normal;
		st_.printable_low_controls = (b & 1);
		return;

	case Expect::EscBChannel:
		{
			uint8_t v = static_cast<uint8_t>(b & 0x7F);
			if (v < 8)
				begin_vertical_tabs(v);
			else
				expect_ = Expect::Normal;
		}
		return;

	case Expect::VTabStops:
		add_vertical_tab(b);
		return;

	case Expect::EscAmpNull:
		if (b == 0)
			expect_ = prof_.model == PrinterModel::EpsonFX
			    ? Expect::EscAmpFirst
			    : Expect::EscAmpLqFirst;
		else
			expect_ = Expect::Normal;
		return;

	case Expect::EscAmpFirst:
		user_first_ = b;
		expect_ = Expect::EscAmpLast;
		return;

	case Expect::EscAmpLast:
		user_last_ = b;
		user_cur_ = user_first_;
		user_data_pos_ = 0;
		expect_ = user_last_ >= user_first_ ? Expect::EscAmpFxData : Expect::Normal;
		return;

	case Expect::EscAmpFxData:
		if (user_data_pos_ == 0) {
			st_.user_char_prefix[user_cur_] = b;
			std::memset(st_.user_char_glyph[user_cur_], 0, sizeof(st_.user_char_glyph[user_cur_]));
		} else {
			uint8_t attr = st_.user_char_prefix[user_cur_];
			uint16_t col = 0;
			for (int pin = 0; pin < 8; pin++) {
				if (!(b & (0x80 >> pin)))
					continue;
				int row = (attr & 0x80) ? pin : pin + 1;
				if (row < 9)
					col |= static_cast<uint16_t>(1U << row);
			}
			st_.user_char_glyph[user_cur_][user_data_pos_ - 1] = col;
		}
		if (++user_data_pos_ >= 12) {
			st_.user_char_defined[user_cur_] = true;
			if (user_cur_ >= user_last_)
				expect_ = Expect::Normal;
			else {
				user_cur_++;
				user_data_pos_ = 0;
			}
		}
		return;

	case Expect::EscAmpLqFirst:
		user_first_ = b;
		expect_ = Expect::EscAmpLqLast;
		return;

	case Expect::EscAmpLqLast:
		user_last_ = b;
		expect_ = Expect::EscAmpLqD0;
		return;

	case Expect::EscAmpLqD0:
		expect_ = Expect::EscAmpLqD1;
		return;

	case Expect::EscAmpLqD1:
		user_lq_data_remain_ = 0;
		if (user_last_ >= user_first_)
			user_lq_data_remain_ = (int)(user_last_ - user_first_ + 1) * (int)b * 3;
		expect_ = Expect::EscAmpLqD2;
		return;

	case Expect::EscAmpLqD2:
		expect_ = user_lq_data_remain_ > 0 ? Expect::EscAmpLqData : Expect::Normal;
		return;

	case Expect::EscAmpLqData:
		if (--user_lq_data_remain_ <= 0)
			expect_ = Expect::Normal;
		return;

	case Expect::EscColon1:
		colon_p1_ = b;
		expect_ = Expect::EscColon2;
		return;

	case Expect::EscColon2:
		colon_p2_ = b;
		expect_ = Expect::EscColon3;
		return;

	case Expect::EscColon3:
		expect_ = Expect::Normal;
		if (prof_.model == PrinterModel::EpsonFX && colon_p1_ == 0 && colon_p2_ == 0 && b == 0)
			copy_fx_rom_to_user_chars();
		return;

	// --- ESC ? s n: reassign graphics command ---
	case Expect::EscQuestionCmd:
		esc_cmd_ = b;
		expect_ = Expect::EscQuestionMode;
		return;

	case Expect::EscQuestionMode:
		expect_ = Expect::Normal;
		{
			int idx = graphics_reassign_index(esc_cmd_);
			if (idx >= 0 && b <= 7)
				gfx_reassign_[idx] = b;
		}
		return;

	// --- ESC byte ---
	case Expect::Esc:
		expect_ = Expect::Normal;
		if (parse_esc_extension(b)) return;
		switch (b) {
		// Reset — p284
		case '@':
			cancel_pending_line();
			st_ = PrinterState{};
			apply_config(cfg_);
			set_default_horizontal_tabs();
			set_default_vertical_tabs();
			reset_graphics_reassignments();
			break;

		// Emphasized (bold) — p283
		case 'E': st_.bold = true; break;
		case 'F': st_.bold = false; break;

		// Double-strike — p283
		case 'G': st_.double_strike = true; st_.proportional = false; break;
		case 'H': st_.double_strike = false; break;

		// Italic — p283
		case '4': st_.italic = true; break;
		case '5': st_.italic = false; break;

		// Script mode off — p283
		case 'T': st_.superscript = false; st_.subscript = false; break;

		// Pitch — p283
		case 'P': st_.pitch_cpi = 10; st_.proportional = false; break;
		case 'M': st_.pitch_cpi = 12; st_.proportional = false; break;
		case 'g': st_.pitch_cpi = 15; break;

		// Line spacing — p285
		case '0': st_.line_spacing_in = 1.0f / 8.0f; break;
		case '2': st_.line_spacing_in = 1.0f / 6.0f; break;
		case '1': st_.line_spacing_in = 7.0f / 72.0f; break;

		// Skip-over-perforation OFF — p285
		case 'O': st_.perf_skip_lines = 0; break;

		// Character set — p284, p291
		case '6': st_.printable_high_controls = true; break;
		case '7': st_.printable_high_controls = false; break;

		// Paper-out detector control — not modeled in PDF output.
		case '8':
		case '9':
			break;

		// One-line unidirectional — p285
		case '<':
			st_.unidirectional_line = true;
			st_.line_dir_ltr = true;
			break;

		// MSB control — p284
		case '#': st_.msb_mode = 0; break;
		case '=': st_.msb_mode = -1; break;
		case '>': st_.msb_mode = 1; break;

		// 1-byte binary parameter commands
		case '-': case '_': case 'S': case 'W': case 'U': case 'A': case '3':
		case 'J': case 'l': case 'Q': case 'N': case 'p': case 'C':
		case 'R': case 'I': case '/': case 'i': case 'j': case 's':
			esc_cmd_ = b;
			expect_ = Expect::Bin1;
			break;

		case '%':
			if (prof_.model == PrinterModel::EpsonFX)
				expect_ = Expect::EscPercent1;
			else {
				esc_cmd_ = b;
				expect_ = Expect::Bin1;
			}
			break;

		// Copy ROM character set to user-defined RAM.
		case ':':
			expect_ = Expect::EscColon1;
			break;

		// Define user characters.
		case '&':
			expect_ = Expect::EscAmpNull;
			break;

		// Master select — p284
		case '!':
			expect_ = Expect::MasterSel;
			break;

		// Horizontal tab setting — p286
		case 'D':
			st_.tab_count = 0;
			htab_last_stop_ = 0;
			expect_ = Expect::TabStops;
			break;

		case 'B':
			begin_vertical_tabs(0);
			break;

		case 'b':
			expect_ = Expect::EscBChannel;
			break;

		// 8-pin graphics — p286
		case 'K': case 'L': case 'Y': case 'Z':
			begin_reassigned_graphics(b);
			break;

		// Select dot graphics — p286
		case '*':
			esc_cmd_ = '*';
			expect_ = Expect::EscStar1;
			break;

		// Nine-pin graphics — p286
		case '^':
			esc_cmd_ = '^';
			gfx_9pin_ = true;
			expect_ = Expect::EscStar1;
			break;

		// Reassign dot-graphics command — p286
		case '?':
			expect_ = Expect::EscQuestionCmd;
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
	case 0x11: st_.selected = true; return;             // p284: DC1
	case 0x13: st_.selected = false; return;            // p284: DC3
	default:
		if (b < 0x20 && low_control_printable(b)) {
			emit_char(b);
			return;
		}
		break;
	}

	switch (b) {
	case 0x0D: carriage_return(); return;              // p285
	case 0x0A: line_feed(); return;                    // p285
	case 0x0B:                                         // p285: VT
		{
			int ch = st_.vtab_channel & 7;
			for (int i = 0; i < st_.vtab_count[ch]; i++) {
				float y = st_.top_margin_in + st_.vtab_stops[ch][i];
				if (y > st_.y_pos + 0.001f) {
					flush_pending_line();
					st_.y_pos = y;
					finish_printed_line();
					return;
				}
			}
			line_feed();
		}
		return;
	case 0x0C: form_feed(); return;                    // p285
	case 0x0E: st_.expanded_line = true; return;       // p283
	case 0x14: st_.expanded_line = false; return;      // p283
	case 0x0F: st_.condensed = true; st_.proportional = false; return; // p283
	case 0x12: st_.condensed = false; return;          // p283
	case 0x18:                                         // p284: CAN
		cancel_pending_line();
		st_.x_pos = st_.left_margin_in;
		finish_printed_line();
		return;
	case 0x08:                                         // p284: BS
		st_.x_pos -= 1.0f / (float)st_.pitch_cpi;
		if (st_.x_pos < st_.left_margin_in)
			st_.x_pos = st_.left_margin_in;
		return;
	case 0x09:                                         // p286: HT
		for (int i = 0; i < st_.tab_count; i++) {
			float tab_pos = st_.tab_stops_in[i];
			if (tab_pos > st_.x_pos) {
				st_.x_pos = tab_pos;
				return;
			}
		}
		return;
	case 0x7F:                                         // p284: DEL
		delete_pending_char();
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
		st_.condensed = false;
		st_.proportional = false;
		return true;
	}
	return false;
}

bool CanonBj10ePrinter::printable_c0(uint8_t b) const
{
	return b == 0x03 || b == 0x04 || b == 0x05 || b == 0x06 || b == 0x15;
}

static void apply_bj10e_nibble_style(uint8_t nibble, bool &state)
{
	if (nibble == 1)
		state = false;
	else if (nibble == 2)
		state = true;
}

struct CanonGraphicsMode {
	float dot_w = 1.0f / 60.0f;
	float pin_h = 1.0f / 180.0f;
	int bytes_per_col = 0;
};

static bool canon_bracket_g_mode(uint8_t mode, CanonGraphicsMode &out)
{
	switch (mode) {
	case 0x00: out = { 1.0f / 60.0f,  1.0f / 72.0f,  1 }; return true;
	case 0x01:
	case 0x02: out = { 1.0f / 120.0f, 1.0f / 72.0f,  1 }; return true;
	case 0x03: out = { 1.0f / 240.0f, 1.0f / 72.0f,  1 }; return true;
	case 0x08: out = { 1.0f / 60.0f,  1.0f / 180.0f, 3 }; return true;
	case 0x09: out = { 1.0f / 120.0f, 1.0f / 180.0f, 3 }; return true;
	case 0x0B: out = { 1.0f / 180.0f, 1.0f / 180.0f, 3 }; return true;
	case 0x0C: out = { 1.0f / 360.0f, 1.0f / 180.0f, 3 }; return true;
	case 0x0D: out = { 1.0f / 120.0f, 1.0f / 360.0f, 6 }; return true;
	case 0x0E: out = { 1.0f / 180.0f, 1.0f / 360.0f, 6 }; return true;
	case 0x0F: out = { 1.0f / 240.0f, 1.0f / 360.0f, 6 }; return true;
	case 0x10: out = { 1.0f / 360.0f, 1.0f / 360.0f, 6 }; return true;
	case 0x11: out = { 1.0f / 360.0f, 1.0f / 360.0f, 8 }; return true;
	default: return false;
	}
}

static bool canon_star_mode(uint8_t mode, CanonGraphicsMode &out)
{
	switch (mode) {
	case 0x00: out = { 1.0f / 60.0f,  1.0f / 72.0f,  1 }; return true;
	case 0x01:
	case 0x02: out = { 1.0f / 120.0f, 1.0f / 72.0f,  1 }; return true;
	case 0x03: out = { 1.0f / 240.0f, 1.0f / 72.0f,  1 }; return true;
	case 0x04: out = { 1.0f / 80.0f,  1.0f / 72.0f,  1 }; return true;
	case 0x06: out = { 1.0f / 90.0f,  1.0f / 72.0f,  1 }; return true;
	case 0x20: out = { 1.0f / 60.0f,  1.0f / 180.0f, 3 }; return true;
	case 0x21: out = { 1.0f / 120.0f, 1.0f / 180.0f, 3 }; return true;
	case 0x26: out = { 1.0f / 90.0f,  1.0f / 180.0f, 3 }; return true;
	case 0x27: out = { 1.0f / 180.0f, 1.0f / 180.0f, 3 }; return true;
	case 0x28: out = { 1.0f / 360.0f, 1.0f / 180.0f, 3 }; return true;
	case 0x2F: out = { 1.0f / 180.0f, 1.0f / 360.0f, 6 }; return true;
	case 0x30: out = { 1.0f / 360.0f, 1.0f / 360.0f, 6 }; return true;
	case 0x40: out = { 1.0f / 360.0f, 1.0f / 360.0f, 8 }; return true;
	default: return false;
	}
}

void CanonBj10ePrinter::derive_print_mode()
{
	st_.bold = bj_raw_emphasized_;
	st_.proportional = false;
	st_.condensed = false;
	st_.pitch_cpi = 10;

	if (bj_raw_proportional_) {
		st_.proportional = true;
	} else if (bj_raw_12_) {
		st_.pitch_cpi = 12;
	} else if (bj_raw_17_ && !bj_raw_emphasized_) {
		st_.condensed = true;
	}
}

void CanonBj10ePrinter::apply_config(const PrinterConfig &cfg)
{
	PrinterSim::apply_config(cfg);
	bool sw2 = (cfg.dip_switches & (1 << 1)) != 0;
	bool sw3 = (cfg.dip_switches & (1 << 2)) != 0;
	bool sw4 = (cfg.dip_switches & (1 << 3)) != 0;
	bool sw5 = (cfg.dip_switches & (1 << 4)) != 0;
	bool sw6 = (cfg.dip_switches & (1 << 5)) != 0;
	bool sw7 = (cfg.dip_switches & (1 << 6)) != 0;
	bool sw9 = (cfg.dip_switches & (1 << 8)) != 0;
	bool sw10 = (cfg.dip_switches & (1 << 9)) != 0;

	st_.bj10e_graphics_density_omit = sw2;
	st_.bj10e_user_font = &bj_user_font_;
	st_.auto_lf = sw3;
	st_.page_height_in = (sw4 ? 72.0f : 66.0f) * st_.line_spacing_in;
	st_.codepage_850 = sw9;
	charset1_controls_ = !sw5;
	cr_after_lf_ = sw6;
	alternate_graphics_mode_ = sw7;
	mode2_ = sw10;

	bj_raw_12_ = cfg.pitch_cpi == 12.0f && !cfg.proportional;
	bj_raw_17_ = cfg.pitch_cpi == 17.0f && !cfg.proportional;
	bj_raw_proportional_ = cfg.proportional;
	bj_raw_emphasized_ = cfg.emphasized;
	derive_print_mode();
	select_downloaded_font(downloaded_font_selected_);
}

void CanonBj10ePrinter::consume_bytes(int count)
{
	consume_remain_ = count;
	canon_expect_ = consume_remain_ > 0 ? CanonExpect::EscBracketData
	                                    : CanonExpect::Normal;
	counted_graphics_ = false;
	canon_payload_len_ = 0;
}

void CanonBj10ePrinter::select_downloaded_font(bool selected)
{
	downloaded_font_selected_ = selected;
	st_.bj10e_user_font = &bj_user_font_;
	st_.bj10e_use_downloaded_font = selected && downloaded_font_present_;
}

void CanonBj10ePrinter::reset_downloaded_fonts()
{
	bj_user_font_ = Bj10eUserFont{};
	downloaded_font_present_ = false;
	downloaded_font_selected_ = false;
	download_payload_.clear();
	download_remain_ = 0;
	select_downloaded_font(false);
}

void CanonBj10ePrinter::start_download(int count)
{
	flush_pending_line();
	download_payload_.clear();
	download_remain_ = count;
	if (download_remain_ <= 0) {
		reset_downloaded_fonts();
		canon_expect_ = CanonExpect::Normal;
		return;
	}
	canon_expect_ = CanonExpect::EscEqData;
}

static uint64_t bj10e_download_column(const uint8_t *src)
{
	uint64_t col = 0;
	for (int byte = 0; byte < 6; byte++) {
		for (int bit = 0; bit < 8; bit++) {
			if (src[byte] & (0x80 >> bit))
				col |= 1ULL << (byte * 8 + bit);
		}
	}
	return col;
}

void CanonBj10ePrinter::parse_downloaded_font_payload()
{
	if (download_payload_.empty()) {
		reset_downloaded_fonts();
		return;
	}

	uint8_t format = download_payload_[0];
	if (mode2_) {
		if (download_payload_.size() < 2) {
			reset_downloaded_fonts();
			return;
		}
		if (download_payload_.size() < 8 || format != 0x25)
			return;

		uint8_t flags = download_payload_[5];
		size_t pos = 8;
		bool any = false;
		while (pos + 4 <= download_payload_.size()) {
			uint8_t ch = download_payload_[pos++];
			(void)download_payload_[pos++]; // attribute low bits affect ROM-side records.
			uint8_t raw_width = download_payload_[pos++];
			(void)download_payload_[pos++];

			int input_width = raw_width == 0 ? 0x24 : raw_width;
			int max_width = (flags & 0x10) ? 0x3c : 0x24;
			int stored_width = input_width > max_width ? max_width : input_width;
			if (stored_width > 60)
				stored_width = 60;

			std::memset(bj_user_font_.glyph[ch], 0, sizeof(bj_user_font_.glyph[ch]));
			for (int col = 0; col < stored_width; col++) {
				uint8_t bytes[6] = {};
				for (int i = 0; i < 6; i++) {
					if (pos < download_payload_.size())
						bytes[i] = download_payload_[pos];
					pos++;
				}
				bj_user_font_.glyph[ch][col] = bj10e_download_column(bytes);
			}
			size_t discard = (size_t)(input_width - stored_width) * 6U;
			pos += discard;
			bj_user_font_.defined[ch] = true;
			bj_user_font_.width[ch] = (uint8_t)stored_width;
			any = true;
		}
		if (any) {
			downloaded_font_present_ = true;
			select_downloaded_font(downloaded_font_selected_);
		}
		return;
	}

	if (format == 0x23 || format == 0x24)
		downloaded_font_present_ = true;
	select_downloaded_font(downloaded_font_selected_);
}

void CanonBj10ePrinter::finish_download()
{
	parse_downloaded_font_payload();
	download_payload_.clear();
	download_remain_ = 0;
	canon_expect_ = CanonExpect::Normal;
}

void CanonBj10ePrinter::apply_print_mode(uint8_t mode)
{
	bj_raw_12_ = false;
	bj_raw_17_ = false;
	bj_raw_proportional_ = false;
	bool downloaded = false;

	switch (mode) {
	case 0x00:
		st_.font_mode = 1;
		break;
	case 0x04:
		downloaded = true;
		st_.font_mode = 1;
		break;
	case 0x02:
		st_.font_mode = 0;
		break;
	case 0x06:
		downloaded = true;
		st_.font_mode = 0;
		break;
	case 0x08:
		bj_raw_12_ = true;
		st_.font_mode = 1;
		break;
	case 0x0C:
		downloaded = true;
		bj_raw_12_ = true;
		st_.font_mode = 1;
		break;
	case 0x0A:
		bj_raw_12_ = true;
		st_.font_mode = 0;
		break;
	case 0x0E:
		downloaded = true;
		bj_raw_12_ = true;
		st_.font_mode = 0;
		break;
	case 0x10:
		bj_raw_17_ = true;
		bj_raw_emphasized_ = false;
		st_.font_mode = 1;
		break;
	case 0x14:
		downloaded = true;
		bj_raw_17_ = true;
		bj_raw_emphasized_ = false;
		st_.font_mode = 1;
		break;
	case 0x12:
		bj_raw_17_ = true;
		bj_raw_emphasized_ = false;
		st_.font_mode = 0;
		break;
	case 0x16:
		downloaded = true;
		bj_raw_17_ = true;
		bj_raw_emphasized_ = false;
		st_.font_mode = 0;
		break;
	case 0x03:
		bj_raw_proportional_ = true;
		st_.font_mode = 0;
		break;
	case 0x07:
		downloaded = true;
		bj_raw_proportional_ = true;
		st_.font_mode = 0;
		break;
	default:
		return;
	}
	select_downloaded_font(downloaded);
	derive_print_mode();
}

void CanonBj10ePrinter::begin_counted_sequence(uint8_t subcmd)
{
	canon_cmd_ = subcmd;
	canon_count_ = 0;
	canon_seen_ = 0;
	canon_payload_len_ = 0;
	counted_graphics_ = false;
	counted_graphics_bytes_per_col_ = 0;
	counted_graphics_buf_len_ = 0;
	canon_expect_ = CanonExpect::EscBracketLo;
}

void CanonBj10ePrinter::handle_counted_byte(uint8_t b)
{
	if (canon_payload_len_ < sizeof(canon_payload_))
		canon_payload_[canon_payload_len_++] = b;

	if (canon_cmd_ == 'g') {
		if (canon_seen_ == 0) {
			CanonGraphicsMode mode;
			counted_graphics_ = true;
			counted_graphics_buf_len_ = 0;
			if (canon_bracket_g_mode(b, mode)) {
				counted_graphics_dot_w_ = mode.dot_w;
				counted_graphics_pin_h_ = mode.pin_h;
				counted_graphics_bytes_per_col_ = mode.bytes_per_col;
			} else {
				counted_graphics_ = false;
				counted_graphics_bytes_per_col_ = 0;
			}
		} else if (counted_graphics_) {
			float old_w = gfx_dot_w_;
			gfx_dot_w_ = counted_graphics_dot_w_;
			counted_graphics_buf_[counted_graphics_buf_len_++] = b;
			if (counted_graphics_buf_len_ >= counted_graphics_bytes_per_col_) {
				if (counted_graphics_bytes_per_col_ == 1)
					emit_gfx_col(counted_graphics_buf_[0]);
				else
					emit_gfx_col_bytes(counted_graphics_buf_,
					                   counted_graphics_bytes_per_col_,
					                   counted_graphics_pin_h_);
				counted_graphics_buf_len_ = 0;
			}
			gfx_dot_w_ = old_w;
		}
	}

	canon_seen_++;
	if (canon_seen_ >= canon_count_)
		finish_counted_sequence();
}

void CanonBj10ePrinter::finish_counted_sequence()
{
	if (canon_cmd_ == '@' && mode2_ && canon_payload_len_ >= 2) {
		uint8_t effects = canon_payload_[1];
		apply_bj10e_nibble_style((uint8_t)(effects & 0x0F),
		                         st_.presentation_highlight);
		apply_bj10e_nibble_style((uint8_t)(effects >> 4),
		                         st_.reverse_image);
	}
	if (canon_cmd_ == '@' && canon_payload_len_ >= 3) {
		uint8_t height = canon_payload_[2];
		uint8_t low = (uint8_t)(height & 0x0F);
		uint8_t high = (uint8_t)(height >> 4);
		apply_bj10e_nibble_style(low, st_.double_high);
		apply_bj10e_nibble_style(high, st_.double_high_motion);
	}
	if (canon_cmd_ == '@' && canon_payload_len_ >= 4) {
		uint8_t dblwide = (uint8_t)(canon_payload_[3] & 0x0F);
		if (dblwide == 1) st_.expanded = false;
		else if (dblwide == 2) st_.expanded = true;
	}
	if (canon_cmd_ == 'T' && canon_payload_len_ >= 4) {
		uint16_t cpid = (uint16_t)(((uint16_t)canon_payload_[2] << 8) |
		                           canon_payload_[3]);
		if (cpid == 850)
			st_.codepage_850 = true;
		else if (cpid == 437)
			st_.codepage_850 = false;
	}
	if (canon_cmd_ == 'K' && canon_payload_len_ >= 1) {
		uint8_t init = canon_payload_[0];
		bool user_init = init == 0 || init == 1 || init == 254;
		bool factory_init = init == 4 || init == 5 || init == 255;
		if (user_init || factory_init) {
			cancel_pending_line();
			st_ = PrinterState{};
			apply_config(factory_init ? default_config_for(PrinterModel::CanonBJ10e) : cfg_);
			set_default_horizontal_tabs();
			set_default_vertical_tabs();
			reset_graphics_reassignments();
			reset_downloaded_fonts();
		}
	}
	if (canon_cmd_ == 'I' && mode2_ && canon_payload_len_ > 0) {
		uint8_t mode = canon_payload_[0];
		if (canon_payload_len_ > 1) {
			switch (mode) {
			case 0x00: case 0x02: case 0x03: case 0x04:
			case 0x06: case 0x07: case 0x08: case 0x0A:
			case 0x0C: case 0x0E: case 0x10: case 0x12:
			case 0x14: case 0x16:
				break;
			default:
				mode = canon_payload_[1];
				break;
			}
		}
		apply_print_mode(mode);
	}

	canon_expect_ = CanonExpect::Normal;
	counted_graphics_ = false;
	counted_graphics_bytes_per_col_ = 0;
	counted_graphics_buf_len_ = 0;
}

bool CanonBj10ePrinter::parse_esc_extension(uint8_t b)
{
	if (PpdsPrinter::parse_esc_extension(b))
	{
		if (b == ':') {
			bj_raw_12_ = true;
			bj_raw_proportional_ = false;
			derive_print_mode();
		}
		return true;
	}

	switch (b) {
	case '5':
		canon_expect_ = CanonExpect::Esc5;
		return true;
	case '6':
		charset1_controls_ = false;
		return true;
	case '7':
		charset1_controls_ = true;
		return true;
	case 'I':
		canon_expect_ = CanonExpect::EscI;
		return true;
	case 'P':
		canon_expect_ = CanonExpect::EscP;
		return true;
	case 'E':
		bj_raw_emphasized_ = true;
		derive_print_mode();
		return true;
	case 'F':
		bj_raw_emphasized_ = false;
		derive_print_mode();
		return true;
	case 'G':
		st_.double_strike = true;
		return true;
	case 'H':
		st_.double_strike = false;
		return true;
	case '>':
	case '?':
	case '@':
	case 'M':
	case 'f':
	case 'n':
		canon_expect_ = CanonExpect::Sink1;
		return true;
	case 'p':
		canon_expect_ = CanonExpect::EscLowerP;
		return true;
	case '!':
		canon_expect_ = CanonExpect::MasterSel;
		return true;
	case 'R':
		set_default_horizontal_tabs();
		return true;
	case 'X':
		canon_expect_ = CanonExpect::EscXLeft;
		return true;
	case 'd':
		canon_expect_ = CanonExpect::EscDLo;
		return true;
	case '=':
		canon_expect_ = CanonExpect::EscEqLo;
		return true;
	case '[':
		canon_expect_ = CanonExpect::EscBracketSub;
		return true;
	case '*':
		canon_expect_ = CanonExpect::EscStarMode;
		return true;
	case '\\':
		canon_expect_ = CanonExpect::AllCharsLo;
		return true;
	case '^':
		canon_expect_ = CanonExpect::AllCharsOne;
		return true;
	case ']':
		flush_pending_line();
		new_page_if_needed();
		page_dirty_ = true;
		st_.y_pos -= st_.line_spacing_in;
		if (st_.y_pos < st_.top_margin_in)
			st_.y_pos = st_.top_margin_in;
		finish_printed_line();
		return true;
	case 'j':
	case 'Q':
		return true;
	default:
		return false;
	}
}

void CanonBj10ePrinter::parse_byte(uint8_t b)
{
	if (consume_remain_ > 0) {
		if (--consume_remain_ <= 0)
			canon_expect_ = CanonExpect::Normal;
		return;
	}

	switch (canon_expect_) {
	case CanonExpect::Normal:
		break;
	case CanonExpect::Esc5:
		st_.auto_lf = (b & 1);
		canon_expect_ = CanonExpect::Normal;
		return;
	case CanonExpect::EscI:
		apply_print_mode(b);
		canon_expect_ = CanonExpect::Normal;
		return;
	case CanonExpect::EscP:
		bj_raw_proportional_ = (b & 1);
		if (bj_raw_proportional_ && !mode2_)
			st_.font_mode = 0;
		derive_print_mode();
		canon_expect_ = CanonExpect::Normal;
		return;
	case CanonExpect::EscLowerP:
		bj_raw_proportional_ = (b & 1);
		derive_print_mode();
		canon_expect_ = CanonExpect::Normal;
		return;
	case CanonExpect::Sink1:
		canon_expect_ = CanonExpect::Normal;
		return;
	case CanonExpect::MasterSel:
		bj_raw_12_ = !!(b & 0x01);
		bj_raw_proportional_ = !!(b & 0x02);
		bj_raw_17_ = !!(b & 0x04);
		bj_raw_emphasized_ = !!(b & 0x08);
		st_.expanded = !!(b & 0x20);
		st_.underline = !!(b & 0x80);
		derive_print_mode();
		canon_expect_ = CanonExpect::Normal;
		return;
	case CanonExpect::EscXLeft:
		canon_p1_ = b;
		canon_expect_ = CanonExpect::EscXRight;
		return;
	case CanonExpect::EscXRight:
		if (canon_p1_ > 0)
			st_.left_margin_in = (float)canon_p1_ / (float)st_.pitch_cpi;
		if (b > canon_p1_)
			st_.right_margin_in = (float)b / (float)st_.pitch_cpi;
		canon_expect_ = CanonExpect::Normal;
		return;
	case CanonExpect::EscDLo:
		canon_p1_ = b;
		canon_expect_ = CanonExpect::EscDHi;
		return;
	case CanonExpect::EscDHi:
		st_.x_pos += (float)((int)canon_p1_ | ((int)b << 8)) / 120.0f;
		canon_expect_ = CanonExpect::Normal;
		return;
	case CanonExpect::EscEqLo:
		canon_p1_ = b;
		canon_expect_ = CanonExpect::EscEqHi;
		return;
	case CanonExpect::EscEqHi:
		start_download((int)canon_p1_ | ((int)b << 8));
		return;
	case CanonExpect::EscEqData:
		download_payload_.push_back(b);
		if (--download_remain_ <= 0)
			finish_download();
		return;
	case CanonExpect::EscBracketSub:
		if (b == '@' || b == 'K' || b == 'T' || b == '\\' || b == 'g' ||
		    (mode2_ && b == 'I'))
			begin_counted_sequence(b);
		else
			canon_expect_ = CanonExpect::Normal;
		return;
	case CanonExpect::EscBracketLo:
		canon_p1_ = b;
		canon_expect_ = CanonExpect::EscBracketHi;
		return;
	case CanonExpect::EscBracketHi:
		canon_count_ = (uint16_t)((uint16_t)canon_p1_ | ((uint16_t)b << 8));
		canon_seen_ = 0;
		if (canon_count_ == 0)
			finish_counted_sequence();
		else
			canon_expect_ = CanonExpect::EscBracketData;
		return;
	case CanonExpect::EscBracketData:
		handle_counted_byte(b);
		return;
	case CanonExpect::AllCharsLo:
		canon_p1_ = b;
		canon_expect_ = CanonExpect::AllCharsHi;
		return;
	case CanonExpect::AllCharsHi:
		canon_count_ = (uint16_t)((uint16_t)canon_p1_ | ((uint16_t)b << 8));
		canon_expect_ = canon_count_ > 0 ? CanonExpect::AllCharsData
		                                 : CanonExpect::Normal;
		return;
	case CanonExpect::AllCharsOne:
		emit_char(b);
		canon_expect_ = CanonExpect::Normal;
		return;
	case CanonExpect::AllCharsData:
		emit_char(b);
		if (--canon_count_ == 0)
			canon_expect_ = CanonExpect::Normal;
		return;
	case CanonExpect::EscStarMode:
		{
			CanonGraphicsMode mode;
			bool known = canon_star_mode(b, mode);
			counted_graphics_ = alternate_graphics_mode_ && known;
			counted_graphics_dot_w_ = known ? mode.dot_w : 1.0f / 60.0f;
			counted_graphics_pin_h_ = known ? mode.pin_h : 1.0f / 180.0f;
			counted_graphics_bytes_per_col_ = known ? mode.bytes_per_col : 1;
			counted_graphics_buf_len_ = 0;
			canon_expect_ = CanonExpect::EscStarLo;
		}
		return;
	case CanonExpect::EscStarLo:
		canon_p1_ = b;
		canon_expect_ = CanonExpect::EscStarHi;
		return;
	case CanonExpect::EscStarHi:
		canon_star_columns_ = (uint16_t)((uint16_t)canon_p1_ | ((uint16_t)b << 8));
		canon_star_remain_ = (int)canon_star_columns_ * counted_graphics_bytes_per_col_;
		canon_expect_ = canon_star_remain_ > 0 ? CanonExpect::EscStarData
		                                       : CanonExpect::Normal;
		return;
	case CanonExpect::EscStarData:
		if (counted_graphics_) {
			float old_w = gfx_dot_w_;
			gfx_dot_w_ = counted_graphics_dot_w_;
			counted_graphics_buf_[counted_graphics_buf_len_++] = b;
			if (counted_graphics_buf_len_ >= counted_graphics_bytes_per_col_) {
				if (counted_graphics_bytes_per_col_ == 1)
					emit_gfx_col(counted_graphics_buf_[0]);
				else
					emit_gfx_col_bytes(counted_graphics_buf_,
					                   counted_graphics_bytes_per_col_,
					                   counted_graphics_pin_h_);
				counted_graphics_buf_len_ = 0;
			}
			gfx_dot_w_ = old_w;
		}
		if (--canon_star_remain_ <= 0) {
			canon_expect_ = CanonExpect::Normal;
			counted_graphics_ = false;
			counted_graphics_buf_len_ = 0;
		}
		return;
	case CanonExpect::FsPrefix:
		if (b == 'C')
			canon_expect_ = CanonExpect::FsSub;
		else
			canon_expect_ = CanonExpect::Normal;
		return;
	case CanonExpect::FsSub:
		switch (b) {
		case 'J':
			canon_expect_ = CanonExpect::FsCJMode;
			break;
		case 'B':
			canon_expect_ = CanonExpect::FsCB1;
			break;
		case 'M':
			consume_bytes(2);
			break;
		case 'R': case 'S': case 'F': case 'I':
			consume_bytes(1);
			break;
		default:
			canon_expect_ = CanonExpect::Normal;
			break;
		}
		return;
	case CanonExpect::FsCJMode:
		canon_p1_ = b;
		canon_expect_ = CanonExpect::FsCJAmount;
		return;
	case CanonExpect::FsCJAmount:
		if (canon_p1_ == 0 || canon_p1_ == 4) {
			float scale = canon_p1_ == 4 ? 1.0f : 2.0f;
			flush_pending_line();
			new_page_if_needed();
			page_dirty_ = true;
			st_.y_pos += scale * (float)b / 360.0f;
			finish_printed_line();
		}
		canon_expect_ = CanonExpect::Normal;
		return;
	case CanonExpect::FsCB1:
		canon_p1_ = b;
		canon_expect_ = CanonExpect::FsCB2;
		return;
	case CanonExpect::FsCB2:
		(void)canon_p1_;
		canon_expect_ = CanonExpect::Normal;
		return;
	}

	if (mode2_ && b == 0x1C) {
		canon_expect_ = CanonExpect::FsPrefix;
		return;
	}
	if (b == 0x0F) {
		bj_raw_17_ = true;
		bj_raw_proportional_ = false;
		derive_print_mode();
		return;
	}
	if (b == 0x12) {
		bj_raw_12_ = false;
		bj_raw_17_ = false;
		bj_raw_proportional_ = false;
		derive_print_mode();
		return;
	}
	if (b == 0x0A) {
		line_feed();
		if (cr_after_lf_) {
			st_.x_pos = st_.left_margin_in;
			st_.expanded_line = false;
		}
		return;
	}

	if (charset1_controls_ && b >= 0x80 && b < 0xA0) {
		EscpPrinter::parse_byte((uint8_t)(b & 0x7F));
		return;
	}
	if (!charset1_controls_ && printable_c0(b)) {
		emit_char(b);
		return;
	}

	EscpPrinter::parse_byte(b);
}

std::unique_ptr<PrinterSim> create_pcl_printer(PrinterModel model, PdfWriter &pdf);
std::unique_ptr<PrinterSim> create_imagewriter_printer(PrinterModel model, PdfWriter &pdf);

std::unique_ptr<PrinterSim> create_printer(PrinterModel model, PdfWriter &pdf)
{
	switch (model) {
	case PrinterModel::EpsonFX:
		return std::make_unique<EscpPrinter>(model, pdf);
	case PrinterModel::EpsonLQ:
		return std::make_unique<Escp2Printer>(model, pdf);
	case PrinterModel::CanonBJ10e:
		return std::make_unique<CanonBj10ePrinter>(model, pdf);
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
