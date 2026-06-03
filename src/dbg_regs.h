// license:BSD-3-Clause
// copyright-holders:Stephen Hurd
// Inspired by VirtualT (Copyright 2006 Ken Pettit, BSD-2-Clause)
#ifndef DBG_REGS_H
#define DBG_REGS_H

#include <FL/Fl_Group.H>

struct v20_t;

class DbgRegsPanel : public Fl_Group {
public:
	DbgRegsPanel(int x, int y, int w, int h, v20_t *cpu);
	void refresh();
	void resize(int X, int Y, int W, int H) override;

private:
	v20_t *m_cpu;

	class Fl_Box *m_reg_label[13];
	class Fl_Input *m_reg[13];
	class Fl_Check_Button *m_flag[9];
	class Fl_Button *m_btn[4];
	class Fl_Box *m_bp_label;
	class Fl_Input *m_bp_addr[8];
	class Fl_Check_Button *m_bp_en[8];
	class Fl_Box *m_trace_label;
	class Fl_Text_Display *m_trace;
	class Fl_Text_Buffer *m_trace_buf;

	void layout(int X, int Y, int W, int H);
	void update_regs();
	void update_flags();
	void update_trace();
	void write_regs();
	void write_flags();
	void write_breakpoints();
	void load_breakpoints();
	void save_breakpoints();

	static void cb_stop(Fl_Widget*, void*);
	static void cb_step(Fl_Widget*, void*);
	static void cb_step_over(Fl_Widget*, void*);
	static void cb_run(Fl_Widget*, void*);
	static void cb_bp_changed(Fl_Widget*, void*);
	static void cb_reg_changed(Fl_Widget*, void*);
};

#endif
