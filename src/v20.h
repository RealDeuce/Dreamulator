// license:BSD-3-Clause
// copyright-holders:Stephen Hurd, MAMEDev (Bryan McPhail)
#ifndef V20_H
#define V20_H

#include <cstdint>

#define V20_CF 0x0001
#define V20_PF 0x0004
#define V20_AF 0x0010
#define V20_ZF 0x0040
#define V20_SF 0x0080
#define V20_TF 0x0100
#define V20_IF 0x0200
#define V20_DF 0x0400
#define V20_OF 0x0800

struct v20_trace_t {
	uint16_t cs, ip, ax, bx, cx, dx, si, di, bp, sp, ds, es, ss, flags;
	uint8_t  opcode;
};

struct v20_t {
	union { uint16_t ax; struct { uint8_t al, ah; }; };
	union { uint16_t cx; struct { uint8_t cl, ch; }; };
	union { uint16_t dx; struct { uint8_t dl, dh; }; };
	union { uint16_t bx; struct { uint8_t bl, bh; }; };
	uint16_t sp, bp, si, di;
	uint16_t es, cs, ss, ds;
	uint16_t ip;
	uint16_t flags;

	bool     halted;
	bool     irq_line;
	uint8_t  irq_vector;
	bool     nmi_line;
	bool     nmi_prev;
	int      seg_override;

	uint8_t  (*mem_read)(void *ctx, uint32_t addr);
	void     (*mem_write)(void *ctx, uint32_t addr, uint8_t val);
	uint8_t  (*io_read)(void *ctx, uint16_t port);
	void     (*io_write)(void *ctx, uint16_t port, uint8_t val);
	void     *ctx;

	/* debug */
	bool     debug_stop;
	bool     debug_step;

#define V20_MAX_BP 8
	uint16_t bp_seg[V20_MAX_BP];
	uint16_t bp_off[V20_MAX_BP];
	bool     bp_enabled[V20_MAX_BP];

#define V20_TRACE_SIZE 2048
	v20_trace_t trace_buf[V20_TRACE_SIZE];
	int      trace_head;
	int      trace_count;
	bool     trace_enabled;

	void     (*debug_cb)(void *debug_ctx);
	void     *debug_ctx;
};

void v20_init(v20_t *cpu);
void v20_reset(v20_t *cpu);
int  v20_exec(v20_t *cpu, int cycles);
void v20_irq(v20_t *cpu, bool level, uint8_t vector);
void v20_nmi(v20_t *cpu, bool level);

#endif
