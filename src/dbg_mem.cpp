// license:BSD-3-Clause
// copyright-holders:Stephen Hurd
// Inspired by VirtualT (Copyright 2004 Ken Pettit and Stephen Hurd, BSD-2-Clause)
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
#include <cctype>

#include "v20.h"
#include "prefs.h"
#include "dbg_mem.h"

DbgMemPanel::DbgMemPanel(int X, int Y, int W, int H, v20_t *cpu)
	: Fl_Group(X, Y, W, H), m_cpu(cpu), m_base(0)
{
	box(FL_FLAT_BOX);

	m_addr_label = new Fl_Box(0, 0, 60, 24, "Address:");
	m_addr_input = new Fl_Input(0, 0, 100, 24);
	m_addr_input->textfont(FL_COURIER);
	m_addr_input->textsize(12);
	m_addr_input->value("00000");
	m_addr_input->callback(cb_goto, this);
	m_addr_input->when(FL_WHEN_ENTER_KEY);

	m_btn_go = new Fl_Button(0, 0, 60, 24, "Go");
	m_btn_go->callback(cb_goto, this);

	m_buf = new Fl_Text_Buffer();
	m_text = new Fl_Text_Display(0, 0, 100, 100);
	m_text->buffer(m_buf);
	m_text->textfont(FL_COURIER);
	m_text->textsize(12);

	end();
	resize(X, Y, W, H);
}

void DbgMemPanel::resize(int X, int Y, int W, int H)
{
	Fl_Widget::resize(X, Y, W, H);
	m_addr_label->resize(X + 10, Y + 10, 60, 24);
	m_addr_input->resize(X + 70, Y + 10, 100, 24);
	m_btn_go->resize(X + 180, Y + 10, 60, 24);
	m_text->resize(X + 5, Y + 44, W - 10, H - 49);
	damage(FL_DAMAGE_ALL);
}

void DbgMemPanel::refresh()
{
	render();
}

void DbgMemPanel::goto_addr(uint32_t addr)
{
	m_base = addr & 0xFFFF0;
	char buf[16];
	snprintf(buf, sizeof(buf), "%05X", m_base);
	m_addr_input->value(buf);
	render();
}

void DbgMemPanel::render()
{
	std::string text;
	char line[128];
	int lines = 32;

	for (int row = 0; row < lines; row++) {
		uint32_t addr = (m_base + (uint32_t)row * 16) & 0xFFFFF;
		int p = snprintf(line, sizeof(line), "%05X  ", addr);

		uint8_t data[16];
		for (int i = 0; i < 16; i++)
			data[i] = m_cpu->mem_read(m_cpu->ctx, (addr + (uint32_t)i) & 0xFFFFF);

		for (int i = 0; i < 16; i++) {
			p += snprintf(line + p, sizeof(line) - (size_t)p, "%02X ", data[i]);
			if (i == 7) line[p++] = ' ';
		}
		p += snprintf(line + p, sizeof(line) - (size_t)p, " ");
		for (int i = 0; i < 16; i++) {
			char c = (data[i] >= 0x20 && data[i] < 0x7f) ? (char)data[i] : '.';
			line[p++] = c;
		}
		line[p++] = '\n';
		line[p] = 0;
		text += line;
	}
	m_buf->text(text.c_str());
}

void DbgMemPanel::cb_goto(Fl_Widget*, void *data)
{
	auto *w = (DbgMemPanel *)data;
	unsigned addr = 0;
	const char *s = w->m_addr_input->value();
	unsigned seg, off;
	if (sscanf(s, "%x:%x", &seg, &off) == 2)
		addr = (seg << 4) + off;
	else
		sscanf(s, "%x", &addr);
	w->goto_addr(addr);
}
