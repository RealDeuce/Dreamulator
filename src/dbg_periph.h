// license:BSD-3-Clause
// copyright-holders:Stephen Hurd
// Inspired by VirtualT (Copyright 2004 Stephen Hurd and Ken Pettit, BSD-2-Clause)
#ifndef DBG_PERIPH_H
#define DBG_PERIPH_H

#include <FL/Fl_Group.H>

struct machine_t;

void periph_log_serial_tx(uint8_t byte);
void periph_log_serial_rx(uint8_t byte);
void periph_log_parallel(uint8_t byte);
void periph_log_io_read(uint16_t port, uint8_t val);
void periph_log_io_write(uint16_t port, uint8_t val);

class DbgPeriphPanel : public Fl_Group {
public:
	DbgPeriphPanel(int x, int y, int w, int h, machine_t *mach);
	void refresh();
	void resize(int X, int Y, int W, int H) override;

private:
	class Fl_Tabs *m_tabs;
	class Fl_Group *m_tab_serial;
	class Fl_Group *m_tab_parallel;
	class Fl_Group *m_tab_io;
	class Fl_Text_Display *m_serial_log;
	class Fl_Text_Buffer  *m_serial_buf;
	class Fl_Button *m_btn_clr_serial;
	class Fl_Text_Display *m_parallel_log;
	class Fl_Text_Buffer  *m_parallel_buf;
	class Fl_Button *m_btn_clr_parallel;
	class Fl_Text_Display *m_io_log;
	class Fl_Text_Buffer  *m_io_buf;
	class Fl_Button *m_btn_clr_io;

	static void cb_clear_serial(Fl_Widget*, void*);
	static void cb_clear_parallel(Fl_Widget*, void*);
	static void cb_clear_io(Fl_Widget*, void*);
};

#endif
