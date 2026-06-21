// license:BSD-3-Clause
// copyright-holders:Stephen Hurd
#include "dotrender.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

void DotRenderer::stamp_pin(PageBitmap &page, float x_in, float y_in,
                            int dpi, float radius_mm, float jitter_mm,
                            float intensity, float sharpness,
                            float overprint_gamma,
                            float radius_variance,
                            float intensity_variance,
                            float edge_softness,
                            float x_scale, float y_scale)
{
	auto stamp_component = [&](float red, float green, float blue) {
		std::normal_distribution<float> jdist(0.0f, jitter_mm);
		float jx = jdist(rng_);
		float jy = jdist(rng_);

		float px = (x_in + jx / 25.4f) * (float)dpi;
		float py = (y_in + jy / 25.4f) * (float)dpi;
		std::normal_distribution<float> rdist(radius_mm,
		                                      radius_mm * radius_variance);
		float r_px = std::max(radius_mm * 0.7f, rdist(rng_)) / 25.4f * (float)dpi;

		std::normal_distribution<float> idist(intensity,
		                                      intensity * intensity_variance);
		float ink = std::max(0.3f, std::min(1.0f, idist(rng_)));

		page.stamp_dot_rgb(px, py, r_px, ink, sharpness,
		                   red, green, blue, overprint_gamma,
		                   edge_softness, x_scale, y_scale);
	};

	if (ribbon_mask_ == 0) {
		stamp_component(ink_red_, ink_green_, ink_blue_);
		return;
	}

	if (ribbon_mask_ & 0x08) {
		stamp_component(0.00f, 0.00f, 0.00f);
		return;
	}

	if (ribbon_mask_ & 0x01)
		stamp_component(1.00f, 0.86f, 0.04f); // yellow
	if (ribbon_mask_ & 0x02)
		stamp_component(0.85f, 0.08f, 0.58f); // magenta
	if (ribbon_mask_ & 0x04)
		stamp_component(0.04f, 0.47f, 0.82f); // cyan
}

void DotRenderer::set_ink_color(float red, float green, float blue)
{
	ink_red_ = std::max(0.0f, std::min(1.0f, red));
	ink_green_ = std::max(0.0f, std::min(1.0f, green));
	ink_blue_ = std::max(0.0f, std::min(1.0f, blue));
	ribbon_mask_ = 0;
}

void DotRenderer::set_ribbon_mask(uint8_t mask)
{
	ribbon_mask_ = mask & 0x0F;
}

void set_ribbon_ink(DotRenderer &dr, uint8_t color)
{
	dr.set_ribbon_mask(color);
}

static void lq500_apply_script_metrics(const PrinterState &st, uint8_t ch,
                                       int &width, int &cell_total, int start)
{
	if (!st.lq500_lq_mode || !(st.superscript || st.subscript))
		return;
	const uint8_t *sec = get_lq500_sec_metrics(1, ch);
	if (!sec || (sec[1] == 0 && sec[2] == 0))
		return;
	width = sec[1];
	cell_total = start + sec[1] + sec[2];
}

static void render_glyph_9pin(DotRenderer &dr, PageBitmap &page,
                               const PrinterState &st, const PrinterProfile &prof,
                               const uint16_t *glyph, int glyph_w,
                               float *pin_vib)
{
	float cw_in = 1.0f / st.pitch_cpi;
	if (st.condensed && st.pitch_cpi <= 10) cw_in = 1.0f / 17.16f;
	if (st.proportional && st.prop_dpi > 0)
		cw_in = static_cast<float>(glyph_w) / static_cast<float>(st.prop_dpi);
	if (st.expanded || st.expanded_line) cw_in *= 2.0f;

	float dot_h_in = 1.0f / 72.0f;
	bool expanded = st.expanded || st.expanded_line;
	float dot_w_in = st.proportional
	    ? (expanded ? 2.0f : 1.0f) / static_cast<float>(st.prop_dpi)
	    : (cw_in / static_cast<float>(glyph_w));
	bool script = st.superscript || st.subscript;
	float expand_dup = expanded
	    ? (prof.model == PrinterModel::EpsonFX ? dot_w_in : dot_w_in * 0.5f)
	    : 0.0f;
	float dot_pitch = (prof.model == PrinterModel::ImageWriter)
	    ? (1.0f / 80.0f) : (1.0f / 60.0f);
	float bold_off = dot_pitch * 0.5f;
	float double_strike_off = 1.0f / 216.0f;

	float ink = prof.dot_intensity;
	float sharp = prof.dot_sharpness;
	if (prof.model == PrinterModel::ImageWriter)
		set_ribbon_ink(dr, st.ribbon_color);
	else
		dr.set_ink_color(0.0f, 0.0f, 0.0f);

	constexpr float vib_decay = 0.85f;
	constexpr float vib_kick = 0.25f;
	constexpr float vib_floor = 0.33f;
	float bidi_offset = st.line_dir_ltr ? 0.0f : (1.0f / 120.0f);
	bool fx_double_strike = prof.model == PrinterModel::EpsonFX &&
	    (st.double_strike || script);

	auto stamp_text_dot = [&](float x, float y, float radius_mm, float jitter_mm) {
		auto stamp_pass = [&](float y_off) {
			dr.stamp_pin(page, x, y + y_off, prof.render_dpi,
			             radius_mm, jitter_mm, ink, sharp,
			             prof.overprint_gamma, prof.radius_variance,
			             prof.intensity_variance, prof.dot_edge_softness,
			             prof.dot_x_scale, prof.dot_y_scale);
			if (expanded)
				dr.stamp_pin(page, x + expand_dup, y + y_off, prof.render_dpi,
				             radius_mm, jitter_mm, ink, sharp,
				             prof.overprint_gamma, prof.radius_variance,
				             prof.intensity_variance, prof.dot_edge_softness,
				             prof.dot_x_scale, prof.dot_y_scale);
			if (st.bold) {
				dr.stamp_pin(page, x + bold_off, y + y_off,
				             prof.render_dpi, radius_mm, jitter_mm, ink, sharp,
				             prof.overprint_gamma, prof.radius_variance,
				             prof.intensity_variance, prof.dot_edge_softness,
				             prof.dot_x_scale, prof.dot_y_scale);
				if (expanded)
					dr.stamp_pin(page, x + expand_dup + bold_off, y + y_off,
					             prof.render_dpi, radius_mm, jitter_mm, ink, sharp,
					             prof.overprint_gamma, prof.radius_variance,
					             prof.intensity_variance, prof.dot_edge_softness,
					             prof.dot_x_scale, prof.dot_y_scale);
			}
		};

		stamp_pass(0.0f);
		if (fx_double_strike)
			stamp_pass(double_strike_off);
	};

	// Script mode: two-pass interleaved printing at half height
	// Pass 1: glyph rows 1,3,5,7 → pins 0-3
	// Pass 2: glyph rows 0,2,4   → pins 0-2 (½ pin offset = 1/144")
	// Subscript shifts the whole block down by ~4.5 pin heights
	static constexpr int script_pass1_rows[] = {1, 3, 5, 7};
	static constexpr int script_pass2_rows[] = {0, 2, 4, 6, 8};
	float script_y_off = st.subscript ? dot_h_in * 4.5f : 0.0f;

	// Half-height: pins 4-7 only (0-indexed: 3-6), two-pass at 144 dpi
	// Pass 1: even glyph rows (0,2,4,6) → 4 pins at base position
	// Pass 2: odd glyph rows (1,3,5,7) → 4 pins offset by 1/144"
	static constexpr int hh_even_rows[] = {0, 2, 4, 6};
	static constexpr int hh_odd_rows[]  = {1, 3, 5, 7};

	for (int col = 0; col < glyph_w; col++) {
		uint16_t column = glyph[col];
		if (st.underline)
			column = static_cast<uint16_t>(column & ~(1 << 8));

		if (pin_vib) {
			for (int pin = 0; pin < 9; pin++)
				pin_vib[pin] *= vib_decay;
		}

		if (st.half_height) {
			float x = st.x_pos + (float)col * dot_w_in + bidi_offset;

			for (int p = 0; p < 4; p++) {
				int row = hh_even_rows[p];
				if (!(column & (1 << row))) continue;
				float y = st.y_pos + (float)(p + 3) * dot_h_in
				          - (9.0f * dot_h_in);
				float jit = prof.jitter_mm;
				if (pin_vib) {
					pin_vib[p + 3] = std::min(1.0f, pin_vib[p + 3] + vib_kick);
					jit *= vib_floor + (1.0f - vib_floor) * pin_vib[p + 3];
				}
				stamp_text_dot(x, y, prof.dot_radius_mm, jit);
			}
			for (int p = 0; p < 4; p++) {
				int row = hh_odd_rows[p];
				if (!(column & (1 << row))) continue;
				float y = st.y_pos + (float)(p + 3) * dot_h_in
				          + (1.0f / 144.0f) - (9.0f * dot_h_in);
				float jit = prof.jitter_mm;
				if (pin_vib) {
					pin_vib[p + 3] = std::min(1.0f, pin_vib[p + 3] + vib_kick);
					jit *= vib_floor + (1.0f - vib_floor) * pin_vib[p + 3];
				}
				stamp_text_dot(x, y, prof.dot_radius_mm, jit);
			}
		} else if (script) {
			float x = st.x_pos + (float)col * dot_w_in + bidi_offset;

			for (int p = 0; p < 4; p++) {
				int row = script_pass1_rows[p];
				if (!(column & (1 << row))) continue;
				float y = st.y_pos + (float)p * dot_h_in + (1.0f / 144.0f)
				          + script_y_off - (9.0f * dot_h_in);
				float jit = prof.jitter_mm;
				if (pin_vib) {
					pin_vib[p] = std::min(1.0f, pin_vib[p] + vib_kick);
					jit *= vib_floor + (1.0f - vib_floor) * pin_vib[p];
				}
				stamp_text_dot(x, y, prof.dot_radius_mm, jit);
			}
			for (int p = 0; p < 5; p++) {
				int row = script_pass2_rows[p];
				if (!(column & (1 << row))) continue;
				float y = st.y_pos + (float)p * dot_h_in
				          + script_y_off - (9.0f * dot_h_in);
				float jit = prof.jitter_mm;
				if (pin_vib) {
					pin_vib[p] = std::min(1.0f, pin_vib[p] + vib_kick);
					jit *= vib_floor + (1.0f - vib_floor) * pin_vib[p];
				}
				stamp_text_dot(x, y, prof.dot_radius_mm, jit);
			}
		} else {
			for (int pin = 0; pin < 9; pin++) {
				if (!(column & (1 << pin))) continue;

				float jit = prof.jitter_mm;
				if (pin_vib) {
					pin_vib[pin] = std::min(1.0f, pin_vib[pin] + vib_kick);
					jit *= vib_floor + (1.0f - vib_floor) * pin_vib[pin];
				}

				float x = st.x_pos + (float)col * dot_w_in + bidi_offset;
				float y = st.y_pos + ((float)pin * dot_h_in)
				          - (9.0f * dot_h_in);

				stamp_text_dot(x, y, prof.dot_radius_mm, jit);
			}
		}
	}

	if (st.underline) {
		float ul_y = (prof.model == PrinterModel::ImageWriter)
		    ? st.y_pos - dot_h_in
		    : st.y_pos + dot_h_in * 0.5f;
		int dots = (int)(cw_in * (float)prof.render_dpi / (1.0f / 120.0f * (float)prof.render_dpi));
		float step = cw_in / (float)dots;
		for (int i = 0; i < dots; i++) {
			dr.stamp_pin(page, st.x_pos + (float)i * step, ul_y,
			             prof.render_dpi, prof.dot_radius_mm * 0.8f,
			             prof.jitter_mm, ink, sharp,
			             prof.overprint_gamma, prof.radius_variance,
			             prof.intensity_variance, prof.dot_edge_softness,
			             prof.dot_x_scale, prof.dot_y_scale);
		}
	}
}

static void render_iw_glyph_9pin(DotRenderer &dr, PageBitmap &page,
                                  const PrinterState &st,
                                  const PrinterProfile &prof,
                                  const uint16_t *glyph, int glyph_w,
                                  float *pin_vib)
{
	uint8_t component_mask = st.ribbon_color & 0x07;
	bool composite = component_mask && (component_mask & (component_mask - 1)) != 0;
	if ((st.ribbon_color & 0x08) || !composite) {
		render_glyph_9pin(dr, page, st, prof, glyph, glyph_w, pin_vib);
		return;
	}

	for (uint8_t remaining = component_mask; remaining; remaining &= (remaining - 1)) {
		uint8_t component = remaining & (uint8_t)(-remaining);
		PrinterState component_state = st;
		component_state.ribbon_color = component;
		render_glyph_9pin(dr, page, component_state, prof, glyph, glyph_w, pin_vib);
	}
}

void ImpactDot9::render_char(PageBitmap &page, const PrinterState &st,
                              const PrinterProfile &prof, uint8_t ch)
{
	if (prof.model == PrinterModel::EpsonFX) {
		bool italic = st.italic || ch >= 128;
		bool user = st.use_user_chars && st.user_char_defined[ch];
		const uint16_t *glyph = user ? st.user_char_glyph[ch]
		    : italic ? get_fx80_italic_glyph(ch)
		             : get_fx80_roman_glyph(ch);
		if (!glyph) return;
		int w = 12;
		PrinterState st2 = st;
		if (st.proportional) {
			uint8_t start = 0;
			if (user) {
				uint8_t attr = st.user_char_prefix[ch];
				start = (uint8_t)((attr >> 4) & 0x07);
				uint8_t end = (uint8_t)(attr & 0x0F);
				w = end >= start ? end - start + 1 : 1;
			} else {
				start = get_fx80_prop_start(ch, italic);
				w = get_fx80_prop_width(ch, italic);
			}
			glyph += start;
			st2.prop_dpi = 120;
			st2.bold = true;
		}
		render_glyph_9pin(*this, page, st2, prof, glyph, w, pin_vib_);
	} else if (prof.model == PrinterModel::ImageWriter) {
		// IW II font switching rules per Technical Reference Manual Ch.4:
		// Internal values match ROM AA70/AA71: 0=corr, 1=NLQ, 2=draft.
		// Draft (font_mode=2) does NOT support bold, expanded, half-height,
		// super/subscript, or proportional — these force correspondence.
		// NLQ (font_mode=1) does NOT support half-height or super/subscript
		// — these force correspondence.
		int eff_mode = st.font_mode;
		bool expanded = st.expanded || st.expanded_line;
		bool script = st.superscript || st.subscript;
		if (eff_mode == 2) {
			if (st.bold || expanded || st.half_height || script || st.proportional)
				eff_mode = 0;
		} else if (eff_mode == 1) {
			if (st.half_height || script)
				eff_mode = 0;
		}

		const uint16_t *glyph = nullptr;
		const uint16_t *glyph_p2 = nullptr;
		int w = 8;
		if (st.use_user_chars && st.user_char_defined[ch]) {
			glyph = st.user_char_glyph[ch];
			w = st.user_char_prefix[ch] & 0x1F;
			if (w == 0) w = 8;
			if (w > 16) w = 16;
		} else if (eff_mode == 1) {
			if (st.proportional) {
				glyph = get_iw2_nlq_prop_p1_glyph(ch);
				glyph_p2 = get_iw2_nlq_prop_p2_glyph(ch);
				if (glyph)
					w = get_iw2_corr_prop_width(ch);
			} else {
				glyph = get_iw2_nlq_fw_p1_glyph(ch);
				glyph_p2 = get_iw2_nlq_fw_p2_glyph(ch);
				if (glyph) w = 16;
			}
		} else if (eff_mode == 0) {
			if (st.proportional) {
				glyph = get_iw2_corr_prop_glyph(ch);
				if (glyph)
					w = get_iw2_corr_prop_width(ch);
			} else {
				glyph = get_iw2_corr_fw_glyph(ch);
				w = 8;
			}
		}
		if (!glyph) {
			glyph = get_iw2_corr_fw_glyph(ch);
			w = 8;
		}
		if (!glyph) return;
		render_iw_glyph_9pin(*this, page, st, prof, glyph, w, pin_vib_);
		if (glyph_p2) {
			PrinterState st2 = st;
			st2.y_pos += 1.0f / 144.0f;
			render_iw_glyph_9pin(*this, page, st2, prof, glyph_p2, w, pin_vib_);
		}
	} else {
		const uint16_t *glyph = get_9pin_glyph(ch);
		if (!glyph) return;
		render_glyph_9pin(*this, page, st, prof, glyph, 12, pin_vib_);
	}
}

// Render a 24-pin glyph.
// start_col: left-padding columns before the glyph body (used to offset
//            the glyph within a fixed-pitch cell at native DPI).
// native_dot_w: if > 0, use this as the horizontal dot pitch instead of
//               spreading glyph_w columns across the full character cell.
//               Set to 1/hres for LQ-500 fonts (1/120 draft, 1/360 LQ).
static void render_glyph_24pin(DotRenderer &dr, PageBitmap &page,
                                const PrinterState &st, const PrinterProfile &prof,
                                const uint8_t *glyph, int glyph_w,
                                int start_col = 0, float native_dot_w = 0.0f)
{
	float cw_in = 1.0f / (float)st.pitch_cpi;
	if (st.expanded || st.expanded_line) cw_in *= 2.0f;
	if (st.condensed) cw_in *= 0.6f;

	float dot_h_in = 1.0f / 180.0f;
	float dot_w_in = (native_dot_w > 0.0f) ? native_dot_w
	                                        : cw_in / (float)glyph_w;
	float x_off = (float)start_col * dot_w_in;

	float y_scale = 1.0f;
	float y_off = 0.0f;
	// LQ-500 double-height and super/subscript are handled as column
	// transforms in lq500_apply_effects(), not as Y coordinate scaling.
	if (prof.model != PrinterModel::EpsonLQ500) {
		if (st.double_high) { y_scale = 2.0f; }
		if (st.superscript) { y_scale = 0.67f; y_off = 0.0f; }
		if (st.subscript) { y_scale = 0.67f; y_off = dot_h_in * 8.0f; }
	}

	float ink = prof.dot_intensity;
	float sharp = prof.dot_sharpness;

	for (int col = 0; col < glyph_w; col++) {
		uint32_t column = (uint32_t)glyph[col * 3] |
		                  ((uint32_t)glyph[col * 3 + 1] << 8) |
		                  ((uint32_t)glyph[col * 3 + 2] << 16);

		for (int pin = 0; pin < 24; pin++) {
			if (!(column & (1U << pin))) continue;

			float x = st.x_pos + x_off + (float)col * dot_w_in;
			float y = st.y_pos + ((float)pin * dot_h_in * y_scale) + y_off
			          - (24.0f * dot_h_in * y_scale);

			dr.stamp_pin(page, x, y, prof.render_dpi,
			             prof.dot_radius_mm, prof.jitter_mm, ink, sharp,
			             prof.overprint_gamma, prof.radius_variance,
			             prof.intensity_variance, prof.dot_edge_softness,
			             prof.dot_x_scale, prof.dot_y_scale);

			// Non-LQ500 bold: extra strike at half-pitch offset
			if (st.bold && prof.model != PrinterModel::EpsonLQ500) {
				dr.stamp_pin(page, x + dot_w_in * 0.5f, y,
				             prof.render_dpi, prof.dot_radius_mm,
				             prof.jitter_mm, ink, sharp,
				             prof.overprint_gamma, prof.radius_variance,
				             prof.intensity_variance, prof.dot_edge_softness,
				             prof.dot_x_scale, prof.dot_y_scale);
			}
		}
	}

	// LQ-500 underline/overline handled in ImpactDot24::render_char()
	// so it fires for all characters including spaces.
	if (prof.model != PrinterModel::EpsonLQ500) {
		if (st.underline) {
			float ul_y = st.y_pos + dot_h_in * 1.5f;
			int dots = (int)(cw_in / dot_w_in);
			for (int i = 0; i < dots; i++) {
				dr.stamp_pin(page, st.x_pos + (float)i * dot_w_in, ul_y,
				             prof.render_dpi, prof.dot_radius_mm,
				             prof.jitter_mm, ink, sharp,
				             prof.overprint_gamma, prof.radius_variance,
				             prof.intensity_variance, prof.dot_edge_softness,
				             prof.dot_x_scale, prof.dot_y_scale);
			}
		}
		if (st.overline) {
			float ol_y = st.y_pos - (24.0f * dot_h_in * y_scale) - dot_h_in;
			int dots = (int)(cw_in / dot_w_in);
			for (int i = 0; i < dots; i++) {
				dr.stamp_pin(page, st.x_pos + (float)i * dot_w_in, ol_y,
				             prof.render_dpi, prof.dot_radius_mm,
				             prof.jitter_mm, ink, sharp,
				             prof.overprint_gamma, prof.radius_variance,
				             prof.intensity_variance, prof.dot_edge_softness,
				             prof.dot_x_scale, prof.dot_y_scale);
			}
		}
	}
}

static uint32_t glyph_24pin_column(const uint8_t *glyph, int col)
{
	return (uint32_t)glyph[col * 3] |
	       ((uint32_t)glyph[col * 3 + 1] << 8) |
	       ((uint32_t)glyph[col * 3 + 2] << 16);
}

static void render_glyph_24pin_lq_interleave(DotRenderer &dr, PageBitmap &page,
                                              const PrinterState &st,
                                              const PrinterProfile &prof,
                                              const uint8_t *glyph, int glyph_w,
                                              int start_col,
                                              float native_dot_w)
{
	float dot_h_in = 1.0f / 180.0f;
	float x_off = (float)start_col * native_dot_w;
	float ink = prof.dot_intensity;
	float sharp = prof.dot_sharpness;

	for (int col = 0; col + 1 < glyph_w; col++) {
		uint32_t column = glyph_24pin_column(glyph, col);
		if (column == 0)
			continue;

		// LQ glyph records are already in 360-DPI column order.  A
		// second strike fills the ROM-authored blank interleave column;
		// consecutive nonblank columns are real glyph geometry.
		if (glyph_24pin_column(glyph, col + 1) != 0)
			continue;

		for (int pin = 0; pin < 24; pin++) {
			if (!(column & (1U << pin))) continue;

			float x = st.x_pos + x_off + (float)(col + 1) * native_dot_w;
			float y = st.y_pos + (float)pin * dot_h_in
			          - 24.0f * dot_h_in;

			dr.stamp_pin(page, x, y, prof.render_dpi,
			             prof.dot_radius_mm, prof.jitter_mm, ink, sharp,
			             prof.overprint_gamma, prof.radius_variance,
			             prof.intensity_variance, prof.dot_edge_softness,
			             prof.dot_x_scale, prof.dot_y_scale);
		}
	}
}

// LQ-500 effect pipeline: transform column data matching firmware order.
// Returns the (possibly reallocated) data pointer and updated width.
static void lq500_apply_effects(const PrinterState &st,
                                 const uint8_t *src, int &w, int &sc,
                                 std::vector<uint8_t> &buf,
                                 const uint8_t *&dh_second_slice,
                                 const uint8_t *&dh_third_slice,
                                 uint8_t ch)
{
	dh_second_slice = nullptr;
	dh_third_slice = nullptr;
	// Effect #2: Condensed-Draft — merge adjacent column pairs, halve
	// metrics. Fires only in Draft mode when condensed is active and
	// proportional/15cpi are not.
	bool condensed_draft = st.condensed && !st.lq500_lq_mode
	    && !st.proportional && st.pitch_cpi != 15;
	if (condensed_draft && w > 1) {
		int nw = (w + 1) / 2;
		std::vector<uint8_t> tmp((size_t)nw * 3, 0);
		uint8_t prev[3] = {};
		for (int c = 0; c < nw; c++) {
			int s0 = c * 2;
			int s1 = s0 + 1;
			for (int b = 0; b < 3; b++) {
				// Merge column pair via OR
				uint8_t merged = src[(size_t)s0 * 3 + (size_t)b];
				if (s1 < w)
					merged |= src[(size_t)s1 * 3 + (size_t)b];
				// Adjacent-dot restriction against previous output
				uint8_t restricted = (uint8_t)(merged & ~prev[b]);
				tmp[(size_t)c * 3 + (size_t)b] = restricted;
				prev[b] = restricted;
			}
		}
		buf = std::move(tmp);
		w = nw;
		sc /= 2;
		src = buf.data();
	}

	// Effect #3: Emphasized — OR a second strike one physical printhead
	// pitch to the right. The offset below is in current glyph-buffer
	// columns: LQ uses half-pitch 360-DPI columns, so the same physical
	// shift is two buffer columns there.
	if (st.bold && w > 0) {
		int bold_offset = st.lq500_lq_mode ? 2 : 1;
		int nw = w + bold_offset;
		std::vector<uint8_t> tmp((size_t)nw * 3, 0);
		std::memcpy(tmp.data(), src, (size_t)w * 3);
		for (int c = 0; c < w; c++) {
			tmp[(size_t)(c + bold_offset) * 3 + 0] |= src[(size_t)c * 3 + 0];
			tmp[(size_t)(c + bold_offset) * 3 + 1] |= src[(size_t)c * 3 + 1];
			tmp[(size_t)(c + bold_offset) * 3 + 2] |= src[(size_t)c * 3 + 2];
		}
		buf = std::move(tmp);
		w = nw;
		src = buf.data();
	}

	// Effect #5: Double-wide ($4830) — three firmware paths:
	// Half-res ($4861, VV:29.7 set): literal column duplication,
	//   each source column → two consecutive dest columns.
	// Normal LQ ($48BB, VV:28.2 clear): ORs adjacent source columns
	//   (no-op for interleaved blanks), writes with blank skip.
	// Super/subscript ($4879, VV:28.2 set): duplication with
	//   adjacent-dot restriction.
	if ((st.expanded || st.expanded_line) && w > 0) {
		int nw = w * 2;
		std::vector<uint8_t> tmp((size_t)nw * 3, 0);
		if (!st.lq500_lq_mode) {
			// Half-res path: literal column duplication
			for (int c = 0; c < w; c++) {
				int dc = c * 2;
				for (int b = 0; b < 3; b++) {
					uint8_t v = src[(size_t)c * 3 + (size_t)b];
					tmp[(size_t)dc * 3 + (size_t)b] = v;
					tmp[(size_t)(dc + 1) * 3 + (size_t)b] = v;
				}
			}
		} else {
			// Normal LQ path: OR with right neighbor, blank skip
			for (int c = 0; c < w; c++) {
				int dc = c * 2;
				for (int b = 0; b < 3; b++) {
					uint8_t v = src[(size_t)c * 3 + (size_t)b];
					if (c > 0 && c < w - 1)
						v |= src[(size_t)(c + 1) * 3 + (size_t)b];
					tmp[(size_t)dc * 3 + (size_t)b] = v;
					// dc+1 stays zero (blank column)
				}
			}
		}
		buf = std::move(tmp);
		w = nw;
		sc *= 2;
		src = buf.data();
	}

	// Effect #6: Italic shear ($4ACE) — two paths:
	// Normal: 4-pin nibbles across 5 destination columns.
	// Double-wide ($4B3E): 2-bit pairs across 11 destination columns.
	if (st.italic && w > 0) {
		bool dw = st.expanded || st.expanded_line;
		int spread = dw ? 11 : 5;
		int nw = w + spread;
		std::vector<uint8_t> tmp((size_t)nw * 3, 0);
		for (int c = 0; c < w; c++) {
			uint8_t b0 = src[(size_t)c * 3 + 0]; // top pins 1-8
			uint8_t b1 = src[(size_t)c * 3 + 1]; // mid pins 9-16
			uint8_t b2 = src[(size_t)c * 3 + 2]; // bot pins 17-24
			if (dw) {
				// Double-wide: each byte split into four 2-bit pairs,
				// each ORed into a separate column at DE, DE+3, DE+6, DE+9.
				// Dest offset starts at +8 columns (EA += 24).
				// After bit reversal: bit pairs map to 2-pin groups.
				// byte 2 (bottom) → dest cols c+0..c+3
				tmp[(size_t)(c+0)*3+2] |= (uint8_t)(b2 & 0xC0);
				tmp[(size_t)(c+1)*3+2] |= (uint8_t)(b2 & 0x30);
				tmp[(size_t)(c+2)*3+2] |= (uint8_t)(b2 & 0x0C);
				tmp[(size_t)(c+3)*3+2] |= (uint8_t)(b2 & 0x03);
				// byte 1 (middle) → dest cols c+4..c+7
				tmp[(size_t)(c+4)*3+1] |= (uint8_t)(b1 & 0xC0);
				tmp[(size_t)(c+5)*3+1] |= (uint8_t)(b1 & 0x30);
				tmp[(size_t)(c+6)*3+1] |= (uint8_t)(b1 & 0x0C);
				if (c+7 < nw)
					tmp[(size_t)(c+7)*3+1] |= (uint8_t)(b1 & 0x03);
				// byte 0 (top) → dest cols c+8..c+11
				if (c+8 < nw)
					tmp[(size_t)(c+8)*3+0] |= (uint8_t)(b0 & 0xC0);
				if (c+9 < nw)
					tmp[(size_t)(c+9)*3+0] |= (uint8_t)(b0 & 0x30);
				if (c+10 < nw)
					tmp[(size_t)(c+10)*3+0] |= (uint8_t)(b0 & 0x0C);
				if (c+11 < nw)
					tmp[(size_t)(c+11)*3+0] |= (uint8_t)(b0 & 0x03);
			} else {
				// Normal: 4-pin nibbles, 5-column spread.
				// After bit reversal: 0xF0 = ROM low nibble (bottom 4),
				// 0x0F = ROM high nibble (top 4).
				// byte 2 (bottom) → dest cols c+0, c+1
				tmp[(size_t)(c+0)*3+2] |= (uint8_t)(b2 & 0xF0);
				tmp[(size_t)(c+1)*3+2] |= (uint8_t)(b2 & 0x0F);
				// byte 1 (middle) → dest cols c+2, c+3
				tmp[(size_t)(c+2)*3+1] |= (uint8_t)(b1 & 0xF0);
				tmp[(size_t)(c+3)*3+1] |= (uint8_t)(b1 & 0x0F);
				// byte 0 (top) → dest cols c+4, c+5
				tmp[(size_t)(c+4)*3+0] |= (uint8_t)(b0 & 0xF0);
				if (c+5 < nw)
					tmp[(size_t)(c+5)*3+0] |= (uint8_t)(b0 & 0x0F);
			}
		}
		buf = std::move(tmp);
		w = nw;
		src = buf.data();
	}

	// Effects #7/#8/#9: Outline+Shadow / Outline / Shadow
	// Character-range gate ($1AF3): VV:28 bit 6 (= VV:23 bit 6) is
	// set by the classifier at $4163 for $B0+/$F0+ characters and
	// cleared by font reconfig at $14CC. Outline/shadow fire only
	// for $20-$AF and user-defined characters.
	// Firmware helpers used by all three effects:
	// $457E (smear): 3-column horizontal OR thickening, width += 2
	// $45F8: shift pin data downward by N, OR into dest at col offset
	// $45B1: shift pin data upward by N, OR into dest at col offset
	// $463F: XOR source into dest with pin 1/24 masking
	uint8_t char_style = st.lq500_char_style;
	bool extended_char = (ch >= 0xB0)
	    && !(st.use_user_chars && st.user_char_24_defined[ch]);
	if (char_style && w > 0 && !extended_char) {
		bool outline = (char_style & 1) != 0;  // VV:2A bit 6
		bool shadow  = (char_style & 2) != 0;  // VV:2A bit 5

		// Shift 3-byte column downward (toward pin 24) by n pins.
		// In our bit-reversed layout: pin 1 = bit 0, pin 24 = bit 23.
		// Downward = left shift. Carry-stick at pin 24 per $45F8.
		auto shift_down = [](const uint8_t *in, uint8_t *out, int n) {
			uint32_t val = (uint32_t)in[0] | ((uint32_t)in[1] << 8)
			             | ((uint32_t)in[2] << 16);
			uint32_t shifted = val << n;
			if (shifted & ~0xFFFFFFu) shifted |= (1u << 23);
			out[0] = (uint8_t)(shifted);
			out[1] = (uint8_t)(shifted >> 8);
			out[2] = (uint8_t)(shifted >> 16);
		};
		// Shift upward (toward pin 1) by n pins. Mirror of $45F8.
		auto shift_up = [](const uint8_t *in, uint8_t *out, int n) {
			uint32_t val = (uint32_t)in[0] | ((uint32_t)in[1] << 8)
			             | ((uint32_t)in[2] << 16);
			uint32_t shifted = val >> n;
			out[0] = (uint8_t)(shifted);
			out[1] = (uint8_t)(shifted >> 8);
			out[2] = (uint8_t)(shifted >> 16);
		};

		// Step 1 (all effects): $457E smear — 3-column OR thickening
		int sw = w + 2;
		std::vector<uint8_t> smeared((size_t)sw * 3, 0);
		for (int c = 0; c < w; c++)
			for (int b = 0; b < 3; b++) {
				uint8_t v = src[(size_t)c * 3 + (size_t)b];
				smeared[(size_t)c * 3 + (size_t)b] |= v;
				smeared[(size_t)(c+1) * 3 + (size_t)b] |= v;
				smeared[(size_t)(c+2) * 3 + (size_t)b] |= v;
			}

		if (outline && shadow) {
			// Effect #7 ($44C4): outline+shadow combined
			// smear (+2) + $4664(B=5) (+10) = w + 12
			int nw = w + 12;
			std::vector<uint8_t> result((size_t)nw * 3, 0);
			// Double-smear: smear the smeared buffer into result
			for (int c = 0; c < sw; c++)
				for (int b = 0; b < 3; b++) {
					uint8_t v = smeared[(size_t)c * 3 + (size_t)b];
					result[(size_t)c * 3 + (size_t)b] |= v;
					if (c + 1 < nw) result[(size_t)(c+1)*3+(size_t)b] |= v;
					if (c + 2 < nw) result[(size_t)(c+2)*3+(size_t)b] |= v;
				}
			// Outline shifts at +1 column: shift-left 1 + shift-right 1
			for (int c = 0; c < sw; c++) {
				uint8_t sh[3];
				shift_up(&smeared[(size_t)c*3], sh, 1);
				for (int b = 0; b < 3; b++)
					result[(size_t)(c+1)*3+(size_t)b] |= sh[b];
				shift_down(&smeared[(size_t)c*3], sh, 1);
				for (int b = 0; b < 3; b++)
					result[(size_t)(c+1)*3+(size_t)b] |= sh[b];
			}
			// Shadow shifts from smeared data
			for (int c = 0; c < sw; c++) {
				uint8_t sh[3];
				// +3 cols, down 1
				shift_down(&smeared[(size_t)c*3], sh, 1);
				if (c + 3 < nw)
					for (int b = 0; b < 3; b++)
						result[(size_t)(c+3)*3+(size_t)b] |= sh[b];
				// +5 cols, down 2
				shift_down(&smeared[(size_t)c*3], sh, 2);
				if (c + 5 < nw)
					for (int b = 0; b < 3; b++)
						result[(size_t)(c+5)*3+(size_t)b] |= sh[b];
			}
			// XOR at +1 column (VV:D1=0: mask pin 1 and pin 24)
			for (int c = 0; c < sw; c++) {
				uint8_t x0 = (uint8_t)(smeared[(size_t)c*3+0] & 0xFE);
				uint8_t x1 = smeared[(size_t)c*3+1];
				uint8_t x2 = (uint8_t)(smeared[(size_t)c*3+2] & 0x7F);
				result[(size_t)(c+1)*3+0] ^= x0;
				result[(size_t)(c+1)*3+1] ^= x1;
				result[(size_t)(c+1)*3+2] ^= x2;
			}
			buf = std::move(result);
			w = nw;
		} else if (outline) {
			// Effect #8 ($43DD): outline
			// smear (+2) + $4664(B=1) (+2) = w + 4
			int nw = w + 4;
			std::vector<uint8_t> result((size_t)nw * 3, 0);
			// Double-smear into result
			for (int c = 0; c < sw; c++)
				for (int b = 0; b < 3; b++) {
					uint8_t v = smeared[(size_t)c * 3 + (size_t)b];
					result[(size_t)c * 3 + (size_t)b] |= v;
					if (c + 1 < nw) result[(size_t)(c+1)*3+(size_t)b] |= v;
					if (c + 2 < nw) result[(size_t)(c+2)*3+(size_t)b] |= v;
				}
			// At +1 column: shift-left 1 (up) + shift-right 1 (down)
			for (int c = 0; c < sw; c++) {
				uint8_t sh[3];
				shift_up(&smeared[(size_t)c*3], sh, 1);
				for (int b = 0; b < 3; b++)
					result[(size_t)(c+1)*3+(size_t)b] |= sh[b];
				shift_down(&smeared[(size_t)c*3], sh, 1);
				for (int b = 0; b < 3; b++)
					result[(size_t)(c+1)*3+(size_t)b] |= sh[b];
			}
			// XOR at +1 column (VV:D1=0: mask pin 1 and pin 24)
			for (int c = 0; c < sw; c++) {
				uint8_t x0 = (uint8_t)(smeared[(size_t)c*3+0] & 0xFE);
				uint8_t x1 = smeared[(size_t)c*3+1];
				uint8_t x2 = (uint8_t)(smeared[(size_t)c*3+2] & 0x7F);
				result[(size_t)(c+1)*3+0] ^= x0;
				result[(size_t)(c+1)*3+1] ^= x1;
				result[(size_t)(c+1)*3+2] ^= x2;
			}
			buf = std::move(result);
			w = nw;
		} else {
			// Effect #9 ($444A): shadow
			// smear (+2) + $4664(B=5) (+10) = w + 12
			int nw = w + 12;
			// Start with copy of smeared data in a wider buffer
			std::vector<uint8_t> result((size_t)nw * 3, 0);
			std::memcpy(result.data(), smeared.data(), (size_t)sw * 3);
			// Three shift-down-and-OR stages from smeared data
			for (int c = 0; c < sw; c++) {
				uint8_t sh[3];
				// +1 col, down 1
				shift_down(&smeared[(size_t)c*3], sh, 1);
				for (int b = 0; b < 3; b++)
					result[(size_t)(c+1)*3+(size_t)b] |= sh[b];
				// +3 cols, down 1
				shift_down(&smeared[(size_t)c*3], sh, 1);
				if (c + 3 < nw)
					for (int b = 0; b < 3; b++)
						result[(size_t)(c+3)*3+(size_t)b] |= sh[b];
				// +5 cols, down 2
				shift_down(&smeared[(size_t)c*3], sh, 2);
				if (c + 5 < nw)
					for (int b = 0; b < 3; b++)
						result[(size_t)(c+5)*3+(size_t)b] |= sh[b];
			}
			// XOR at +0 (VV:D1=1: include pin 1, exclude pin 24)
			for (int c = 0; c < sw; c++) {
				result[(size_t)c*3+0] ^= smeared[(size_t)c*3+0];
				result[(size_t)c*3+1] ^= smeared[(size_t)c*3+1];
				result[(size_t)c*3+2] ^=
				    (uint8_t)(smeared[(size_t)c*3+2] & 0x7F);
			}
			buf = std::move(result);
			w = nw;
		}
		src = buf.data();
	}
	// Effect #10: Double-height ($4900) — nibble expansion via $49AD.
	// $49AD expands high nibble: bit7→$C0, bit6→$30, bit5→$0C, bit4→$03.
	// Firmware renders 3 slices at different paper positions:
	//   Iter 0: slice $01 (src pins 1-10 → 20 output pins), advance 20
	//   Iter 1: slice $02 (src pins 11-22 → 24 output pins), advance 6
	//   Iter 2: slice $04 (src pins 23-24 → 4 output pins at pins 19-22)
	// We produce all 3 slices; the caller renders at offsets 0, 20, 26.
	if (st.double_high && w > 0) {
		auto expand = [](uint8_t a) -> uint8_t {
			uint8_t r = 0;
			if (a & 0x80) r |= 0xC0;
			if (a & 0x40) r |= 0x30;
			if (a & 0x20) r |= 0x0C;
			if (a & 0x10) r |= 0x03;
			return r;
		};
		auto revbyte = [](uint8_t v) -> uint8_t {
			v = (uint8_t)(((v & 0xF0) >> 4) | ((v & 0x0F) << 4));
			v = (uint8_t)(((v & 0xCC) >> 2) | ((v & 0x33) << 2));
			v = (uint8_t)(((v & 0xAA) >> 1) | ((v & 0x55) << 1));
			return v;
		};
		// Save source before reallocating buf (src may alias buf)
		std::vector<uint8_t> dh_src(src, src + (size_t)w * 3);
		// 3 slices × w columns × 3 bytes
		buf.assign((size_t)w * 3 * 3, 0);
		uint8_t *s0out = buf.data();
		uint8_t *s1out = buf.data() + (size_t)w * 3;
		uint8_t *s2out = buf.data() + (size_t)w * 3 * 2;
		for (int c = 0; c < w; c++) {
			uint8_t b0 = revbyte(dh_src[(size_t)c * 3 + 0]);
			uint8_t b1 = revbyte(dh_src[(size_t)c * 3 + 1]);
			uint8_t b2 = revbyte(dh_src[(size_t)c * 3 + 2]);
			// Slice $01: source pins 1-10 → output pins 1-20
			s0out[(size_t)c*3+0] = revbyte(expand(b0));
			s0out[(size_t)c*3+1] = revbyte(expand((uint8_t)(b0 << 4)));
			s0out[(size_t)c*3+2] = revbyte(expand((uint8_t)(b1 & 0xC0)));
			// Slice $02: source pins 11-22 → output pins 1-24
			s1out[(size_t)c*3+0] = revbyte(expand((uint8_t)((b1 << 2) & 0xF0)));
			uint8_t m1 = (uint8_t)(((b1 << 5) & 0xC0) | ((b2 >> 2) & 0x30));
			s1out[(size_t)c*3+1] = revbyte(expand(m1));
			s1out[(size_t)c*3+2] = revbyte(expand((uint8_t)((b2 << 2) & 0xF0)));
			// Slice $04: source pins 23-24 → output pins 19-22
			s2out[(size_t)c*3+2] = revbyte(expand((uint8_t)((b2 << 5) & 0x60)));
		}
		src = s0out;
		dh_second_slice = s1out;
		dh_third_slice = s2out;
	}
	(void)sc;
}

void ImpactDot24::render_char(PageBitmap &page, const PrinterState &st,
                               const PrinterProfile &prof, uint8_t ch)
{
	if (prof.model == PrinterModel::EpsonLQ500) {
		float hres = st.lq500_lq_mode
		    ? static_cast<float>(prof.lq_hres)
		    : static_cast<float>(prof.draft_hres);
		float ndw = 1.0f / hres;

		const uint8_t *data = nullptr;
		int w = 0;
		int sc = 0;
		int cell_total = 0;  // start + width + advance for underline

		bool script_rom_glyph = false;

		// User-defined 24-pin characters take priority
		if (st.use_user_chars && st.user_char_24_defined[ch]) {
			data = st.user_char_24_glyph[ch];
			w = st.user_char_24_d1[ch];
			sc = st.user_char_24_d0[ch];
			cell_total = sc + w;
		} else {
			// VV:A6 bit 4 = VV:23 bit 4 (super/subscript active).
			// SI/DC2 condensed (VV:22 bit 5) does NOT set this bit —
			// condensed operates purely through effect #2 ($49C5).
			bool script = st.superscript || st.subscript;
			int idx = lq500_font_index(st.lq500_family, st.lq500_lq_mode,
			                            st.pitch_cpi == 12, st.proportional,
			                            script);
			auto info = get_lq500_glyph(idx, ch);
			data = info.data;
			w = info.width;
			sc = info.start;
			cell_total = info.start + info.width + info.advance;
			lq500_apply_script_metrics(st, ch, w, cell_total, sc);
			script_rom_glyph = script;
		}
		std::vector<uint8_t> script_align_buf;
		if (script_rom_glyph && data && w > 0 && st.subscript) {
			script_align_buf.assign(static_cast<size_t>(w) * 3, 0);
			for (int col = 0; col < w; col++) {
				script_align_buf[static_cast<size_t>(col) * 3 + 1] = data[static_cast<size_t>(col) * 3 + 0];
				script_align_buf[static_cast<size_t>(col) * 3 + 2] = data[static_cast<size_t>(col) * 3 + 1];
			}
			data = script_align_buf.data();
		}
		if (!data || w <= 0) {
			if (!st.underline) return;
			w = 0;
		}

		// Apply print effect pipeline (column data transformations)
		std::vector<uint8_t> effect_buf;
		const uint8_t *dh_slice1 = nullptr;
		const uint8_t *dh_slice2 = nullptr;
		if (st.bold || st.italic || st.lq500_char_style || st.condensed
		    || st.double_high || st.expanded || st.expanded_line) {
			lq500_apply_effects(st, data, w, sc, effect_buf,
			                    dh_slice1, dh_slice2, ch);
			if (!effect_buf.empty())
				data = effect_buf.data();
		}

		auto render_lq500_slice = [&](const PrinterState &base,
		                              const uint8_t *slice) {
			render_glyph_24pin(*this, page, base, prof, slice, w, sc, ndw);
			if (base.lq500_lq_mode)
				render_glyph_24pin_lq_interleave(*this, page, base, prof,
				                                 slice, w, sc, ndw);
		};

		auto render_lq500_double_strike_slice =
		    [&](const PrinterState &base, const uint8_t *slice) {
			render_lq500_slice(base, slice);
			// LQ-500 setup records repeat double-strike with advance index 0
			// (normal $6803, double-height $6816), so this is a same-position
			// overstrike rather than a paper-feed step.
			if (base.double_strike)
				render_lq500_slice(base, slice);
		};

		// Render glyph (top slice, or full glyph if not double-height)
		if (w > 0)
			render_lq500_double_strike_slice(st, data);

		// Double-height: firmware renders 3 slices via VV:89 tiling:
		//   Iter 0: slice $01 at position 0, advance 20/180"
		//   Iter 1: slice $02 at position 20/180", advance 6/180"
		//   Iter 2: slice $04 at position 26/180"
		// data already points to slice 0 (top).
		if (dh_slice1 && w > 0) {
			float dot_h = 1.0f / 180.0f;
			// Slice 1: pins 11-22 → 24 output pins at Y+20/180"
			PrinterState st1 = st;
			st1.y_pos += 20.0f * dot_h;
			render_lq500_double_strike_slice(st1, dh_slice1);
		}
		if (dh_slice2 && w > 0) {
			float dot_h = 1.0f / 180.0f;
			// Slice 2: pins 23-24 → pins 19-22 at Y+26/180"
			PrinterState st2dh = st;
			st2dh.y_pos += 26.0f * dot_h;
			render_lq500_double_strike_slice(st2dh, dh_slice2);
		}

		// Underline: rendered per-character after glyph write,
		// matching firmware $1EBC-$1F22. Fires for all characters
		// including spaces.
		// Confirmed pin mapping: D7..D0 → H17..H24 for byte 2.
		// VV:B2=$01 → D0 → H24 → pin 23 (0-indexed after reversal).
		// VV:B2=$04 → D2 → H22 → pin 21 (double-height case).
		// Alternating dots at native dot pitch, two-pass fills gaps.
		if (st.underline) {
			float dot_h_in = 1.0f / 180.0f;
			// Double-height underline: VV:B2=$04 (pin 21) rendered
			// on the last slice at Y+26/180".  Normal: VV:B2=$01
			// (pin 23) at the current head position.
			int ul_pin = st.double_high ? 21 : 23;
			float ul_y_base = st.double_high
			    ? st.y_pos + 26.0f * dot_h_in
			    : st.y_pos;
			float ul_y = ul_y_base + (float)ul_pin * dot_h_in
			             - 24.0f * dot_h_in;
			// Underline spans the full character cell from font metrics.
			// Effects that change metrics (expanded, condensed) are
			// already reflected in cell_total via the effect pipeline,
			// but expanded doubles metrics at the ESC/P level too.
			int ul_total = cell_total;
			if (st.expanded || st.expanded_line) ul_total *= 2;
			int total_cols = std::max(1, ul_total);
			float ink = prof.dot_intensity;
			if (st.lq500_lq_mode) {
				// LQ: alternating dots, two-pass fills gaps
				for (int i = 0; i < total_cols; i += 2) {
					stamp_pin(page, st.x_pos + (float)i * ndw,
					          ul_y, prof.render_dpi, prof.dot_radius_mm,
					          prof.jitter_mm, ink, prof.dot_sharpness,
					          prof.overprint_gamma, prof.radius_variance,
					          prof.intensity_variance, prof.dot_edge_softness,
					          prof.dot_x_scale, prof.dot_y_scale);
				}
				for (int i = 0; i < total_cols; i += 2) {
					stamp_pin(page, st.x_pos + ndw + (float)i * ndw,
					          ul_y, prof.render_dpi, prof.dot_radius_mm,
					          prof.jitter_mm, ink, prof.dot_sharpness,
					          prof.overprint_gamma, prof.radius_variance,
					          prof.intensity_variance, prof.dot_edge_softness,
					          prof.dot_x_scale, prof.dot_y_scale);
				}
			} else {
				// Draft: every column at native pitch
				for (int i = 0; i < total_cols; i++) {
					stamp_pin(page, st.x_pos + (float)i * ndw,
					          ul_y, prof.render_dpi, prof.dot_radius_mm,
					          prof.jitter_mm, ink, prof.dot_sharpness,
					          prof.overprint_gamma, prof.radius_variance,
					          prof.intensity_variance, prof.dot_edge_softness,
					          prof.dot_x_scale, prof.dot_y_scale);
				}
			}
		}
		return;
	}
	// IBM X24E and other 24-pin models: use existing 24-pin font
	const uint8_t *glyph = get_24pin_glyph(ch);
	if (glyph)
		render_glyph_24pin(*this, page, st, prof, glyph, 12);
}

static constexpr uint64_t BJ10E_48_DOT_MASK = (1ULL << 48) - 1ULL;
static constexpr int BJ10E_FRAC_STEPS = 64;
static constexpr int BJ10E_MAX_GLYPH_COLS = 96;

struct Bj10eInkSample {
	int dx;
	int dy;
	float transmit;
};

struct Bj10eInkKernel {
	int dpi = 0;
	int qx = 0;
	int qy = 0;
	float radius_mm = 0.0f;
	float intensity = 0.0f;
	std::vector<Bj10eInkSample> samples;
};

static void quantize_frac(float v, int &base, int &q)
{
	base = (int)std::floor(v);
	float frac = v - (float)base;
	q = (int)std::lround(frac * (float)BJ10E_FRAC_STEPS);
	if (q >= BJ10E_FRAC_STEPS) {
		q = 0;
		base++;
	}
}

static const Bj10eInkKernel &bj10e_ink_kernel(const PrinterProfile &prof,
                                              int qx, int qy)
{
	static std::vector<Bj10eInkKernel> cache;

	for (const Bj10eInkKernel &k : cache) {
		if (k.dpi == prof.render_dpi && k.qx == qx && k.qy == qy &&
		    k.radius_mm == prof.dot_radius_mm && k.intensity == prof.dot_intensity)
			return k;
	}

	Bj10eInkKernel kernel;
	kernel.dpi = prof.render_dpi;
	kernel.qx = qx;
	kernel.qy = qy;
	kernel.radius_mm = prof.dot_radius_mm;
	kernel.intensity = prof.dot_intensity;

	float frac_x = (float)qx / (float)BJ10E_FRAC_STEPS;
	float frac_y = (float)qy / (float)BJ10E_FRAC_STEPS;
	float sigma = prof.dot_radius_mm / 25.4f * (float)prof.render_dpi;
	float density = prof.dot_intensity;
	float core_sx = sigma;
	float core_sy = sigma * 1.10f;
	float halo_sx = sigma * 1.85f;
	float halo_sy = sigma * 2.00f;
	float max_x = std::max(core_sx, halo_sx) * 3.0f;
	float max_y = std::max(core_sy, halo_sy) * 3.0f;
	float core_inv_2sx2 = 1.0f / (2.0f * core_sx * core_sx);
	float core_inv_2sy2 = 1.0f / (2.0f * core_sy * core_sy);
	float halo_inv_2sx2 = 1.0f / (2.0f * halo_sx * halo_sx);
	float halo_inv_2sy2 = 1.0f / (2.0f * halo_sy * halo_sy);

	int x0 = (int)std::floor(frac_x - max_x);
	int x1 = (int)std::ceil(frac_x + max_x);
	int y0 = (int)std::floor(frac_y - max_y);
	int y1 = (int)std::ceil(frac_y + max_y);
	for (int dy = y0; dy <= y1; dy++) {
		float fy = (float)dy - frac_y;
		for (int dx = x0; dx <= x1; dx++) {
			float fx = (float)dx - frac_x;
			float effect = 0.0f;
			float core = std::exp(-(fx * fx * core_inv_2sx2 +
			                        fy * fy * core_inv_2sy2));
			if (core >= 0.002f)
				effect += density * core;
			float halo = std::exp(-(fx * fx * halo_inv_2sx2 +
			                        fy * fy * halo_inv_2sy2));
			if (halo >= 0.002f)
				effect += density * 0.05f * halo;
			if (effect > 0.0f)
				kernel.samples.push_back({dx, dy, std::exp(-effect)});
		}
	}

	cache.push_back(std::move(kernel));
	return cache.back();
}

static void stamp_bj10e_drop(PageBitmap &page, const Bj10eInkKernel &kernel,
                             int base_x, int base_y)
{
	uint8_t *buf = page.data();
	int w = page.width();
	int h = page.height();
	int stride = page.stride();
	int channels = page.channels();
	for (const Bj10eInkSample &s : kernel.samples) {
		int x = base_x + s.dx;
		int y = base_y + s.dy;
		if (x < 0 || x >= w || y < 0 || y >= h)
			continue;
		uint8_t *p = buf + (size_t)y * (size_t)stride +
		             (size_t)x * (size_t)channels;
		for (int c = 0; c < channels; c++) {
			p[c] = (uint8_t)std::max(0.0f, std::min(255.0f,
			                     (float)p[c] * s.transmit));
		}
	}
}

void stamp_bj10e_dot(PageBitmap &page, const PrinterProfile &prof,
                     int xdot, int ydot)
{
	const Bj10eInkKernel &kernel = bj10e_ink_kernel(prof, 0, 0);
	int x = (int)std::lround((float)xdot * (float)prof.render_dpi / 360.0f);
	int y = (int)std::lround((float)ydot * (float)prof.render_dpi / 360.0f);
	stamp_bj10e_drop(page, kernel, x, y);
}

static void bj10e_apply_emphasis(uint64_t *cols, int count)
{
	if (count <= 1)
		return;
	uint64_t prev = cols[0];
	for (int i = 1; i < count; i++) {
		uint64_t src = cols[i];
		cols[i] |= prev;
		prev = src;
	}
}

static void bj10e_apply_double_strike(uint64_t *cols, int count)
{
	for (int i = 0; i < count; i++)
		cols[i] = (cols[i] | (cols[i] << 1)) & BJ10E_48_DOT_MASK;
}

static void bj10e_apply_double_width(uint64_t *cols, int &count)
{
	int out_count = std::min(count * 2, BJ10E_MAX_GLYPH_COLS);
	for (int src = count - 1, dst = out_count - 1; src >= 0 && dst >= 0; src--) {
		cols[dst--] = cols[src];
		if (dst >= 0)
			cols[dst--] = cols[src];
	}
	count = out_count;
}

static void bj10e_apply_underline(uint64_t *cols, int count)
{
	constexpr uint64_t underline = (1ULL << 45) | (1ULL << 46) | (1ULL << 47);
	for (int i = 0; i < count; i++)
		cols[i] |= underline;
}

static void bj10e_apply_overline(uint64_t *cols, int count)
{
	constexpr uint64_t overline = (1ULL << 0) | (1ULL << 1) | (1ULL << 2);
	for (int i = 0; i < count; i++)
		cols[i] |= overline;
}

static uint64_t bj10e_mask_column(const uint8_t *table, int table_len,
                                  int active_bytes, int col)
{
	uint64_t mask = 0;
	int index = (col * 8) % table_len;
	for (int byte = 0; byte < active_bytes; byte++) {
		uint8_t value = table[index % table_len];
		for (int bit = 0; bit < 8; bit++) {
			if (value & (0x80 >> bit))
				mask |= 1ULL << (byte * 8 + bit);
		}
		index++;
	}
	return mask;
}

static void bj10e_apply_presentation_highlight(uint64_t *cols, int count)
{
	static constexpr uint8_t or_mask[] = {
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60,
		0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x06, 0x00, 0x06, 0x00, 0x06, 0x00, 0x06, 0x00,
		0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
		0x00, 0x06, 0x00, 0x06, 0x00, 0x06, 0x00, 0x06,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	};
	for (int i = 0; i < count; i++)
		cols[i] |= bj10e_mask_column(or_mask, (int)sizeof(or_mask), 6, i);
}

static void bj10e_apply_script(uint64_t *cols, int count, bool subscript)
{
	for (int i = 0; i < count; i++) {
		uint64_t out = 0;
		for (int src_row = 0; src_row < 48; src_row += 2) {
			if (!(cols[i] & (1ULL << src_row)))
				continue;
			int dst_row = src_row / 2 + (subscript ? 20 : 0);
			if (dst_row < 48)
				out |= 1ULL << dst_row;
		}
		cols[i] = out;
	}
}

static void stamp_bj10e_column(PageBitmap &page,
                               const Bj10eInkKernel &kernel, uint64_t column,
                               int base_x, int base_y, int row_step_px,
                               bool double_high, bool economy, int xdot)
{
	for (int ydot = 0; ydot < 48; ydot++) {
		if (!(column & (1ULL << ydot))) continue;
		int row = double_high ? ydot * 2 : ydot;
		if (!economy || ((xdot + row) & 1) == 0)
			stamp_bj10e_drop(page, kernel, base_x,
			                 base_y + row * row_step_px);
		if (double_high) {
			int lower_row = row + 1;
			if (!economy || ((xdot + lower_row) & 1) == 0)
				stamp_bj10e_drop(page, kernel, base_x,
				                 base_y + lower_row * row_step_px);
		}
	}
}

std::vector<uint64_t> build_bj10e_glyph_columns(const PrinterState &st,
                                                uint8_t ch)
{
	bool secondary = !st.proportional && !st.condensed && st.pitch_cpi >= 12.0f;
	Bj10eGlyph glyph = get_bj10e_glyph(ch, st.codepage_850, secondary,
	                                   st.proportional);
	if (st.bj10e_use_downloaded_font && st.bj10e_user_font &&
	    st.bj10e_user_font->defined[ch]) {
		glyph = Bj10eGlyph{
			st.bj10e_user_font->glyph[ch],
			st.bj10e_user_font->width[ch]
		};
	}
	if (!glyph.cols || glyph.width == 0)
		return {};

	uint64_t cols[BJ10E_MAX_GLYPH_COLS];
	int col_count = 0;
	int advance_dots = 36;

	if (st.condensed && !st.proportional && glyph.width >= 36) {
		for (int xdot = 0; xdot < 18; xdot++)
			cols[col_count++] = glyph.cols[xdot * 2] | glyph.cols[xdot * 2 + 1];
		advance_dots = 21;
	} else {
		for (int xdot = 0; xdot < glyph.width && col_count < BJ10E_MAX_GLYPH_COLS; xdot++)
			cols[col_count++] = glyph.cols[xdot];
		advance_dots = glyph.width;
	}

	while (col_count < advance_dots && col_count < BJ10E_MAX_GLYPH_COLS)
		cols[col_count++] = 0;

	if (st.superscript || st.subscript)
		bj10e_apply_script(cols, col_count, st.subscript);
	if (st.bold)
		bj10e_apply_emphasis(cols, col_count);
	if (st.underline)
		bj10e_apply_underline(cols, col_count);
	if (st.overline)
		bj10e_apply_overline(cols, col_count);
	if (st.presentation_highlight)
		bj10e_apply_presentation_highlight(cols, col_count);
	if (st.reverse_image) {
		for (int i = 0; i < col_count; i++)
			cols[i] = (~cols[i]) & BJ10E_48_DOT_MASK;
	}
	if (st.double_strike)
		bj10e_apply_double_strike(cols, col_count);
	if (st.expanded || st.expanded_line)
		bj10e_apply_double_width(cols, col_count);

	return std::vector<uint64_t>(cols, cols + col_count);
}

static void render_bj10e_glyph(PageBitmap &page,
                               const PrinterState &st,
                               const PrinterProfile &prof, uint8_t ch)
{
	std::vector<uint64_t> cols = build_bj10e_glyph_columns(st, ch);
	if (cols.empty())
		return;

	float base_y = st.y_pos - 48.0f / 360.0f;
	float bidi_offset = st.line_dir_ltr ? 0.0f : (0.20f / 360.0f);
	int absolute_xdot = (int)std::lround((st.x_pos + bidi_offset) * 360.0f);

	int base_x = 0;
	int base_y_px = 0;
	int qx = 0;
	int qy = 0;
	quantize_frac((st.x_pos + bidi_offset) * (float)prof.render_dpi, base_x, qx);
	quantize_frac(base_y * (float)prof.render_dpi, base_y_px, qy);
	const Bj10eInkKernel &kernel = bj10e_ink_kernel(prof, qx, qy);
	int row_step_px = std::max(1, (int)std::lround((float)prof.render_dpi / 360.0f));
	bool economy = st.font_mode == 1;

	for (size_t xdot = 0; xdot < cols.size(); xdot++)
		stamp_bj10e_column(page, kernel, cols[xdot], base_x + (int)xdot,
		                    base_y_px, row_step_px, st.double_high, economy,
		                    absolute_xdot + (int)xdot);
}

void InkjetDot::render_char(PageBitmap &page, const PrinterState &st,
                             const PrinterProfile &prof, uint8_t ch)
{
	render_bj10e_glyph(page, st, prof, ch);
}

void TonerDot::render_char(PageBitmap &page, const PrinterState &st,
                            const PrinterProfile &prof, uint8_t ch)
{
	const uint16_t *glyph = get_9pin_glyph(ch);
	if (!glyph) return;
	render_glyph_9pin(*this, page, st, prof, glyph, 12, nullptr);
}

std::unique_ptr<DotRenderer> create_dot_renderer(const PrinterProfile &prof)
{
	switch (prof.tech) {
	case DotTech::Impact9:  return std::make_unique<ImpactDot9>();
	case DotTech::Impact24: return std::make_unique<ImpactDot24>();
	case DotTech::Inkjet:   return std::make_unique<InkjetDot>();
	case DotTech::Toner:    return std::make_unique<TonerDot>();
	}
	return std::make_unique<ImpactDot9>();
}
