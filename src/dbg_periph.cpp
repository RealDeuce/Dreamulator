// license:BSD-3-Clause
// copyright-holders:Stephen Hurd
// Inspired by VirtualT (Copyright 2004 Stephen Hurd and Ken Pettit, BSD-2-Clause)
#include <FL/Fl.H>
#include <FL/Fl_Tabs.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Text_Display.H>
#include <FL/Fl_Text_Buffer.H>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <sys/time.h>

#include "machine.h"
#include "prefs.h"
#include "dbg_periph.h"

/* ---- ring log ---- */

#define LOG_MAX 8192

struct log_entry {
	double   time;
	uint16_t port;
	uint8_t  val;
	uint8_t  flags;
};

enum { F_TX = 1, F_RX = 2, F_PAR = 3, F_IO_R = 4, F_IO_W = 5 };

static log_entry g_serial_log[LOG_MAX];
static int       g_serial_head, g_serial_count;

static log_entry g_parallel_log[LOG_MAX];
static int       g_parallel_head, g_parallel_count;

static log_entry g_io_log[LOG_MAX];
static int       g_io_head, g_io_count;

static double now_secs(void)
{
	struct timeval tv;
	gettimeofday(&tv, nullptr);
	return tv.tv_sec + tv.tv_usec * 1e-6;
}

static void log_add(log_entry *ring, int &head, int &count,
                    uint16_t port, uint8_t val, uint8_t flags)
{
	ring[head].time = now_secs();
	ring[head].port = port;
	ring[head].val = val;
	ring[head].flags = flags;
	head = (head + 1) % LOG_MAX;
	if (count < LOG_MAX) count++;
}

void periph_log_serial_tx(uint8_t byte)
{
	log_add(g_serial_log, g_serial_head, g_serial_count, 0xC0, byte, F_TX);
}

void periph_log_serial_rx(uint8_t byte)
{
	log_add(g_serial_log, g_serial_head, g_serial_count, 0xC0, byte, F_RX);
}

void periph_log_parallel(uint8_t byte)
{
	log_add(g_parallel_log, g_parallel_head, g_parallel_count, 0x40, byte, F_PAR);
}

void periph_log_io_read(uint16_t port, uint8_t val)
{
	log_add(g_io_log, g_io_head, g_io_count, port, val, F_IO_R);
}

void periph_log_io_write(uint16_t port, uint8_t val)
{
	log_add(g_io_log, g_io_head, g_io_count, port, val, F_IO_W);
}

/* ---- format helpers ---- */

static void format_serial(Fl_Text_Buffer *buf, log_entry *ring, int head, int count)
{
	buf->text("");
	char line[128];
	int start = (head - count + LOG_MAX) % LOG_MAX;
	int show = count < 500 ? count : 500;
	int begin = (head - show + LOG_MAX) % LOG_MAX;

	for (int i = 0; i < show; i++) {
		int idx = (begin + i) % LOG_MAX;
		log_entry *e = &ring[idx];
		const char *dir = (e->flags == F_TX) ? "TX" : "RX";
		char ch = (e->val >= 0x20 && e->val < 0x7f) ? (char)e->val : '.';
		snprintf(line, sizeof(line), "%s %02X '%c'\n", dir, e->val, ch);
		buf->append(line);
	}
}

static void format_parallel(Fl_Text_Buffer *buf, log_entry *ring, int head, int count)
{
	buf->text("");
	char line[128];
	int show = count < 500 ? count : 500;
	int begin = (head - show + LOG_MAX) % LOG_MAX;

	for (int i = 0; i < show; i++) {
		int idx = (begin + i) % LOG_MAX;
		log_entry *e = &ring[idx];
		char ch = (e->val >= 0x20 && e->val < 0x7f) ? (char)e->val : '.';
		snprintf(line, sizeof(line), "OUT %02X '%c'\n", e->val, ch);
		buf->append(line);
	}
}

static void format_io(Fl_Text_Buffer *buf, log_entry *ring, int head, int count)
{
	buf->text("");
	char line[128];
	int show = count < 500 ? count : 500;
	int begin = (head - show + LOG_MAX) % LOG_MAX;

	for (int i = 0; i < show; i++) {
		int idx = (begin + i) % LOG_MAX;
		log_entry *e = &ring[idx];
		const char *dir = (e->flags == F_IO_R) ? "IN " : "OUT";
		snprintf(line, sizeof(line), "%s  %04X = %02X\n", dir, e->port, e->val);
		buf->append(line);
	}
}

/* ---- window ---- */

DbgPeriphWindow::DbgPeriphWindow(machine_t *mach)
	: Fl_Double_Window(500, 450, "Peripheral Monitor"), m_mach(mach)
{
	m_tabs = new Fl_Tabs(0, 0, 500, 420);

	{ Fl_Group *g = new Fl_Group(0, 25, 500, 395, "Serial");
	  m_serial_buf = new Fl_Text_Buffer();
	  m_serial_log = new Fl_Text_Display(5, 30, 490, 355);
	  m_serial_log->buffer(m_serial_buf);
	  m_serial_log->textfont(FL_COURIER);
	  m_serial_log->textsize(11);
	  Fl_Button *bc = new Fl_Button(5, 390, 80, 24, "Clear");
	  bc->callback(cb_clear_serial, this);
	  g->end();
	}

	{ Fl_Group *g = new Fl_Group(0, 25, 500, 395, "Parallel");
	  m_parallel_buf = new Fl_Text_Buffer();
	  m_parallel_log = new Fl_Text_Display(5, 30, 490, 355);
	  m_parallel_log->buffer(m_parallel_buf);
	  m_parallel_log->textfont(FL_COURIER);
	  m_parallel_log->textsize(11);
	  Fl_Button *bc = new Fl_Button(5, 390, 80, 24, "Clear");
	  bc->callback(cb_clear_parallel, this);
	  g->end();
	}

	{ Fl_Group *g = new Fl_Group(0, 25, 500, 395, "I/O Ports");
	  m_io_buf = new Fl_Text_Buffer();
	  m_io_log = new Fl_Text_Display(5, 30, 490, 355);
	  m_io_log->buffer(m_io_buf);
	  m_io_log->textfont(FL_COURIER);
	  m_io_log->textsize(11);
	  Fl_Button *bc = new Fl_Button(5, 390, 80, 24, "Clear");
	  bc->callback(cb_clear_io, this);
	  g->end();
	}

	m_tabs->end();
	end();

	int wx = 100, wy = 400, ww = 500, wh = 450;
	prefs_load_window("dbg_periph", wx, wy, ww, wh);
	position(wx, wy);
	size(ww, wh);
}

void DbgPeriphWindow::refresh()
{
	format_serial(m_serial_buf, g_serial_log, g_serial_head, g_serial_count);
	format_parallel(m_parallel_buf, g_parallel_log, g_parallel_head, g_parallel_count);
	format_io(m_io_buf, g_io_log, g_io_head, g_io_count);
}

void DbgPeriphWindow::cb_clear_serial(Fl_Widget*, void *data)
{
	g_serial_head = g_serial_count = 0;
	((DbgPeriphWindow *)data)->m_serial_buf->text("");
}

void DbgPeriphWindow::cb_clear_parallel(Fl_Widget*, void *data)
{
	g_parallel_head = g_parallel_count = 0;
	((DbgPeriphWindow *)data)->m_parallel_buf->text("");
}

void DbgPeriphWindow::cb_clear_io(Fl_Widget*, void *data)
{
	g_io_head = g_io_count = 0;
	((DbgPeriphWindow *)data)->m_io_buf->text("");
}
