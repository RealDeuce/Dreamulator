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
#include <string>
#include <cstring>
#include <cstdlib>

#include "v20.h"
#include "dis86.h"
#include "prefs.h"
#include "dbg_dis.h"

DbgDisPanel::DbgDisPanel(int X, int Y, int W, int H, v20_t *cpu)
	: Fl_Group(X, Y, W, H), m_cpu(cpu)
{
	box(FL_FLAT_BOX);
	m_seg = cpu->cs;
	m_off = cpu->ip;

	m_addr_label = new Fl_Box(0, 0, 60, 24, "Address:");
	m_addr_input = new Fl_Input(0, 0, 120, 24);
	m_addr_input->textfont(FL_COURIER);
	m_addr_input->textsize(12);
	m_addr_input->callback(cb_goto, this);
	m_addr_input->when(FL_WHEN_ENTER_KEY);

	m_btn_go = new Fl_Button(0, 0, 60, 24, "Go");
	m_btn_go->callback(cb_goto, this);

	m_btn_pc = new Fl_Button(0, 0, 100, 24, "Follow PC");
	m_btn_pc->callback(cb_follow_pc, this);

	m_buf = new Fl_Text_Buffer();
	m_text = new Fl_Text_Display(0, 0, 100, 100);
	m_text->buffer(m_buf);
	m_text->textfont(FL_COURIER);
	m_text->textsize(12);

	end();
	resize(X, Y, W, H);

	char buf[16];
	snprintf(buf, sizeof(buf), "%04X:%04X", m_seg, m_off);
	m_addr_input->value(buf);
}

void DbgDisPanel::resize(int X, int Y, int W, int H)
{
	Fl_Widget::resize(X, Y, W, H);
	m_addr_label->resize(X + 10, Y + 10, 60, 24);
	m_addr_input->resize(X + 70, Y + 10, 120, 24);
	m_btn_go->resize(X + 200, Y + 10, 60, 24);
	m_btn_pc->resize(X + 270, Y + 10, 100, 24);
	m_text->resize(X + 5, Y + 44, W - 10, H - 49);
	damage(FL_DAMAGE_ALL);
}

void DbgDisPanel::refresh()
{
	m_seg = m_cpu->cs;
	m_off = m_cpu->ip;
	char buf[16];
	snprintf(buf, sizeof(buf), "%04X:%04X", m_seg, m_off);
	m_addr_input->value(buf);
	disassemble_region();
}

void DbgDisPanel::goto_addr(uint16_t seg, uint16_t off)
{
	m_seg = seg;
	m_off = off;
	char buf[16];
	snprintf(buf, sizeof(buf), "%04X:%04X", seg, off);
	m_addr_input->value(buf);
	disassemble_region();
}

void DbgDisPanel::disassemble_region()
{
	std::string text;
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
		text += line;
		off += (uint16_t)len;
	}
	m_buf->text(text.c_str());
}

void DbgDisPanel::cb_goto(Fl_Widget*, void *data)
{
	auto *w = (DbgDisPanel *)data;
	unsigned seg = 0, off = 0;
	sscanf(w->m_addr_input->value(), "%x:%x", &seg, &off);
	w->goto_addr((uint16_t)seg, (uint16_t)off);
}

void DbgDisPanel::cb_follow_pc(Fl_Widget*, void *data)
{
	auto *w = (DbgDisPanel *)data;
	w->goto_addr(w->m_cpu->cs, w->m_cpu->ip);
}
