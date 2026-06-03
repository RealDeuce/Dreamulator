// license:BSD-3-Clause
// copyright-holders:Stephen Hurd
// Inspired by VirtualT (Copyright 2004 Ken Pettit and Stephen Hurd, BSD-2-Clause)
#ifndef DBG_MEM_H
#define DBG_MEM_H

#include <FL/Fl_Double_Window.H>

struct v20_t;

class DbgMemWindow : public Fl_Double_Window {
public:
	DbgMemWindow(v20_t *cpu);
	void refresh();
	void goto_addr(uint32_t addr);

private:
	v20_t *m_cpu;
	uint32_t m_base;
	class Fl_Input *m_addr_input;
	class Fl_Text_Display *m_text;
	class Fl_Text_Buffer *m_buf;

	void render();
	static void cb_goto(Fl_Widget*, void*);
};

#endif
