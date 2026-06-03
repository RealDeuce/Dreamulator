// license:BSD-3-Clause
// copyright-holders:Stephen Hurd
// Inspired by VirtualT (Copyright 2004 Ken Pettit and Stephen Hurd, BSD-2-Clause)
#ifndef DBG_MEM_H
#define DBG_MEM_H

#include <FL/Fl_Group.H>

struct v20_t;

class DbgMemPanel : public Fl_Group {
public:
	DbgMemPanel(int x, int y, int w, int h, v20_t *cpu);
	void refresh();
	void resize(int X, int Y, int W, int H) override;
	void goto_addr(uint32_t addr);

private:
	v20_t *m_cpu;
	uint32_t m_base;
	class Fl_Box *m_addr_label;
	class Fl_Input *m_addr_input;
	class Fl_Button *m_btn_go;
	class Fl_Text_Display *m_text;
	class Fl_Text_Buffer *m_buf;

	void render();
	static void cb_goto(Fl_Widget*, void*);
};

#endif
