// license:BSD-3-Clause
// copyright-holders:Stephen Hurd
#include "dotrender.h"
#include <algorithm>
#include <cmath>
#include <vector>

void DotRenderer::stamp_pin(PageBitmap &page, float x_in, float y_in,
                            int dpi, float radius_mm, float jitter_mm,
                            float intensity, float sharpness)
{
	auto stamp_component = [&](float red, float green, float blue) {
		std::normal_distribution<float> jdist(0.0f, jitter_mm);
		float jx = jdist(rng_);
		float jy = jdist(rng_);

		float px = (x_in + jx / 25.4f) * (float)dpi;
		float py = (y_in + jy / 25.4f) * (float)dpi;
		std::normal_distribution<float> rdist(radius_mm, radius_mm * 0.075f);
		float r_px = std::max(radius_mm * 0.7f, rdist(rng_)) / 25.4f * (float)dpi;

		std::normal_distribution<float> idist(intensity, intensity * 0.075f);
		float ink = std::max(0.3f, std::min(1.0f, idist(rng_)));

		page.stamp_dot_rgb(px, py, r_px, ink, sharpness,
		                   red, green, blue);
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
			             radius_mm, jitter_mm, ink, sharp);
			if (expanded)
				dr.stamp_pin(page, x + expand_dup, y + y_off, prof.render_dpi,
				             radius_mm, jitter_mm, ink, sharp);
			if (st.bold) {
				dr.stamp_pin(page, x + bold_off, y + y_off,
				             prof.render_dpi, radius_mm, jitter_mm, ink, sharp);
				if (expanded)
					dr.stamp_pin(page, x + expand_dup + bold_off, y + y_off,
					             prof.render_dpi, radius_mm, jitter_mm, ink, sharp);
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
			             prof.jitter_mm, ink, sharp);
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
		// Draft (font_mode=1) does NOT support bold, expanded, half-height,
		// super/subscript, or proportional — these force correspondence.
		// NLQ (font_mode=2) does NOT support half-height or super/subscript
		// — these force correspondence.
		int eff_mode = st.font_mode;
		bool expanded = st.expanded || st.expanded_line;
		bool script = st.superscript || st.subscript;
		if (eff_mode == 1) {
			if (st.bold || expanded || st.half_height || script || st.proportional)
				eff_mode = 0;
		} else if (eff_mode == 2) {
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
		} else if (eff_mode == 2) {
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
