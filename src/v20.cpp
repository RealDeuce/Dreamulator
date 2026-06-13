// license:BSD-3-Clause
// copyright-holders:Stephen Hurd, MAMEDev (Bryan McPhail)
#include "v20.h"
#include <climits>
#include <cstdio>
#include <cstddef>

/* even-parity lookup */
static constexpr uint8_t ptab[256] = {
	1,0,0,1,0,1,1,0,0,1,1,0,1,0,0,1,0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,0,
	0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,0,1,0,0,1,0,1,1,0,0,1,1,0,1,0,0,1,
	0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,0,1,0,0,1,0,1,1,0,0,1,1,0,1,0,0,1,
	1,0,0,1,0,1,1,0,0,1,1,0,1,0,0,1,0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,0,
	0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,0,1,0,0,1,0,1,1,0,0,1,1,0,1,0,0,1,
	1,0,0,1,0,1,1,0,0,1,1,0,1,0,0,1,0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,0,
	1,0,0,1,0,1,1,0,0,1,1,0,1,0,0,1,0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,0,
	0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,0,1,0,0,1,0,1,1,0,0,1,1,0,1,0,0,1,
};

/* ---- memory helpers ---- */

static inline uint8_t  rb(v20_t *c, uint32_t a) { return c->mem_read(c->ctx, a & 0xFFFFF); }
static inline void     wb(v20_t *c, uint32_t a, uint8_t v) { c->mem_write(c->ctx, a & 0xFFFFF, v); }
static inline uint16_t rw(v20_t *c, uint32_t a) { return (uint16_t)(rb(c,a) | (rb(c,a+1) << 8)); }
static inline void     ww(v20_t *c, uint32_t a, uint16_t v) { wb(c,a,(uint8_t)v); wb(c,a+1,v>>8); }

static inline uint32_t soff(uint16_t s, uint16_t o) { return ((uint32_t)s<<4)+o; }
#define MRB(s,o)    rb(c, soff(s,o))
#define MRW(s,o)    rw(c, soff(s,o))
#define MWB(s,o,v)  wb(c, soff(s,o), v)
#define MWW(s,o,v)  ww(c, soff(s,o), v)

static inline uint8_t  f8(v20_t *c) { uint8_t  v=MRB(c->cs,c->ip); c->ip++;   return v; }
static inline uint16_t f16(v20_t *c){ uint16_t v=MRW(c->cs,c->ip); c->ip += 2; return v; }

/* ---- register access by 8086 encoding ---- */

static inline uint16_t *r16p(v20_t *c, int i) {
	static constexpr size_t t[]={
		offsetof(v20_t,ax), offsetof(v20_t,cx), offsetof(v20_t,dx), offsetof(v20_t,bx),
		offsetof(v20_t,sp), offsetof(v20_t,bp), offsetof(v20_t,si), offsetof(v20_t,di)};
	return (uint16_t*)((char*)c + t[i&7]);
}
static inline uint16_t *srp(v20_t *c, int i) {
	static constexpr size_t t[]={
		offsetof(v20_t,es), offsetof(v20_t,cs), offsetof(v20_t,ss), offsetof(v20_t,ds)};
	return (uint16_t*)((char*)c + t[i&3]);
}
static inline uint8_t gr8(v20_t *c, int i) {
	uint16_t v = *r16p(c, i&3);
	return (i&4) ? (v>>8) : (uint8_t)v;
}
static inline void sr8(v20_t *c, int i, uint8_t v) {
	uint16_t *r = r16p(c, i&3);
	if (i&4) *r = (uint16_t)((*r & 0x00FF) | (v << 8));
	else     *r = (*r & 0xFF00) | v;
}

/* ---- stack ---- */

static inline void     push(v20_t *c, uint16_t v) { c->sp -= 2; MWW(c->ss, c->sp, v); }
static inline uint16_t pop(v20_t *c) { uint16_t v = MRW(c->ss, c->sp); c->sp += 2; return v; }

/* ---- ALU with flags ---- */

static uint32_t alu(v20_t *c, int op, uint32_t a, uint32_t b, int w)
{
	uint32_t cf = (c->flags & V20_CF) ? 1 : 0;
	uint32_t mask = w ? 0xFFFF : 0xFF;
	uint32_t sign = w ? 0x8000 : 0x80;
	uint32_t r;

	switch (op) {
	case 0: r = a + b;          break;
	case 1: r = a | b;          break;
	case 2: r = a + b + cf;     break;
	case 3: r = a - b - cf;     break;
	case 4: r = a & b;          break;
	case 5: case 7: r = a - b;  break;
	case 6: r = a ^ b;          break;
	default: r = 0;             break;
	}

	uint32_t rm = r & mask;
	uint16_t f = c->flags & ~(V20_CF|V20_PF|V20_AF|V20_ZF|V20_SF|V20_OF);

	if (!rm)       f |= V20_ZF;
	if (rm & sign) f |= V20_SF;
	if (ptab[rm & 0xFF]) f |= V20_PF;

	switch (op) {
	case 0: case 2:
		if (r > mask) f |= V20_CF;
		if ((a ^ b ^ r) & 0x10) f |= V20_AF;
		if (((rm ^ a) & (rm ^ b)) & sign) f |= V20_OF;
		break;
	case 3: case 5: case 7:
		if (r > mask) f |= V20_CF;
		if ((a ^ b ^ r) & 0x10) f |= V20_AF;
		if (((a ^ b) & (a ^ rm)) & sign) f |= V20_OF;
		break;
	default: break;
	}

	c->flags = f;
	return rm;
}

/* ---- ModR/M ---- */

typedef struct { int reg, mod, rm; uint16_t off, seg; } ea_t;

static ea_t decode_ea(v20_t *c)
{
	uint8_t b = f8(c);
	ea_t e;
	e.reg = (b>>3)&7;
	e.mod = b>>6;
	e.rm  = b&7;
	e.off = 0;
	e.seg = 0;

	if (e.mod == 3) return e;

	uint16_t off = 0;
	int ds = 3;

	switch (e.rm) {
	case 0: off = c->bx + c->si; break;
	case 1: off = c->bx + c->di; break;
	case 2: off = c->bp + c->si; ds = 2; break;
	case 3: off = c->bp + c->di; ds = 2; break;
	case 4: off = c->si; break;
	case 5: off = c->di; break;
	case 6: if (e.mod==0) { off = f16(c); } else { off = c->bp; ds = 2; } break;
	case 7: off = c->bx; break;
	}

	if      (e.mod == 1) off += (int8_t)f8(c);
	else if (e.mod == 2) off += f16(c);

	e.off = off;
	e.seg = c->seg_override ? *srp(c, *c->seg_override) : *srp(c, ds);
	return e;
}

static inline uint8_t  ea_rb(v20_t *c, ea_t *e) { return (e->mod==3) ? gr8(c,e->rm) : MRB(e->seg,e->off); }
static inline uint16_t ea_rw(v20_t *c, ea_t *e) { return (e->mod==3) ? *r16p(c,e->rm) : MRW(e->seg,e->off); }
static inline void ea_wb(v20_t *c, ea_t *e, uint8_t v)  { if(e->mod==3) sr8(c,e->rm,v); else MWB(e->seg,e->off,v); }
static inline void ea_ww(v20_t *c, ea_t *e, uint16_t v) { if(e->mod==3) *r16p(c,e->rm)=v; else MWW(e->seg,e->off,v); }

/* ---- shift / rotate ---- */

static uint32_t shf(v20_t *c, int op, uint32_t val, int cnt, int w)
{
	uint32_t mask = w ? 0xFFFF : 0xFF;
	uint32_t sign = w ? 0x8000 : 0x80;
	int bits = w ? 16 : 8;

	cnt &= 0x1F;
	if (!cnt) return val;

	uint32_t r = val;
	int cf = 0, ocf = (c->flags & V20_CF) ? 1 : 0;

	for (int i = 0; i < cnt; i++) {
		switch (op) {
		case 0: cf=(r>>(bits-1))&1; r=((r<<1)|(uint32_t)cf)&mask; break;
		case 1: cf=r&1; r=((r>>1)|((uint32_t)cf<<(bits-1)))&mask; break;
		case 2: cf=(r>>(bits-1))&1; r=((r<<1)|(uint32_t)ocf)&mask; ocf=cf; break;
		case 3: cf=r&1; r=((r>>1)|((uint32_t)ocf<<(bits-1)))&mask; ocf=cf; break;
		case 4: case 6: cf=(r>>(bits-1))&1; r=(r<<1)&mask; break;
		case 5: cf=r&1; r=(r>>1)&mask; break;
		case 7: cf=r&1; r=((r&sign)?(r>>1)|sign:r>>1)&mask; break;
		}
	}

	uint16_t f = c->flags;
	f = (f & ~V20_CF) | (cf ? V20_CF : 0);

	if (op >= 4) {
		f &= ~(V20_ZF|V20_SF|V20_PF);
		if (!(r & mask)) f |= V20_ZF;
		if (r & sign)    f |= V20_SF;
		if (ptab[r & 0xFF]) f |= V20_PF;
	}

	if (cnt == 1) {
		f &= ~V20_OF;
		if (op==0||op==4||op==6) { if (((r>>(bits-1))^(uint32_t)cf)&1) f|=V20_OF; }
		else if (op==1)          { if (((r>>(bits-1))^((r>>(bits-2))&1))&1) f|=V20_OF; }
		else if (op==5)          { if (val & sign) f|=V20_OF; }
	}

	c->flags = f;
	return r;
}

/* ---- INT ---- */

static void do_int(v20_t *c, uint8_t vec)
{
	// Bit 15 (MD flag): 1=native mode, 0=8080 emulation mode.
	// Saved so IRET can restore the correct mode.
	uint16_t saved_flags = c->flags;
	if (!c->mode_8080) saved_flags |= 0x8000;
	push(c, saved_flags);
	push(c, c->cs);
	push(c, c->ip);
	c->flags &= ~(V20_IF | V20_TF);
	c->ip = rw(c, (uint32_t)vec * 4);
	c->cs = rw(c, (uint32_t)vec * 4 + 2);
}

/* ---- INC / DEC helpers ---- */

static inline void inc16(v20_t *c, uint16_t *r) {
	uint16_t o = *r; (*r)++;
	uint16_t f = c->flags & ~(V20_PF|V20_AF|V20_ZF|V20_SF|V20_OF);
	if (!*r) f|=V20_ZF; if (*r&0x8000) f|=V20_SF;
	if (ptab[*r&0xFF]) f|=V20_PF;
	if ((o^1^*r)&0x10) f|=V20_AF;
	if (o==0x7FFF) f|=V20_OF;
	c->flags = f;
}
static inline void dec16(v20_t *c, uint16_t *r) {
	uint16_t o = *r; (*r)--;
	uint16_t f = c->flags & ~(V20_PF|V20_AF|V20_ZF|V20_SF|V20_OF);
	if (!*r) f|=V20_ZF; if (*r&0x8000) f|=V20_SF;
	if (ptab[*r&0xFF]) f|=V20_PF;
	if ((o^1^*r)&0x10) f|=V20_AF;
	if (o==0x8000) f|=V20_OF;
	c->flags = f;
}

/* ---- string operations ---- */

static int s_movsb(v20_t *c, bool rep) {
	uint16_t ss = c->seg_override ? *srp(c, *c->seg_override) : c->ds;
	int d = (c->flags&V20_DF)?-1:1; int n=0;
	if (rep && !c->cx) return 5;
	do { MWB(c->es,c->di,MRB(ss,c->si)); c->si+=d; c->di+=d; n++;
	     if(!rep) return 7; c->cx--; } while(c->cx); return 5+2*n;
}
static int s_movsw(v20_t *c, bool rep) {
	uint16_t ss = c->seg_override ? *srp(c, *c->seg_override) : c->ds;
	int d = (c->flags&V20_DF)?-2:2; int n=0;
	if (rep && !c->cx) return 5;
	do { MWW(c->es,c->di,MRW(ss,c->si)); c->si+=d; c->di+=d; n++;
	     if(!rep) return 9; c->cx--; } while(c->cx); return 5+2*n;
}
static int s_stosb(v20_t *c, bool rep) {
	int d = (c->flags&V20_DF)?-1:1; int n=0;
	if (rep && !c->cx) return 5;
	do { MWB(c->es,c->di,c->al()); c->di+=d; n++;
	     if(!rep) return 3; c->cx--; } while(c->cx); return 5+2*n;
}
static int s_stosw(v20_t *c, bool rep) {
	int d = (c->flags&V20_DF)?-2:2; int n=0;
	if (rep && !c->cx) return 5;
	do { MWW(c->es,c->di,c->ax); c->di+=d; n++;
	     if(!rep) return 3; c->cx--; } while(c->cx); return 5+2*n;
}
static int s_lodsb(v20_t *c, bool rep) {
	uint16_t ss = c->seg_override ? *srp(c, *c->seg_override) : c->ds;
	int d = (c->flags&V20_DF)?-1:1; int n=0;
	if (rep && !c->cx) return 5;
	do { c->al()=MRB(ss,c->si); c->si+=d; n++;
	     if(!rep) return 5; c->cx--; } while(c->cx); return 5+2*n;
}
static int s_lodsw(v20_t *c, bool rep) {
	uint16_t ss = c->seg_override ? *srp(c, *c->seg_override) : c->ds;
	int d = (c->flags&V20_DF)?-2:2; int n=0;
	if (rep && !c->cx) return 5;
	do { c->ax=MRW(ss,c->si); c->si+=d; n++;
	     if(!rep) return 5; c->cx--; } while(c->cx); return 5+2*n;
}
static int s_cmpsb(v20_t *c, bool rep, bool repne, bool repc, bool repnc) {
	uint16_t ss = c->seg_override ? *srp(c, *c->seg_override) : c->ds;
	int d = (c->flags&V20_DF)?-1:1; int n=0;
	bool has_rep = rep||repne||repc||repnc;
	if (has_rep && !c->cx) return 5;
	do {
		alu(c, 7, MRB(ss,c->si), MRB(c->es,c->di), 0);
		c->si+=d; c->di+=d; n++;
		if(!has_rep) return 8;
		c->cx--;
		if (rep   && !(c->flags&V20_ZF)) return 5+2*n;
		if (repne &&  (c->flags&V20_ZF)) return 5+2*n;
		if (repc  && !(c->flags&V20_CF)) return 5+2*n;
		if (repnc &&  (c->flags&V20_CF)) return 5+2*n;
	} while(c->cx); return 5+2*n;
}
static int s_cmpsw(v20_t *c, bool rep, bool repne, bool repc, bool repnc) {
	uint16_t ss = c->seg_override ? *srp(c, *c->seg_override) : c->ds;
	int d = (c->flags&V20_DF)?-2:2; int n=0;
	bool has_rep = rep||repne||repc||repnc;
	if (has_rep && !c->cx) return 5;
	do {
		alu(c, 7, MRW(ss,c->si), MRW(c->es,c->di), 1);
		c->si+=d; c->di+=d; n++;
		if(!has_rep) return 10;
		c->cx--;
		if (rep   && !(c->flags&V20_ZF)) return 5+2*n;
		if (repne &&  (c->flags&V20_ZF)) return 5+2*n;
		if (repc  && !(c->flags&V20_CF)) return 5+2*n;
		if (repnc &&  (c->flags&V20_CF)) return 5+2*n;
	} while(c->cx); return 5+2*n;
}
static int s_scasb(v20_t *c, bool rep, bool repne, bool repc, bool repnc) {
	int d = (c->flags&V20_DF)?-1:1; int n=0;
	bool has_rep = rep||repne||repc||repnc;
	if (has_rep && !c->cx) return 5;
	do {
		alu(c, 7, c->al(), MRB(c->es,c->di), 0);
		c->di+=d; n++;
		if(!has_rep) return 6;
		c->cx--;
		if (rep   && !(c->flags&V20_ZF)) return 5+2*n;
		if (repne &&  (c->flags&V20_ZF)) return 5+2*n;
		if (repc  && !(c->flags&V20_CF)) return 5+2*n;
		if (repnc &&  (c->flags&V20_CF)) return 5+2*n;
	} while(c->cx); return 5+2*n;
}
static int s_scasw(v20_t *c, bool rep, bool repne, bool repc, bool repnc) {
	int d = (c->flags&V20_DF)?-2:2; int n=0;
	bool has_rep = rep||repne||repc||repnc;
	if (has_rep && !c->cx) return 5;
	do {
		alu(c, 7, c->ax, MRW(c->es,c->di), 1);
		c->di+=d; n++;
		if(!has_rep) return 8;
		c->cx--;
		if (rep   && !(c->flags&V20_ZF)) return 5+2*n;
		if (repne &&  (c->flags&V20_ZF)) return 5+2*n;
		if (repc  && !(c->flags&V20_CF)) return 5+2*n;
		if (repnc &&  (c->flags&V20_CF)) return 5+2*n;
	} while(c->cx); return 5+2*n;
}

/* ---- INS / OUTS string I/O ---- */

static int s_insb(v20_t *c, bool rep) {
	int d = (c->flags&V20_DF)?-1:1; int n=0;
	if (rep && !c->cx) return 5;
	do { MWB(c->es,c->di,c->io_read(c->ctx,c->dx)); c->di+=d; n++;
	     if(!rep) return 8; c->cx--; } while(c->cx); return 5+2*n;
}
static int s_insw(v20_t *c, bool rep) {
	int d = (c->flags&V20_DF)?-2:2; int n=0;
	if (rep && !c->cx) return 5;
	do { uint8_t lo=c->io_read(c->ctx,c->dx); uint8_t hi=c->io_read(c->ctx,(uint16_t)(c->dx+1));
	     MWW(c->es,c->di,(uint16_t)(lo|(hi<<8))); c->di+=d; n++;
	     if(!rep) return 8; c->cx--; } while(c->cx); return 5+2*n;
}
static int s_outsb(v20_t *c, bool rep) {
	uint16_t ss = c->seg_override ? *srp(c, *c->seg_override) : c->ds;
	int d = (c->flags&V20_DF)?-1:1; int n=0;
	if (rep && !c->cx) return 5;
	do { c->io_write(c->ctx,c->dx,MRB(ss,c->si)); c->si+=d; n++;
	     if(!rep) return 8; c->cx--; } while(c->cx); return 5+2*n;
}
static int s_outsw(v20_t *c, bool rep) {
	uint16_t ss = c->seg_override ? *srp(c, *c->seg_override) : c->ds;
	int d = (c->flags&V20_DF)?-2:2; int n=0;
	if (rep && !c->cx) return 5;
	do { uint16_t v=MRW(ss,c->si);
	     c->io_write(c->ctx,c->dx,(uint8_t)v); c->io_write(c->ctx,(uint16_t)(c->dx+1),(uint8_t)(v>>8));
	     c->si+=d; n++;
	     if(!rep) return 8; c->cx--; } while(c->cx); return 5+2*n;
}

static uint16_t flags_from_8080(uint8_t f);  // forward decl

/* ---- V20 extensions (0x0F prefix) ---- */

static void v20_bitop(v20_t *c, uint8_t op2)
{
	int w   = op2 & 1;
	int fn  = (op2 >> 1) & 3;
	int imm = op2 & 8;

	ea_t e = decode_ea(c);
	int bit;

	if (imm) {
		uint8_t ib = f8(c);
		bit = w ? (ib & 0xF) : (ib & 7);
	} else {
		bit = w ? (c->cl() & 0xF) : (c->cl() & 7);
	}

	if (w) {
		uint16_t v = ea_rw(c, &e);
		uint16_t mask = (uint16_t)(1 << bit);
		switch (fn) {
		case 0: /* TEST */
			c->flags &= ~(V20_CF|V20_OF|V20_ZF);
			if (!(v & mask)) c->flags |= V20_ZF;
			break;
		case 1: ea_ww(c, &e, v & ~mask); break;    /* CLR */
		case 2: ea_ww(c, &e, v | mask); break;     /* SET */
		case 3: ea_ww(c, &e, v ^ mask); break;     /* NOT */
		}
	} else {
		uint8_t v = ea_rb(c, &e);
		uint8_t mask = (uint8_t)(1 << bit);
		switch (fn) {
		case 0:
			c->flags &= ~(V20_CF|V20_OF|V20_ZF);
			if (!(v & mask)) c->flags |= V20_ZF;
			break;
		case 1: ea_wb(c, &e, v & ~mask); break;
		case 2: ea_wb(c, &e, v | mask); break;
		case 3: ea_wb(c, &e, v ^ mask); break;
		}
	}
}

static void v20_add4s(v20_t *c)
{
	int n = (c->cl() + 1) / 2;
	uint16_t ss = c->seg_override ? *srp(c, *c->seg_override) : c->ds;
	int carry = 0;

	for (int i = 0; i < n; i++) {
		uint8_t s = MRB(ss, (uint16_t)(c->si + i));
		uint8_t d = MRB(c->es, (uint16_t)(c->di + i));
		int lo = (d & 0x0F) + (s & 0x0F) + carry;
		carry = 0;
		if (lo > 9) { lo -= 10; carry = 1; }
		int hi = ((d >> 4) & 0x0F) + ((s >> 4) & 0x0F) + carry;
		carry = 0;
		if (hi > 9) { hi -= 10; carry = 1; }
		MWB(c->es, (uint16_t)(c->di + i), (uint8_t)((hi << 4) | lo));
	}

	c->flags &= ~(V20_CF | V20_ZF);
	if (carry) c->flags |= V20_CF;
	int nz = 0;
	for (int i = 0; i < n; i++) nz |= MRB(c->es, (uint16_t)(c->di + i));
	if (!nz) c->flags |= V20_ZF;
}

static void v20_sub4s(v20_t *c)
{
	int n = (c->cl() + 1) / 2;
	uint16_t ss = c->seg_override ? *srp(c, *c->seg_override) : c->ds;
	int borrow = 0;

	for (int i = 0; i < n; i++) {
		uint8_t s = MRB(ss, (uint16_t)(c->si + i));
		uint8_t d = MRB(c->es, (uint16_t)(c->di + i));
		int lo = (d & 0x0F) - (s & 0x0F) - borrow;
		borrow = 0;
		if (lo < 0) { lo += 10; borrow = 1; }
		int hi = ((d >> 4) & 0x0F) - ((s >> 4) & 0x0F) - borrow;
		borrow = 0;
		if (hi < 0) { hi += 10; borrow = 1; }
		MWB(c->es, (uint16_t)(c->di + i), (uint8_t)((hi << 4) | lo));
	}

	c->flags &= ~(V20_CF | V20_ZF);
	if (borrow) c->flags |= V20_CF;
	int nz = 0;
	for (int i = 0; i < n; i++) nz |= MRB(c->es, (uint16_t)(c->di + i));
	if (!nz) c->flags |= V20_ZF;
}

static void v20_cmp4s(v20_t *c)
{
	int n = (c->cl() + 1) / 2;
	uint16_t ss = c->seg_override ? *srp(c, *c->seg_override) : c->ds;
	int borrow = 0, nz = 0;

	for (int i = 0; i < n; i++) {
		uint8_t s = MRB(ss, (uint16_t)(c->si + i));
		uint8_t d = MRB(c->es, (uint16_t)(c->di + i));
		int lo = (d & 0x0F) - (s & 0x0F) - borrow;
		borrow = 0;
		if (lo < 0) { lo += 10; borrow = 1; }
		int hi = ((d >> 4) & 0x0F) - ((s >> 4) & 0x0F) - borrow;
		borrow = 0;
		if (hi < 0) { hi += 10; borrow = 1; }
		nz |= (hi << 4) | lo;
	}

	c->flags &= ~(V20_CF | V20_ZF);
	if (borrow) c->flags |= V20_CF;
	if (!nz) c->flags |= V20_ZF;
}

static void v20_rol4(v20_t *c)
{
	uint16_t ss = c->seg_override ? *srp(c, *c->seg_override) : c->ds;
	uint8_t mem = MRB(ss, c->si);
	uint8_t al_hi = c->al() >> 4;
	c->al() = (uint8_t)((c->al() << 4) | (mem >> 4));
	MWB(ss, c->si, (uint8_t)((mem << 4) | al_hi));
}

static void v20_ror4(v20_t *c)
{
	uint16_t ss = c->seg_override ? *srp(c, *c->seg_override) : c->ds;
	uint8_t mem = MRB(ss, c->si);
	uint8_t al_lo = c->al() & 0x0F;
	c->al() = (uint8_t)((mem << 4) | (c->al() >> 4));
	MWB(ss, c->si, (uint8_t)((al_lo << 4) | (mem >> 4)));
}

static void exec_0f(v20_t *c)
{
	uint8_t op2 = f8(c);

	if (op2 >= 0x10 && op2 <= 0x1F) {
		v20_bitop(c, op2);
		return;
	}

	switch (op2) {
	case 0x20: v20_add4s(c); break;
	case 0x22: v20_sub4s(c); break;
	case 0x26: v20_cmp4s(c); break;
	case 0x28: v20_rol4(c); break;
	case 0x2A: v20_ror4(c); break;
	case 0x31: { /* INS (bit field insert) */
		ea_t e = decode_ea(c);
		int bitpos = c->cl() & 0x0F;
		int width = ((c->cl() >> 4) & 0x0F) + 1;
		uint16_t src = *r16p(c, e.reg);
		uint16_t dst = ea_rw(c, &e);
		uint16_t mask = (uint16_t)(((1U << width) - 1) << bitpos);
		dst = (uint16_t)((dst & ~mask) | ((src << bitpos) & mask));
		ea_ww(c, &e, dst);
		break;
	}
	case 0x33: { /* EXT (bit field extract) */
		ea_t e = decode_ea(c);
		int bitpos = c->cl() & 0x0F;
		int width = ((c->cl() >> 4) & 0x0F) + 1;
		uint16_t src = ea_rw(c, &e);
		*r16p(c, e.reg) = (uint16_t)((src >> bitpos) & ((1U << width) - 1));
		break;
	}
	case 0xFD: { /* RETEM - return from emulation to 8080 mode */
		/* Pop PC, PS, PSW from 8080 stack (BP) and re-enter 8080 mode */
		uint16_t pc_val = MRW(c->ds, c->bp); c->bp += 2;
		uint16_t ps_val = MRW(c->ds, c->bp); c->bp += 2;
		uint16_t psw = MRW(c->ds, c->bp); c->bp += 2;
		c->ip = pc_val;
		c->cs = ps_val;
		c->flags = flags_from_8080((uint8_t)psw);
		c->al() = (uint8_t)(psw >> 8);
		c->mode_8080 = true;
		break;
	}
	case 0xFF: { /* BRKEM - enter 8080 emulation mode */
		uint8_t vec = f8(c);
		do_int(c, vec);
		c->mode_8080 = true;
		break;
	}
	default:
		fprintf(stderr, "UNIMPL 0F %02X at %04X:%04X\n", op2, c->cs, (uint16_t)(c->ip-2));
		break;
	}
}

/* ---- conditional jump helper ---- */

static bool jcc(v20_t *c, int cc) {
	switch (cc) {
	case 0x0: return !!(c->flags & V20_OF);
	case 0x1: return  !(c->flags & V20_OF);
	case 0x2: return !!(c->flags & V20_CF);
	case 0x3: return  !(c->flags & V20_CF);
	case 0x4: return !!(c->flags & V20_ZF);
	case 0x5: return  !(c->flags & V20_ZF);
	case 0x6: return !!(c->flags & (V20_CF|V20_ZF));
	case 0x7: return  !(c->flags & (V20_CF|V20_ZF));
	case 0x8: return !!(c->flags & V20_SF);
	case 0x9: return  !(c->flags & V20_SF);
	case 0xa: return !!(c->flags & V20_PF);
	case 0xb: return  !(c->flags & V20_PF);
	case 0xc: return !!(c->flags & V20_SF) != !!(c->flags & V20_OF);
	case 0xd: return !!(c->flags & V20_SF) == !!(c->flags & V20_OF);
	case 0xe: return (c->flags & V20_ZF) || (!!(c->flags&V20_SF)!=!!(c->flags&V20_OF));
	case 0xf: return !(c->flags & V20_ZF) && (!!(c->flags&V20_SF)==!!(c->flags&V20_OF));
	}
	return false;
}

/* ==== main dispatch ==== */

static int exec_one(v20_t *c)
{
	std::optional<int> sov;
	bool rep = false, repne = false;
	bool repc = false, repnc = false;
	uint8_t op;
	int cyc = 0;

pfx:
	op = f8(c);
	switch (op) {
	case 0x26: sov=0; cyc+=2; goto pfx;
	case 0x2e: sov=1; cyc+=2; goto pfx;
	case 0x36: sov=2; cyc+=2; goto pfx;
	case 0x3e: sov=3; cyc+=2; goto pfx;
	case 0xf0: cyc+=2; goto pfx;
	case 0xf2: repne=true; rep=false; repc=false; repnc=false; cyc+=2; goto pfx;
	case 0xf3: rep=true; repne=false; repc=false; repnc=false; cyc+=2; goto pfx;
	case 0x64: repnc=true; rep=false; repne=false; repc=false; cyc+=2; goto pfx;
	case 0x65: repc=true; rep=false; repne=false; repnc=false; cyc+=2; goto pfx;
	default: break;
	}
	c->seg_override = sov;

	switch (op) {

	/* ---- ALU r/m,reg  /  reg,r/m  /  AL/AX,imm ---- */
	case 0x00: case 0x01: case 0x02: case 0x03: case 0x04: case 0x05:
	case 0x08: case 0x09: case 0x0a: case 0x0b: case 0x0c: case 0x0d:
	case 0x10: case 0x11: case 0x12: case 0x13: case 0x14: case 0x15:
	case 0x18: case 0x19: case 0x1a: case 0x1b: case 0x1c: case 0x1d:
	case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: case 0x25:
	case 0x28: case 0x29: case 0x2a: case 0x2b: case 0x2c: case 0x2d:
	case 0x30: case 0x31: case 0x32: case 0x33: case 0x34: case 0x35:
	case 0x38: case 0x39: case 0x3a: case 0x3b: case 0x3c: case 0x3d: {
		int aop = (op>>3)&7, sub = op&7, w = sub&1;
		if (sub < 4) {
			ea_t e = decode_ea(c);
			if (sub & 2) {
				if (w) { uint16_t r=(uint16_t)alu(c,aop,*r16p(c,e.reg),ea_rw(c,&e),1); if(aop!=7) *r16p(c,e.reg)=r; }
				else   { uint8_t  r=(uint8_t)alu(c,aop,gr8(c,e.reg),ea_rb(c,&e),0);   if(aop!=7) sr8(c,e.reg,r); }
				cyc += (e.mod==3) ? 2 : (w ? 15 : 11);
			} else {
				if (w) { uint16_t r=(uint16_t)alu(c,aop,ea_rw(c,&e),*r16p(c,e.reg),1); if(aop!=7) ea_ww(c,&e,r); }
				else   { uint8_t  r=(uint8_t)alu(c,aop,ea_rb(c,&e),gr8(c,e.reg),0);   if(aop!=7) ea_wb(c,&e,r); }
				cyc += (e.mod==3) ? 2 : (w ? 24 : 16);
			}
		} else if (sub==4) {
			uint8_t r=(uint8_t)alu(c,aop,c->al(),f8(c),0); if(aop!=7) c->al()=r; cyc+=4;
		} else {
			uint16_t r=(uint16_t)alu(c,aop,c->ax,f16(c),1); if(aop!=7) c->ax=r; cyc+=4;
		}
		break;
	}

	/* ---- PUSH/POP segment ---- */
	case 0x06: push(c, c->es); cyc+=12; break;
	case 0x07: c->es = pop(c); cyc+=12; break;
	case 0x0e: push(c, c->cs); cyc+=12; break;
	case 0x0f: exec_0f(c); cyc+=12; break;
	case 0x16: push(c, c->ss); cyc+=12; break;
	case 0x17: c->ss = pop(c); cyc+=12; break;
	case 0x1e: push(c, c->ds); cyc+=12; break;
	case 0x1f: c->ds = pop(c); cyc+=12; break;

	/* ---- BCD ---- */
	case 0x27: { /* DAA */
		uint8_t oa = c->al(); int ocf = c->flags&V20_CF;
		c->flags &= ~V20_CF;
		if ((c->al()&0x0F)>9 || (c->flags&V20_AF)) {
			c->al()+=6; c->flags|=V20_AF;
			if (ocf || c->al() < oa) c->flags|=V20_CF;
		} else c->flags&=~V20_AF;
		if (oa>0x99 || ocf) { c->al()+=0x60; c->flags|=V20_CF; }
		c->flags &= ~(V20_SF|V20_ZF|V20_PF);
		if (!c->al()) c->flags|=V20_ZF; if (c->al()&0x80) c->flags|=V20_SF;
		if (ptab[c->al()]) c->flags|=V20_PF;
		cyc+=10; break;
	}
	case 0x2f: { /* DAS */
		uint8_t oa = c->al(); int ocf = c->flags&V20_CF;
		c->flags &= ~V20_CF;
		if ((c->al()&0x0F)>9 || (c->flags&V20_AF)) {
			c->al()-=6; c->flags|=V20_AF;
			if (ocf || c->al() > oa) c->flags|=V20_CF;
		} else c->flags&=~V20_AF;
		if (oa>0x99 || ocf) { c->al()-=0x60; c->flags|=V20_CF; }
		c->flags &= ~(V20_SF|V20_ZF|V20_PF);
		if (!c->al()) c->flags|=V20_ZF; if (c->al()&0x80) c->flags|=V20_SF;
		if (ptab[c->al()]) c->flags|=V20_PF;
		cyc+=10; break;
	}
	case 0x37: /* AAA */
		if ((c->al()&0x0F)>9 || (c->flags&V20_AF)) {
			c->al()+=6; c->ah()++; c->flags|=V20_AF|V20_CF;
		} else { c->flags&=~(V20_AF|V20_CF); }
		c->al() &= 0x0F;
		cyc+=7; break;
	case 0x3f: /* AAS */
		if ((c->al()&0x0F)>9 || (c->flags&V20_AF)) {
			c->al()-=6; c->ah()--; c->flags|=V20_AF|V20_CF;
		} else { c->flags&=~(V20_AF|V20_CF); }
		c->al() &= 0x0F;
		cyc+=7; break;

	/* ---- INC reg16 ---- */
	case 0x40: case 0x41: case 0x42: case 0x43:
	case 0x44: case 0x45: case 0x46: case 0x47:
		inc16(c, r16p(c, op&7)); cyc+=2; break;

	/* ---- DEC reg16 ---- */
	case 0x48: case 0x49: case 0x4a: case 0x4b:
	case 0x4c: case 0x4d: case 0x4e: case 0x4f:
		dec16(c, r16p(c, op&7)); cyc+=2; break;

	/* ---- PUSH reg16 ---- */
	case 0x50: case 0x51: case 0x52: case 0x53:
	case 0x54: case 0x55: case 0x56: case 0x57:
		push(c, *r16p(c, op&7)); cyc+=12; break;

	/* ---- POP reg16 ---- */
	case 0x58: case 0x59: case 0x5a: case 0x5b:
	case 0x5c: case 0x5d: case 0x5e: case 0x5f:
		*r16p(c, op&7) = pop(c); cyc+=12; break;

	/* ---- PUSHA / POPA ---- */
	case 0x60: { uint16_t t=c->sp; for(int i=0;i<8;i++) push(c,(i==4)?t:*r16p(c,i)); cyc+=36; break; }
	case 0x61: { for(int i=7;i>=0;i--) { uint16_t v=pop(c); if(i!=4) *r16p(c,i)=v; } cyc+=40; break; }

	/* ---- BOUND ---- */
	case 0x62: {
		ea_t e=decode_ea(c);
		int16_t idx=(int16_t)*r16p(c,e.reg);
		int16_t lo=(int16_t)MRW(e.seg,e.off);
		int16_t hi=(int16_t)MRW(e.seg,(uint16_t)(e.off+2));
		if(idx<lo||idx>hi) do_int(c,5);
		cyc+=13; break;
	}

	/* ---- PUSH imm ---- */
	case 0x68: push(c, f16(c)); cyc+=12; break;
	case 0x6a: push(c, (uint16_t)(int16_t)(int8_t)f8(c)); cyc+=11; break;

	/* ---- IMUL imm ---- */
	case 0x69: { ea_t e=decode_ea(c); int32_t a=(int16_t)ea_rw(c,&e); int32_t b=(int16_t)f16(c);
		int32_t r=a*b; *r16p(c,e.reg)=(uint16_t)r;
		c->flags &= ~(V20_CF|V20_OF); if(r!=(int16_t)r) c->flags|=V20_CF|V20_OF;
		cyc+=(e.mod==3)?42:52; break; }
	case 0x6b: { ea_t e=decode_ea(c); int32_t a=(int16_t)ea_rw(c,&e); int32_t b=(int8_t)f8(c);
		int32_t r=a*b; *r16p(c,e.reg)=(uint16_t)r;
		c->flags &= ~(V20_CF|V20_OF); if(r!=(int16_t)r) c->flags|=V20_CF|V20_OF;
		cyc+=(e.mod==3)?34:44; break; }

	/* ---- INS / OUTS ---- */
	case 0x6c: cyc+=s_insb(c, rep); break;
	case 0x6d: cyc+=s_insw(c, rep); break;
	case 0x6e: cyc+=s_outsb(c, rep); break;
	case 0x6f: cyc+=s_outsw(c, rep); break;

	/* ---- Jcc ---- */
	case 0x70: case 0x71: case 0x72: case 0x73:
	case 0x74: case 0x75: case 0x76: case 0x77:
	case 0x78: case 0x79: case 0x7a: case 0x7b:
	case 0x7c: case 0x7d: case 0x7e: case 0x7f: {
		int8_t d = (int8_t)f8(c);
		if (jcc(c, op&0xF)) { c->ip += d; cyc+=14; } else { cyc+=4; }
		break;
	}

	/* ---- ALU r/m, imm ---- */
	case 0x80: case 0x81: case 0x82: case 0x83: {
		ea_t e = decode_ea(c);
		int w = op & 1;
		uint32_t imm;
		if (op==0x81) imm=f16(c);
		else if (op==0x83) imm=(uint16_t)(int16_t)(int8_t)f8(c);
		else imm=f8(c);
		if (w) { uint16_t r=(uint16_t)alu(c,e.reg,ea_rw(c,&e),imm,1); if(e.reg!=7) ea_ww(c,&e,r); }
		else   { uint8_t  r=(uint8_t)alu(c,e.reg,ea_rb(c,&e),imm,0); if(e.reg!=7) ea_wb(c,&e,r); }
		cyc += (e.mod==3) ? 4 : (w ? 26 : 18);
		break;
	}

	/* ---- TEST r/m, r ---- */
	case 0x84: { ea_t e=decode_ea(c); alu(c,4,ea_rb(c,&e),gr8(c,e.reg),0); cyc+=(e.mod==3)?2:10; break; }
	case 0x85: { ea_t e=decode_ea(c); alu(c,4,ea_rw(c,&e),*r16p(c,e.reg),1); cyc+=(e.mod==3)?2:14; break; }

	/* ---- XCHG r/m, r ---- */
	case 0x86: { ea_t e=decode_ea(c); uint8_t t=ea_rb(c,&e); ea_wb(c,&e,gr8(c,e.reg)); sr8(c,e.reg,t); cyc+=(e.mod==3)?3:16; break; }
	case 0x87: { ea_t e=decode_ea(c); uint16_t t=ea_rw(c,&e); ea_ww(c,&e,*r16p(c,e.reg)); *r16p(c,e.reg)=t; cyc+=(e.mod==3)?3:24; break; }

	/* ---- MOV r/m, r ---- */
	case 0x88: { ea_t e=decode_ea(c); ea_wb(c,&e,gr8(c,e.reg)); cyc+=(e.mod==3)?2:9; break; }
	case 0x89: { ea_t e=decode_ea(c); ea_ww(c,&e,*r16p(c,e.reg)); cyc+=(e.mod==3)?2:13; break; }
	case 0x8a: { ea_t e=decode_ea(c); sr8(c,e.reg,ea_rb(c,&e)); cyc+=(e.mod==3)?2:11; break; }
	case 0x8b: { ea_t e=decode_ea(c); *r16p(c,e.reg)=ea_rw(c,&e); cyc+=(e.mod==3)?2:15; break; }

	/* ---- MOV r/m, sreg / LEA / MOV sreg, r/m ---- */
	case 0x8c: { ea_t e=decode_ea(c); ea_ww(c,&e,*srp(c,e.reg&3)); cyc+=(e.mod==3)?2:15; break; }
	case 0x8d: { ea_t e=decode_ea(c); *r16p(c,e.reg)=e.off; cyc+=4; break; }
	case 0x8e: { ea_t e=decode_ea(c); *srp(c,e.reg&3)=ea_rw(c,&e); cyc+=(e.mod==3)?2:15; break; }

	/* ---- POP r/m ---- */
	case 0x8f: { ea_t e=decode_ea(c); ea_ww(c,&e,pop(c)); cyc+=21; break; }

	/* ---- NOP / XCHG AX, reg ---- */
	case 0x90: cyc+=3; break;
	case 0x91: case 0x92: case 0x93: case 0x94:
	case 0x95: case 0x96: case 0x97:
		{ uint16_t t=c->ax; c->ax=*r16p(c,op&7); *r16p(c,op&7)=t; cyc+=3; break; }

	case 0x98: c->ax = (uint16_t)(int16_t)(int8_t)c->al(); cyc+=2; break; /* CBW */
	case 0x99: c->dx = (c->ax & 0x8000) ? 0xFFFF : 0; cyc+=5; break; /* CWD */

	/* ---- CALL far ---- */
	case 0x9a: { uint16_t o=f16(c); uint16_t s=f16(c); push(c,c->cs); push(c,c->ip); c->ip=o; c->cs=s; cyc+=28; break; }

	case 0x9b: cyc+=3; break; /* WAIT */

	/* ---- PUSHF / POPF ---- */
	case 0x9c: push(c, (c->flags & 0x0FD5) | 0xF002); cyc+=12; break;
	case 0x9d: c->flags = (pop(c) & 0x0FD5) | 0x0002; cyc+=12; break;

	/* ---- SAHF / LAHF ---- */
	case 0x9e: c->flags = (c->flags & 0xFF00) | (c->ah() & 0xD5) | 0x02; cyc+=3; break;
	case 0x9f: c->ah() = (uint8_t)((c->flags & 0xD5) | 0x02); cyc+=3; break;

	/* ---- MOV AL/AX, [addr] ---- */
	case 0xa0: { uint16_t a=f16(c); uint16_t s=sov ? *srp(c, *sov) : c->ds; c->al()=MRB(s,a); cyc+=10; break; }
	case 0xa1: { uint16_t a=f16(c); uint16_t s=sov ? *srp(c, *sov) : c->ds; c->ax=MRW(s,a); cyc+=10; break; }
	case 0xa2: { uint16_t a=f16(c); uint16_t s=sov ? *srp(c, *sov) : c->ds; MWB(s,a,c->al()); cyc+=10; break; }
	case 0xa3: { uint16_t a=f16(c); uint16_t s=sov ? *srp(c, *sov) : c->ds; MWW(s,a,c->ax); cyc+=10; break; }

	/* ---- string ops ---- */
	case 0xa4: cyc+=s_movsb(c, rep); break;
	case 0xa5: cyc+=s_movsw(c, rep); break;
	case 0xa6: cyc+=s_cmpsb(c, rep, repne, repc, repnc); break;
	case 0xa7: cyc+=s_cmpsw(c, rep, repne, repc, repnc); break;

	/* ---- TEST AL/AX, imm ---- */
	case 0xa8: alu(c,4,c->al(),f8(c),0); cyc+=4; break;
	case 0xa9: alu(c,4,c->ax,f16(c),1); cyc+=4; break;

	case 0xaa: cyc+=s_stosb(c, rep); break;
	case 0xab: cyc+=s_stosw(c, rep); break;
	case 0xac: cyc+=s_lodsb(c, rep); break;
	case 0xad: cyc+=s_lodsw(c, rep); break;
	case 0xae: cyc+=s_scasb(c, rep, repne, repc, repnc); break;
	case 0xaf: cyc+=s_scasw(c, rep, repne, repc, repnc); break;

	/* ---- MOV reg8, imm8 ---- */
	case 0xb0: case 0xb1: case 0xb2: case 0xb3:
	case 0xb4: case 0xb5: case 0xb6: case 0xb7:
		sr8(c, op&7, f8(c)); cyc+=4; break;

	/* ---- MOV reg16, imm16 ---- */
	case 0xb8: case 0xb9: case 0xba: case 0xbb:
	case 0xbc: case 0xbd: case 0xbe: case 0xbf:
		*r16p(c, op&7) = f16(c); cyc+=4; break;

	/* ---- shift r/m, imm8 (186) ---- */
	case 0xc0: { ea_t e=decode_ea(c); uint8_t n=f8(c); ea_wb(c,&e,(uint8_t)shf(c,e.reg,ea_rb(c,&e),n,0)); cyc+=(e.mod==3?3:24)+(n&0x1f); break; }
	case 0xc1: { ea_t e=decode_ea(c); uint8_t n=f8(c); ea_ww(c,&e,(uint16_t)shf(c,e.reg,ea_rw(c,&e),n,1)); cyc+=(e.mod==3?3:24)+(n&0x1f); break; }

	/* ---- RET near ---- */
	case 0xc2: { uint16_t n=f16(c); c->ip=pop(c); c->sp+=n; cyc+=12; break; }
	case 0xc3: c->ip = pop(c); cyc+=8; break;

	/* ---- LES / LDS ---- */
	case 0xc4: { ea_t e=decode_ea(c); *r16p(c,e.reg)=MRW(e.seg,e.off); c->es=MRW(e.seg,(uint16_t)(e.off+2)); cyc+=21; break; }
	case 0xc5: { ea_t e=decode_ea(c); *r16p(c,e.reg)=MRW(e.seg,e.off); c->ds=MRW(e.seg,(uint16_t)(e.off+2)); cyc+=21; break; }

	/* ---- MOV r/m, imm ---- */
	case 0xc6: { ea_t e=decode_ea(c); ea_wb(c,&e,f8(c)); cyc+=(e.mod==3)?4:13; break; }
	case 0xc7: { ea_t e=decode_ea(c); ea_ww(c,&e,f16(c)); cyc+=(e.mod==3)?4:13; break; }

	/* ---- ENTER / LEAVE ---- */
	case 0xc8: {
		uint16_t sz=f16(c); uint8_t lv=f8(c)&0x1F;
		push(c, c->bp);
		uint16_t fr = c->sp;
		for (int i=1; i<lv; i++) { c->bp-=2; push(c, MRW(c->ss,c->bp)); }
		if (lv) push(c, fr);
		c->bp = fr;
		c->sp -= sz;
		cyc += 15 + 12*lv;
		break;
	}
	case 0xc9: c->sp=c->bp; c->bp=pop(c); cyc+=8; break;

	/* ---- RET far ---- */
	case 0xca: { uint16_t n=f16(c); c->ip=pop(c); c->cs=pop(c); c->sp+=n; cyc+=28; break; }
	case 0xcb: c->ip=pop(c); c->cs=pop(c); cyc+=20; break;

	/* ---- INT ---- */
	case 0xcc: do_int(c, 3); cyc+=52; break;
	case 0xcd: do_int(c, f8(c)); cyc+=52; break;
	case 0xce: if (c->flags & V20_OF) { do_int(c, 4); cyc+=52; } else { cyc+=4; } break;

	/* ---- IRET ---- */
	case 0xcf: {
		c->ip=pop(c); c->cs=pop(c); uint16_t f=pop(c);
		// MD flag (bit 15): if clear, re-enter 8080 emulation mode
		c->mode_8080 = !(f & 0x8000);
		c->flags = f & 0x0FFF;
		cyc+=32; break;
	}

	/* ---- shifts r/m, 1 / CL ---- */
	case 0xd0: { ea_t e=decode_ea(c); ea_wb(c,&e,(uint8_t)shf(c,e.reg,ea_rb(c,&e),1,0)); cyc+=(e.mod==3)?3:24; break; }
	case 0xd1: { ea_t e=decode_ea(c); ea_ww(c,&e,(uint16_t)shf(c,e.reg,ea_rw(c,&e),1,1)); cyc+=(e.mod==3)?3:24; break; }
	case 0xd2: { ea_t e=decode_ea(c); ea_wb(c,&e,(uint8_t)shf(c,e.reg,ea_rb(c,&e),c->cl(),0)); cyc+=(e.mod==3?3:24)+(c->cl()&0x1f); break; }
	case 0xd3: { ea_t e=decode_ea(c); ea_ww(c,&e,(uint16_t)shf(c,e.reg,ea_rw(c,&e),c->cl(),1)); cyc+=(e.mod==3?3:24)+(c->cl()&0x1f); break; }

	/* ---- AAM / AAD ---- */
	case 0xd4: { uint8_t b=f8(c); if(b) { c->ah()=c->al()/b; c->al()=c->al()%b; }
		c->flags &= ~(V20_SF|V20_ZF|V20_PF);
		if(!c->ax) c->flags|=V20_ZF; if(c->al()&0x80) c->flags|=V20_SF;
		if(ptab[c->al()]) c->flags|=V20_PF; cyc+=19; break; }
	case 0xd5: { uint8_t b=f8(c); c->al()=(uint8_t)(c->ah()*b+c->al()); c->ah()=0;
		c->flags &= ~(V20_SF|V20_ZF|V20_PF);
		if(!c->al()) c->flags|=V20_ZF; if(c->al()&0x80) c->flags|=V20_SF;
		if(ptab[c->al()]) c->flags|=V20_PF; cyc+=10; break; }

	case 0xd6: c->al() = (c->flags & V20_CF) ? 0xFF : 0x00; cyc+=3; break; /* SALC */

	/* ---- XLAT ---- */
	case 0xd7: { uint16_t s=sov ? *srp(c, *sov) : c->ds; c->al()=MRB(s,(uint16_t)(c->bx+c->al())); cyc+=10; break; }

	/* ---- ESC (FPU, ignore) ---- */
	case 0xd8: case 0xd9: case 0xda: case 0xdb:
	case 0xdc: case 0xdd: case 0xde: case 0xdf:
		{ [[maybe_unused]] ea_t e=decode_ea(c); cyc+=2; break; }

	/* ---- LOOP / JCXZ ---- */
	case 0xe0: { int8_t d=(int8_t)f8(c); c->cx--; if(c->cx && !(c->flags&V20_ZF)) c->ip+=d; cyc+=14; break; }
	case 0xe1: { int8_t d=(int8_t)f8(c); c->cx--; if(c->cx &&  (c->flags&V20_ZF)) c->ip+=d; cyc+=14; break; }
	case 0xe2: { int8_t d=(int8_t)f8(c); c->cx--; if(c->cx) c->ip+=d; cyc+=14; break; }
	case 0xe3: { int8_t d=(int8_t)f8(c); if(!c->cx) c->ip+=d; cyc+=13; break; }

	/* ---- IN / OUT imm8 ---- */
	case 0xe4: c->al() = c->io_read(c->ctx, f8(c)); cyc+=10; break;
	case 0xe5: { uint16_t p=f8(c); c->al()=c->io_read(c->ctx,p); c->ah()=c->io_read(c->ctx,p+1); cyc+=10; break; }
	case 0xe6: c->io_write(c->ctx, f8(c), c->al()); cyc+=10; break;
	case 0xe7: { uint16_t p=f8(c); c->io_write(c->ctx,p,c->al()); c->io_write(c->ctx,p+1,c->ah()); cyc+=10; break; }

	/* ---- CALL / JMP near ---- */
	case 0xe8: { int16_t d=(int16_t)f16(c); push(c,c->ip); c->ip+=d; cyc+=19; break; }
	case 0xe9: { int16_t d=(int16_t)f16(c); c->ip+=d; cyc+=10; break; }
	case 0xea: { uint16_t o=f16(c); uint16_t s=f16(c); c->ip=o; c->cs=s; cyc+=28; break; }
	case 0xeb: { int8_t d=(int8_t)f8(c); c->ip+=d; cyc+=10; break; }

	/* ---- IN / OUT DX ---- */
	case 0xec: c->al() = c->io_read(c->ctx, c->dx); cyc+=8; break;
	case 0xed: c->al()=c->io_read(c->ctx,c->dx); c->ah()=c->io_read(c->ctx,c->dx+1); cyc+=8; break;
	case 0xee: c->io_write(c->ctx, c->dx, c->al()); cyc+=8; break;
	case 0xef: c->io_write(c->ctx,c->dx,c->al()); c->io_write(c->ctx,c->dx+1,c->ah()); cyc+=8; break;

	/* ---- HLT / CMC ---- */
	case 0xf4: c->halted = true; cyc+=2; break;
	case 0xf5: c->flags ^= V20_CF; cyc+=2; break;

	/* ---- Group 3: TEST/NOT/NEG/MUL/IMUL/DIV/IDIV ---- */
	case 0xf6: case 0xf7: {
		ea_t e = decode_ea(c);
		int w = op & 1;
		int m = (e.mod==3) ? 0 : 1;
		switch (e.reg) {
		case 0: case 1:
			if (w) alu(c,4,ea_rw(c,&e),f16(c),1);
			else   alu(c,4,ea_rb(c,&e),f8(c),0);
			cyc += m ? (w?14:10) : 4;
			break;
		case 2:
			if (w) ea_ww(c,&e,~ea_rw(c,&e));
			else   ea_wb(c,&e,~ea_rb(c,&e));
			cyc += m ? (w?24:16) : 2;
			break;
		case 3:
			if (w) { uint16_t v=ea_rw(c,&e); ea_ww(c,&e,(uint16_t)alu(c,5,0,v,1)); if(v) c->flags|=V20_CF; }
			else   { uint8_t  v=ea_rb(c,&e); ea_wb(c,&e,(uint8_t)alu(c,5,0,v,0)); if(v) c->flags|=V20_CF; }
			cyc += m ? (w?24:16) : 2;
			break;
		case 4: /* MUL */
			if (w) { uint32_t r=(uint32_t)c->ax*ea_rw(c,&e); c->ax=(uint16_t)r; c->dx=r>>16;
				c->flags&=~(V20_CF|V20_OF); if(c->dx) c->flags|=V20_CF|V20_OF; }
			else   { uint16_t r=(uint16_t)c->al()*ea_rb(c,&e); c->ax=r;
				c->flags&=~(V20_CF|V20_OF); if(c->ah()) c->flags|=V20_CF|V20_OF; }
			cyc += m ? 43 : 32;
			break;
		case 5: /* IMUL */
			if (w) { int32_t r=(int32_t)(int16_t)c->ax*(int16_t)ea_rw(c,&e); c->ax=(uint16_t)r; c->dx=(uint16_t)(r>>16);
				c->flags&=~(V20_CF|V20_OF); if(r!=(int16_t)r) c->flags|=V20_CF|V20_OF; }
			else   { int16_t r=(int16_t)(int8_t)c->al()*(int8_t)ea_rb(c,&e); c->ax=(uint16_t)r;
				c->flags&=~(V20_CF|V20_OF); if(r!=(int8_t)r) c->flags|=V20_CF|V20_OF; }
			cyc += m ? 43 : 32;
			break;
		case 6: /* DIV */
			if (w) { uint32_t n=((uint32_t)c->dx<<16)|c->ax; uint16_t d=ea_rw(c,&e);
				if(!d||n/d>0xFFFF){do_int(c,0);break;} c->ax=(uint16_t)(n/d); c->dx=(uint16_t)(n%d); }
			else   { uint16_t n=c->ax; uint8_t d=ea_rb(c,&e);
				if(!d||n/d>0xFF){do_int(c,0);break;} c->al()=(uint8_t)(n/d); c->ah()=(uint8_t)(n%d); }
			cyc += m ? (w?47:29) : (w?35:19);
			break;
		case 7: /* IDIV */
			if (w) { int32_t n=(int32_t)(((uint32_t)c->dx<<16)|c->ax); int16_t d=(int16_t)ea_rw(c,&e);
				if(!d||(n==INT32_MIN&&d==-1)){do_int(c,0);break;} int32_t q=n/d;
				if(q!=(int16_t)q){do_int(c,0);break;} c->ax=(uint16_t)q; c->dx=(uint16_t)(n%d); }
			else   { int16_t n=(int16_t)c->ax; int8_t d=(int8_t)ea_rb(c,&e);
				if(!d){do_int(c,0);break;} int16_t q=n/d;
				if(q!=(int8_t)q){do_int(c,0);break;} c->al()=(uint8_t)q; c->ah()=(uint8_t)(n%d); }
			cyc += m ? (w?51:33) : (w?39:23);
			break;
		}
		break;
	}

	/* ---- flag control ---- */
	case 0xf8: c->flags &= ~V20_CF; cyc+=2; break;
	case 0xf9: c->flags |=  V20_CF; cyc+=2; break;
	case 0xfa: c->flags &= ~V20_IF; cyc+=2; break;
	case 0xfb: c->flags |=  V20_IF; cyc+=2; break;
	case 0xfc: c->flags &= ~V20_DF; cyc+=2; break;
	case 0xfd: c->flags |=  V20_DF; cyc+=2; break;

	/* ---- Group 4: INC/DEC r/m8 ---- */
	case 0xfe: {
		ea_t e = decode_ea(c);
		uint8_t v = ea_rb(c, &e);
		uint16_t f = c->flags & ~(V20_PF|V20_AF|V20_ZF|V20_SF|V20_OF);
		uint8_t r;
		if (e.reg == 0) {
			r = v + 1;
			if ((v^1^r) & 0x10) f |= V20_AF;
			if (v == 0x7F) f |= V20_OF;
		} else {
			r = v - 1;
			if ((v^1^r) & 0x10) f |= V20_AF;
			if (v == 0x80) f |= V20_OF;
		}
		if (!r) f |= V20_ZF;
		if (r & 0x80) f |= V20_SF;
		if (ptab[r]) f |= V20_PF;
		c->flags = f;
		ea_wb(c, &e, r);
		cyc += (e.mod==3) ? 2 : 16;
		break;
	}

	/* ---- Group 5: INC/DEC/CALL/JMP/PUSH r/m16 ---- */
	case 0xff: {
		ea_t e = decode_ea(c);
		switch (e.reg) {
		case 0: { uint16_t v=ea_rw(c,&e); uint16_t f=c->flags&~(V20_PF|V20_AF|V20_ZF|V20_SF|V20_OF);
			uint16_t r=v+1;
			if((v^1^r)&0x10) f|=V20_AF; if(v==0x7FFF) f|=V20_OF;
			if(!r) f|=V20_ZF; if(r&0x8000) f|=V20_SF; if(ptab[r&0xFF]) f|=V20_PF;
			c->flags=f; ea_ww(c,&e,r); cyc+=(e.mod==3)?2:24; break; }
		case 1: { uint16_t v=ea_rw(c,&e); uint16_t f=c->flags&~(V20_PF|V20_AF|V20_ZF|V20_SF|V20_OF);
			uint16_t r=v-1;
			if((v^1^r)&0x10) f|=V20_AF; if(v==0x8000) f|=V20_OF;
			if(!r) f|=V20_ZF; if(r&0x8000) f|=V20_SF; if(ptab[r&0xFF]) f|=V20_PF;
			c->flags=f; ea_ww(c,&e,r); cyc+=(e.mod==3)?2:24; break; }
		case 2: push(c,c->ip); c->ip=ea_rw(c,&e); cyc+=16; break;
		case 3: push(c,c->cs); push(c,c->ip); c->ip=MRW(e.seg,e.off); c->cs=MRW(e.seg,(uint16_t)(e.off+2)); cyc+=29; break;
		case 4: c->ip=ea_rw(c,&e); cyc+=11; break;
		case 5: c->ip=MRW(e.seg,e.off); c->cs=MRW(e.seg,(uint16_t)(e.off+2)); cyc+=28; break;
		case 6: push(c, ea_rw(c,&e)); cyc+=21; break;
		default: break;
		}
		break;
	}

	default:
		fprintf(stderr, "UNIMPL %02X at %04X:%04X\n", op, c->cs, (uint16_t)(c->ip-1));
		c->halted = true;
		cyc += 2;
		break;
	}
	return cyc ? cyc : 4;
}

/* ---- 8080 emulation mode ---- */

// 8080 flag byte: bit 7=S, 6=Z, 4=AC, 2=P, 1=always 1, 0=CY
static uint8_t flags_to_8080(uint16_t f)
{
	uint8_t r = 0x02; // bit 1 always set
	if (f & V20_CF) r |= 0x01;
	if (f & V20_PF) r |= 0x04;
	if (f & V20_AF) r |= 0x10;
	if (f & V20_ZF) r |= 0x40;
	if (f & V20_SF) r |= 0x80;
	return r;
}

static uint16_t flags_from_8080(uint8_t f)
{
	uint16_t r = 0x0002; // bit 1 always set in x86 flags too
	if (f & 0x01) r |= V20_CF;
	if (f & 0x04) r |= V20_PF;
	if (f & 0x10) r |= V20_AF;
	if (f & 0x40) r |= V20_ZF;
	if (f & 0x80) r |= V20_SF;
	return r;
}

// 8080 register accessors using V20 register mapping
#define E80_A(c)  (c)->al()
#define E80_F(c)  (c)->ah()
#define E80_B(c)  (c)->ch()
#define E80_C(c)  (c)->cl()
#define E80_D(c)  (c)->dh()
#define E80_E(c)  (c)->dl()
#define E80_H(c)  (c)->bh()
#define E80_L(c)  (c)->bl()
#define E80_BC(c) (c)->cx
#define E80_DE(c) (c)->dx
#define E80_HL(c) (c)->bx

// 8080 memory access through DS segment
#define E80_RB(c,a) MRB((c)->ds, (a))
#define E80_WB(c,a,v) MWB((c)->ds, (a), (v))
#define E80_RW(c,a) MRW((c)->ds, (a))
#define E80_WW(c,a,v) MWW((c)->ds, (a), (v))

// 8080 ALU with flag setting
static uint8_t e80_alu_add(v20_t *c, uint8_t a, uint8_t b, uint8_t cy)
{
	uint16_t r = (uint16_t)a + b + cy;
	uint16_t f = 0x02;
	if (r & 0x100)   f |= V20_CF;
	if (!(r & 0xFF))  f |= V20_ZF;
	if (r & 0x80)    f |= V20_SF;
	if (ptab[r & 0xFF]) f |= V20_PF;
	if ((a ^ b ^ r) & 0x10) f |= V20_AF;
	c->flags = f;
	return (uint8_t)r;
}

static uint8_t e80_alu_sub(v20_t *c, uint8_t a, uint8_t b, uint8_t cy)
{
	uint16_t r = (uint16_t)a - b - cy;
	uint16_t f = 0x02;
	if (r & 0x100)   f |= V20_CF;
	if (!(r & 0xFF))  f |= V20_ZF;
	if (r & 0x80)    f |= V20_SF;
	if (ptab[r & 0xFF]) f |= V20_PF;
	if ((a ^ b ^ r) & 0x10) f |= V20_AF;
	c->flags = f;
	return (uint8_t)r;
}

static void e80_alu_logic(v20_t *c, uint8_t r)
{
	uint16_t f = 0x02;
	if (!r) f |= V20_ZF;
	if (r & 0x80) f |= V20_SF;
	if (ptab[r]) f |= V20_PF;
	c->flags = f; // CF=0, AF=0 for logic ops
}

static inline uint8_t e80_get_r(v20_t *c, int r)
{
	switch (r) {
	case 0: return (uint8_t)E80_B(c); case 1: return (uint8_t)E80_C(c);
	case 2: return (uint8_t)E80_D(c); case 3: return (uint8_t)E80_E(c);
	case 4: return (uint8_t)E80_H(c); case 5: return (uint8_t)E80_L(c);
	case 6: return E80_RB(c, E80_HL(c));
	case 7: return (uint8_t)E80_A(c);
	default: return 0;
	}
}

static inline void e80_set_r(v20_t *c, int r, uint8_t v)
{
	switch (r) {
	case 0: E80_B(c) = v; break; case 1: E80_C(c) = v; break;
	case 2: E80_D(c) = v; break; case 3: E80_E(c) = v; break;
	case 4: E80_H(c) = v; break; case 5: E80_L(c) = v; break;
	case 6: E80_WB(c, E80_HL(c), v); break;
	case 7: E80_A(c) = v; break;
	}
}

static inline uint16_t *e80_rp(v20_t *c, int rp)
{
	switch (rp) {
	case 0: return &c->cx;  // BC
	case 1: return &c->dx;  // DE
	case 2: return &c->bx;  // HL
	case 3: return &c->bp;  // SP (8080 SP = V20 BP)
	default: return &c->bx;
	}
}

// Push/pop in 8080 mode use DS0:BP (BP is the 8080 stack pointer)
static inline void e80_push(v20_t *c, uint16_t v)
{
	c->bp -= 2;
	E80_WW(c, c->bp, v);
}

static inline uint16_t e80_pop(v20_t *c)
{
	uint16_t v = E80_RW(c, c->bp);
	c->bp += 2;
	return v;
}

// Cycle counts use the closest V20 native instruction equivalent where
// a clear mapping exists.  NEC did not document 8080-mode cycle counts;
// these are best-effort approximations, not authoritative values.
static int exec_8080(v20_t *c)
{
	uint8_t op = E80_RB(c, c->ip);
	c->ip++;
	int cyc = 3;  // NOP default — native NOP is 3

	uint8_t a = (uint8_t)E80_A(c);
	int dst = (op >> 3) & 7;
	int src = op & 7;
	int rp  = (op >> 4) & 3;

	switch (op) {
	case 0x00: break; // NOP — native NOP=3

	// LXI rp, d16 — native MOV r16,imm16 = 4
	case 0x01: case 0x11: case 0x21: case 0x31:
		*e80_rp(c, rp) = (uint16_t)(E80_RB(c, c->ip) | (E80_RB(c, (uint16_t)(c->ip+1)) << 8));
		c->ip += 2; cyc = 4; break;

	// STAX B/D — native MOV [reg],AL mem = 9
	case 0x02: E80_WB(c, E80_BC(c), a); cyc=9; break;
	case 0x12: E80_WB(c, E80_DE(c), a); cyc=9; break;

	// INX rp — native INC r16 = 2
	case 0x03: case 0x13: case 0x23: case 0x33:
		(*e80_rp(c, rp))++; cyc=2; break;

	// INR r — native INC r/m8: reg=2, mem=16
	case 0x04: case 0x0c: case 0x14: case 0x1c:
	case 0x24: case 0x2c: case 0x34: case 0x3c: {
		uint8_t v = e80_get_r(c, dst);
		uint8_t r = v + 1;
		uint16_t f = (c->flags & V20_CF) | 0x02;
		if (!r) f |= V20_ZF;
		if (r & 0x80) f |= V20_SF;
		if (ptab[r]) f |= V20_PF;
		if ((v ^ 1 ^ r) & 0x10) f |= V20_AF;
		c->flags = f;
		e80_set_r(c, dst, r);
		cyc = (dst == 6) ? 16 : 2;
		break;
	}

	// DCR r — native DEC r/m8: reg=2, mem=16
	case 0x05: case 0x0d: case 0x15: case 0x1d:
	case 0x25: case 0x2d: case 0x35: case 0x3d: {
		uint8_t v = e80_get_r(c, dst);
		uint8_t r = v - 1;
		uint16_t f = (c->flags & V20_CF) | 0x02;
		if (!r) f |= V20_ZF;
		if (r & 0x80) f |= V20_SF;
		if (ptab[r]) f |= V20_PF;
		if ((v ^ 1 ^ r) & 0x10) f |= V20_AF;
		c->flags = f;
		e80_set_r(c, dst, r);
		cyc = (dst == 6) ? 16 : 2;
		break;
	}

	// MVI r, d8 — native MOV r/m8,imm: reg=4, mem=13
	case 0x06: case 0x0e: case 0x16: case 0x1e:
	case 0x26: case 0x2e: case 0x36: case 0x3e:
		e80_set_r(c, dst, E80_RB(c, c->ip)); c->ip++;
		cyc = (dst == 6) ? 13 : 4; break;

	// RLC — native ROL AL,1 reg = 3
	case 0x07: {
		uint8_t cy = (a >> 7) & 1;
		E80_A(c) = (uint8_t)((a << 1) | cy);
		c->flags = (c->flags & ~V20_CF) | (cy ? V20_CF : 0);
		cyc = 3; break;
	}

	// DAD rp — native ADD r16,r16 reg = 3
	case 0x09: case 0x19: case 0x29: case 0x39: {
		uint32_t r = (uint32_t)E80_HL(c) + *e80_rp(c, rp);
		c->flags = (c->flags & ~V20_CF) | ((r & 0x10000) ? V20_CF : 0);
		E80_HL(c) = (uint16_t)r;
		cyc = 3; break;
	}

	// LDAX B/D — native MOV AL,[reg] mem = 11
	case 0x0a: E80_A(c) = E80_RB(c, E80_BC(c)); cyc=11; break;
	case 0x1a: E80_A(c) = E80_RB(c, E80_DE(c)); cyc=11; break;

	// DCX rp — native DEC r16 = 2
	case 0x0b: case 0x1b: case 0x2b: case 0x3b:
		(*e80_rp(c, rp))--; cyc=2; break;

	// RRC — native ROR AL,1 reg = 3
	case 0x0f: {
		uint8_t cy = a & 1;
		E80_A(c) = (uint8_t)((a >> 1) | (cy << 7));
		c->flags = (c->flags & ~V20_CF) | (cy ? V20_CF : 0);
		cyc = 3; break;
	}

	// RAL — native RCL AL,1 reg = 3
	case 0x17: {
		uint8_t cy = (c->flags & V20_CF) ? 1 : 0;
		c->flags = (c->flags & ~V20_CF) | ((a & 0x80) ? V20_CF : 0);
		E80_A(c) = (uint8_t)((a << 1) | cy);
		cyc = 3; break;
	}

	// RAR — native RCR AL,1 reg = 3
	case 0x1f: {
		uint8_t cy = (c->flags & V20_CF) ? 1 : 0;
		c->flags = (c->flags & ~V20_CF) | ((a & 1) ? V20_CF : 0);
		E80_A(c) = (uint8_t)((a >> 1) | (cy << 7));
		cyc = 3; break;
	}

	// SHLD addr — native MOV [addr],r16 = 13
	case 0x22: {
		uint16_t addr = (uint16_t)(E80_RB(c, c->ip) | (E80_RB(c, (uint16_t)(c->ip+1)) << 8));
		c->ip += 2;
		E80_WW(c, addr, E80_HL(c));
		cyc = 13; break;
	}

	// DAA — native DAA = 10
	case 0x27: {
		uint8_t v = (uint8_t)E80_A(c);
		uint16_t f = c->flags;
		if ((v & 0x0F) > 9 || (f & V20_AF)) { v += 6; f |= V20_AF; } else f &= ~V20_AF;
		if (v > 0x9F || (f & V20_CF)) { v += 0x60; f |= V20_CF; }
		if (!v) f |= V20_ZF; else f &= ~V20_ZF;
		if (v & 0x80) f |= V20_SF; else f &= ~V20_SF;
		if (ptab[v]) f |= V20_PF; else f &= ~V20_PF;
		c->flags = f;
		E80_A(c) = v;
		cyc = 10; break;
	}

	// LHLD addr — native MOV r16,[addr] = 10
	case 0x2a: {
		uint16_t addr = (uint16_t)(E80_RB(c, c->ip) | (E80_RB(c, (uint16_t)(c->ip+1)) << 8));
		c->ip += 2;
		E80_HL(c) = E80_RW(c, addr);
		cyc = 10; break;
	}

	// CMA — native NOT AL = 3
	case 0x2f: E80_A(c) = (uint8_t)~a; cyc=3; break;

	// STA addr — native MOV [addr],AL = 10
	case 0x32: {
		uint16_t addr = (uint16_t)(E80_RB(c, c->ip) | (E80_RB(c, (uint16_t)(c->ip+1)) << 8));
		c->ip += 2;
		E80_WB(c, addr, a);
		cyc = 10; break;
	}

	// STC — native STC = 2
	case 0x37: c->flags |= V20_CF; cyc=2; break;

	// LDA addr — native MOV AL,[addr] = 10
	case 0x3a: {
		uint16_t addr = (uint16_t)(E80_RB(c, c->ip) | (E80_RB(c, (uint16_t)(c->ip+1)) << 8));
		c->ip += 2;
		E80_A(c) = E80_RB(c, addr);
		cyc = 10; break;
	}

	// CMC — native CMC = 2
	case 0x3f: c->flags ^= V20_CF; cyc=2; break;

	// MOV dst,src — native MOV r/m8,r/m8: reg=2, mem=9/11
	case 0x76: c->halted = true; cyc=2; break; // HLT — native HLT=2
	case 0x40: case 0x41: case 0x42: case 0x43: case 0x44: case 0x45: case 0x46: case 0x47:
	case 0x48: case 0x49: case 0x4a: case 0x4b: case 0x4c: case 0x4d: case 0x4e: case 0x4f:
	case 0x50: case 0x51: case 0x52: case 0x53: case 0x54: case 0x55: case 0x56: case 0x57:
	case 0x58: case 0x59: case 0x5a: case 0x5b: case 0x5c: case 0x5d: case 0x5e: case 0x5f:
	case 0x60: case 0x61: case 0x62: case 0x63: case 0x64: case 0x65: case 0x66: case 0x67:
	case 0x68: case 0x69: case 0x6a: case 0x6b: case 0x6c: case 0x6d: case 0x6e: case 0x6f:
	case 0x70: case 0x71: case 0x72: case 0x73: case 0x74: case 0x75: case 0x77:
	case 0x78: case 0x79: case 0x7a: case 0x7b: case 0x7c: case 0x7d: case 0x7e: case 0x7f:
		e80_set_r(c, dst, e80_get_r(c, src));
		cyc = (dst == 6) ? 9 : (src == 6) ? 11 : 2;
		break;

	// ADD/ADC/SUB/SBB/ANA/XRA/ORA/CMP r — native ALU r/m8: reg=2, mem=10
	case 0x80: case 0x81: case 0x82: case 0x83: case 0x84: case 0x85: case 0x86: case 0x87:
		E80_A(c) = e80_alu_add(c, a, e80_get_r(c, src), 0); cyc=(src==6)?10:2; break;
	case 0x88: case 0x89: case 0x8a: case 0x8b: case 0x8c: case 0x8d: case 0x8e: case 0x8f:
		E80_A(c) = e80_alu_add(c, a, e80_get_r(c, src), (c->flags&V20_CF)?1:0); cyc=(src==6)?10:2; break;
	case 0x90: case 0x91: case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: case 0x97:
		E80_A(c) = e80_alu_sub(c, a, e80_get_r(c, src), 0); cyc=(src==6)?10:2; break;
	case 0x98: case 0x99: case 0x9a: case 0x9b: case 0x9c: case 0x9d: case 0x9e: case 0x9f:
		E80_A(c) = e80_alu_sub(c, a, e80_get_r(c, src), (c->flags&V20_CF)?1:0); cyc=(src==6)?10:2; break;
	case 0xa0: case 0xa1: case 0xa2: case 0xa3: case 0xa4: case 0xa5: case 0xa6: case 0xa7:
		E80_A(c) = a & e80_get_r(c, src); e80_alu_logic(c, (uint8_t)E80_A(c)); cyc=(src==6)?10:2; break;
	case 0xa8: case 0xa9: case 0xaa: case 0xab: case 0xac: case 0xad: case 0xae: case 0xaf:
		E80_A(c) = a ^ e80_get_r(c, src); e80_alu_logic(c, (uint8_t)E80_A(c)); cyc=(src==6)?10:2; break;
	case 0xb0: case 0xb1: case 0xb2: case 0xb3: case 0xb4: case 0xb5: case 0xb6: case 0xb7:
		E80_A(c) = a | e80_get_r(c, src); e80_alu_logic(c, (uint8_t)E80_A(c)); cyc=(src==6)?10:2; break;
	case 0xb8: case 0xb9: case 0xba: case 0xbb: case 0xbc: case 0xbd: case 0xbe: case 0xbf:
		e80_alu_sub(c, a, e80_get_r(c, src), 0); cyc=(src==6)?10:2; break;

	// Rcc — native RET near=8, Jcc overhead ~4 not-taken
	case 0xc0: if(!(c->flags&V20_ZF)){c->ip=e80_pop(c);cyc=12;}else cyc=4; break;
	case 0xc8: if( (c->flags&V20_ZF)){c->ip=e80_pop(c);cyc=12;}else cyc=4; break;
	case 0xd0: if(!(c->flags&V20_CF)){c->ip=e80_pop(c);cyc=12;}else cyc=4; break;
	case 0xd8: if( (c->flags&V20_CF)){c->ip=e80_pop(c);cyc=12;}else cyc=4; break;
	case 0xe0: if(!(c->flags&V20_PF)){c->ip=e80_pop(c);cyc=12;}else cyc=4; break;
	case 0xe8: if( (c->flags&V20_PF)){c->ip=e80_pop(c);cyc=12;}else cyc=4; break;
	case 0xf0: if(!(c->flags&V20_SF)){c->ip=e80_pop(c);cyc=12;}else cyc=4; break;
	case 0xf8: if( (c->flags&V20_SF)){c->ip=e80_pop(c);cyc=12;}else cyc=4; break;

	// POP rp / POP PSW — native POP = 12
	case 0xc1: E80_BC(c) = e80_pop(c); cyc=12; break;
	case 0xd1: E80_DE(c) = e80_pop(c); cyc=12; break;
	case 0xe1: E80_HL(c) = e80_pop(c); cyc=12; break;
	case 0xf1: { uint16_t v=e80_pop(c); E80_A(c)=(uint8_t)(v>>8); c->flags=flags_from_8080((uint8_t)v); cyc=12; break; }

	// Jcc addr — native Jcc: taken=14, not-taken=4
	case 0xc2: case 0xca: case 0xd2: case 0xda:
	case 0xe2: case 0xea: case 0xf2: case 0xfa: {
		uint16_t addr = (uint16_t)(E80_RB(c, c->ip) | (E80_RB(c, (uint16_t)(c->ip+1)) << 8));
		c->ip += 2;
		bool cond;
		switch ((op >> 3) & 7) {
		case 0: cond = !(c->flags & V20_ZF); break;
		case 1: cond =  (c->flags & V20_ZF) != 0; break;
		case 2: cond = !(c->flags & V20_CF); break;
		case 3: cond =  (c->flags & V20_CF) != 0; break;
		case 4: cond = !(c->flags & V20_PF); break;
		case 5: cond =  (c->flags & V20_PF) != 0; break;
		case 6: cond = !(c->flags & V20_SF); break;
		case 7: cond =  (c->flags & V20_SF) != 0; break;
		default: cond = false;
		}
		if (cond) { c->ip = addr; cyc = 14; } else cyc = 4;
		break;
	}

	// JMP addr — native JMP near = 10
	case 0xc3:
		c->ip = (uint16_t)(E80_RB(c, c->ip) | (E80_RB(c, (uint16_t)(c->ip+1)) << 8));
		cyc = 10; break;

	// Ccc addr — native CALL near=19, Jcc not-taken=4
	case 0xc4: case 0xcc: case 0xd4: case 0xdc:
	case 0xe4: case 0xec: case 0xf4: case 0xfc: {
		uint16_t addr = (uint16_t)(E80_RB(c, c->ip) | (E80_RB(c, (uint16_t)(c->ip+1)) << 8));
		c->ip += 2;
		bool cond;
		switch ((op >> 3) & 7) {
		case 0: cond = !(c->flags & V20_ZF); break;
		case 1: cond =  (c->flags & V20_ZF) != 0; break;
		case 2: cond = !(c->flags & V20_CF); break;
		case 3: cond =  (c->flags & V20_CF) != 0; break;
		case 4: cond = !(c->flags & V20_PF); break;
		case 5: cond =  (c->flags & V20_PF) != 0; break;
		case 6: cond = !(c->flags & V20_SF); break;
		case 7: cond =  (c->flags & V20_SF) != 0; break;
		default: cond = false;
		}
		if (cond) { e80_push(c, c->ip); c->ip = addr; cyc=19; }
		else cyc=4;
		break;
	}

	// PUSH rp / PUSH PSW — native PUSH = 12
	case 0xc5: e80_push(c, E80_BC(c)); cyc=12; break;
	case 0xd5: e80_push(c, E80_DE(c)); cyc=12; break;
	case 0xe5: e80_push(c, E80_HL(c)); cyc=12; break;
	case 0xf5: e80_push(c, (uint16_t)(((uint16_t)(uint8_t)E80_A(c) << 8) | flags_to_8080(c->flags))); cyc=12; break;

	// ADI/ACI/SUI/SBI/ANI/XRI/ORI/CPI imm8 — native ALU AL,imm8 = 4
	case 0xc6: E80_A(c) = e80_alu_add(c, a, E80_RB(c, c->ip), 0); c->ip++; cyc=4; break;
	case 0xce: E80_A(c) = e80_alu_add(c, a, E80_RB(c, c->ip), (c->flags&V20_CF)?1:0); c->ip++; cyc=4; break;
	case 0xd6: E80_A(c) = e80_alu_sub(c, a, E80_RB(c, c->ip), 0); c->ip++; cyc=4; break;
	case 0xde: E80_A(c) = e80_alu_sub(c, a, E80_RB(c, c->ip), (c->flags&V20_CF)?1:0); c->ip++; cyc=4; break;
	case 0xe6: E80_A(c) = a & E80_RB(c, c->ip); e80_alu_logic(c, (uint8_t)E80_A(c)); c->ip++; cyc=4; break;
	case 0xee: E80_A(c) = a ^ E80_RB(c, c->ip); e80_alu_logic(c, (uint8_t)E80_A(c)); c->ip++; cyc=4; break;
	case 0xf6: E80_A(c) = a | E80_RB(c, c->ip); e80_alu_logic(c, (uint8_t)E80_A(c)); c->ip++; cyc=4; break;
	case 0xfe: e80_alu_sub(c, a, E80_RB(c, c->ip), 0); c->ip++; cyc=4; break;

	// RST n — native CALL near=19 (push + jump to fixed addr)
	case 0xc7: case 0xcf: case 0xd7: case 0xdf:
	case 0xe7: case 0xef: case 0xf7: case 0xff:
		e80_push(c, c->ip); c->ip = (uint16_t)(op & 0x38); cyc=19; break;

	// RET — native RET near = 8
	case 0xc9: c->ip = e80_pop(c); cyc=8; break;

	// CALL addr — native CALL near = 19
	case 0xcd: {
		uint16_t addr = (uint16_t)(E80_RB(c, c->ip) | (E80_RB(c, (uint16_t)(c->ip+1)) << 8));
		c->ip += 2;
		e80_push(c, c->ip);
		c->ip = addr;
		cyc = 19; break;
	}

	// OUT port — native OUT imm8 = 10
	case 0xd3:
		c->io_write(c->ctx, E80_RB(c, c->ip), a);
		c->ip++; cyc=10; break;

	// IN port — native IN imm8 = 10
	case 0xdb:
		E80_A(c) = c->io_read(c->ctx, E80_RB(c, c->ip));
		c->ip++; cyc=10; break;

	// XTHL — no native equivalent; 8080 timing
	case 0xe3: {
		uint16_t t = E80_RW(c, c->bp);
		E80_WW(c, c->bp, E80_HL(c));
		E80_HL(c) = t;
		cyc = 18; break;
	}

	// PCHL — native JMP r/m16 = 11
	case 0xe9: c->ip = E80_HL(c); cyc=11; break;

	// XCHG — native XCHG = 3
	case 0xeb: { uint16_t t=E80_DE(c); E80_DE(c)=E80_HL(c); E80_HL(c)=t; cyc=3; break; }

	// CALLN imm8: call native mode routine from 8080 mode.
	// Push PSW, PS, PC onto 8080 stack (BP). Set MD=1 (native).
	// Load PS:PC from IVT. Native ISR returns via RETI which
	// restores MD=0 to re-enter 8080 mode.
	case 0xed: {
		uint8_t vec = E80_RB(c, c->ip); c->ip++;
		// Push PSW (flags in low byte, A in high byte)
		e80_push(c, (uint16_t)(((uint16_t)(uint8_t)E80_A(c) << 8) | flags_to_8080(c->flags)));
		// Push PS (code segment)
		e80_push(c, c->cs);
		// Push PC
		e80_push(c, c->ip);
		// Load PS:PC from IVT
		uint32_t ivt = (uint32_t)vec * 4;
		c->ip = rw(c, ivt);
		c->cs = rw(c, ivt + 2);
		c->mode_8080 = false;
		cyc = 19; break;
	}

	// DI / EI — native CLI/STI = 2
	case 0xf3: c->flags &= ~V20_IF; cyc=2; break;
	case 0xfb: c->flags |= V20_IF; cyc=2; break;

	// SPHL — 8080 SP is V20 BP
	case 0xf9: c->bp = E80_HL(c); cyc=2; break;

	default:
		fprintf(stderr, "UNIMPL 8080 %02X at DS:%04X\n", op, (uint16_t)(c->ip-1));
		cyc = 4;
		break;
	}
	return cyc;
}

/* ---- interrupt check ---- */

static void check_irq(v20_t *c)
{
	if (c->nmi_line && !c->nmi_prev) {
		c->nmi_prev = true;
		do_int(c, 2);
		c->halted = false;
		return;
	}
	c->nmi_prev = c->nmi_line;

	if (c->irq_line && (c->flags & V20_IF)) {
		do_int(c, c->irq_vector);
		c->halted = false;
	}
}

/* ---- public API ---- */

void v20_init(v20_t *c)
{
	*c = v20_t{};
}

void v20_reset(v20_t *c)
{
	c->ax = c->bx = c->cx = c->dx = 0;
	c->si = c->di = c->bp = c->sp = 0;
	c->ds = c->es = c->ss = 0;
	c->cs = 0xFFFF;
	c->ip = 0x0000;
	c->flags = 0x0002;
	c->halted = false;
	c->mode_8080 = false;
	c->irq_line = false;
	c->nmi_line = false;
	c->nmi_prev = false;
	c->seg_override.reset();
}

static void record_trace(v20_t *c, uint8_t opcode)
{
	v20_trace_t *t = &c->trace_buf[(size_t)c->trace_head];
	t->cs = c->cs; t->ip = c->ip; t->opcode = opcode;
	t->ax = c->ax; t->bx = c->bx; t->cx = c->cx; t->dx = c->dx;
	t->si = c->si; t->di = c->di; t->bp = c->bp; t->sp = c->sp;
	t->ds = c->ds; t->es = c->es; t->ss = c->ss; t->flags = c->flags;
	c->trace_head = (c->trace_head + 1) % V20_TRACE_SIZE;
	if (c->trace_count < V20_TRACE_SIZE) c->trace_count++;
}

static bool check_breakpoints(v20_t *c)
{
	for (size_t i = 0; i < c->bp_enabled.size(); i++)
		if (c->bp_enabled[i] && c->cs == c->bp_seg[i] && c->ip == c->bp_off[i])
			return true;
	return false;
}

int v20_exec(v20_t *c, int target)
{
	int done = 0;
	while (done < target) {
		if (c->debug_stop && !c->debug_step) {
			done = target;
			break;
		}

		check_irq(c);
		if (c->halted) { done = target; break; }

		if (c->trace_enabled)
			record_trace(c, c->mem_read(c->ctx, ((uint32_t)c->cs << 4) + c->ip));

		done += c->mode_8080 ? exec_8080(c) : exec_one(c);

		if (c->debug_step) {
			c->debug_step = false;
			c->debug_stop = true;
		}

		if (check_breakpoints(c))
			c->debug_stop = true;

		if (c->debug_stop && c->debug_cb)
			c->debug_cb(c->debug_ctx);
	}
	return done;
}

void v20_irq(v20_t *c, bool level, uint8_t vector)
{
	c->irq_line = level;
	c->irq_vector = vector;
}

void v20_nmi(v20_t *c, bool level)
{
	c->nmi_line = level;
}
