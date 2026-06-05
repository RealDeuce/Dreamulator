// license:BSD-3-Clause
// copyright-holders:Stephen Hurd
#include "pdfwriter.h"
#include "printer.h"
#include <cstring>
#include <stdexcept>
#include <zlib.h>

PdfWriter::PdfWriter(const std::string &path)
{
	fp_ = fopen(path.c_str(), "wb");
	if (!fp_)
		throw std::runtime_error("cannot open " + path);

	fprintf(fp_, "%%PDF-1.4\n%%%c%c%c%c\n", 0xC0, 0xC1, 0xC2, 0xC3);

	catalog_id_ = alloc_obj();
	pages_id_ = alloc_obj();

	font_id_ = alloc_obj();
	begin_obj(font_id_);
	fprintf(fp_, "<< /Type /Font /Subtype /Type1 /BaseFont /Courier\n");
	fprintf(fp_, "   /Encoding /WinAnsiEncoding >>\n");
	end_obj();
}

PdfWriter::~PdfWriter()
{
	if (fp_) {
		finish();
		fclose(fp_);
	}
}

int PdfWriter::alloc_obj()
{
	return next_id_++;
}

void PdfWriter::begin_obj(int id)
{
	long pos = ftell(fp_);
	xref_.push_back({id, pos});
	fprintf(fp_, "%d 0 obj\n", id);
}

void PdfWriter::end_obj()
{
	fprintf(fp_, "endobj\n");
}

long PdfWriter::write_image_stream(const PageBitmap &bmp)
{
	size_t raw_len = (size_t)bmp.width() * (size_t)bmp.height();
	uLong bound = compressBound((uLong)raw_len);
	std::vector<uint8_t> comp(bound);
	uLong comp_len = bound;
	int rc = compress2(comp.data(), &comp_len, bmp.data(), (uLong)raw_len, 6);
	if (rc != Z_OK)
		throw std::runtime_error("zlib compress failed");

	int img_id = alloc_obj();
	begin_obj(img_id);
	fprintf(fp_, "<< /Type /XObject /Subtype /Image\n");
	fprintf(fp_, "   /Width %d /Height %d\n", bmp.width(), bmp.height());
	fprintf(fp_, "   /ColorSpace /DeviceGray /BitsPerComponent 8\n");
	fprintf(fp_, "   /Filter /FlateDecode /Length %lu >>\n", (unsigned long)comp_len);
	fprintf(fp_, "stream\n");
	fwrite(comp.data(), 1, comp_len, fp_);
	fprintf(fp_, "\nendstream\n");
	end_obj();

	return img_id;
}

static void pdf_escape(char *out, size_t out_sz, uint16_t codepoint)
{
	uint8_t ch;
	if (codepoint < 0x100)
		ch = static_cast<uint8_t>(codepoint);
	else
		ch = '?';

	if (ch == '(' || ch == ')' || ch == '\\') {
		snprintf(out, out_sz, "\\%c", ch);
	} else if (ch < 0x20 || ch > 0x7E) {
		snprintf(out, out_sz, "\\%03o", ch);
	} else {
		out[0] = static_cast<char>(ch);
		out[1] = '\0';
	}
}

void PdfWriter::add_page(const PageBitmap &bmp, [[maybe_unused]] int dpi,
                         const std::vector<TextGlyph> &text)
{
	int img_id = (int)write_image_stream(bmp);

	std::string content;
	char buf[256];

	snprintf(buf, sizeof(buf),
		"q %.2f 0 0 %.2f 0 0 cm /Img Do Q\n",
		page_w_pt_, page_h_pt_);
	content += buf;

	if (!text.empty()) {
		content += "BT\n3 Tr\n";
		float cur_size = 0;
		for (auto &g : text) {
			if (g.size_pt != cur_size) {
				snprintf(buf, sizeof(buf), "/F1 %.2f Tf\n", g.size_pt);
				content += buf;
				cur_size = g.size_pt;
			}
			float px = g.x_in * 72.0f;
			float py = page_h_pt_ - g.y_in * 72.0f;
			char esc[8];
			pdf_escape(esc, sizeof(esc), g.codepoint);
			snprintf(buf, sizeof(buf),
				"1 0 0 1 %.2f %.2f Tm (%s) Tj\n", px, py, esc);
			content += buf;
		}
		content += "ET\n";
	}

	int stream_id = alloc_obj();
	begin_obj(stream_id);
	fprintf(fp_, "<< /Length %zu >>\nstream\n", content.size());
	fwrite(content.data(), 1, content.size(), fp_);
	fprintf(fp_, "\nendstream\n");
	end_obj();

	int res_id = alloc_obj();
	begin_obj(res_id);
	fprintf(fp_, "<< /XObject << /Img %d 0 R >>\n", img_id);
	fprintf(fp_, "   /Font << /F1 %d 0 R >> >>\n", font_id_);
	end_obj();

	int page_id = alloc_obj();
	begin_obj(page_id);
	fprintf(fp_, "<< /Type /Page /Parent %d 0 R\n", pages_id_);
	fprintf(fp_, "   /MediaBox [0 0 %.2f %.2f]\n", page_w_pt_, page_h_pt_);
	fprintf(fp_, "   /Contents %d 0 R\n", stream_id);
	fprintf(fp_, "   /Resources %d 0 R >>\n", res_id);
	end_obj();

	page_ids_.push_back(page_id);
}

void PdfWriter::finish()
{
	if (!fp_) return;

	begin_obj(pages_id_);
	fprintf(fp_, "<< /Type /Pages /Kids [");
	for (int id : page_ids_)
		fprintf(fp_, " %d 0 R", id);
	fprintf(fp_, " ] /Count %zu >>\n", page_ids_.size());
	end_obj();

	begin_obj(catalog_id_);
	fprintf(fp_, "<< /Type /Catalog /Pages %d 0 R >>\n", pages_id_);
	end_obj();

	long xref_off = ftell(fp_);
	fprintf(fp_, "xref\n0 %d\n", next_id_);
	fprintf(fp_, "0000000000 65535 f \n");

	std::vector<long> offsets((size_t)next_id_, 0);
	for (auto &e : xref_)
		offsets[(size_t)e.id] = e.offset;

	for (int i = 1; i < next_id_; i++)
		fprintf(fp_, "%010ld 00000 n \n", offsets[(size_t)i]);

	fprintf(fp_, "trailer\n<< /Size %d /Root %d 0 R >>\n",
		next_id_, catalog_id_);
	fprintf(fp_, "startxref\n%ld\n%%%%EOF\n", xref_off);

	fclose(fp_);
	fp_ = nullptr;
}
