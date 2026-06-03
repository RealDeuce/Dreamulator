// license:BSD-3-Clause
// copyright-holders:Stephen Hurd
// Inspired by VirtualT (Copyright 2006 Ken Pettit, BSD-2-Clause)
#ifndef DBG_REGS_H
#define DBG_REGS_H

#include <FL/Fl_Double_Window.H>

extern "C" { struct v20; }

class DbgRegsWindow : public Fl_Double_Window {
public:
	DbgRegsWindow(struct v20 *cpu);
	void refresh();

private:
	struct v20 *m_cpu;

	class Fl_Input *m_reg[13];
	class Fl_Check_Button *m_flag[9];
	class Fl_Input *m_bp_addr[8];
	class Fl_Check_Button *m_bp_en[8];
	class Fl_Text_Display *m_trace;
	class Fl_Text_Buffer *m_trace_buf;

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
