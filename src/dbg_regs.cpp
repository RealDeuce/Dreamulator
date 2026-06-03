// license:BSD-3-Clause
// copyright-holders:Stephen Hurd
// Inspired by VirtualT (Copyright 2006 Ken Pettit, BSD-2-Clause)
#include <FL/Fl.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Text_Display.H>
#include <FL/Fl_Text_Buffer.H>
#include <cstdio>
#include <string>
#include <cstring>
#include <cstdlib>

#include "v20.h"
#include "dis86.h"
#include "prefs.h"
#include "dbg_regs.h"

static const char *reg_names[] = {
	"AX","BX","CX","DX","SI","DI","BP","SP","CS","DS","ES","SS","IP"
};
static const char *flag_names[] = {
	"OF","DF","IF","TF","SF","ZF","AF","PF","CF"
};
static const int flag_bits[] = {
	V20_OF, V20_DF, V20_IF, V20_TF, V20_SF, V20_ZF, V20_AF, V20_PF, V20_CF
};
static const char *btn_labels[] = {
	"Stop (F6)", "Step (F7)", "Over (F8)", "Run (F5)"
};

static uint16_t *reg_ptr(v20_t *c, int i)
{
	switch (i) {
	case 0: return &c->ax; case 1: return &c->bx; case 2: return &c->cx;
	case 3: return &c->dx; case 4: return &c->si; case 5: return &c->di;
	case 6: return &c->bp; case 7: return &c->sp; case 8: return &c->cs;
	case 9: return &c->ds; case 10: return &c->es; case 11: return &c->ss;
	case 12: return &c->ip;
	}
	return nullptr;
}

DbgRegsPanel::DbgRegsPanel(int X, int Y, int W, int H, v20_t *cpu)
	: Fl_Group(X, Y, W, H), m_cpu(cpu)
{
	box(FL_FLAT_BOX);

	for (int i = 0; i < 13; i++) {
		m_reg_label[i] = new Fl_Box(0, 0, 30, 24, reg_names[i]);
		m_reg[i] = new Fl_Input(0, 0, 60, 24);
		m_reg[i]->textfont(FL_COURIER);
		m_reg[i]->textsize(12);
		m_reg[i]->callback(cb_reg_changed, this);
		m_reg[i]->when(FL_WHEN_ENTER_KEY);
	}

	for (int i = 0; i < 9; i++) {
		m_flag[i] = new Fl_Check_Button(0, 0, 40, 20, flag_names[i]);
		m_flag[i]->labelsize(11);
	}

	Fl_Callback *btn_cbs[] = { cb_stop, cb_step, cb_step_over, cb_run };
	int btn_keys[] = { FL_F+6, FL_F+7, FL_F+8, FL_F+5 };
	for (int i = 0; i < 4; i++) {
		m_btn[i] = new Fl_Button(0, 0, 80, 28, btn_labels[i]);
		m_btn[i]->callback(btn_cbs[i], this);
		m_btn[i]->shortcut(btn_keys[i]);
	}
	m_btn[2]->size(100, 28);

	m_bp_label = new Fl_Box(0, 0, 150, 18, "Breakpoints (seg:off):");
	for (int i = 0; i < 8; i++) {
		m_bp_en[i] = new Fl_Check_Button(0, 0, 24, 22);
		m_bp_en[i]->callback(cb_bp_changed, this);
		m_bp_addr[i] = new Fl_Input(0, 0, 90, 22);
		m_bp_addr[i]->textfont(FL_COURIER);
		m_bp_addr[i]->textsize(12);
		m_bp_addr[i]->callback(cb_bp_changed, this);
		m_bp_addr[i]->when(FL_WHEN_ENTER_KEY);
	}

	m_trace_label = new Fl_Box(0, 0, 150, 18, "Instruction Trace:");
	m_trace_buf = new Fl_Text_Buffer();
	m_trace = new Fl_Text_Display(0, 0, 100, 100);
	m_trace->buffer(m_trace_buf);
	m_trace->textfont(FL_COURIER);
	m_trace->textsize(11);

	end();
	layout(X, Y, W, H);
	load_breakpoints();
}

void DbgRegsPanel::layout(int X, int Y, int W, int H)
{
	int col_w = (W - 20) / 2;
	int cy = Y + 10;

	for (int i = 0; i < 13; i++) {
		int col = (i < 7) ? 0 : 1;
		int row = (i < 7) ? i : i - 7;
		int lx = X + 10 + col * col_w;
		int ly = cy + row * 28;
		m_reg_label[i]->resize(lx, ly, 30, 24);
		m_reg[i]->resize(lx + 30, ly, 60, 24);
	}
	cy += 7 * 28 + 8;

	int fw = (W - 20) / 9;
	if (fw < 30) fw = 30;
	for (int i = 0; i < 9; i++)
		m_flag[i]->resize(X + 10 + i * fw, cy, fw, 20);
	cy += 28;

	int bw = (W - 30) / 4;
	if (bw < 60) bw = 60;
	for (int i = 0; i < 4; i++)
		m_btn[i]->resize(X + 10 + i * (bw + 4), cy, bw, 28);
	cy += 36;

	m_bp_label->resize(X + 10, cy, 150, 18);
	cy += 20;
	for (int row = 0; row < 4; row++) {
		int i = row, j = row + 4;
		m_bp_en[i]->resize(X + 10, cy, 24, 22);
		m_bp_addr[i]->resize(X + 36, cy, 90, 22);
		m_bp_en[j]->resize(X + 10 + col_w, cy, 24, 22);
		m_bp_addr[j]->resize(X + 36 + col_w, cy, 90, 22);
		cy += 24;
	}
	cy += 8;

	m_trace_label->resize(X + 10, cy, 150, 18);
	cy += 20;
	int th = Y + H - cy - 5;
	if (th < 0) th = 0;
	m_trace->resize(X + 5, cy, W - 10, th);
}

void DbgRegsPanel::resize(int X, int Y, int W, int H)
{
	Fl_Widget::resize(X, Y, W, H);
	layout(X, Y, W, H);
	damage(FL_DAMAGE_ALL);
}

void DbgRegsPanel::refresh()
{
	update_regs();
	update_flags();
	update_trace();
}

void DbgRegsPanel::update_regs()
{
	char buf[16];
	for (int i = 0; i < 13; i++) {
		snprintf(buf, sizeof(buf), "%04X", *reg_ptr(m_cpu, i));
		m_reg[i]->value(buf);
	}
}

void DbgRegsPanel::update_flags()
{
	for (int i = 0; i < 9; i++)
		m_flag[i]->value(!!(m_cpu->flags & flag_bits[i]));
}

void DbgRegsPanel::update_trace()
{
	std::string text;
	char line[256];
	int count = m_cpu->trace_count;

	int show = count < 40 ? count : 40;
	int begin = (m_cpu->trace_head - show + V20_TRACE_SIZE) % V20_TRACE_SIZE;

	for (int i = 0; i < show; i++) {
		int idx = (begin + i) % V20_TRACE_SIZE;
		v20_trace_t *t = &m_cpu->trace_buf[(size_t)idx];

		uint8_t code[8];
		for (int j = 0; j < 8; j++)
			code[j] = m_cpu->mem_read(m_cpu->ctx,
				((uint32_t)t->cs << 4) + (uint16_t)(t->ip + j));

		char dis[80];
		dis86(code, 8, t->ip, dis, sizeof(dis));

		snprintf(line, sizeof(line), "%04X:%04X  %-28s AX=%04X BX=%04X CX=%04X DX=%04X\n",
			t->cs, t->ip, dis, t->ax, t->bx, t->cx, t->dx);
		text += line;
	}
	m_trace_buf->text(text.c_str());
}

void DbgRegsPanel::write_regs()
{
	for (int i = 0; i < 13; i++) {
		unsigned val;
		if (sscanf(m_reg[i]->value(), "%x", &val) == 1)
			*reg_ptr(m_cpu, i) = (uint16_t)val;
	}
}

void DbgRegsPanel::write_flags()
{
	uint16_t f = 0x0002;
	for (int i = 0; i < 9; i++)
		if (m_flag[i]->value()) f |= flag_bits[i];
	m_cpu->flags = f;
}

void DbgRegsPanel::write_breakpoints()
{
	for (size_t i = 0; i < m_cpu->bp_enabled.size(); i++) {
		m_cpu->bp_enabled[i] = m_bp_en[i]->value();
		unsigned seg = 0, off = 0;
		sscanf(m_bp_addr[i]->value(), "%x:%x", &seg, &off);
		m_cpu->bp_seg[i] = (uint16_t)seg;
		m_cpu->bp_off[i] = (uint16_t)off;
	}
	save_breakpoints();
}

void DbgRegsPanel::load_breakpoints()
{
	for (int i = 0; i < 8; i++) {
		char key[16], buf[32];
		snprintf(key, sizeof(key), "bp%d", i);
		prefs_get_str("breakpoints", key, buf, sizeof(buf), "");
		m_bp_addr[i]->value(buf);
		snprintf(key, sizeof(key), "bp%d_en", i);
		m_bp_en[i]->value(prefs_get_int("breakpoints", key, 0));
	}
	write_breakpoints();
}

void DbgRegsPanel::save_breakpoints()
{
	for (int i = 0; i < 8; i++) {
		char key[16];
		snprintf(key, sizeof(key), "bp%d", i);
		prefs_set_str("breakpoints", key, m_bp_addr[i]->value());
		snprintf(key, sizeof(key), "bp%d_en", i);
		prefs_set_int("breakpoints", key, m_bp_en[i]->value());
	}
}

void DbgRegsPanel::cb_stop(Fl_Widget*, void *data) {
	auto *w = (DbgRegsPanel *)data;
	w->m_cpu->debug_stop = true;
	w->refresh();
}

void DbgRegsPanel::cb_step(Fl_Widget*, void *data) {
	auto *w = (DbgRegsPanel *)data;
	w->write_regs();
	w->m_cpu->debug_step = true;
}

void DbgRegsPanel::cb_step_over(Fl_Widget*, void *data) {
	auto *w = (DbgRegsPanel *)data;
	w->write_regs();

	uint8_t code[2];
	v20_t *c = w->m_cpu;
	code[0] = c->mem_read(c->ctx, ((uint32_t)c->cs << 4) + c->ip);
	if (code[0] == 0xE8 || code[0] == 0x9A || code[0] == 0xCC || code[0] == 0xCD) {
		uint8_t buf[8];
		for (int i = 0; i < 8; i++)
			buf[i] = c->mem_read(c->ctx, ((uint32_t)c->cs << 4) + c->ip + (uint32_t)i);
		char dis[80];
		int len = dis86(buf, 8, c->ip, dis, sizeof(dis));
		c->bp_seg[V20_MAX_BP - 1] = c->cs;
		c->bp_off[V20_MAX_BP - 1] = c->ip + (uint16_t)len;
		c->bp_enabled[V20_MAX_BP - 1] = true;
		c->debug_stop = false;
	} else {
		c->debug_step = true;
	}
}

void DbgRegsPanel::cb_run(Fl_Widget*, void *data) {
	auto *w = (DbgRegsPanel *)data;
	w->write_regs();
	w->m_cpu->debug_stop = false;
}

void DbgRegsPanel::cb_bp_changed(Fl_Widget*, void *data) {
	((DbgRegsPanel *)data)->write_breakpoints();
}

void DbgRegsPanel::cb_reg_changed(Fl_Widget*, void *data) {
	((DbgRegsPanel *)data)->write_regs();
}
