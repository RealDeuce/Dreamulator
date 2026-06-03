// license:BSD-3-Clause
// copyright-holders:Stephen Hurd
// Inspired by VirtualT (Copyright 2004 Stephen Hurd and Ken Pettit, BSD-2-Clause)
#include <FL/Fl.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Text_Display.H>
#include <FL/Fl_Text_Buffer.H>
#include <cstdio>
#include <cstring>
#include <cstdlib>

#include "v20.h"
#include "dis86.h"
#include "prefs.h"
#include "dbg_dis.h"

DbgDisWindow::DbgDisWindow(v20_t *cpu)
	: Fl_Double_Window(500, 500, "Disassembly"), m_cpu(cpu)
{
	m_seg = cpu->cs;
	m_off = cpu->ip;

	new Fl_Box(10, 10, 60, 24, "Address:");
	m_addr_input = new Fl_Input(70, 10, 120, 24);
	m_addr_input->textfont(FL_COURIER);
	m_addr_input->textsize(12);
	m_addr_input->callback(cb_goto, this);
	m_addr_input->when(FL_WHEN_ENTER_KEY);

	Fl_Button *bgo = new Fl_Button(200, 10, 60, 24, "Go");
	bgo->callback(cb_goto, this);

	Fl_Button *bpc = new Fl_Button(270, 10, 100, 24, "Follow PC");
	bpc->callback(cb_follow_pc, this);

	m_buf = new Fl_Text_Buffer();
	m_text = new Fl_Text_Display(10, 44, 480, 446);
	m_text->buffer(m_buf);
	m_text->textfont(FL_COURIER);
	m_text->textsize(12);

	end();

	int wx = 520, wy = 100, ww = 500, wh = 500;
	prefs_load_window("dbg_dis", wx, wy, ww, wh);
	position(wx, wy);
	size(ww, wh);

	char buf[16];
	snprintf(buf, sizeof(buf), "%04X:%04X", m_seg, m_off);
	m_addr_input->value(buf);
}

void DbgDisWindow::refresh()
{
	m_seg = m_cpu->cs;
	m_off = m_cpu->ip;
	char buf[16];
	snprintf(buf, sizeof(buf), "%04X:%04X", m_seg, m_off);
	m_addr_input->value(buf);
	disassemble_region();
}

void DbgDisWindow::goto_addr(uint16_t seg, uint16_t off)
{
	m_seg = seg;
	m_off = off;
	char buf[16];
	snprintf(buf, sizeof(buf), "%04X:%04X", seg, off);
	m_addr_input->value(buf);
	disassemble_region();
}

void DbgDisWindow::disassemble_region()
{
	m_buf->text("");
	char line[256], dis[128];
	uint16_t off = m_off;

	for (int i = 0; i < 100; i++) {
		uint8_t code[8];
		for (int j = 0; j < 8; j++)
			code[j] = m_cpu->mem_read(m_cpu->ctx,
				((uint32_t)m_seg << 4) + (uint16_t)(off + j));

		int len = dis86(code, 8, off, dis, sizeof(dis));

		char hex[32] = "";
		int hpos = 0;
		for (int j = 0; j < len && j < 6; j++)
			hpos += snprintf(hex + hpos, sizeof(hex) - hpos, "%02X ", code[j]);

		char marker = (m_seg == m_cpu->cs && off == m_cpu->ip) ? '>' : ' ';
		snprintf(line, sizeof(line), "%c%04X:%04X  %-18s %s\n",
			marker, m_seg, off, hex, dis);
		m_buf->append(line);
		off += (uint16_t)len;
	}
}

void DbgDisWindow::cb_goto(Fl_Widget*, void *data)
{
	auto *w = (DbgDisWindow *)data;
	unsigned seg = 0, off = 0;
	sscanf(w->m_addr_input->value(), "%x:%x", &seg, &off);
	w->goto_addr((uint16_t)seg, (uint16_t)off);
}

void DbgDisWindow::cb_follow_pc(Fl_Widget*, void *data)
{
	auto *w = (DbgDisWindow *)data;
	w->goto_addr(w->m_cpu->cs, w->m_cpu->ip);
}
