// license:BSD-3-Clause
// copyright-holders:Stephen Hurd
#ifndef PRINTER_H
#define PRINTER_H

#include "pagebuf.h"
#include "pdfwriter.h"
#include <cstddef>
#include <cstdint>
#include <vector>

enum class PrinterModel {
	IbmX24E,
	IbmXIII,
	EpsonLQ500,
	EpsonFX,
	CanonBJ10e,
	HpJet,
	ImageWriter,
};

enum class DotTech {
	Impact9,
	Impact24,
	Inkjet,
	Toner,
};

struct PrinterProfile {
	PrinterModel model;
	const char  *name;
	DotTech      tech;
	int          render_dpi;
	int          draft_hres;
	int          draft_vres;
	int          lq_hres;
	int          lq_vres;
	int          cell_w_draft;
	int          cell_h;
	int          cell_w_lq;
	float        dot_radius_mm;
	float        jitter_mm;
	float        dot_intensity;
	float        dot_sharpness;
	float        overprint_gamma;
	float        radius_variance;
	float        intensity_variance;
};

const PrinterProfile &profile_for(PrinterModel m);

struct PrinterConfig {
	float pitch_cpi = 10;
	bool slashed_zero = false;
	bool emphasized = false;
	int  charset = 0;
	bool auto_lf = false;
	int  perf_skip = 0;
	// Canon BJ-10e: 0=High Quality/full composition, 1=Economy dot omission.
	int  font_mode = 0;
	int  page_length_lines = 66;
	int  dip_switches = 0;
	bool double_width = false;
	bool double_high = false;
	bool proportional = false;
};

PrinterConfig default_config_for(PrinterModel model);
PrinterConfig load_printer_config(const char *path, PrinterModel model);
float parse_pitch(const char *val, PrinterModel model);

struct Bj10eUserFont {
	bool defined[256] = {};
	uint8_t width[256] = {};
	uint64_t glyph[256][60] = {};
};

struct PrinterState {
	float x_pos = 0;
	float y_pos = 0;

	float pitch_cpi = 10;
	bool  bold = false;
	bool  underline = false;
	bool  overline = false;
	bool  double_strike = false;
	bool  superscript = false;
	bool  subscript = false;
	bool  expanded = false;
	bool  expanded_line = false;
	bool  condensed = false;
	bool  italic = false;

	float line_spacing_in = 1.0f / 6.0f;
	bool  unidirectional = false;
	bool  unidirectional_line = false;
	bool  line_dir_ltr = true;

	float left_margin_in = 0.25f;
	float right_margin_in = 8.25f;
	float top_margin_in = 0.5f;

	float page_width_in = 8.5f;
	float page_height_in = 11.0f;

	bool  auto_lf = false;
	int   perf_skip_lines = 0;
	int   charset = 0;
	bool  slashed_zero = false;
	bool  reverse_image = false;
	bool  presentation_highlight = false;

	bool  mousetext_mode = false;
	bool  include_8th_bit = false;
	bool  half_height = false;
	bool  double_high = false;
	bool  double_high_motion = false;
	bool  reverse_lf = false;
	int   font_mode = 0;
	bool  cr_insertion = true;
	bool  lf_when_full = false;
	bool  proportional = false;
	bool  codepage_850 = false;
	uint8_t ribbon_color = 8;
	bool  bj10e_graphics_density_omit = false;
	bool  bj10e_use_downloaded_font = false;
	const Bj10eUserFont *bj10e_user_font = nullptr;
	bool  selected = true;
	bool  printable_low_controls = false;
	bool  printable_high_controls = false;
	int   msb_mode = 0; // -1 force 0, 0 accept as sent, 1 force 1
	bool  use_user_chars = false;
	bool  use_high_user_chars = false;
	uint8_t user_char_24_d0[256] = {};
	uint8_t user_char_24_d1[256] = {};
	uint8_t user_char_24_d2[256] = {};
	uint8_t user_char_24_glyph[256][108] = {};  // max 36 cols × 3 bytes
	bool    user_char_24_defined[256] = {};
	uint8_t lq500_family = 0;     // ESC k: 0=Roman, 1=SansSerif
	bool    lq500_lq_mode = false; // ESC x: false=Draft, true=LQ
	uint8_t lq500_char_table = 1; // ESC t: 0=Italic, 1=Graphics, 2=remap
	uint8_t lq500_interchar = 0;  // ESC SP: extra dot spacing
	uint8_t lq500_justify = 0;    // ESC a: 0=left, 1=center, 2=right, 3=full
	uint8_t lq500_char_style = 0; // ESC q: 0=normal, 1=outline, 2=shadow, 3=both
	int   prop_dpi = 144;
	int   prop_spacing = 0;
	int   tab_stops[32] = {};
	float tab_stops_in[32] = {};
	int   tab_count = 0;
	float vtab_stops[8][16] = {};
	int   vtab_count[8] = {};
	int   vtab_channel = 0;
	bool  user_char_defined[256] = {};
	uint8_t user_char_prefix[256] = {};
	uint16_t user_char_glyph[256][16] = {};
};

class DotRenderer;

struct PendingLineChar {
	uint8_t ch = 0;
	PrinterState state = {};
	float advance_in = 0.0f;
	bool has_text = false;
	TextGlyph text = {};
	size_t bj10e_dot_count = 0;
};

struct Bj10ePendingDot {
	int xdot = 0;
	int ydot = 0;
	bool omit = false;
};

class PrinterSim {
public:
	PrinterSim(PrinterModel model, PdfWriter &pdf);
	virtual ~PrinterSim();

	PrinterSim(const PrinterSim &) = delete;
	PrinterSim &operator=(const PrinterSim &) = delete;

	void feed(const uint8_t *data, size_t len);
	void flush();
	virtual void apply_config(const PrinterConfig &cfg);

	const PrinterState &state() const { return st_; }

protected:
	virtual void parse_byte(uint8_t b) = 0;

	void emit_char(uint8_t ch);
	void flush_pending_line();
	void cancel_pending_line();
	void delete_pending_char();
	void reset_printer_state(const PrinterConfig &cfg);
	void wrap_full_line(bool feed);
	size_t queue_bj10e_text_dots(uint8_t ch, const PrinterState &state);
	void queue_bj10e_graphics_dot(float x_in, float y_in, bool omit);
	void flush_bj10e_pending_dots();
	void carriage_return();
	void line_feed();
	void form_feed();
	void new_page_if_needed();
	void mark_line_output(bool force_ltr = false);
	void advance_line_direction();
	void finish_printed_line();

	PrinterState st_;
	PrinterConfig cfg_;
	const PrinterProfile &prof_;
	PdfWriter &pdf_;
	std::unique_ptr<PageBitmap> page_;
	std::unique_ptr<DotRenderer> dots_;
	bool page_dirty_ = false;
	bool line_output_ = false;
	bool line_force_ltr_ = false;
	std::vector<TextGlyph> text_buf_;
	std::vector<PendingLineChar> pending_line_;
	std::vector<Bj10ePendingDot> bj10e_pending_dots_;
};

std::unique_ptr<PrinterSim> create_printer(PrinterModel model, PdfWriter &pdf);

#endif
