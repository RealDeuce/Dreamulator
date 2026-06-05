// license:BSD-3-Clause
// copyright-holders:Stephen Hurd
#include "dotrender.h"
#include <cmath>

void DotRenderer::stamp_pin(PageBitmap &page, float x_in, float y_in,
                            int dpi, float radius_mm, float jitter_mm,
                            float intensity, float sharpness)
{
	std::normal_distribution<float> jdist(0.0f, jitter_mm);
	float jx = jdist(rng_);
	float jy = jdist(rng_);

	float px = (x_in + jx / 25.4f) * (float)dpi;
	float py = (y_in + jy / 25.4f) * (float)dpi;
	std::normal_distribution<float> rdist(radius_mm, radius_mm * 0.075f);
	float r_px = std::max(radius_mm * 0.7f, rdist(rng_)) / 25.4f * (float)dpi;

	std::normal_distribution<float> idist(intensity, intensity * 0.075f);
	float ink = std::max(0.3f, std::min(1.0f, idist(rng_)));

	page.stamp_dot(px, py, r_px, ink, sharpness);
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
	float expand_dup = expanded ? (dot_w_in * 0.5f) : 0.0f;
	float dot_pitch = (prof.model == PrinterModel::ImageWriter)
	    ? (1.0f / 80.0f) : (1.0f / 60.0f);
	float bold_off = dot_pitch * 0.5f;

	float ink = prof.dot_intensity;
	float sharp = prof.dot_sharpness;

	constexpr float vib_decay = 0.85f;
	constexpr float vib_kick = 0.25f;
	constexpr float vib_floor = 0.33f;
	float bidi_offset = st.line_dir_ltr ? 0.0f : (1.0f / 120.0f);

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
				dr.stamp_pin(page, x, y, prof.render_dpi,
				             prof.dot_radius_mm, jit, ink, sharp);
				if (expanded)
					dr.stamp_pin(page, x + expand_dup, y, prof.render_dpi,
					             prof.dot_radius_mm, jit, ink, sharp);
				if (st.bold) {
					dr.stamp_pin(page, x + bold_off, y, prof.render_dpi,
					             prof.dot_radius_mm, jit, ink, sharp);
					if (expanded)
						dr.stamp_pin(page, x + expand_dup + bold_off, y,
						             prof.render_dpi, prof.dot_radius_mm, jit, ink, sharp);
				}
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
				dr.stamp_pin(page, x, y, prof.render_dpi,
				             prof.dot_radius_mm, jit, ink, sharp);
				if (expanded)
					dr.stamp_pin(page, x + expand_dup, y, prof.render_dpi,
					             prof.dot_radius_mm, jit, ink, sharp);
				if (st.bold) {
					dr.stamp_pin(page, x + bold_off, y, prof.render_dpi,
					             prof.dot_radius_mm, jit, ink, sharp);
					if (expanded)
						dr.stamp_pin(page, x + expand_dup + bold_off, y,
						             prof.render_dpi, prof.dot_radius_mm, jit, ink, sharp);
				}
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
				dr.stamp_pin(page, x, y, prof.render_dpi,
				             prof.dot_radius_mm, jit, ink, sharp);
				if (expanded)
					dr.stamp_pin(page, x + expand_dup, y, prof.render_dpi,
					             prof.dot_radius_mm, jit, ink, sharp);
				if (st.bold) {
					dr.stamp_pin(page, x + bold_off, y, prof.render_dpi,
					             prof.dot_radius_mm, jit, ink, sharp);
					if (expanded)
						dr.stamp_pin(page, x + expand_dup + bold_off, y,
						             prof.render_dpi, prof.dot_radius_mm, jit, ink, sharp);
				}
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
				dr.stamp_pin(page, x, y, prof.render_dpi,
				             prof.dot_radius_mm, jit, ink, sharp);
				if (expanded)
					dr.stamp_pin(page, x + expand_dup, y, prof.render_dpi,
					             prof.dot_radius_mm, jit, ink, sharp);
				if (st.bold) {
					dr.stamp_pin(page, x + bold_off, y, prof.render_dpi,
					             prof.dot_radius_mm, jit, ink, sharp);
					if (expanded)
						dr.stamp_pin(page, x + expand_dup + bold_off, y,
						             prof.render_dpi, prof.dot_radius_mm, jit, ink, sharp);
				}
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

				dr.stamp_pin(page, x, y, prof.render_dpi,
				             prof.dot_radius_mm, jit, ink, sharp);
				if (expanded)
					dr.stamp_pin(page, x + expand_dup, y, prof.render_dpi,
					             prof.dot_radius_mm, jit, ink, sharp);

				if (st.bold) {
					dr.stamp_pin(page, x + bold_off, y,
					             prof.render_dpi, prof.dot_radius_mm,
					             jit, ink, sharp);
					if (expanded)
						dr.stamp_pin(page, x + expand_dup + bold_off, y,
						             prof.render_dpi, prof.dot_radius_mm,
						             jit, ink, sharp);
				}
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
			             prof.jitter_mm, ink, sharp);
		}
	}
}

void ImpactDot9::render_char(PageBitmap &page, const PrinterState &st,
                              const PrinterProfile &prof, uint8_t ch)
{
	if (prof.model == PrinterModel::EpsonFX) {
		const uint16_t *glyph = get_fx80_roman_glyph(ch);
		if (!glyph) return;
		render_glyph_9pin(*this, page, st, prof, glyph, 12, pin_vib_);
	} else if (prof.model == PrinterModel::ImageWriter) {
		const uint16_t *glyph = nullptr;
		int w = 8;
		if (st.proportional) {
			glyph = get_iw2_nlq_glyph(ch);
			if (glyph)
				w = get_iw2_nlq_width(ch);
		}
		if (!glyph) {
			glyph = get_iw2_draft_glyph(ch);
			w = 8;
		}
		if (!glyph) return;
		render_glyph_9pin(*this, page, st, prof, glyph, w, pin_vib_);
	} else {
		const uint16_t *glyph = get_9pin_glyph(ch);
		if (!glyph) return;
		render_glyph_9pin(*this, page, st, prof, glyph, 12, pin_vib_);
	}
}

static void render_glyph_24pin(DotRenderer &dr, PageBitmap &page,
                                const PrinterState &st, const PrinterProfile &prof,
                                const uint8_t *glyph, int glyph_w)
{
	float cw_in = 1.0f / (float)st.pitch_cpi;
	if (st.expanded || st.expanded_line) cw_in *= 2.0f;
	if (st.condensed) cw_in *= 0.6f;

	float dot_h_in = 1.0f / 180.0f;
	float dot_w_in = cw_in / (float)glyph_w;

	float y_scale = 1.0f;
	float y_off = 0.0f;
	if (st.superscript) { y_scale = 0.67f; y_off = 0.0f; }
	if (st.subscript) { y_scale = 0.67f; y_off = dot_h_in * 8.0f; }

	float ink = prof.dot_intensity;
	float sharp = prof.dot_sharpness;

	for (int col = 0; col < glyph_w; col++) {
		uint32_t column = (uint32_t)glyph[col * 3] |
		                  ((uint32_t)glyph[col * 3 + 1] << 8) |
		                  ((uint32_t)glyph[col * 3 + 2] << 16);
		for (int pin = 0; pin < 24; pin++) {
			if (!(column & (1U << pin))) continue;

			float x = st.x_pos + (float)col * dot_w_in;
			float y = st.y_pos + ((float)pin * dot_h_in * y_scale) + y_off
			          - (24.0f * dot_h_in);

			dr.stamp_pin(page, x, y, prof.render_dpi,
			             prof.dot_radius_mm, prof.jitter_mm, ink, sharp);

			if (st.bold) {
				dr.stamp_pin(page, x + dot_w_in * 0.25f, y,
				             prof.render_dpi, prof.dot_radius_mm,
				             prof.jitter_mm, ink * 0.8f, sharp);
			}
		}
	}

	if (st.underline) {
		float ul_y = st.y_pos + dot_h_in * 1.5f;
		int dots = (int)(cw_in / (1.0f / 360.0f));
		float step = cw_in / (float)dots;
		for (int i = 0; i < dots; i++) {
			dr.stamp_pin(page, st.x_pos + (float)i * step, ul_y,
			             prof.render_dpi, prof.dot_radius_mm,
			             prof.jitter_mm, ink, sharp);
		}
	}
}

void ImpactDot24::render_char(PageBitmap &page, const PrinterState &st,
                               const PrinterProfile &prof, uint8_t ch)
{
	const uint16_t *glyph = get_9pin_glyph(ch);
	if (!glyph) return;
	render_glyph_9pin(*this, page, st, prof, glyph, 12, pin_vib_);
}

void InkjetDot::render_char(PageBitmap &page, const PrinterState &st,
                             const PrinterProfile &prof, uint8_t ch)
{
	const uint16_t *glyph = get_9pin_glyph(ch);
	if (!glyph) return;
	render_glyph_9pin(*this, page, st, prof, glyph, 12, nullptr);
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
