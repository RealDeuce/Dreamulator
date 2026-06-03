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

DbgRegsWindow::DbgRegsWindow(v20_t *cpu)
	: Fl_Double_Window(420, 560, "CPU Registers"), m_cpu(cpu)
{
	int y = 10;

	/* registers: 2 columns */
	for (int i = 0; i < 13; i++) {
		int col = (i < 7) ? 0 : 1;
		int row = (i < 7) ? i : i - 7;
		int lx = 10 + col * 200;
		int ly = y + row * 28;

		new Fl_Box(lx, ly, 30, 24, reg_names[i]);
		m_reg[i] = new Fl_Input(lx + 30, ly, 60, 24);
		m_reg[i]->textfont(FL_COURIER);
		m_reg[i]->textsize(12);
		m_reg[i]->callback(cb_reg_changed, this);
		m_reg[i]->when(FL_WHEN_ENTER_KEY);
	}
	y += 7 * 28 + 8;

	/* flags */
	for (int i = 0; i < 9; i++) {
		m_flag[i] = new Fl_Check_Button(10 + i * 44, y, 40, 20, flag_names[i]);
		m_flag[i]->labelsize(11);
	}
	y += 28;

	/* control buttons */
	Fl_Button *bstop = new Fl_Button(10, y, 80, 28, "Stop (F6)");
	bstop->callback(cb_stop, this);
	bstop->shortcut(FL_F + 6);

	Fl_Button *bstep = new Fl_Button(100, y, 80, 28, "Step (F7)");
	bstep->callback(cb_step, this);
	bstep->shortcut(FL_F + 7);

	Fl_Button *bover = new Fl_Button(190, y, 100, 28, "Over (F8)");
	bover->callback(cb_step_over, this);
	bover->shortcut(FL_F + 8);

	Fl_Button *brun = new Fl_Button(300, y, 80, 28, "Run (F5)");
	brun->callback(cb_run, this);
	brun->shortcut(FL_F + 5);
	y += 36;

	/* breakpoints */
	new Fl_Box(10, y, 100, 18, "Breakpoints (seg:off):");
	y += 20;
	for (int i = 0; i < 8; i++) {
		m_bp_en[i] = new Fl_Check_Button(10, y, 24, 22);
		m_bp_en[i]->callback(cb_bp_changed, this);
		m_bp_addr[i] = new Fl_Input(36, y, 90, 22);
		m_bp_addr[i]->textfont(FL_COURIER);
		m_bp_addr[i]->textsize(12);
		m_bp_addr[i]->callback(cb_bp_changed, this);
		m_bp_addr[i]->when(FL_WHEN_ENTER_KEY);
		if (i < 4) {
			/* second column */
			m_bp_en[i+4] = new Fl_Check_Button(210, y, 24, 22);
			m_bp_en[i+4]->callback(cb_bp_changed, this);
			m_bp_addr[i+4] = new Fl_Input(236, y, 90, 22);
			m_bp_addr[i+4]->textfont(FL_COURIER);
			m_bp_addr[i+4]->textsize(12);
			m_bp_addr[i+4]->callback(cb_bp_changed, this);
			m_bp_addr[i+4]->when(FL_WHEN_ENTER_KEY);
		}
		if (i < 4) y += 24;
		else break;
	}
	y += 32;

	/* trace display */
	new Fl_Box(10, y, 100, 18, "Instruction Trace:");
	y += 20;
	m_trace_buf = new Fl_Text_Buffer();
	m_trace = new Fl_Text_Display(10, y, 400, 180);
	m_trace->buffer(m_trace_buf);
	m_trace->textfont(FL_COURIER);
	m_trace->textsize(11);

	end();

	load_breakpoints();
	m_cpu->trace_enabled = true;

	int wx = 100, wy = 100, ww = 420, wh = 560;
	prefs_load_window("dbg_regs", wx, wy, ww, wh);
	position(wx, wy);
}

void DbgRegsWindow::refresh()
{
	update_regs();
	update_flags();
	update_trace();
}

void DbgRegsWindow::update_regs()
{
	char buf[16];
	for (int i = 0; i < 13; i++) {
		snprintf(buf, sizeof(buf), "%04X", *reg_ptr(m_cpu, i));
		m_reg[i]->value(buf);
	}
}

void DbgRegsWindow::update_flags()
{
	for (int i = 0; i < 9; i++)
		m_flag[i]->value(!!(m_cpu->flags & flag_bits[i]));
}

void DbgRegsWindow::update_trace()
{
	m_trace_buf->text("");
	char line[256];
	int count = m_cpu->trace_count;
	int start = (m_cpu->trace_head - count + V20_TRACE_SIZE) % V20_TRACE_SIZE;

	int show = count < 40 ? count : 40;
	int begin = (m_cpu->trace_head - show + V20_TRACE_SIZE) % V20_TRACE_SIZE;

	for (int i = 0; i < show; i++) {
		int idx = (begin + i) % V20_TRACE_SIZE;
		v20_trace_t *t = &m_cpu->trace_buf[idx];

		uint8_t code[8];
		for (int j = 0; j < 8; j++)
			code[j] = m_cpu->mem_read(m_cpu->ctx,
				((uint32_t)t->cs << 4) + (uint16_t)(t->ip + j));

		char dis[80];
		dis86(code, 8, t->ip, dis, sizeof(dis));

		snprintf(line, sizeof(line), "%04X:%04X  %-28s AX=%04X BX=%04X CX=%04X DX=%04X\n",
			t->cs, t->ip, dis, t->ax, t->bx, t->cx, t->dx);
		m_trace_buf->append(line);
	}
}

void DbgRegsWindow::write_regs()
{
	for (int i = 0; i < 13; i++) {
		unsigned val;
		if (sscanf(m_reg[i]->value(), "%x", &val) == 1)
			*reg_ptr(m_cpu, i) = (uint16_t)val;
	}
}

void DbgRegsWindow::write_flags()
{
	uint16_t f = 0x0002;
	for (int i = 0; i < 9; i++)
		if (m_flag[i]->value()) f |= flag_bits[i];
	m_cpu->flags = f;
}

void DbgRegsWindow::write_breakpoints()
{
	for (int i = 0; i < 8; i++) {
		m_cpu->bp_enabled[i] = m_bp_en[i]->value();
		unsigned seg = 0, off = 0;
		sscanf(m_bp_addr[i]->value(), "%x:%x", &seg, &off);
		m_cpu->bp_seg[i] = (uint16_t)seg;
		m_cpu->bp_off[i] = (uint16_t)off;
	}
	save_breakpoints();
}

void DbgRegsWindow::load_breakpoints()
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

void DbgRegsWindow::save_breakpoints()
{
	for (int i = 0; i < 8; i++) {
		char key[16];
		snprintf(key, sizeof(key), "bp%d", i);
		prefs_set_str("breakpoints", key, m_bp_addr[i]->value());
		snprintf(key, sizeof(key), "bp%d_en", i);
		prefs_set_int("breakpoints", key, m_bp_en[i]->value());
	}
}

void DbgRegsWindow::cb_stop(Fl_Widget*, void *data) {
	auto *w = (DbgRegsWindow *)data;
	w->m_cpu->debug_stop = true;
	w->refresh();
}

void DbgRegsWindow::cb_step(Fl_Widget*, void *data) {
	auto *w = (DbgRegsWindow *)data;
	w->write_regs();
	w->m_cpu->debug_step = true;
}

void DbgRegsWindow::cb_step_over(Fl_Widget*, void *data) {
	auto *w = (DbgRegsWindow *)data;
	w->write_regs();

	uint8_t code[2];
	v20_t *c = w->m_cpu;
	code[0] = c->mem_read(c->ctx, ((uint32_t)c->cs << 4) + c->ip);
	if (code[0] == 0xE8 || code[0] == 0x9A || code[0] == 0xCC || code[0] == 0xCD) {
		uint8_t buf[8];
		for (int i = 0; i < 8; i++)
			buf[i] = c->mem_read(c->ctx, ((uint32_t)c->cs << 4) + c->ip + i);
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

void DbgRegsWindow::cb_run(Fl_Widget*, void *data) {
	auto *w = (DbgRegsWindow *)data;
	w->write_regs();
	w->m_cpu->debug_stop = false;
}

void DbgRegsWindow::cb_bp_changed(Fl_Widget*, void *data) {
	((DbgRegsWindow *)data)->write_breakpoints();
}

void DbgRegsWindow::cb_reg_changed(Fl_Widget*, void *data) {
	((DbgRegsWindow *)data)->write_regs();
}
