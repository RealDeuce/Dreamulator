// license:BSD-3-Clause
// copyright-holders:Stephen Hurd
#ifndef PAGEBUF_H
#define PAGEBUF_H

#include <cstddef>
#include <cstdint>
#include <memory>

class PageBitmap {
public:
	PageBitmap(int width_px, int height_px);

	int width() const { return w_; }
	int height() const { return h_; }
	int channels() const { return 3; }
	int stride() const { return w_ * channels(); }
	size_t raw_size() const { return (size_t)stride() * (size_t)h_; }
	bool is_grayscale() const;
	const uint8_t *data() const { return buf_.get(); }
	uint8_t *data() { return buf_.get(); }

	void clear(uint8_t val = 255);

	uint8_t pixel(int x, int y) const;
	void set_pixel(int x, int y, uint8_t val);

	void stamp_dot(float cx, float cy, float radius, float intensity,
	               float sharpness = 2.0f, float overprint_gamma = 1.0f);
	void stamp_dot_rgb(float cx, float cy, float radius, float intensity,
	                   float sharpness, float red, float green, float blue,
	                   float overprint_gamma = 1.0f,
	                   float edge_softness = 0.0f,
	                   float x_scale = 1.0f, float y_scale = 1.0f);
	void stamp_ink_dot(float cx, float cy, float sigma_x, float sigma_y,
	                   float density);

	static PageBitmap letter_at_dpi(int dpi);
	static PageBitmap at_dpi(float width_in, float height_in, int dpi);

private:
	int w_, h_;
	std::unique_ptr<uint8_t[]> buf_;
};

#endif
