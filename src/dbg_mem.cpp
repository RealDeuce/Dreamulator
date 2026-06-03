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
#include <cstring>
#include <cstdlib>
#include <cctype>

extern "C" {
#include "v20.h"
}
#include "prefs.h"
#include "dbg_mem.h"

DbgMemWindow::DbgMemWindow(v20_t *cpu)
	: Fl_Double_Window(620, 500, "Memory Editor"), m_cpu(cpu), m_base(0)
{
	new Fl_Box(10, 10, 60, 24, "Address:");
	m_addr_input = new Fl_Input(70, 10, 100, 24);
	m_addr_input->textfont(FL_COURIER);
	m_addr_input->textsize(12);
	m_addr_input->value("00000");
	m_addr_input->callback(cb_goto, this);
	m_addr_input->when(FL_WHEN_ENTER_KEY);

	Fl_Button *bgo = new Fl_Button(180, 10, 60, 24, "Go");
	bgo->callback(cb_goto, this);

	m_buf = new Fl_Text_Buffer();
	m_text = new Fl_Text_Display(10, 44, 600, 446);
	m_text->buffer(m_buf);
	m_text->textfont(FL_COURIER);
	m_text->textsize(12);

	end();

	int wx = 520, wy = 300, ww = 620, wh = 500;
	prefs_load_window("dbg_mem", wx, wy, ww, wh);
	position(wx, wy);
	size(ww, wh);
}

void DbgMemWindow::refresh()
{
	render();
}

void DbgMemWindow::goto_addr(uint32_t addr)
{
	m_base = addr & 0xFFFF0;
	char buf[16];
	snprintf(buf, sizeof(buf), "%05X", m_base);
	m_addr_input->value(buf);
	render();
}

void DbgMemWindow::render()
{
	m_buf->text("");
	char line[128];
	int lines = 32;

	for (int row = 0; row < lines; row++) {
		uint32_t addr = (m_base + row * 16) & 0xFFFFF;
		int p = snprintf(line, sizeof(line), "%05X  ", addr);

		uint8_t data[16];
		for (int i = 0; i < 16; i++)
			data[i] = m_cpu->mem_read(m_cpu->ctx, (addr + i) & 0xFFFFF);

		for (int i = 0; i < 16; i++) {
			p += snprintf(line + p, sizeof(line) - p, "%02X ", data[i]);
			if (i == 7) line[p++] = ' ';
		}
		p += snprintf(line + p, sizeof(line) - p, " ");
		for (int i = 0; i < 16; i++) {
			char c = (data[i] >= 0x20 && data[i] < 0x7f) ? (char)data[i] : '.';
			line[p++] = c;
		}
		line[p++] = '\n';
		line[p] = 0;
		m_buf->append(line);
	}
}

void DbgMemWindow::cb_goto(Fl_Widget*, void *data)
{
	auto *w = (DbgMemWindow *)data;
	unsigned addr = 0;
	const char *s = w->m_addr_input->value();
	unsigned seg, off;
	if (sscanf(s, "%x:%x", &seg, &off) == 2)
		addr = (seg << 4) + off;
	else
		sscanf(s, "%x", &addr);
	w->goto_addr(addr);
}
