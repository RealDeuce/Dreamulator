// license:BSD-3-Clause
// copyright-holders:Stephen Hurd
// Inspired by VirtualT (Copyright 2004 Stephen Hurd and Ken Pettit, BSD-2-Clause)
#ifndef DBG_DIS_H
#define DBG_DIS_H

#include <FL/Fl_Group.H>

struct v20_t;

class DbgDisPanel : public Fl_Group {
public:
	DbgDisPanel(int x, int y, int w, int h, v20_t *cpu);
	void refresh();
	void resize(int X, int Y, int W, int H) override;
	void goto_addr(uint16_t seg, uint16_t off);

private:
	v20_t *m_cpu;
	uint16_t m_seg, m_off;
	class Fl_Box *m_addr_label;
	class Fl_Input *m_addr_input;
	class Fl_Button *m_btn_go;
	class Fl_Button *m_btn_pc;
	class Fl_Text_Display *m_text;
	class Fl_Text_Buffer *m_buf;

	void disassemble_region();
	static void cb_goto(Fl_Widget*, void*);
	static void cb_follow_pc(Fl_Widget*, void*);
};

#endif
