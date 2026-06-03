// license:BSD-3-Clause
// copyright-holders:Stephen Hurd
// Inspired by VirtualT (Copyright 2004 Ken Pettit and Stephen Hurd, BSD-2-Clause)
#ifndef DBG_MEM_H
#define DBG_MEM_H

#include <FL/Fl_Double_Window.H>

extern "C" { struct v20; }

class DbgMemWindow : public Fl_Double_Window {
public:
	DbgMemWindow(struct v20 *cpu);
	void refresh();
	void goto_addr(uint32_t addr);

private:
	struct v20 *m_cpu;
	uint32_t m_base;
	class Fl_Input *m_addr_input;
	class Fl_Text_Display *m_text;
	class Fl_Text_Buffer *m_buf;

	void render();
	static void cb_goto(Fl_Widget*, void*);
};

#endif
