// license:BSD-3-Clause
// copyright-holders:Stephen Hurd
#include "pagebuf.h"
#include <algorithm>
#include <cmath>
#include <cstring>

PageBitmap::PageBitmap(int width_px, int height_px)
	: w_(width_px), h_(height_px),
	  buf_(std::make_unique<uint8_t[]>((size_t)w_ * (size_t)h_))
{
	clear();
}

void PageBitmap::clear(uint8_t val)
{
	std::memset(buf_.get(), val, (size_t)w_ * (size_t)h_);
}

uint8_t PageBitmap::pixel(int x, int y) const
{
	if (x < 0 || x >= w_ || y < 0 || y >= h_) return 255;
	return buf_[(size_t)y * (size_t)w_ + (size_t)x];
}

void PageBitmap::set_pixel(int x, int y, uint8_t val)
{
	if (x < 0 || x >= w_ || y < 0 || y >= h_) return;
	buf_[(size_t)y * (size_t)w_ + (size_t)x] = val;
}

void PageBitmap::stamp_dot(float cx, float cy, float radius, float intensity,
                           float sharpness)
{
	int x0 = (int)std::floor(cx - radius);
	int y0 = (int)std::floor(cy - radius);
	int x1 = (int)std::ceil(cx + radius);
	int y1 = (int)std::ceil(cy + radius);

	x0 = std::max(0, x0);
	y0 = std::max(0, y0);
	x1 = std::min(w_ - 1, x1);
	y1 = std::min(h_ - 1, y1);

	float r2 = radius * radius;
	for (int y = y0; y <= y1; y++) {
		float dy = (float)y - cy;
		for (int x = x0; x <= x1; x++) {
			float dx = (float)x - cx;
			float d2 = dx * dx + dy * dy;
			if (d2 > r2) continue;

			float t = 1.0f - d2 / r2;
			float ink = intensity * std::pow(t, sharpness);

			size_t idx = (size_t)y * (size_t)w_ + (size_t)x;
			float cur = buf_[idx] / 255.0f;
			float result = cur * (1.0f - ink);
			buf_[idx] = (uint8_t)std::max(0.0f, std::min(255.0f, result * 255.0f));
		}
	}
}

void PageBitmap::stamp_ink_dot(float cx, float cy, float sigma_x, float sigma_y,
                               float density)
{
	if (sigma_x <= 0.0f || sigma_y <= 0.0f || density <= 0.0f)
		return;

	int x0 = (int)std::floor(cx - sigma_x * 3.0f);
	int y0 = (int)std::floor(cy - sigma_y * 3.0f);
	int x1 = (int)std::ceil(cx + sigma_x * 3.0f);
	int y1 = (int)std::ceil(cy + sigma_y * 3.0f);

	x0 = std::max(0, x0);
	y0 = std::max(0, y0);
	x1 = std::min(w_ - 1, x1);
	y1 = std::min(h_ - 1, y1);

	float inv_2sx2 = 1.0f / (2.0f * sigma_x * sigma_x);
	float inv_2sy2 = 1.0f / (2.0f * sigma_y * sigma_y);
	for (int y = y0; y <= y1; y++) {
		float dy = (float)y - cy;
		for (int x = x0; x <= x1; x++) {
			float dx = (float)x - cx;
			float coverage = std::exp(-(dx * dx * inv_2sx2 + dy * dy * inv_2sy2));
			if (coverage < 0.002f) continue;

			size_t idx = (size_t)y * (size_t)w_ + (size_t)x;
			float cur = buf_[idx] / 255.0f;
			float result = cur * std::exp(-density * coverage);
			buf_[idx] = (uint8_t)std::max(0.0f, std::min(255.0f, result * 255.0f));
		}
	}
}

PageBitmap PageBitmap::letter_at_dpi(int dpi)
{
	int w = (int)(8.5f * (float)dpi);
	int h = (int)(11.0f * (float)dpi);
	return PageBitmap(w, h);
}
