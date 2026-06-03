#include "v20.h"
#include <stdio.h>
#include <string.h>
#include <stddef.h>

/* even-parity lookup */
static const uint8_t ptab[256] = {
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
static inline uint16_t rw(v20_t *c, uint32_t a) { return rb(c,a)|((uint16_t)rb(c,a+1)<<8); }
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
	static const size_t t[]={
		offsetof(v20_t,ax), offsetof(v20_t,cx), offsetof(v20_t,dx), offsetof(v20_t,bx),
		offsetof(v20_t,sp), offsetof(v20_t,bp), offsetof(v20_t,si), offsetof(v20_t,di)};
	return (uint16_t*)((char*)c + t[i&7]);
}
static inline uint16_t *srp(v20_t *c, int i) {
	static const size_t t[]={
		offsetof(v20_t,es), offsetof(v20_t,cs), offsetof(v20_t,ss), offsetof(v20_t,ds)};
	return (uint16_t*)((char*)c + t[i&3]);
}
static inline uint8_t gr8(v20_t *c, int i) {
	uint16_t v = *r16p(c, i&3);
	return (i&4) ? (v>>8) : (uint8_t)v;
}
static inline void sr8(v20_t *c, int i, uint8_t v) {
	uint16_t *r = r16p(c, i&3);
	if (i&4) *r = (*r & 0x00FF) | ((uint16_t)v<<8);
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
	e.seg = (c->seg_override >= 0) ? *srp(c, c->seg_override) : *srp(c, ds);
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
		case 0: cf=(r>>(bits-1))&1; r=((r<<1)|cf)&mask; break;
		case 1: cf=r&1; r=((r>>1)|((uint32_t)cf<<(bits-1)))&mask; break;
		case 2: cf=(r>>(bits-1))&1; r=((r<<1)|ocf)&mask; ocf=cf; break;
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
		if (op==0||op==4||op==6) { if (((r>>(bits-1))^cf)&1) f|=V20_OF; }
		else if (op==1)          { if (((r>>(bits-1))^((r>>(bits-2))&1))&1) f|=V20_OF; }
		else if (op==5)          { if (val & sign) f|=V20_OF; }
	}

	c->flags = f;
	return r;
}

/* ---- INT ---- */

static void do_int(v20_t *c, uint8_t vec)
{
	push(c, c->flags);
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

static void s_movsb(v20_t *c, bool rep) {
	uint16_t ss = (c->seg_override>=0)?*srp(c,c->seg_override):c->ds;
	int d = (c->flags&V20_DF)?-1:1;
	do { MWB(c->es,c->di,MRB(ss,c->si)); c->si+=d; c->di+=d;
	     if(!rep) return; c->cx--; } while(c->cx);
}
static void s_movsw(v20_t *c, bool rep) {
	uint16_t ss = (c->seg_override>=0)?*srp(c,c->seg_override):c->ds;
	int d = (c->flags&V20_DF)?-2:2;
	do { MWW(c->es,c->di,MRW(ss,c->si)); c->si+=d; c->di+=d;
	     if(!rep) return; c->cx--; } while(c->cx);
}
static void s_stosb(v20_t *c, bool rep) {
	int d = (c->flags&V20_DF)?-1:1;
	do { MWB(c->es,c->di,c->al); c->di+=d;
	     if(!rep) return; c->cx--; } while(c->cx);
}
static void s_stosw(v20_t *c, bool rep) {
	int d = (c->flags&V20_DF)?-2:2;
	do { MWW(c->es,c->di,c->ax); c->di+=d;
	     if(!rep) return; c->cx--; } while(c->cx);
}
static void s_lodsb(v20_t *c, bool rep) {
	uint16_t ss = (c->seg_override>=0)?*srp(c,c->seg_override):c->ds;
	int d = (c->flags&V20_DF)?-1:1;
	do { c->al=MRB(ss,c->si); c->si+=d;
	     if(!rep) return; c->cx--; } while(c->cx);
}
static void s_lodsw(v20_t *c, bool rep) {
	uint16_t ss = (c->seg_override>=0)?*srp(c,c->seg_override):c->ds;
	int d = (c->flags&V20_DF)?-2:2;
	do { c->ax=MRW(ss,c->si); c->si+=d;
	     if(!rep) return; c->cx--; } while(c->cx);
}
static void s_cmpsb(v20_t *c, bool rep, bool repne) {
	uint16_t ss = (c->seg_override>=0)?*srp(c,c->seg_override):c->ds;
	int d = (c->flags&V20_DF)?-1:1;
	bool has_rep = rep||repne;
	do {
		alu(c, 7, MRB(ss,c->si), MRB(c->es,c->di), 0);
		c->si+=d; c->di+=d;
		if(!has_rep) return;
		c->cx--;
		if (rep   && !(c->flags&V20_ZF)) return;
		if (repne &&  (c->flags&V20_ZF)) return;
	} while(c->cx);
}
static void s_cmpsw(v20_t *c, bool rep, bool repne) {
	uint16_t ss = (c->seg_override>=0)?*srp(c,c->seg_override):c->ds;
	int d = (c->flags&V20_DF)?-2:2;
	bool has_rep = rep||repne;
	do {
		alu(c, 7, MRW(ss,c->si), MRW(c->es,c->di), 1);
		c->si+=d; c->di+=d;
		if(!has_rep) return;
		c->cx--;
		if (rep   && !(c->flags&V20_ZF)) return;
		if (repne &&  (c->flags&V20_ZF)) return;
	} while(c->cx);
}
static void s_scasb(v20_t *c, bool rep, bool repne) {
	int d = (c->flags&V20_DF)?-1:1;
	bool has_rep = rep||repne;
	do {
		alu(c, 7, c->al, MRB(c->es,c->di), 0);
		c->di+=d;
		if(!has_rep) return;
		c->cx--;
		if (rep   && !(c->flags&V20_ZF)) return;
		if (repne &&  (c->flags&V20_ZF)) return;
	} while(c->cx);
}
static void s_scasw(v20_t *c, bool rep, bool repne) {
	int d = (c->flags&V20_DF)?-2:2;
	bool has_rep = rep||repne;
	do {
		alu(c, 7, c->ax, MRW(c->es,c->di), 1);
		c->di+=d;
		if(!has_rep) return;
		c->cx--;
		if (rep   && !(c->flags&V20_ZF)) return;
		if (repne &&  (c->flags&V20_ZF)) return;
	} while(c->cx);
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

static void exec_one(v20_t *c)
{
	int sov = -1;
	bool rep = false, repne = false;
	uint8_t op;

pfx:
	op = f8(c);
	switch (op) {
	case 0x26: sov=0; goto pfx;
	case 0x2e: sov=1; goto pfx;
	case 0x36: sov=2; goto pfx;
	case 0x3e: sov=3; goto pfx;
	case 0xf0: goto pfx;
	case 0xf2: repne=true; rep=false; goto pfx;
	case 0xf3: rep=true; repne=false; goto pfx;
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
				if (w) { uint16_t r=alu(c,aop,*r16p(c,e.reg),ea_rw(c,&e),1); if(aop!=7) *r16p(c,e.reg)=r; }
				else   { uint8_t  r=alu(c,aop,gr8(c,e.reg),ea_rb(c,&e),0);   if(aop!=7) sr8(c,e.reg,r); }
			} else {
				if (w) { uint16_t r=alu(c,aop,ea_rw(c,&e),*r16p(c,e.reg),1); if(aop!=7) ea_ww(c,&e,r); }
				else   { uint8_t  r=alu(c,aop,ea_rb(c,&e),gr8(c,e.reg),0);   if(aop!=7) ea_wb(c,&e,r); }
			}
		} else if (sub==4) {
			uint8_t r=alu(c,aop,c->al,f8(c),0); if(aop!=7) c->al=r;
		} else {
			uint16_t r=alu(c,aop,c->ax,f16(c),1); if(aop!=7) c->ax=r;
		}
		break;
	}

	/* ---- PUSH/POP segment ---- */
	case 0x06: push(c, c->es); break;
	case 0x07: c->es = pop(c); break;
	case 0x0e: push(c, c->cs); break;
	case 0x0f: break;
	case 0x16: push(c, c->ss); break;
	case 0x17: c->ss = pop(c); break;
	case 0x1e: push(c, c->ds); break;
	case 0x1f: c->ds = pop(c); break;

	/* ---- BCD ---- */
	case 0x27: { /* DAA */
		uint8_t oa = c->al; int ocf = c->flags&V20_CF;
		c->flags &= ~V20_CF;
		if ((c->al&0x0F)>9 || (c->flags&V20_AF)) {
			c->al+=6; c->flags|=V20_AF;
			if (ocf || c->al < oa) c->flags|=V20_CF;
		} else c->flags&=~V20_AF;
		if (oa>0x99 || ocf) { c->al+=0x60; c->flags|=V20_CF; }
		c->flags &= ~(V20_SF|V20_ZF|V20_PF);
		if (!c->al) c->flags|=V20_ZF; if (c->al&0x80) c->flags|=V20_SF;
		if (ptab[c->al]) c->flags|=V20_PF;
		break;
	}
	case 0x2f: { /* DAS */
		uint8_t oa = c->al; int ocf = c->flags&V20_CF;
		c->flags &= ~V20_CF;
		if ((c->al&0x0F)>9 || (c->flags&V20_AF)) {
			c->al-=6; c->flags|=V20_AF;
			if (ocf || c->al > oa) c->flags|=V20_CF;
		} else c->flags&=~V20_AF;
		if (oa>0x99 || ocf) { c->al-=0x60; c->flags|=V20_CF; }
		c->flags &= ~(V20_SF|V20_ZF|V20_PF);
		if (!c->al) c->flags|=V20_ZF; if (c->al&0x80) c->flags|=V20_SF;
		if (ptab[c->al]) c->flags|=V20_PF;
		break;
	}
	case 0x37: /* AAA */
		if ((c->al&0x0F)>9 || (c->flags&V20_AF)) {
			c->al+=6; c->ah++; c->flags|=V20_AF|V20_CF;
		} else { c->flags&=~(V20_AF|V20_CF); }
		c->al &= 0x0F;
		break;
	case 0x3f: /* AAS */
		if ((c->al&0x0F)>9 || (c->flags&V20_AF)) {
			c->al-=6; c->ah--; c->flags|=V20_AF|V20_CF;
		} else { c->flags&=~(V20_AF|V20_CF); }
		c->al &= 0x0F;
		break;

	/* ---- INC reg16 ---- */
	case 0x40: case 0x41: case 0x42: case 0x43:
	case 0x44: case 0x45: case 0x46: case 0x47:
		inc16(c, r16p(c, op&7)); break;

	/* ---- DEC reg16 ---- */
	case 0x48: case 0x49: case 0x4a: case 0x4b:
	case 0x4c: case 0x4d: case 0x4e: case 0x4f:
		dec16(c, r16p(c, op&7)); break;

	/* ---- PUSH reg16 ---- */
	case 0x50: case 0x51: case 0x52: case 0x53:
	case 0x54: case 0x55: case 0x56: case 0x57:
		push(c, *r16p(c, op&7)); break;

	/* ---- POP reg16 ---- */
	case 0x58: case 0x59: case 0x5a: case 0x5b:
	case 0x5c: case 0x5d: case 0x5e: case 0x5f:
		*r16p(c, op&7) = pop(c); break;

	/* ---- PUSHA / POPA ---- */
	case 0x60: { uint16_t t=c->sp; for(int i=0;i<8;i++) push(c,(i==4)?t:*r16p(c,i)); break; }
	case 0x61: { for(int i=7;i>=0;i--) { uint16_t v=pop(c); if(i!=4) *r16p(c,i)=v; } break; }

	/* ---- BOUND ---- */
	case 0x62: { ea_t e=decode_ea(c); (void)e; break; }

	/* ---- PUSH imm ---- */
	case 0x68: push(c, f16(c)); break;
	case 0x6a: push(c, (uint16_t)(int16_t)(int8_t)f8(c)); break;

	/* ---- IMUL imm ---- */
	case 0x69: { ea_t e=decode_ea(c); int32_t a=(int16_t)ea_rw(c,&e); int32_t b=(int16_t)f16(c);
		int32_t r=a*b; *r16p(c,e.reg)=(uint16_t)r;
		c->flags &= ~(V20_CF|V20_OF); if(r!=(int16_t)r) c->flags|=V20_CF|V20_OF; break; }
	case 0x6b: { ea_t e=decode_ea(c); int32_t a=(int16_t)ea_rw(c,&e); int32_t b=(int8_t)f8(c);
		int32_t r=a*b; *r16p(c,e.reg)=(uint16_t)r;
		c->flags &= ~(V20_CF|V20_OF); if(r!=(int16_t)r) c->flags|=V20_CF|V20_OF; break; }

	/* ---- INS / OUTS (stub) ---- */
	case 0x6c: case 0x6d: case 0x6e: case 0x6f: break;

	/* ---- Jcc ---- */
	case 0x70: case 0x71: case 0x72: case 0x73:
	case 0x74: case 0x75: case 0x76: case 0x77:
	case 0x78: case 0x79: case 0x7a: case 0x7b:
	case 0x7c: case 0x7d: case 0x7e: case 0x7f: {
		int8_t d = (int8_t)f8(c);
		if (jcc(c, op&0xF)) c->ip += d;
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
		if (w) { uint16_t r=alu(c,e.reg,ea_rw(c,&e),imm,1); if(e.reg!=7) ea_ww(c,&e,r); }
		else   { uint8_t  r=alu(c,e.reg,ea_rb(c,&e),imm,0); if(e.reg!=7) ea_wb(c,&e,r); }
		break;
	}

	/* ---- TEST r/m, r ---- */
	case 0x84: { ea_t e=decode_ea(c); alu(c,4,ea_rb(c,&e),gr8(c,e.reg),0); break; }
	case 0x85: { ea_t e=decode_ea(c); alu(c,4,ea_rw(c,&e),*r16p(c,e.reg),1); break; }

	/* ---- XCHG r/m, r ---- */
	case 0x86: { ea_t e=decode_ea(c); uint8_t t=ea_rb(c,&e); ea_wb(c,&e,gr8(c,e.reg)); sr8(c,e.reg,t); break; }
	case 0x87: { ea_t e=decode_ea(c); uint16_t t=ea_rw(c,&e); ea_ww(c,&e,*r16p(c,e.reg)); *r16p(c,e.reg)=t; break; }

	/* ---- MOV r/m, r ---- */
	case 0x88: { ea_t e=decode_ea(c); ea_wb(c,&e,gr8(c,e.reg)); break; }
	case 0x89: { ea_t e=decode_ea(c); ea_ww(c,&e,*r16p(c,e.reg)); break; }
	case 0x8a: { ea_t e=decode_ea(c); sr8(c,e.reg,ea_rb(c,&e)); break; }
	case 0x8b: { ea_t e=decode_ea(c); *r16p(c,e.reg)=ea_rw(c,&e); break; }

	/* ---- MOV r/m, sreg / LEA / MOV sreg, r/m ---- */
	case 0x8c: { ea_t e=decode_ea(c); ea_ww(c,&e,*srp(c,e.reg&3)); break; }
	case 0x8d: { ea_t e=decode_ea(c); *r16p(c,e.reg)=e.off; break; }
	case 0x8e: { ea_t e=decode_ea(c); *srp(c,e.reg&3)=ea_rw(c,&e); break; }

	/* ---- POP r/m ---- */
	case 0x8f: { ea_t e=decode_ea(c); ea_ww(c,&e,pop(c)); break; }

	/* ---- NOP / XCHG AX, reg ---- */
	case 0x90: break;
	case 0x91: case 0x92: case 0x93: case 0x94:
	case 0x95: case 0x96: case 0x97:
		{ uint16_t t=c->ax; c->ax=*r16p(c,op&7); *r16p(c,op&7)=t; break; }

	case 0x98: c->ax = (uint16_t)(int16_t)(int8_t)c->al; break; /* CBW */
	case 0x99: c->dx = (c->ax & 0x8000) ? 0xFFFF : 0; break; /* CWD */

	/* ---- CALL far ---- */
	case 0x9a: { uint16_t o=f16(c); uint16_t s=f16(c); push(c,c->cs); push(c,c->ip); c->ip=o; c->cs=s; break; }

	case 0x9b: break; /* WAIT */

	/* ---- PUSHF / POPF ---- */
	case 0x9c: push(c, (c->flags & 0x0FD5) | 0xF002); break;
	case 0x9d: c->flags = (pop(c) & 0x0FD5) | 0x0002; break;

	/* ---- SAHF / LAHF ---- */
	case 0x9e: c->flags = (c->flags & 0xFF00) | (c->ah & 0xD5) | 0x02; break;
	case 0x9f: c->ah = (uint8_t)((c->flags & 0xD5) | 0x02); break;

	/* ---- MOV AL/AX, [addr] ---- */
	case 0xa0: { uint16_t a=f16(c); uint16_t s=(sov>=0)?*srp(c,sov):c->ds; c->al=MRB(s,a); break; }
	case 0xa1: { uint16_t a=f16(c); uint16_t s=(sov>=0)?*srp(c,sov):c->ds; c->ax=MRW(s,a); break; }
	case 0xa2: { uint16_t a=f16(c); uint16_t s=(sov>=0)?*srp(c,sov):c->ds; MWB(s,a,c->al); break; }
	case 0xa3: { uint16_t a=f16(c); uint16_t s=(sov>=0)?*srp(c,sov):c->ds; MWW(s,a,c->ax); break; }

	/* ---- string ops ---- */
	case 0xa4: s_movsb(c, rep); break;
	case 0xa5: s_movsw(c, rep); break;
	case 0xa6: s_cmpsb(c, rep, repne); break;
	case 0xa7: s_cmpsw(c, rep, repne); break;

	/* ---- TEST AL/AX, imm ---- */
	case 0xa8: alu(c,4,c->al,f8(c),0); break;
	case 0xa9: alu(c,4,c->ax,f16(c),1); break;

	case 0xaa: s_stosb(c, rep); break;
	case 0xab: s_stosw(c, rep); break;
	case 0xac: s_lodsb(c, rep); break;
	case 0xad: s_lodsw(c, rep); break;
	case 0xae: s_scasb(c, rep, repne); break;
	case 0xaf: s_scasw(c, rep, repne); break;

	/* ---- MOV reg8, imm8 ---- */
	case 0xb0: case 0xb1: case 0xb2: case 0xb3:
	case 0xb4: case 0xb5: case 0xb6: case 0xb7:
		sr8(c, op&7, f8(c)); break;

	/* ---- MOV reg16, imm16 ---- */
	case 0xb8: case 0xb9: case 0xba: case 0xbb:
	case 0xbc: case 0xbd: case 0xbe: case 0xbf:
		*r16p(c, op&7) = f16(c); break;

	/* ---- shift r/m, imm8 (186) ---- */
	case 0xc0: { ea_t e=decode_ea(c); uint8_t n=f8(c); ea_wb(c,&e,shf(c,e.reg,ea_rb(c,&e),n,0)); break; }
	case 0xc1: { ea_t e=decode_ea(c); uint8_t n=f8(c); ea_ww(c,&e,shf(c,e.reg,ea_rw(c,&e),n,1)); break; }

	/* ---- RET near ---- */
	case 0xc2: { uint16_t n=f16(c); c->ip=pop(c); c->sp+=n; break; }
	case 0xc3: c->ip = pop(c); break;

	/* ---- LES / LDS ---- */
	case 0xc4: { ea_t e=decode_ea(c); *r16p(c,e.reg)=MRW(e.seg,e.off); c->es=MRW(e.seg,(uint16_t)(e.off+2)); break; }
	case 0xc5: { ea_t e=decode_ea(c); *r16p(c,e.reg)=MRW(e.seg,e.off); c->ds=MRW(e.seg,(uint16_t)(e.off+2)); break; }

	/* ---- MOV r/m, imm ---- */
	case 0xc6: { ea_t e=decode_ea(c); ea_wb(c,&e,f8(c)); break; }
	case 0xc7: { ea_t e=decode_ea(c); ea_ww(c,&e,f16(c)); break; }

	/* ---- ENTER / LEAVE ---- */
	case 0xc8: {
		uint16_t sz=f16(c); uint8_t lv=f8(c)&0x1F;
		push(c, c->bp);
		uint16_t fr = c->sp;
		for (int i=1; i<lv; i++) { c->bp-=2; push(c, MRW(c->ss,c->bp)); }
		if (lv) push(c, fr);
		c->bp = fr;
		c->sp -= sz;
		break;
	}
	case 0xc9: c->sp=c->bp; c->bp=pop(c); break;

	/* ---- RET far ---- */
	case 0xca: { uint16_t n=f16(c); c->ip=pop(c); c->cs=pop(c); c->sp+=n; break; }
	case 0xcb: c->ip=pop(c); c->cs=pop(c); break;

	/* ---- INT ---- */
	case 0xcc: do_int(c, 3); break;
	case 0xcd: do_int(c, f8(c)); break;
	case 0xce: if (c->flags & V20_OF) do_int(c, 4); break;

	/* ---- IRET ---- */
	case 0xcf: c->ip=pop(c); c->cs=pop(c); c->flags=pop(c); break;

	/* ---- shifts r/m, 1 / CL ---- */
	case 0xd0: { ea_t e=decode_ea(c); ea_wb(c,&e,shf(c,e.reg,ea_rb(c,&e),1,0)); break; }
	case 0xd1: { ea_t e=decode_ea(c); ea_ww(c,&e,shf(c,e.reg,ea_rw(c,&e),1,1)); break; }
	case 0xd2: { ea_t e=decode_ea(c); ea_wb(c,&e,shf(c,e.reg,ea_rb(c,&e),c->cl,0)); break; }
	case 0xd3: { ea_t e=decode_ea(c); ea_ww(c,&e,shf(c,e.reg,ea_rw(c,&e),c->cl,1)); break; }

	/* ---- AAM / AAD ---- */
	case 0xd4: { uint8_t b=f8(c); if(b) { c->ah=c->al/b; c->al=c->al%b; }
		c->flags &= ~(V20_SF|V20_ZF|V20_PF);
		if(!c->ax) c->flags|=V20_ZF; if(c->al&0x80) c->flags|=V20_SF;
		if(ptab[c->al]) c->flags|=V20_PF; break; }
	case 0xd5: { uint8_t b=f8(c); c->al=(uint8_t)(c->ah*b+c->al); c->ah=0;
		c->flags &= ~(V20_SF|V20_ZF|V20_PF);
		if(!c->al) c->flags|=V20_ZF; if(c->al&0x80) c->flags|=V20_SF;
		if(ptab[c->al]) c->flags|=V20_PF; break; }

	case 0xd6: c->al = (c->flags & V20_CF) ? 0xFF : 0x00; break; /* SALC */

	/* ---- XLAT ---- */
	case 0xd7: { uint16_t s=(sov>=0)?*srp(c,sov):c->ds; c->al=MRB(s,(uint16_t)(c->bx+c->al)); break; }

	/* ---- ESC (FPU, ignore) ---- */
	case 0xd8: case 0xd9: case 0xda: case 0xdb:
	case 0xdc: case 0xdd: case 0xde: case 0xdf:
		{ ea_t e=decode_ea(c); (void)e; break; }

	/* ---- LOOP / JCXZ ---- */
	case 0xe0: { int8_t d=(int8_t)f8(c); c->cx--; if(c->cx && !(c->flags&V20_ZF)) c->ip+=d; break; }
	case 0xe1: { int8_t d=(int8_t)f8(c); c->cx--; if(c->cx &&  (c->flags&V20_ZF)) c->ip+=d; break; }
	case 0xe2: { int8_t d=(int8_t)f8(c); c->cx--; if(c->cx) c->ip+=d; break; }
	case 0xe3: { int8_t d=(int8_t)f8(c); if(!c->cx) c->ip+=d; break; }

	/* ---- IN / OUT imm8 ---- */
	case 0xe4: c->al = c->io_read(c->ctx, f8(c)); break;
	case 0xe5: { uint16_t p=f8(c); c->al=c->io_read(c->ctx,p); c->ah=c->io_read(c->ctx,p+1); break; }
	case 0xe6: c->io_write(c->ctx, f8(c), c->al); break;
	case 0xe7: { uint16_t p=f8(c); c->io_write(c->ctx,p,c->al); c->io_write(c->ctx,p+1,c->ah); break; }

	/* ---- CALL / JMP near ---- */
	case 0xe8: { int16_t d=(int16_t)f16(c); push(c,c->ip); c->ip+=d; break; }
	case 0xe9: { int16_t d=(int16_t)f16(c); c->ip+=d; break; }
	case 0xea: { uint16_t o=f16(c); uint16_t s=f16(c); c->ip=o; c->cs=s; break; }
	case 0xeb: { int8_t d=(int8_t)f8(c); c->ip+=d; break; }

	/* ---- IN / OUT DX ---- */
	case 0xec: c->al = c->io_read(c->ctx, c->dx); break;
	case 0xed: c->al=c->io_read(c->ctx,c->dx); c->ah=c->io_read(c->ctx,c->dx+1); break;
	case 0xee: c->io_write(c->ctx, c->dx, c->al); break;
	case 0xef: c->io_write(c->ctx,c->dx,c->al); c->io_write(c->ctx,c->dx+1,c->ah); break;

	/* ---- HLT / CMC ---- */
	case 0xf4: c->halted = true; break;
	case 0xf5: c->flags ^= V20_CF; break;

	/* ---- Group 3: TEST/NOT/NEG/MUL/IMUL/DIV/IDIV ---- */
	case 0xf6: case 0xf7: {
		ea_t e = decode_ea(c);
		int w = op & 1;
		switch (e.reg) {
		case 0: case 1:
			if (w) alu(c,4,ea_rw(c,&e),f16(c),1);
			else   alu(c,4,ea_rb(c,&e),f8(c),0);
			break;
		case 2:
			if (w) ea_ww(c,&e,~ea_rw(c,&e));
			else   ea_wb(c,&e,~ea_rb(c,&e));
			break;
		case 3:
			if (w) { uint16_t v=ea_rw(c,&e); ea_ww(c,&e,alu(c,5,0,v,1)); if(v) c->flags|=V20_CF; }
			else   { uint8_t  v=ea_rb(c,&e); ea_wb(c,&e,alu(c,5,0,v,0)); if(v) c->flags|=V20_CF; }
			break;
		case 4: /* MUL */
			if (w) { uint32_t r=(uint32_t)c->ax*ea_rw(c,&e); c->ax=(uint16_t)r; c->dx=r>>16;
				c->flags&=~(V20_CF|V20_OF); if(c->dx) c->flags|=V20_CF|V20_OF; }
			else   { uint16_t r=(uint16_t)c->al*ea_rb(c,&e); c->ax=r;
				c->flags&=~(V20_CF|V20_OF); if(c->ah) c->flags|=V20_CF|V20_OF; }
			break;
		case 5: /* IMUL */
			if (w) { int32_t r=(int32_t)(int16_t)c->ax*(int16_t)ea_rw(c,&e); c->ax=(uint16_t)r; c->dx=(uint16_t)(r>>16);
				c->flags&=~(V20_CF|V20_OF); if(r!=(int16_t)r) c->flags|=V20_CF|V20_OF; }
			else   { int16_t r=(int16_t)(int8_t)c->al*(int8_t)ea_rb(c,&e); c->ax=(uint16_t)r;
				c->flags&=~(V20_CF|V20_OF); if(r!=(int8_t)r) c->flags|=V20_CF|V20_OF; }
			break;
		case 6: /* DIV */
			if (w) { uint32_t n=((uint32_t)c->dx<<16)|c->ax; uint16_t d=ea_rw(c,&e);
				if(!d||n/d>0xFFFF){do_int(c,0);break;} c->ax=(uint16_t)(n/d); c->dx=(uint16_t)(n%d); }
			else   { uint16_t n=c->ax; uint8_t d=ea_rb(c,&e);
				if(!d||n/d>0xFF){do_int(c,0);break;} c->al=(uint8_t)(n/d); c->ah=(uint8_t)(n%d); }
			break;
		case 7: /* IDIV */
			if (w) { int32_t n=(int32_t)(((uint32_t)c->dx<<16)|c->ax); int16_t d=(int16_t)ea_rw(c,&e);
				if(!d){do_int(c,0);break;} int32_t q=n/d;
				if(q!=(int16_t)q){do_int(c,0);break;} c->ax=(uint16_t)q; c->dx=(uint16_t)(n%d); }
			else   { int16_t n=(int16_t)c->ax; int8_t d=(int8_t)ea_rb(c,&e);
				if(!d){do_int(c,0);break;} int16_t q=n/d;
				if(q!=(int8_t)q){do_int(c,0);break;} c->al=(uint8_t)q; c->ah=(uint8_t)(n%d); }
			break;
		}
		break;
	}

	/* ---- flag control ---- */
	case 0xf8: c->flags &= ~V20_CF; break;
	case 0xf9: c->flags |=  V20_CF; break;
	case 0xfa: c->flags &= ~V20_IF; break;
	case 0xfb: c->flags |=  V20_IF; break;
	case 0xfc: c->flags &= ~V20_DF; break;
	case 0xfd: c->flags |=  V20_DF; break;

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
			c->flags=f; ea_ww(c,&e,r); break; }
		case 1: { uint16_t v=ea_rw(c,&e); uint16_t f=c->flags&~(V20_PF|V20_AF|V20_ZF|V20_SF|V20_OF);
			uint16_t r=v-1;
			if((v^1^r)&0x10) f|=V20_AF; if(v==0x8000) f|=V20_OF;
			if(!r) f|=V20_ZF; if(r&0x8000) f|=V20_SF; if(ptab[r&0xFF]) f|=V20_PF;
			c->flags=f; ea_ww(c,&e,r); break; }
		case 2: push(c,c->ip); c->ip=ea_rw(c,&e); break;
		case 3: push(c,c->cs); push(c,c->ip); c->ip=MRW(e.seg,e.off); c->cs=MRW(e.seg,(uint16_t)(e.off+2)); break;
		case 4: c->ip=ea_rw(c,&e); break;
		case 5: c->ip=MRW(e.seg,e.off); c->cs=MRW(e.seg,(uint16_t)(e.off+2)); break;
		case 6: push(c, ea_rw(c,&e)); break;
		default: break;
		}
		break;
	}

	default:
		fprintf(stderr, "UNIMPL %02X at %04X:%04X\n", op, c->cs, (uint16_t)(c->ip-1));
		c->halted = true;
		break;
	}
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
	memset(c, 0, sizeof(*c));
	c->seg_override = -1;
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
	c->irq_line = false;
	c->nmi_line = false;
	c->nmi_prev = false;
	c->seg_override = -1;
}

int v20_exec(v20_t *c, int target)
{
	int done = 0;
	while (done < target) {
		check_irq(c);
		if (c->halted) { done = target; break; }
		exec_one(c);
		done++;
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
