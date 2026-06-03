// license:BSD-3-Clause
// copyright-holders:Stephen Hurd
// Inspired by VirtualT (Copyright 2004 Stephen Hurd and Ken Pettit, BSD-2-Clause)
#ifndef DBG_DIS_H
#define DBG_DIS_H

#include <FL/Fl_Double_Window.H>

extern "C" { struct v20; }

class DbgDisWindow : public Fl_Double_Window {
public:
	DbgDisWindow(struct v20 *cpu);
	void refresh();
	void goto_addr(uint16_t seg, uint16_t off);

private:
	struct v20 *m_cpu;
	uint16_t m_seg, m_off;
	class Fl_Input *m_addr_input;
	class Fl_Text_Display *m_text;
	class Fl_Text_Buffer *m_buf;

	void disassemble_region();
	static void cb_goto(Fl_Widget*, void*);
	static void cb_follow_pc(Fl_Widget*, void*);
};

#endif
