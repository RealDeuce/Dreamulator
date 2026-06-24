// license:BSD-3-Clause
// copyright-holders:Stephen Hurd
//
// Filesystem-backed DreamLink host for real DreamWriter hardware.
#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

namespace fs = std::filesystem;

static constexpr uint8_t PFX = 0x13;
static constexpr uint8_t END = 0x11;
static constexpr uint8_t ESC = 0x08;
static constexpr uint8_t EOF_MARK = 0x1A;

struct Options {
	std::string tty;
	fs::path root = ".";
	std::string name = "Dreamulator";
	bool verbose = false;
};

struct DirEntry {
	fs::path path;
	std::string base;
	std::string suffix;
	uint32_t size = 0;
	uint16_t time = 0;
	uint16_t date = 0;
};

struct Handle {
	enum class Mode { Read, Write };
	Mode mode;
	fs::path path;
	std::vector<uint8_t> data;
};

class DreamLinkServer {
public:
	explicit DreamLinkServer(Options opt) : opt_(std::move(opt)) {}
	int run();

private:
	int open_serial();
	bool configure_serial();
	bool read_byte(uint8_t &b);
	bool read_exact(uint8_t *buf, size_t len);
	bool write_all(const uint8_t *buf, size_t len);
	bool send(std::initializer_list<uint8_t> bytes);
	bool send_response(uint8_t cmd, uint8_t status,
	                   const std::vector<uint8_t> &payload = {});

	bool read_command();
	void refresh_directory();
	bool send_directory_entry(uint8_t cmd);
	bool read_ack();

	std::string read_c_string();
	std::string normalize_name(std::string_view name) const;
	std::string host_filename_for_name(std::string_view name) const;
	fs::path path_for_name(std::string_view name) const;
	uint16_t alloc_handle(Handle handle);
	Handle *find_handle(uint16_t h);

	bool handle_handshake();
	bool handle_probe_name();
	bool handle_listing(uint8_t cmd);
	bool handle_delete();
	bool handle_rename();
	bool handle_create();
	bool handle_open();
	bool handle_close();
	bool handle_read();
	bool handle_write();
	bool handle_format();

	bool receive_write_stream(Handle &handle);
	bool send_read_stream(const std::vector<uint8_t> &data);
	bool stream_write_byte(uint8_t b, int &pos, uint8_t &sum);
	bool finish_read_block(int &pos, uint8_t &sum, bool eof);

#ifdef __GNUC__
	__attribute__((format(printf, 2, 3)))
#endif
	void logf(const char *fmt, ...) const;

	Options opt_;
	int fd_ = -1;
	std::vector<DirEntry> entries_;
	size_t list_index_ = 0;
	uint16_t next_handle_ = 1;
	std::map<uint16_t, Handle> handles_;
};

static void usage(const char *argv0)
{
	std::fprintf(stderr,
	    "Usage: %s --tty DEV [--root DIR] [--name NAME] [--verbose]\n"
	    "\n"
	    "Serve a directory as a DreamLink host over 9600 8N1 serial.\n",
	    argv0);
}

static std::string compact_component(std::string_view s, size_t max)
{
	std::string out;
	for (char ch : s) {
		unsigned char c = static_cast<unsigned char>(ch);
		if (c >= 0x20 && ch != '/' && ch != '\\' && ch != ':') {
			out.push_back(ch);
			if (out.size() >= max)
				break;
		}
	}
	return out;
}

static void pack_fat_timestamp(const fs::path &path, uint16_t &packed_time,
                               uint16_t &packed_date)
{
	struct stat st;
	if (stat(path.c_str(), &st) != 0) {
		packed_time = 0;
		packed_date = static_cast<uint16_t>((1 << 5) | 1); // 1980-01-01
		return;
	}

	struct tm tm;
	if (!localtime_r(&st.st_mtime, &tm)) {
		packed_time = 0;
		packed_date = static_cast<uint16_t>((1 << 5) | 1);
		return;
	}

	int year_offset = tm.tm_year + 1900 - 1980;
	if (year_offset < 0)
		year_offset = 0;
	while (year_offset >= 100)
		year_offset -= 100;
	int month = std::clamp(tm.tm_mon + 1, 1, 12);
	int day = std::clamp(tm.tm_mday, 1, 31);
	int hour = std::clamp(tm.tm_hour, 0, 23);
	int minute = std::clamp(tm.tm_min, 0, 59);
	int second = std::clamp(tm.tm_sec, 0, 59);

	packed_time = static_cast<uint16_t>((hour << 11) | (minute << 5) | (second / 2));
	packed_date = static_cast<uint16_t>((year_offset << 9) | (month << 5) | day);
}

int DreamLinkServer::open_serial()
{
	fd_ = open(opt_.tty.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
	if (fd_ < 0) {
		std::perror(opt_.tty.c_str());
		return -1;
	}
	if (!configure_serial()) {
		close(fd_);
		fd_ = -1;
		return -1;
	}
	int flags = fcntl(fd_, F_GETFL, 0);
	if (flags < 0 || fcntl(fd_, F_SETFL, flags & ~O_NONBLOCK) < 0) {
		std::perror("fcntl");
		close(fd_);
		fd_ = -1;
		return -1;
	}
	return 0;
}

bool DreamLinkServer::configure_serial()
{
	struct termios tio;
	if (tcgetattr(fd_, &tio) < 0) {
		std::perror("tcgetattr");
		return false;
	}
	cfmakeraw(&tio);
	cfsetispeed(&tio, B9600);
	cfsetospeed(&tio, B9600);
	tio.c_cflag |= CLOCAL | CREAD;
#ifdef CSIZE
	tio.c_cflag &= static_cast<tcflag_t>(~static_cast<tcflag_t>(CSIZE));
#endif
#ifdef CS8
	tio.c_cflag |= CS8;
#endif
#ifdef PARENB
	tio.c_cflag &= static_cast<tcflag_t>(~static_cast<tcflag_t>(PARENB));
#endif
#ifdef CSTOPB
	tio.c_cflag &= static_cast<tcflag_t>(~static_cast<tcflag_t>(CSTOPB));
#endif
#ifdef CRTSCTS
	tio.c_cflag &= static_cast<tcflag_t>(~static_cast<tcflag_t>(CRTSCTS));
#endif
#ifdef VMIN
	tio.c_cc[VMIN] = 1;
#endif
#ifdef VTIME
	tio.c_cc[VTIME] = 0;
#endif
	if (tcsetattr(fd_, TCSANOW, &tio) < 0) {
		std::perror("tcsetattr");
		return false;
	}
	return true;
}

bool DreamLinkServer::read_byte(uint8_t &b)
{
	for (;;) {
		ssize_t n = read(fd_, &b, 1);
		if (n == 1) {
			if (opt_.verbose)
				std::fprintf(stderr, "< %02X\n", b);
			return true;
		}
		if (n < 0 && errno == EINTR)
			continue;
		if (n < 0)
			std::perror("read");
		else
			std::fprintf(stderr, "serial closed\n");
		return false;
	}
}

bool DreamLinkServer::read_exact(uint8_t *buf, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		if (!read_byte(buf[i]))
			return false;
	}
	return true;
}

bool DreamLinkServer::write_all(const uint8_t *buf, size_t len)
{
	for (size_t off = 0; off < len;) {
		ssize_t n = write(fd_, buf + off, len - off);
		if (n > 0) {
			if (opt_.verbose) {
				for (ssize_t i = 0; i < n; i++)
					std::fprintf(stderr, "> %02X\n", buf[off + static_cast<size_t>(i)]);
			}
			off += static_cast<size_t>(n);
			continue;
		}
		if (n < 0 && errno == EINTR)
			continue;
		std::perror("write");
		return false;
	}
	return true;
}

bool DreamLinkServer::send(std::initializer_list<uint8_t> bytes)
{
	std::vector<uint8_t> tmp(bytes);
	return write_all(tmp.data(), tmp.size());
}

bool DreamLinkServer::send_response(uint8_t cmd, uint8_t status,
                                    const std::vector<uint8_t> &payload)
{
	std::vector<uint8_t> out = { PFX, cmd, status };
	out.insert(out.end(), payload.begin(), payload.end());
	out.push_back(END);
	return write_all(out.data(), out.size());
}

void DreamLinkServer::logf(const char *fmt, ...) const
{
	va_list ap;
	va_start(ap, fmt);
	std::vfprintf(stderr, fmt, ap);
	va_end(ap);
}

void DreamLinkServer::refresh_directory()
{
	entries_.clear();
	for (const auto &de : fs::directory_iterator(opt_.root)) {
		if (!de.is_regular_file())
			continue;
		std::string filename = de.path().filename().string();
		std::string stem = filename;
		std::string ext;
		size_t dot = filename.find_last_of('.');
		if (dot != std::string::npos) {
			stem = filename.substr(0, dot);
			ext = filename.substr(dot + 1);
		}
		DirEntry e;
		e.path = de.path();
		e.base = compact_component(stem, 8);
		e.suffix = compact_component(ext, 4);
		if (e.base.empty())
			continue;
		uintmax_t sz = de.file_size();
		e.size = static_cast<uint32_t>(std::min<uintmax_t>(sz, 0xFFFFFFFFu));
		pack_fat_timestamp(e.path, e.time, e.date);
		entries_.push_back(std::move(e));
	}
	std::sort(entries_.begin(), entries_.end(), [](const DirEntry &a, const DirEntry &b) {
		if (a.base != b.base)
			return a.base < b.base;
		return a.suffix < b.suffix;
	});
}

std::string DreamLinkServer::normalize_name(std::string_view name) const
{
	std::string out;
	for (char ch : name) {
		unsigned char c = static_cast<unsigned char>(ch);
		if (c >= 0x20 && ch != '/' && ch != '\\' && ch != ':')
			out.push_back(static_cast<char>(std::toupper(c)));
	}
	return out;
}

std::string DreamLinkServer::host_filename_for_name(std::string_view name) const
{
	size_t dot = name.find_last_of('.');
	if (dot != std::string_view::npos) {
		std::string base = compact_component(name.substr(0, dot), 8);
		std::string suffix = compact_component(name.substr(dot + 1), 4);
		if (base.empty())
			base = "UNTITLED";
		return suffix.empty() ? base : base + "." + suffix;
	}

	if (name.size() > 8) {
		std::string base = compact_component(name.substr(0, 8), 8);
		std::string suffix = compact_component(name.substr(8), 4);
		if (base.empty())
			base = "UNTITLED";
		return suffix.empty() ? base : base + "." + suffix;
	}

	std::string clean = compact_component(name, 8);
	return clean.empty() ? "UNTITLED" : clean;
}

fs::path DreamLinkServer::path_for_name(std::string_view name) const
{
	std::string key = normalize_name(name);
	for (const DirEntry &e : entries_) {
		if (key == normalize_name(e.base) ||
		    key == normalize_name(e.base + e.suffix) ||
		    key == normalize_name(e.base + "." + e.suffix))
			return e.path;
	}

	return opt_.root / host_filename_for_name(name);
}

uint16_t DreamLinkServer::alloc_handle(Handle handle)
{
	uint16_t h = next_handle_++;
	if (next_handle_ == 0)
		next_handle_ = 1;
	handles_.emplace(h, std::move(handle));
	return h;
}

Handle *DreamLinkServer::find_handle(uint16_t h)
{
	auto it = handles_.find(h);
	return it == handles_.end() ? nullptr : &it->second;
}

std::string DreamLinkServer::read_c_string()
{
	std::string out;
	for (;;) {
		uint8_t b = 0;
		if (!read_byte(b))
			return {};
		if (b == 0)
			break;
		if (out.size() < 255)
			out.push_back(static_cast<char>(b));
	}
	return out;
}

bool DreamLinkServer::read_ack()
{
	uint8_t first = 0;
	if (!read_byte(first))
		return false;
	if (first == PFX) {
		uint8_t ack[2] = {};
		if (!read_exact(ack, sizeof(ack)))
			return false;
		if (ack[0] != 0x06 || ack[1] != END)
			std::fprintf(stderr, "warning: expected ACK 13 06 11, got 13 %02X %02X\n",
			             ack[0], ack[1]);
		return true;
	}

	uint8_t trailer = 0;
	if (!read_byte(trailer))
		return false;
	if (first != 0x06 || trailer != END)
		std::fprintf(stderr, "warning: expected ACK 06 11, got %02X %02X\n", first, trailer);
	return true;
}

bool DreamLinkServer::handle_handshake()
{
	uint8_t trailer = 0;
	if (!read_byte(trailer))
		return false;
	logf("handshake\n");
	if (trailer != END)
		std::fprintf(stderr, "warning: handshake trailer %02X\n", trailer);
	return send({ PFX, 0x18, 0x06, END });
}

bool DreamLinkServer::handle_probe_name()
{
	uint8_t trailer = 0;
	if (!read_byte(trailer))
		return false;
	logf("probe name\n");
	std::vector<uint8_t> payload;
	for (char ch : opt_.name)
		payload.push_back(static_cast<uint8_t>(ch));
	payload.push_back(0);
	payload.push_back(0);
	if (!send_response(0x47, 0x00, payload))
		return false;
	return read_ack();
}

bool DreamLinkServer::send_directory_entry(uint8_t cmd)
{
	if (list_index_ >= entries_.size()) {
		logf("list %02X: end\n", cmd);
		return send_response(cmd, 0x01, { 0x12 });
	}

	const DirEntry &e = entries_[list_index_++];
	logf("list %02X: %s%s%s (%u bytes)\n", cmd, e.base.c_str(),
	     e.suffix.empty() ? "" : ".", e.suffix.c_str(), e.size);

	std::vector<uint8_t> payload;
	payload.push_back(0x00); // attribute/status
	payload.push_back(static_cast<uint8_t>(e.time & 0xFF));
	payload.push_back(static_cast<uint8_t>((e.time >> 8) & 0xFF));
	payload.push_back(static_cast<uint8_t>(e.date & 0xFF));
	payload.push_back(static_cast<uint8_t>((e.date >> 8) & 0xFF));
	payload.push_back(static_cast<uint8_t>(e.size & 0xFF));
	payload.push_back(static_cast<uint8_t>((e.size >> 8) & 0xFF));
	payload.push_back(static_cast<uint8_t>((e.size >> 16) & 0xFF));
	payload.push_back(static_cast<uint8_t>((e.size >> 24) & 0xFF));
	for (size_t i = 0; i < 8; i++)
		payload.push_back(i < e.base.size() ? static_cast<uint8_t>(e.base[i]) : static_cast<uint8_t>(' '));
	for (size_t i = 0; i < 4; i++)
		payload.push_back(i < e.suffix.size() ? static_cast<uint8_t>(e.suffix[i]) : static_cast<uint8_t>(' '));
	payload.push_back(0x00);
	payload.push_back(0x00); // extra
	if (!send_response(cmd, 0x00, payload))
		return false;
	return read_ack();
}

bool DreamLinkServer::handle_listing(uint8_t cmd)
{
	uint8_t trailer = 0;
	if (!read_byte(trailer))
		return false;
	if (cmd == 0x4E) {
		refresh_directory();
		list_index_ = 0;
	}
	return send_directory_entry(cmd);
}

bool DreamLinkServer::handle_delete()
{
	std::string name = read_c_string();
	uint8_t tail[2] = {};
	if (!read_exact(tail, sizeof(tail)))
		return false;
	refresh_directory();
	fs::path path = path_for_name(name);
	logf("delete %s -> %s\n", name.c_str(), path.string().c_str());
	std::error_code ec;
	bool removed = fs::remove(path, ec);
	return send_response(0x13, (ec || !removed) ? 0x01 : 0x00,
	                     (ec || !removed) ? std::vector<uint8_t>{ 0x02 } : std::vector<uint8_t>{});
}

bool DreamLinkServer::handle_rename()
{
	std::string old_name = read_c_string();
	std::string new_name = read_c_string();
	uint8_t tail[2] = {};
	if (!read_exact(tail, sizeof(tail)))
		return false;
	refresh_directory();
	fs::path old_path = path_for_name(old_name);
	fs::path new_path = path_for_name(new_name);
	logf("rename %s -> %s\n", old_path.string().c_str(), new_path.string().c_str());
	std::error_code ec;
	bool old_exists = fs::exists(old_path, ec);
	if (ec || !old_exists)
		return send_response(0x17, 0x01, { 0x02 });
	bool same_path = false;
	std::error_code equiv_ec;
	if (fs::exists(new_path, ec)) {
		same_path = fs::equivalent(old_path, new_path, equiv_ec);
		if (ec || equiv_ec || !same_path)
			return send_response(0x17, 0x01, { 0x02 });
	}
	if (!same_path) {
		fs::rename(old_path, new_path, ec);
		if (ec)
			return send_response(0x17, 0x01, { 0x02 });
	}
	refresh_directory();
	return send_response(0x17, 0x00);
}

bool DreamLinkServer::handle_create()
{
	uint8_t stamp[4] = {};
	if (!read_exact(stamp, sizeof(stamp)))
		return false;
	std::string name = read_c_string();
	uint8_t tail[2] = {};
	if (!read_exact(tail, sizeof(tail)))
		return false;

	refresh_directory();
	Handle handle { Handle::Mode::Write, path_for_name(name), {} };
	logf("create %s -> %s\n", name.c_str(), handle.path.string().c_str());
	uint16_t h = alloc_handle(std::move(handle));
	return send_response(0x3C, 0x00, {
		static_cast<uint8_t>(h >> 8), static_cast<uint8_t>(h & 0xFF)
	});
}

bool DreamLinkServer::handle_open()
{
	uint8_t mode = 0;
	if (!read_byte(mode))
		return false;
	std::string name = read_c_string();
	uint8_t tail[2] = {};
	if (!read_exact(tail, sizeof(tail)))
		return false;

	refresh_directory();
	fs::path path = path_for_name(name);
	std::ifstream in(path, std::ios::binary);
	if (!in) {
		logf("open %s -> not found (%s)\n", name.c_str(), path.string().c_str());
		return send_response(0x3D, 0x01, { 0x02 });
	}
	std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)),
	                          std::istreambuf_iterator<char>());
	Handle handle { Handle::Mode::Read, path, std::move(data) };
	uint16_t h = alloc_handle(std::move(handle));
	logf("open %s -> handle %u (%s)\n", name.c_str(), h, path.string().c_str());
	return send_response(0x3D, 0x00, {
		static_cast<uint8_t>(h >> 8), static_cast<uint8_t>(h & 0xFF)
	});
}

bool DreamLinkServer::handle_close()
{
	uint8_t buf[3] = {};
	if (!read_exact(buf, sizeof(buf)))
		return false;
	uint16_t h = static_cast<uint16_t>((buf[0] << 8) | buf[1]);
	auto it = handles_.find(h);
	if (it == handles_.end()) {
		logf("close unknown handle %u (ignored)\n", h);
		return send_response(0x3E, 0x00);
	}
	logf("close handle %u\n", h);
	handles_.erase(it);
	return send_response(0x3E, 0x00);
}

bool DreamLinkServer::handle_read()
{
	uint8_t buf[3] = {};
	if (!read_exact(buf, sizeof(buf)))
		return false;
	uint16_t h = static_cast<uint16_t>((buf[0] << 8) | buf[1]);
	Handle *handle = find_handle(h);
	if (!handle || handle->mode != Handle::Mode::Read) {
		logf("read bad handle %u\n", h);
		return send_response(0x3F, 0x01, { 0x02 });
	}
	logf("read handle %u: %zu bytes\n", h, handle->data.size());
	if (!send({ PFX, 0x3F, 0x00 }))
		return false;
	return send_read_stream(handle->data);
}

bool DreamLinkServer::handle_write()
{
	uint8_t buf[2] = {};
	if (!read_exact(buf, sizeof(buf)))
		return false;
	uint16_t h = static_cast<uint16_t>((buf[0] << 8) | buf[1]);
	Handle *handle = find_handle(h);
	if (!handle || handle->mode != Handle::Mode::Write) {
		logf("write bad handle %u\n", h);
		return false;
	}
	logf("write handle %u\n", h);
	if (!receive_write_stream(*handle))
		return false;
	return send_response(0x40, 0x00);
}

bool DreamLinkServer::handle_format()
{
	uint8_t rest[3] = {};
	if (!read_exact(rest, sizeof(rest)))
		return false;
	logf("format/init\n");
	bool ok = true;
	for (const auto &de : fs::directory_iterator(opt_.root)) {
		std::error_code ec;
		if (!fs::is_regular_file(de.symlink_status(ec)) || ec)
			continue;
		fs::remove(de.path(), ec);
		if (ec) {
			std::fprintf(stderr, "%s: %s\n", de.path().string().c_str(), ec.message().c_str());
			ok = false;
		}
	}
	handles_.clear();
	refresh_directory();
	return send_response(0x44, ok ? 0x00 : 0x01, ok ? std::vector<uint8_t>{} : std::vector<uint8_t>{ 0x02 });
}

bool DreamLinkServer::receive_write_stream(Handle &handle)
{
	for (;;) {
		uint8_t b = 0;
		if (!read_byte(b))
			return false;

		if (b == PFX) {
			// Continuation blocks can restart with a fresh prefix.
			continue;
		}
		if (b == ESC) {
			uint8_t enc = 0;
			if (!read_byte(enc))
				return false;
			handle.data.push_back(static_cast<uint8_t>(enc - 0x60));
			continue;
		}
		if (b == EOF_MARK) {
			// Final block padding/checksum is terminated by 11. Some ROMs do
			// not send the documented 15 11 prompt after the EOF trailer.
			for (;;) {
				uint8_t x = 0;
				if (!read_byte(x))
					return false;
				if (x == END)
					break;
			}
			if (!send({ PFX, 0x06, END }))
				return false;
			std::ofstream out(handle.path, std::ios::binary | std::ios::trunc);
			if (!out) {
				std::perror(handle.path.string().c_str());
				return false;
			}
			out.write(reinterpret_cast<const char *>(handle.data.data()),
			          static_cast<std::streamsize>(handle.data.size()));
			logf("stored %zu bytes -> %s\n", handle.data.size(),
			     handle.path.string().c_str());
			return true;
		}
		if (b == 0x15) {
			uint8_t trailer = 0;
			if (!read_byte(trailer))
				return false;
			if (trailer == END && !send({ PFX, 0x06, END }))
				return false;
			continue;
		}
		if (b < 0x20) {
			// Raw control bytes in the stream are block padding/check/trailer.
			continue;
		}
		handle.data.push_back(b);
	}
}

bool DreamLinkServer::stream_write_byte(uint8_t b, int &pos, uint8_t &sum)
{
	if (pos >= 0x01FD) {
		if (!finish_read_block(pos, sum, false))
			return false;
	}
	if (!write_all(&b, 1))
		return false;
	pos++;
	return true;
}

bool DreamLinkServer::finish_read_block(int &pos, uint8_t &sum, bool eof)
{
	if (eof) {
		if (!write_all(&EOF_MARK, 1))
			return false;
		pos++;
	}
	while (pos < 0x01FE) {
		uint8_t zero = 0;
		if (!write_all(&zero, 1))
			return false;
		pos++;
	}
	if (!write_all(&sum, 1))
		return false;
	if (!write_all(&END, 1))
		return false;
	if (!read_ack())
		return false;
	pos = 1;
	sum = 0;
	return true;
}

bool DreamLinkServer::send_read_stream(const std::vector<uint8_t> &data)
{
	int pos = 3;
	uint8_t sum = 0x3F;
	for (uint8_t b : data) {
		sum = static_cast<uint8_t>(sum + b);
		if (b < 0x20) {
			if (!stream_write_byte(ESC, pos, sum))
				return false;
			if (!stream_write_byte(static_cast<uint8_t>(b + 0x60), pos, sum))
				return false;
		} else if (!stream_write_byte(b, pos, sum)) {
			return false;
		}
	}
	return finish_read_block(pos, sum, true);
}

bool DreamLinkServer::read_command()
{
	uint8_t b = 0;
	for (;;) {
		if (!read_byte(b))
			return false;
		if (b == PFX)
			break;
		if (b == 0x18) {
			logf("prefixless handshake\n");
			return handle_handshake();
		}
		if (b == EOF_MARK) {
			logf("raw EOF cleanup block\n");
			for (;;) {
				uint8_t x = 0;
				if (!read_byte(x))
					return false;
				if (x == END) {
					if (!send({ PFX, 0x06, END }))
						return false;
					return send_response(0x40, 0x00);
				}
			}
		}
	}

	uint8_t cmd = 0;
	if (!read_byte(cmd))
		return false;

	switch (cmd) {
	case 0x18: return handle_handshake();
	case 0x47: return handle_probe_name();
	case 0x4E:
	case 0x4F: return handle_listing(cmd);
	case 0x13: return handle_delete();
	case 0x17: return handle_rename();
	case 0x3C: return handle_create();
	case 0x3D: return handle_open();
	case 0x3E: return handle_close();
	case 0x3F: return handle_read();
	case 0x40: return handle_write();
	case 0x44: return handle_format();
	case 0x06:
		logf("host saw ACK shortcut\n");
		return true;
	default:
		std::fprintf(stderr, "unknown DreamLink command %02X\n", cmd);
		return true;
	}
}

int DreamLinkServer::run()
{
	std::error_code ec;
	fs::create_directories(opt_.root, ec);
	if (ec) {
		std::fprintf(stderr, "%s: %s\n", opt_.root.string().c_str(), ec.message().c_str());
		return 1;
	}
	if (open_serial() < 0)
		return 1;
	std::fprintf(stderr, "DreamLink: %s serving %s as \"%s\"\n",
	             opt_.tty.c_str(), fs::absolute(opt_.root).string().c_str(),
	             opt_.name.c_str());
	while (read_command()) {
	}
	close(fd_);
	fd_ = -1;
	return 1;
}

int main(int argc, char **argv)
{
	Options opt;
	for (int i = 1; i < argc; i++) {
		if (!std::strcmp(argv[i], "--tty") && i + 1 < argc) {
			opt.tty = argv[++i];
		} else if (!std::strcmp(argv[i], "--root") && i + 1 < argc) {
			opt.root = argv[++i];
		} else if (!std::strcmp(argv[i], "--name") && i + 1 < argc) {
			opt.name = argv[++i];
		} else if (!std::strcmp(argv[i], "--verbose")) {
			opt.verbose = true;
		} else if (!std::strcmp(argv[i], "--help") || !std::strcmp(argv[i], "-h")) {
			usage(argv[0]);
			return 0;
		} else {
			std::fprintf(stderr, "unknown argument: %s\n", argv[i]);
			usage(argv[0]);
			return 1;
		}
	}
	if (opt.tty.empty()) {
		usage(argv[0]);
		return 1;
	}
	DreamLinkServer server(std::move(opt));
	return server.run();
}
