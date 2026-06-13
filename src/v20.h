// license:BSD-3-Clause
// copyright-holders:Stephen Hurd, MAMEDev (Bryan McPhail)
#ifndef V20_H
#define V20_H

#include <array>
#include <cstdint>
#include <optional>

#define V20_CF 0x0001
#define V20_PF 0x0004
#define V20_AF 0x0010
#define V20_ZF 0x0040
#define V20_SF 0x0080
#define V20_TF 0x0100
#define V20_IF 0x0200
#define V20_DF 0x0400
#define V20_OF 0x0800

constexpr int V20_MAX_BP    = 8;
constexpr int V20_TRACE_SIZE = 2048;

class RegByte {
	uint16_t &reg_;
	unsigned shift_;
public:
	RegByte(uint16_t &r, unsigned s) : reg_(r), shift_(s) {}
	operator uint8_t() const { return (uint8_t)(reg_ >> shift_); }
	RegByte& operator=(uint8_t v) {
		reg_ = (uint16_t)(((unsigned)reg_ & ~(0xFFU << shift_)) | ((unsigned)v << shift_));
		return *this;
	}
	RegByte& operator=(const RegByte &o) { return *this = (uint8_t)o; }
	RegByte& operator+=(uint8_t v) { return *this = (uint8_t)(*this + v); }
	RegByte& operator-=(uint8_t v) { return *this = (uint8_t)(*this - v); }
	RegByte& operator&=(uint8_t v) { return *this = (uint8_t)(*this & v); }
	RegByte& operator|=(uint8_t v) { return *this = (uint8_t)(*this | v); }
	RegByte& operator^=(uint8_t v) { return *this = (uint8_t)(*this ^ v); }
	RegByte& operator++()    { return *this = (uint8_t)(*this + 1); }
	RegByte& operator--()    { return *this = (uint8_t)(*this - 1); }
	uint8_t  operator++(int) { uint8_t t = *this; ++*this; return t; }
	uint8_t  operator--(int) { uint8_t t = *this; --*this; return t; }
};

struct v20_trace_t {
	uint16_t cs = 0, ip = 0;
	uint16_t ax = 0, bx = 0, cx = 0, dx = 0;
	uint16_t si = 0, di = 0, bp = 0, sp = 0;
	uint16_t ds = 0, es = 0, ss = 0, flags = 0;
	uint8_t  opcode = 0;
};

struct v20_t {
	uint16_t ax = 0, cx = 0, dx = 0, bx = 0;
	uint16_t sp = 0, bp = 0, si = 0, di = 0;
	uint16_t es = 0, cs = 0, ss = 0, ds = 0;
	uint16_t ip = 0;
	uint16_t flags = 0;

	RegByte al() { return {ax, 0}; }
	RegByte ah() { return {ax, 8}; }
	RegByte cl() { return {cx, 0}; }
	RegByte ch() { return {cx, 8}; }
	RegByte dl() { return {dx, 0}; }
	RegByte dh() { return {dx, 8}; }
	RegByte bl() { return {bx, 0}; }
	RegByte bh() { return {bx, 8}; }

	bool     halted = false;
	bool     mode_8080 = false;
	bool     irq_line = false;
	uint8_t  irq_vector = 0;
	bool     nmi_line = false;
	bool     nmi_prev = false;
	std::optional<int> seg_override;

	uint8_t  (*mem_read)(void *ctx, uint32_t addr) = nullptr;
	void     (*mem_write)(void *ctx, uint32_t addr, uint8_t val) = nullptr;
	uint8_t  (*io_read)(void *ctx, uint16_t port) = nullptr;
	void     (*io_write)(void *ctx, uint16_t port, uint8_t val) = nullptr;
	void     *ctx = nullptr;

	bool     debug_stop = false;
	bool     debug_step = false;

	std::array<uint16_t, V20_MAX_BP> bp_seg{};
	std::array<uint16_t, V20_MAX_BP> bp_off{};
	std::array<bool, V20_MAX_BP>     bp_enabled{};

	std::array<v20_trace_t, V20_TRACE_SIZE> trace_buf{};
	int      trace_head = 0;
	int      trace_count = 0;
	bool     trace_enabled = false;

	void     (*debug_cb)(void *debug_ctx) = nullptr;
	void     *debug_ctx = nullptr;
};

void v20_init(v20_t *cpu);
void v20_reset(v20_t *cpu);
int  v20_exec(v20_t *cpu, int cycles);
void v20_irq(v20_t *cpu, bool level, uint8_t vector);
void v20_nmi(v20_t *cpu, bool level);

#endif
