// license:BSD-3-Clause
// copyright-holders:Stephen Hurd
#include "pdfwriter.h"
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

void PdfWriter::add_page(const PageBitmap &bmp, [[maybe_unused]] int dpi)
{
	int img_id = (int)write_image_stream(bmp);

	char content[256];
	int clen = snprintf(content, sizeof(content),
		"q %.2f 0 0 %.2f 0 0 cm /Img Do Q\n",
		page_w_pt_, page_h_pt_);

	int stream_id = alloc_obj();
	begin_obj(stream_id);
	fprintf(fp_, "<< /Length %d >>\nstream\n", clen);
	fwrite(content, 1, (size_t)clen, fp_);
	fprintf(fp_, "\nendstream\n");
	end_obj();

	int res_id = alloc_obj();
	begin_obj(res_id);
	fprintf(fp_, "<< /XObject << /Img %d 0 R >> >>\n", img_id);
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
