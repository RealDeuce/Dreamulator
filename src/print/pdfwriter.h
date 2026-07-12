// license:BSD-3-Clause
// copyright-holders:Stephen Hurd
#ifndef PDFWRITER_H
#define PDFWRITER_H

#include "pagebuf.h"
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

struct TextGlyph {
	float x_in, y_in;
	uint16_t codepoint;
	float width_in;
	float size_pt;
	uint8_t style;
	static constexpr uint8_t BOLD      = 1;
	static constexpr uint8_t UNDERLINE = 2;
	static constexpr uint8_t SUPER     = 4;
	static constexpr uint8_t SUB       = 8;
};

class PdfWriter {
public:
	explicit PdfWriter(const std::string &path);
	~PdfWriter();

	PdfWriter(const PdfWriter &) = delete;
	PdfWriter &operator=(const PdfWriter &) = delete;

	void add_page(const PageBitmap &bmp, int dpi,
	              const std::vector<TextGlyph> &text = {});
	void finish();

private:
	struct ObjPos { int id; long offset; };

	int alloc_obj();
	void begin_obj(int id);
	void end_obj();
	long write_image_stream(const PageBitmap &bmp);

	FILE *fp_ = nullptr;
	int next_id_ = 1;
	std::vector<ObjPos> xref_;

	int catalog_id_ = 0;
	int pages_id_ = 0;
	int font_ids_[4] = {};
	std::vector<int> page_ids_;
};

#endif
