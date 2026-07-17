// license:BSD-3-Clause
// copyright-holders:Stephen Hurd
#include <FL/Fl.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Menu_Bar.H>
#include <FL/Fl_Tile.H>
#include <FL/Fl_Widget.H>
#include <FL/Fl_File_Chooser.H>
#include <FL/fl_ask.H>
#include <FL/fl_draw.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Spinner.H>
#include <FL/Fl_Return_Button.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Group.H>
#ifdef HAS_PORTAUDIO
#include <portaudio.h>
#endif
#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/mman.h>
#ifndef _WIN32
#include <csignal>
#endif

#include "machine.h"
#include "paths.h"
#include "print/printer.h"
#include "print/fontljii.h"
#include "prefs.h"
#include "dbg_regs.h"
#include "dbg_dis.h"
#include "dbg_mem.h"
#include "remote.h"
#include "dbg_periph.h"

#define SCALE       2
#define MENUBAR_H   25
#define SAMPLE_RATE 48000
#define AUDIO_BUF   512
#define BEEP_VOL    0.05f

class LcdWidget;

/* ---- globals ---- */

static machine_t    g_mach;
#ifdef HAS_PORTAUDIO
static PaStream    *g_audio;
#endif
static int          g_speed = 1;
static bool         g_shutting_down = false;
static int          g_remote_port = 0;
static DbgRegsPanel   *g_dbg_regs   = nullptr;
static DbgDisPanel    *g_dbg_dis    = nullptr;
static DbgMemPanel    *g_dbg_mem    = nullptr;
static DbgPeriphPanel *g_dbg_periph = nullptr;

static Fl_Double_Window *g_win_regs   = nullptr;
static Fl_Double_Window *g_win_dis    = nullptr;
static Fl_Double_Window *g_win_mem    = nullptr;
static Fl_Double_Window *g_win_periph = nullptr;

static Fl_Tile *g_dock       = nullptr;
static LcdWidget *g_lcd      = nullptr;
static int g_lcd_h           = 0;
static int g_dock_h          = 300;

static bool g_docked_regs   = false;
static bool g_docked_dis    = false;
static bool g_docked_mem    = false;
static bool g_docked_periph = false;
static Fl_Double_Window *g_main_win = nullptr;
static char         g_nvram_path[1024];
static char         g_pccard_path[1024];
static char         g_floppy_path[1024];
static const model_t *g_model;

/* ---- keyboard map ---- */

struct kmap { int key; int row, bit; };

static const kmap g_keymap[] = {
	{ FL_Shift_L,  0,0 }, { FL_Shift_R,  0,1 },
	{ FL_Left,     0,3 }, { FL_Enter,    0,4 },
	{ FL_Alt_L,    1,0 }, { '`',         1,1 },
	{ FL_Escape,   1,2 }, { ' ',         1,3 }, { '5',  1,6 },
	{ FL_Control_L,2,0 }, { FL_Caps_Lock,2,1 },
	{ '1',         2,2 }, { FL_Tab,      2,3 },
	{ '3',  3,0 }, { '2',  3,1 }, { 'q',  3,2 }, { 'w',  3,3 },
	{ 'e',  3,4 }, { 's',  3,6 }, { 'd',  3,7 },
	{ '4',  4,0 }, { 'z',  4,2 }, { 'x',  4,3 },
	{ 'a',  4,4 }, { 'r',  4,6 }, { 'f',  4,7 },
	{ 'b',  5,2 }, { 'v',  5,3 }, { 't',  5,4 },
	{ 'y',  5,5 }, { 'g',  5,6 }, { 'c',  5,7 },
	{ '6',  6,0 }, { FL_Down,  6,1 }, { FL_Insert, 6,2 },
	{ FL_Right, 6,3 }, { '\\', 6,4 }, { '/', 6,5 },
	{ 'h',  6,6 }, { 'n',  6,7 },
	{ '=',  7,0 }, { '7',  7,1 }, { FL_Page_Up,  7,2 },
	{ FL_Up, 7,3 }, { FL_Page_Down, 7,4 },
	{ 'u',  7,5 }, { 'm',  7,6 }, { 'k',  7,7 },
	{ '8',  8,0 }, { '-',  8,1 }, { ']',  8,2 }, { '[',  8,3 },
	{ '\'', 8,4 }, { 'i',  8,5 }, { 'j',  8,6 }, { ',',  8,7 },
	{ '0',  9,0 }, { '9',  9,1 }, { FL_BackSpace, 9,2 },
	{ 'p',  9,3 }, { ';',  9,4 }, { 'l',  9,5 },
	{ 'o',  9,6 }, { '.',  9,7 },
	{ 0, -1, -1 }
};

/* ---- LCD widget ---- */

class LcdWidget : public Fl_Widget {
	uint32_t argb[LCD_WIDTH * MAX_LCD_H];
	uint8_t  rgb[LCD_WIDTH * SCALE * MAX_LCD_H * SCALE * 3];
	bool     power_down = false;
public:
	LcdWidget(int x, int y, int w, int h) : Fl_Widget(x, y, w, h) {}

	void release_keys() {
		machine_keys_all_up(&g_mach);
		if (power_down) {
			machine_power_button(&g_mach, false);
			power_down = false;
		}
	}

	void draw() override {
		machine_render_lcd(&g_mach, argb);
		int sw = LCD_WIDTH, sh = g_mach.lcd_height;
		for (int y = 0; y < sh; y++) {
			for (int x = 0; x < sw; x++) {
				uint32_t p = argb[y * sw + x];
				uint8_t r = (p >> 16) & 0xFF;
				uint8_t g = (p >> 8) & 0xFF;
				uint8_t b = p & 0xFF;
				for (int sy = 0; sy < SCALE; sy++) {
					int row = (y * SCALE + sy) * sw * SCALE * 3;
					for (int sx = 0; sx < SCALE; sx++) {
						int di = row + (x * SCALE + sx) * 3;
						rgb[di] = r; rgb[di+1] = g; rgb[di+2] = b;
					}
				}
			}
		}
		fl_draw_image(rgb, this->x(), this->y(), sw * SCALE, sh * SCALE, 3);
	}

	int handle(int event) override {
		if (event == FL_FOCUS) return 1;

		if (event == FL_UNFOCUS || event == FL_HIDE || event == FL_DEACTIVATE) {
			release_keys();
			return 1;
		}

		if (event == FL_KEYDOWN || event == FL_KEYUP) {
			int key = Fl::event_key();
			bool down = (event == FL_KEYDOWN);

			if (key == FL_End) {
				machine_power_button(&g_mach, down);
				power_down = down;
				return 1;
			}

			for (const kmap *k = g_keymap; k->row >= 0; k++) {
				if (k->key == key) {
					if (down) machine_key_down(&g_mach, k->row, k->bit);
					else      machine_key_up(&g_mach, k->row, k->bit);
					return 1;
				}
			}
			return 0;
		}
		return Fl_Widget::handle(event);
	}
};

#ifdef HAS_PORTAUDIO
/* ---- PortAudio callback ---- */

static int pa_callback(const void *, void *out, unsigned long frames,
	const PaStreamCallbackTimeInfo *, PaStreamCallbackFlags, void *data)
{
	machine_t *m = (machine_t *)data;
	float *buf = (float *)out;
	uint16_t div = (uint16_t)(m->buzzer_low | (m->buzzer_high << 8));
	float freq = (div && m->buzzer_on) ? (float)(XTAL / 64) / (float)div : 0.0f;
	float step = freq / (float)SAMPLE_RATE;

	for (unsigned i = 0; i < frames; i++) {
		if (freq > 0.0f) {
			buf[i] = (m->beeper_phase < 0.5f) ? BEEP_VOL : -BEEP_VOL;
			m->beeper_phase += step;
			if (m->beeper_phase >= 1.0f) m->beeper_phase -= 1.0f;
		} else {
			buf[i] = 0.0f;
			m->beeper_phase = 0.0f;
		}
	}
	return paContinue;
}
#endif

static void sync_dock_height();

/* ---- emulation tick ---- */

static void emu_tick(void *)
{
	int cycles;
	switch (g_speed) {
	case 0: cycles = CPU_CLOCK / 120; break;
	default:
	case 1: cycles = CPU_CLOCK / 60; break;
	case 2: cycles = CPU_CLOCK / 30; break;
	case 3: cycles = CPU_CLOCK / 6; break;
	}
	if (g_shutting_down) return;
	g_mach.cpu.trace_enabled = (g_dbg_regs && g_dbg_regs->visible_r());
	sync_dock_height();
	machine_step(&g_mach, cycles);
	if (machine_pdf_check_idle(&g_mach, 5)) {
		if (g_mach.cent_backend == CentBackend::Pdf)
			g_mach.cent_backend = CentBackend::File;
		fprintf(stderr, "PDF printer: auto-finished (idle timeout)\n");
	}
	g_lcd->redraw();
	if (g_dbg_periph && g_dbg_periph->visible_r()) g_dbg_periph->refresh();
	Fl::repeat_timeout((g_speed == 3) ? 0.001 : 1.0/60.0, emu_tick, nullptr);
}

static bool reconnect_uart(UartBackend backend, int port, const char *path) {
	auto saved_txrdy = g_mach.uart.txrdy_cb;
	auto saved_rxrdy = g_mach.uart.rxrdy_cb;
	auto saved_ctx   = g_mach.uart.cb_ctx;
	uart_destroy(&g_mach.uart);
	if (uart_init(&g_mach.uart, backend, port, path) < 0) {
		g_mach.uart.txrdy_cb = saved_txrdy;
		g_mach.uart.rxrdy_cb = saved_rxrdy;
		g_mach.uart.cb_ctx   = saved_ctx;
		return false;
	}
	g_mach.uart.txrdy_cb = saved_txrdy;
	g_mach.uart.rxrdy_cb = saved_rxrdy;
	g_mach.uart.cb_ctx   = saved_ctx;
	return true;
}

/* ---- menu callbacks ---- */

/* ---- debug callbacks ---- */

static void debug_monitor_cb(void *)
{
	if (g_dbg_regs && g_dbg_regs->visible_r()) g_dbg_regs->refresh();
	if (g_dbg_dis && g_dbg_dis->visible_r()) g_dbg_dis->refresh();
	if (g_dbg_periph && g_dbg_periph->visible_r()) g_dbg_periph->refresh();
}

/* ---- dock infrastructure ---- */

static void cb_dbg_win_close(Fl_Widget *w, void *) { w->hide(); }

static bool panel_in_dock(Fl_Group *panel, bool docked)
{
	return docked && panel && panel->visible() && panel->parent() == g_dock;
}

static int dock_panel_count()
{
	int n = 0;
	if (panel_in_dock(g_dbg_regs, g_docked_regs)) n++;
	if (panel_in_dock(g_dbg_dis, g_docked_dis)) n++;
	if (panel_in_dock(g_dbg_mem, g_docked_mem)) n++;
	if (panel_in_dock(g_dbg_periph, g_docked_periph)) n++;
	return n;
}

static void relayout_dock()
{
	if (!g_dock || !g_main_win) return;

	int lcd_bottom = MENUBAR_H + g_lcd_h * SCALE;
	int n = dock_panel_count();
	if (n == 0) {
		g_dock->hide();
		g_main_win->resizable(nullptr);
		g_main_win->size(g_main_win->w(), lcd_bottom);
		g_main_win->size_range(LCD_WIDTH * SCALE, lcd_bottom,
		                       0, lcd_bottom);
		g_main_win->redraw();
		return;
	}

	int win_w = g_main_win->w();
	int win_h = lcd_bottom + g_dock_h;

	Fl_Group *panels[4];
	int count = 0;
	if (panel_in_dock(g_dbg_regs, g_docked_regs)) panels[count++] = g_dbg_regs;
	if (panel_in_dock(g_dbg_dis, g_docked_dis)) panels[count++] = g_dbg_dis;
	if (panel_in_dock(g_dbg_mem, g_docked_mem)) panels[count++] = g_dbg_mem;
	if (panel_in_dock(g_dbg_periph, g_docked_periph)) panels[count++] = g_dbg_periph;

	int pw = win_w / count;
	for (int i = 0; i < count; i++) {
		int px = i * pw;
		int w = (i == count - 1) ? (win_w - px) : pw;
		panels[i]->resize(px, lcd_bottom, w, g_dock_h);
		panels[i]->show();
	}

	g_dock->Fl_Widget::resize(0, lcd_bottom, win_w, g_dock_h);
	g_dock->show();
	g_main_win->resizable(nullptr);
	g_main_win->size(win_w, win_h);
	g_main_win->resizable(g_dock);
	g_main_win->init_sizes();
	g_main_win->size_range(LCD_WIDTH * SCALE, lcd_bottom + 100);
	g_dock->init_sizes();
	g_main_win->redraw();
}

static void sync_dock_height()
{
	int lcd_w = LCD_WIDTH * SCALE;
	int lcd_x = (g_main_win->w() - lcd_w) / 2;
	if (lcd_x < 0) lcd_x = 0;
	if (g_lcd->x() != lcd_x)
		g_lcd->position(lcd_x, MENUBAR_H);

	if (!g_dock || !g_dock->visible()) return;
	int lcd_bottom = MENUBAR_H + g_lcd_h * SCALE;
	int new_h = g_main_win->h() - lcd_bottom;
	if (new_h < 100) new_h = 100;
	if (new_h != g_dock_h) {
		g_dock_h = new_h;
		relayout_dock();
	}
}

static Fl_Double_Window *make_dbg_window(Fl_Group *panel, int dw, int dh,
                                         int min_w, int min_h,
                                         const char *title, const char *pref_key)
{
	int wx, wy, ww = dw, wh = dh;
	wx = 100; wy = 100;
	prefs_load_window(pref_key, wx, wy, ww, wh);

	Fl_Double_Window *win = new Fl_Double_Window(ww, wh, title);
	win->callback(cb_dbg_win_close);
	win->size_range(min_w, min_h);
	panel->resize(0, 0, ww, wh);
	win->add(panel);
	win->resizable(panel);
	win->end();
	win->position(wx, wy);
	return win;
}

static void dock_panel(Fl_Group *panel, Fl_Double_Window *&win)
{
	if (win) {
		win->remove(panel);
		win->hide();
		delete win;
		win = nullptr;
	}
	g_dock->add(panel);
	relayout_dock();
}

static void undock_panel(Fl_Group *panel, Fl_Double_Window *&win,
                         int dw, int dh, int min_w, int min_h,
                         const char *title, const char *pref_key)
{
	g_dock->remove(panel);
	win = make_dbg_window(panel, dw, dh, min_w, min_h, title, pref_key);
	win->show();
	relayout_dock();
}

/* ---- debug show/dock callbacks ---- */

static bool panel_visible(Fl_Group *panel, Fl_Double_Window *win, bool docked) {
	if (!panel) return false;
	if (docked) return panel->visible_r();
	return win && win->visible();
}

static void toggle_panel(Fl_Group *&panel, Fl_Double_Window *&win, bool docked,
                         int dw, int dh, int min_w, int min_h,
                         const char *title, const char *pref_key,
                         void (*create_fn)(Fl_Group *&))
{
	if (panel_visible(panel, win, docked)) {
		if (docked) {
			g_dock->remove(panel);
			panel->hide();
			relayout_dock();
		} else {
			win->hide();
		}
		return;
	}
	if (!panel) {
		create_fn(panel);
		if (docked) {
			g_dock->add(panel);
			relayout_dock();
		} else {
			win = make_dbg_window(panel, dw, dh, min_w, min_h, title, pref_key);
		}
	}
	if (docked) {
		g_dock->add(panel);
		panel->show();
		relayout_dock();
	} else {
		win->show();
	}
}

static void create_regs(Fl_Group *&p) { p = (Fl_Group *)new DbgRegsPanel(0,0,420,560,&g_mach.cpu); }
static void create_dis(Fl_Group *&p)  { p = (Fl_Group *)new DbgDisPanel(0,0,500,500,&g_mach.cpu); }
static void create_mem(Fl_Group *&p)  { p = (Fl_Group *)new DbgMemPanel(0,0,620,500,&g_mach.cpu); }
static void create_periph(Fl_Group *&p) { p = (Fl_Group *)new DbgPeriphPanel(0,0,500,450,&g_mach); }

static void cb_show_regs(Fl_Widget *, void *) {
	toggle_panel((Fl_Group *&)g_dbg_regs, g_win_regs, g_docked_regs,
	             420, 560, 300, 280, "CPU Registers", "dbg_regs", create_regs);
}

static void cb_show_dis(Fl_Widget *, void *) {
	toggle_panel((Fl_Group *&)g_dbg_dis, g_win_dis, g_docked_dis,
	             500, 500, 300, 120, "Disassembly", "dbg_dis", create_dis);
}

static void cb_show_mem(Fl_Widget *, void *) {
	toggle_panel((Fl_Group *&)g_dbg_mem, g_win_mem, g_docked_mem,
	             620, 500, 300, 120, "Memory Editor", "dbg_mem", create_mem);
}

static void cb_show_periph(Fl_Widget *, void *) {
	toggle_panel((Fl_Group *&)g_dbg_periph, g_win_periph, g_docked_periph,
	             500, 450, 250, 120, "Peripheral Monitor", "dbg_periph", create_periph);
}

static void cb_dock_regs(Fl_Widget *, void *) {
	g_docked_regs = !g_docked_regs;
	if (!g_dbg_regs) return;
	if (g_docked_regs)
		dock_panel(g_dbg_regs, g_win_regs);
	else
		undock_panel(g_dbg_regs, g_win_regs, 420, 560, 300, 280,
		             "CPU Registers", "dbg_regs");
}

static void cb_dock_dis(Fl_Widget *, void *) {
	g_docked_dis = !g_docked_dis;
	if (!g_dbg_dis) return;
	if (g_docked_dis)
		dock_panel(g_dbg_dis, g_win_dis);
	else
		undock_panel(g_dbg_dis, g_win_dis, 500, 500, 300, 120,
		             "Disassembly", "dbg_dis");
}

static void cb_dock_mem(Fl_Widget *, void *) {
	g_docked_mem = !g_docked_mem;
	if (!g_dbg_mem) return;
	if (g_docked_mem)
		dock_panel(g_dbg_mem, g_win_mem);
	else
		undock_panel(g_dbg_mem, g_win_mem, 620, 500, 300, 120,
		             "Memory Editor", "dbg_mem");
}

static void cb_dock_periph(Fl_Widget *, void *) {
	g_docked_periph = !g_docked_periph;
	if (!g_dbg_periph) return;
	if (g_docked_periph)
		dock_panel(g_dbg_periph, g_win_periph);
	else
		undock_panel(g_dbg_periph, g_win_periph, 500, 450, 250, 120,
		             "Peripheral Monitor", "dbg_periph");
}

/* ---- main callbacks ---- */

static void save_prefs() {
	if (g_main_win) {
		int save_h = MENUBAR_H + g_lcd_h * SCALE;
		prefs_save_window("main", g_main_win->x(), g_main_win->y(),
		                  g_main_win->w(), save_h);
	}
	if (g_win_regs && g_win_regs->visible())
		prefs_save_window("dbg_regs", g_win_regs->x(), g_win_regs->y(),
		                  g_win_regs->w(), g_win_regs->h());
	if (g_win_dis && g_win_dis->visible())
		prefs_save_window("dbg_dis", g_win_dis->x(), g_win_dis->y(),
		                  g_win_dis->w(), g_win_dis->h());
	if (g_win_mem && g_win_mem->visible())
		prefs_save_window("dbg_mem", g_win_mem->x(), g_win_mem->y(),
		                  g_win_mem->w(), g_win_mem->h());
	if (g_win_periph && g_win_periph->visible())
		prefs_save_window("dbg_periph", g_win_periph->x(), g_win_periph->y(),
		                  g_win_periph->w(), g_win_periph->h());
	prefs_set_int("dock", "regs", g_docked_regs);
	prefs_set_int("dock", "dis", g_docked_dis);
	prefs_set_int("dock", "mem", g_docked_mem);
	prefs_set_int("dock", "periph", g_docked_periph);
	if (g_dock && g_dock->visible())
		prefs_set_int("dock", "height", g_dock_h);
}

static void final_exit(int code) {
	save_prefs();
	remote_shutdown();
#ifdef HAS_PORTAUDIO
	if (g_audio) { Pa_StopStream(g_audio); Pa_CloseStream(g_audio); g_audio = nullptr; }
	Pa_Terminate();
#endif
	machine_close_pccard(&g_mach);
	machine_close_nvram(&g_mach);
	machine_close_rom(&g_mach);
	fdc_destroy(&g_mach.fdc);
	uart_destroy(&g_mach.uart);
	exit(code);
}

static void shutdown_poll(void *) {
	static int ticks = 0;
	if (!g_mach.lcd_on) {
		fprintf(stderr, "Shutdown: firmware acknowledged\n");
		machine_power_button(&g_mach, false);
		final_exit(0);
	}
	if (++ticks >= 120) {
		fprintf(stderr, "Shutdown: firmware did not respond within 2 seconds\n");
		machine_power_button(&g_mach, false);
		final_exit(1);
	}
	machine_step(&g_mach, CPU_CLOCK / 60);
	Fl::repeat_timeout(1.0/60.0, shutdown_poll, nullptr);
}

static void cb_quit(Fl_Widget *, void *) {
	if (!g_mach.lcd_on) {
		final_exit(0);
		return;
	}
	g_shutting_down = true;
	fprintf(stderr, "Shutdown: pressing power button...\n");
	machine_power_button(&g_mach, true);
	Fl::add_timeout(1.0/60.0, shutdown_poll, nullptr);
}

static void cb_power(Fl_Widget *, void *) {
	machine_power_button(&g_mach, true);
	Fl::add_timeout(0.2, [](void *) { machine_power_button(&g_mach, false); }, nullptr);
}

static void cb_clear_nvram(Fl_Widget *, void *) {
	if (!fl_choice("Clear all NVRAM and restart?\nThis erases all saved documents and settings.", "Cancel", "Clear", nullptr))
		return;
	if (g_mach.ram && g_mach.ram_size > 0)
		memset(g_mach.ram, 0, g_mach.ram_size);
	machine_reset(&g_mach);
}

static void cb_battery(Fl_Widget *, void *v) {
	bool *flag = (bool *)v;
	*flag = !*flag;
}

static void open_pccard(const char *path) {
	machine_close_pccard(&g_mach);
	snprintf(g_pccard_path, sizeof(g_pccard_path), "%s", path);
	if (machine_open_pccard(&g_mach, g_pccard_path) < 0) {
		g_pccard_path[0] = 0;
		fl_alert("Cannot open PC Card %s", path);
	}
}

static void cb_insert_pccard(Fl_Widget *, void *) {
	const char *path = fl_file_chooser("Insert PC Card", "*", g_pccard_path[0] ? g_pccard_path : nullptr);
	if (path) open_pccard(path);
}

static void cb_new_pccard(Fl_Widget *, void *) {
	static const struct { const char *label; uint32_t size; } sizes[] = {
		{"128 KB", 128*1024}, {"256 KB", 256*1024}, {"512 KB", 512*1024},
		{"1 MB", 1024*1024},
	};
	Fl_Menu_Item popup[] = {
		{"128 KB",0,0,0,0,0,0,0,0}, {"256 KB",0,0,0,0,0,0,0,0},
		{"512 KB",0,0,0,0,0,0,0,0}, {"1 MB",0,0,0,0,0,0,0,0},
		{0,0,0,0,0,0,0,0,0}
	};
	const Fl_Menu_Item *pick = popup->popup(Fl::event_x(), Fl::event_y(), "Card Size");
	if (!pick) return;
	int idx = (int)(pick - popup);

	char default_card[1024]; make_data_path("card.bin", default_card, sizeof(default_card));
	const char *path = fl_file_chooser("Save New PC Card As", "*", default_card);
	if (!path) return;

	int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) { fl_alert("Cannot create %s", path); return; }
	(void)ftruncate(fd, (off_t)sizes[idx].size);
	uint8_t *p = (uint8_t *)mmap(nullptr, sizes[idx].size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	if (p != MAP_FAILED) { memset(p, 0xFF, sizes[idx].size); munmap(p, sizes[idx].size); }
	close(fd);

	open_pccard(path);
}

static void cb_eject_pccard(Fl_Widget *, void *) {
	machine_close_pccard(&g_mach);
	g_pccard_path[0] = 0;
}

static void open_floppy(const char *path) {
	fdc_destroy(&g_mach.fdc);
	fdc_init(&g_mach.fdc);
	snprintf(g_floppy_path, sizeof(g_floppy_path), "%s", path);
	if (fdc_load_disk(&g_mach.fdc, g_floppy_path) < 0)
		fprintf(stderr, "Failed to load floppy: %s\n", g_floppy_path);
}

static void cb_insert_floppy(Fl_Widget *, void *) {
	const char *path = fl_file_chooser("Insert Floppy", "*.img\t*", g_floppy_path[0] ? g_floppy_path : nullptr);
	if (path) open_floppy(path);
}

static void cb_new_floppy(Fl_Widget *, void *) {
	char default_floppy[1024]; make_data_path("floppy.img", default_floppy, sizeof(default_floppy));
	const char *path = fl_file_chooser("Save New Floppy As", "*.img", default_floppy);
	if (!path) return;

	FILE *f = fopen(path, "wb");
	if (!f) { fl_alert("Cannot create %s", path); return; }
	uint8_t zero[512];
	memset(zero, 0, sizeof(zero));
	for (int i = 0; i < 80 * 2 * 18; i++) fwrite(zero, 1, 512, f);
	fclose(f);

	open_floppy(path);
}

static void cb_eject_floppy(Fl_Widget *, void *) {
	fdc_destroy(&g_mach.fdc);
	fdc_init(&g_mach.fdc);
	g_floppy_path[0] = 0;
}


static bool show_iw_dip_dialog(PrinterConfig &cfg)
{
	Fl_Window win(340, 360, "ImageWriter II DIP Switches");
	win.set_modal();

	Fl_Box hdr1(10, 5, 320, 20, "Character & Page Settings");
	hdr1.labelfont(FL_BOLD);
	hdr1.align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

	Fl_Choice charset(160, 30, 170, 25, "Character Set:");
	charset.add("American");
	charset.add("French");
	charset.add("German");
	charset.add("British");
	charset.add("Danish");
	charset.add("Swedish");
	charset.add("Italian");
	charset.add("Spanish");
	charset.add("Japanese");
	charset.value(cfg.charset);

	Fl_Choice pitch(160, 60, 170, 25, "Pitch:");
	pitch.add("Extended (9 CPI)");
	pitch.add("Pica (10 CPI)");
	pitch.add("Elite (12 CPI)");
	pitch.value(cfg.pitch_cpi <= 9 ? 0 : cfg.pitch_cpi <= 10 ? 1 : 2);

	Fl_Choice pglen(160, 90, 170, 25, "Page Length:");
	pglen.add("11 inch (66 lines)");
	pglen.add("12 inch (72 lines)");
	pglen.value(cfg.page_length_lines > 66 ? 1 : 0);

	Fl_Check_Button perf(10, 120, 320, 25, "Perforation Skip");
	perf.value(cfg.perf_skip > 0 ? 1 : 0);

	Fl_Check_Button autolf(10, 145, 320, 25, "Auto LF after CR");
	autolf.value(cfg.auto_lf ? 1 : 0);

	Fl_Box hdr2(10, 175, 320, 20, "Front Panel");
	hdr2.labelfont(FL_BOLD);
	hdr2.align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

	Fl_Choice font(160, 200, 170, 25, "Print Quality:");
	font.add("Standard");
	font.add("Near Letter Quality");
	font.add("Draft");
	font.value(cfg.font_mode);

	Fl_Check_Button slashed(10, 235, 320, 25, "Slashed Zeros");
	slashed.value(cfg.slashed_zero ? 1 : 0);

	bool ok = false;
	Fl_Return_Button btn_ok(160, 320, 80, 30, "OK");
	btn_ok.callback([](Fl_Widget *w, void *d) {
		*static_cast<bool *>(d) = true;
		w->window()->hide();
	}, &ok);
	Fl_Button btn_cancel(250, 320, 80, 30, "Cancel");
	btn_cancel.callback([](Fl_Widget *w, void *) {
		w->window()->hide();
	});

	win.end();
	win.show();
	while (win.shown()) Fl::wait();

	if (!ok) return false;

	cfg.charset = charset.value();
	static constexpr float pitches[] = {9, 10, 12};
	cfg.pitch_cpi = pitches[pitch.value()];
	cfg.page_length_lines = pglen.value() ? 72 : 66;
	cfg.perf_skip = perf.value() ? 6 : 0;
	cfg.auto_lf = autolf.value();
	cfg.font_mode = font.value();
	cfg.slashed_zero = slashed.value();
	return true;
}

static bool show_fx_dip_dialog(PrinterConfig &cfg)
{
	Fl_Window win(340, 280, "Epson FX-80 DIP Switches");
	win.set_modal();

	Fl_Box hdr1(10, 5, 320, 20, "Character & Page Settings");
	hdr1.labelfont(FL_BOLD);
	hdr1.align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

	Fl_Choice charset(160, 30, 170, 25, "Character Set:");
	charset.add("American");
	charset.add("French");
	charset.add("German");
	charset.add("British");
	charset.add("Danish");
	charset.add("Swedish");
	charset.add("Italian");
	charset.add("Spanish");
	charset.add("Japanese");
	charset.value(cfg.charset);

	Fl_Choice pitch(160, 60, 170, 25, "Pitch:");
	pitch.add("Pica (10 CPI)");
	pitch.add("Compressed (17 CPI)");
	pitch.value(cfg.pitch_cpi >= 17 ? 1 : 0);

	Fl_Check_Button perf(10, 90, 320, 25, "Perforation Skip");
	perf.value(cfg.perf_skip > 0 ? 1 : 0);

	Fl_Check_Button autolf(10, 115, 320, 25, "Auto LF after CR");
	autolf.value(cfg.auto_lf ? 1 : 0);

	Fl_Check_Button emph(10, 140, 320, 25, "Emphasized (Bold)");
	emph.value(cfg.emphasized ? 1 : 0);

	Fl_Check_Button slashed(10, 165, 320, 25, "Slashed Zeros");
	slashed.value(cfg.slashed_zero ? 1 : 0);

	bool ok = false;
	Fl_Return_Button btn_ok(160, 240, 80, 30, "OK");
	btn_ok.callback([](Fl_Widget *w, void *d) {
		*static_cast<bool *>(d) = true;
		w->window()->hide();
	}, &ok);
	Fl_Button btn_cancel(250, 240, 80, 30, "Cancel");
	btn_cancel.callback([](Fl_Widget *w, void *) {
		w->window()->hide();
	});

	win.end();
	win.show();
	while (win.shown()) Fl::wait();

	if (!ok) return false;

	cfg.charset = charset.value();
	cfg.pitch_cpi = pitch.value() ? 17 : 10;
	cfg.perf_skip = perf.value() ? 6 : 0;
	cfg.auto_lf = autolf.value();
	cfg.emphasized = emph.value();
	cfg.slashed_zero = slashed.value();
	return true;
}

static bool show_bj_settings_dialog(PrinterConfig &cfg)
{
	Fl_Window win(620, 620, "Canon BJ-10e Settings");
	win.set_modal();

	Fl_Box hdr1(10, 5, 600, 20, "Front Panel");
	hdr1.labelfont(FL_BOLD);
	hdr1.align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

	Fl_Choice pitch(180, 30, 220, 25, "Pitch:");
	pitch.add("Pica (10 CPI)");
	pitch.add("Elite (12 CPI)");
	pitch.add("Condensed (17 CPI)");
	pitch.add("Proportional");
	pitch.value(cfg.proportional ? 3 : cfg.pitch_cpi >= 17 ? 2 : cfg.pitch_cpi >= 12 ? 1 : 0);

	Fl_Check_Button emph(10, 65, 390, 25, "Emphasized");
	emph.value(cfg.emphasized ? 1 : 0);

	Fl_Check_Button double_high(210, 65, 190, 25, "Double-high");
	double_high.value(cfg.double_high ? 1 : 0);

	Fl_Check_Button double_width(410, 65, 190, 25, "Double-width");
	double_width.value(cfg.double_width ? 1 : 0);

	Fl_Check_Button slashed(10, 95, 190, 25, "Slashed Zeros");
	slashed.value(cfg.slashed_zero ? 1 : 0);

	Fl_Choice quality(410, 95, 190, 25, "Quality:");
	quality.add("High Quality");
	quality.add("Economy");
	quality.value(cfg.font_mode == 1 ? 1 : 0);

	Fl_Box hdr2(10, 155, 600, 20, "DIP Selectors");
	hdr2.labelfont(FL_BOLD);
	hdr2.align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

	Fl_Check_Button dip1(10, 180, 590, 25, "1 Auto sheet feeder mode: ON=Enable, OFF=Disable");
	Fl_Check_Button dip2(10, 210, 590, 25, "2 Graphics image density: ON=Normal, OFF=High");
	Fl_Check_Button dip3(10, 240, 590, 25, "3 Automatic line feed: ON=CR with LF, OFF=CR only");
	Fl_Check_Button dip4(10, 270, 590, 25, "4 Page length: ON=305mm/12in, OFF=279mm/11in");
	Fl_Check_Button dip5(10, 300, 590, 25, "5 Character set: ON=Set 2, OFF=Set 1");
	Fl_Check_Button dip6(10, 330, 590, 25, "6 Automatic carriage return: ON=LF with CR, OFF=LF only");
	Fl_Check_Button dip7(10, 360, 590, 25, "7 Alternate graphics mode: ON=Enable, OFF=Disable");
	Fl_Check_Button dip8(10, 390, 590, 25, "8 Receive buffer/download memory: ON=3KB/34KB, OFF=37KB/0KB");
	Fl_Check_Button dip9(10, 420, 590, 25, "9 Code page: ON=Multilingual 850, OFF=USA 437");
	Fl_Check_Button dip10(10, 450, 590, 25, "10 Printer control mode: ON=Mode 2, OFF=Mode 1");
	Fl_Check_Button *dips[] = {
		&dip1, &dip2, &dip3, &dip4, &dip5,
		&dip6, &dip7, &dip8, &dip9, &dip10
	};
	for (int i = 0; i < 10; i++)
		dips[i]->value((cfg.dip_switches & (1 << i)) ? 1 : 0);

	bool ok = false;
	Fl_Return_Button btn_ok(440, 580, 80, 30, "OK");
	btn_ok.callback([](Fl_Widget *w, void *d) {
		*static_cast<bool *>(d) = true;
		w->window()->hide();
	}, &ok);
	Fl_Button btn_cancel(530, 580, 80, 30, "Cancel");
	btn_cancel.callback([](Fl_Widget *w, void *) {
		w->window()->hide();
	});

	win.end();
	win.show();
	while (win.shown()) Fl::wait();

	if (!ok) return false;

	static constexpr float pitches[] = {10, 12, 17, 10};
	cfg.pitch_cpi = pitches[pitch.value()];
	cfg.proportional = pitch.value() == 3;
	cfg.emphasized = emph.value();
	cfg.double_high = double_high.value();
	cfg.double_width = double_width.value();
	cfg.slashed_zero = slashed.value();
	cfg.font_mode = quality.value() == 1 ? 1 : 0;
	cfg.dip_switches = 0;
	for (int i = 0; i < 10; i++)
		if (dips[i]->value())
			cfg.dip_switches |= (1 << i);
	cfg.auto_lf = (cfg.dip_switches & (1 << 2)) != 0;
	cfg.page_length_lines = (cfg.dip_switches & (1 << 3)) ? 72 : 66;
	return true;
}

struct LjiiSymbolChoice {
	const char *label;
	int value;
};

static constexpr LjiiSymbolChoice kLjiiSymbolChoices[] = {
	{"Roman-8 (8U)", 0x0115},
	{"ECMA-94 Latin 1 (0N)", 0x000e},
	{"PC-8 (10U)", 0x0155},
	{"PC-8 Denmark/Norway (11U)", 0x0175},
	{"ISO 2 International (2U)", 0x0055},
	{"ISO 4 United Kingdom (1E)", 0x0025},
	{"ISO 6 ASCII (0U)", 0x0015},
	{"ISO 10 Swedish (3S)", 0x0073},
	{"ISO 11 Swedish (0S)", 0x0013},
	{"ISO 14 JIS ASCII (0K)", 0x000b},
	{"ISO 15 Italian (0I)", 0x0009},
	{"ISO 16 Portuguese (4S)", 0x0093},
	{"ISO 17 Spanish (2S)", 0x0053},
	{"ISO 21 German (1G)", 0x0027},
	{"ISO 25 French (0F)", 0x0006},
	{"ISO 57 Chinese (2K)", 0x004b},
	{"ISO 60 Danish/Norwegian (0D)", 0x0004},
	{"ISO 61 Norwegian V2 (1D)", 0x0024},
	{"ISO 69 French (1F)", 0x0026},
};

static int ljii_cartridge_choice_id(const Fl_Choice &choice)
{
	const LjiiCartridgeInfo *info = ljii_cartridge_info(
		static_cast<size_t>(choice.value()));
	return info ? info->id : 0;
}

static int ljii_cartridge_choice_index(int id)
{
	for (size_t i = 0; i < ljii_cartridge_count(); i++) {
		const LjiiCartridgeInfo *info = ljii_cartridge_info(i);
		if (info && info->id == id)
			return (int)i;
	}
	return 0;
}

static std::string ljii_menu_component(const char *label);

static void populate_ljii_cartridge_choice(Fl_Choice &choice, int id)
{
	for (size_t i = 0; i < ljii_cartridge_count(); i++) {
		const LjiiCartridgeInfo *info = ljii_cartridge_info(i);
		if (info) {
			std::string label = ljii_menu_component(info->label);
			choice.add(label.c_str());
		}
	}
	choice.value(ljii_cartridge_choice_index(id));
}

static LjiiCartridgeSlots ljii_selected_cartridges(
	const Fl_Choice &slot_1, const Fl_Choice &slot_2)
{
	return {{ ljii_cartridge_choice_id(slot_1),
	          ljii_cartridge_choice_id(slot_2) }};
}

static std::string ljii_menu_component(const char *label)
{
	std::string escaped;
	for (const char *p = label; p && *p; p++) {
		if (*p == '/' || *p == '\\')
			escaped += '\\';
		if (*p == '&')
			escaped += '&';
		escaped += *p;
	}
	return escaped;
}

static int ljii_font_choice_id(const Fl_Choice &choice)
{
	const Fl_Menu_Item *item = choice.mvalue();
	return item && item->user_data()
	    ? static_cast<int>(reinterpret_cast<intptr_t>(item->user_data()) - 1)
	    : 0;
}

static void populate_ljii_font_choice(Fl_Choice &choice, int selected_id,
	                                  LjiiCartridgeSlots cartridges,
	                                  int orientation)
{
	choice.clear();
	for (size_t i = 0; i < ljii_default_font_count(); i++) {
		const LjiiDefaultFontInfo *font = ljii_default_font_info(i);
		if (!font || !ljii_default_font_available(
			    font->id, cartridges, orientation))
			continue;
		std::string path;
		if (font->request.exact_cartridge == 0) {
			path = "Resident/";
		} else {
			const LjiiCartridgeInfo *cartridge = find_ljii_cartridge(
				font->request.exact_cartridge);
			path = ljii_menu_component(
				cartridge ? cartridge->label : "Cartridge") + "/";
			path += ljii_menu_component(font->family) + "/";
		}
		path += ljii_menu_component(font->label);
		choice.add(
			path.c_str(), 0, nullptr,
			reinterpret_cast<void *>(static_cast<intptr_t>(font->id) + 1));
	}
	int selected_index = -1;
	int fallback_index = -1;
	for (int i = 0; i < choice.size(); i++) {
		const Fl_Menu_Item &item = choice.menu()[i];
		if (!item.user_data())
			continue;
		int id = static_cast<int>(
			reinterpret_cast<intptr_t>(item.user_data()) - 1);
		if (id == 0)
			fallback_index = i;
		if (id == selected_id)
			selected_index = i;
	}
	choice.value(selected_index >= 0 ? selected_index : fallback_index);
}

static int ljii_symbol_choice_value(const Fl_Choice &choice)
{
	const Fl_Menu_Item *item = choice.mvalue();
	return item && item->user_data()
	    ? static_cast<int>(reinterpret_cast<intptr_t>(item->user_data()) - 1)
	    : 0x0115;
}

static void populate_ljii_symbol_choice(Fl_Choice &choice, int selected_symbol,
	                                    LjiiCartridgeSlots cartridges,
	                                    int orientation)
{
	choice.clear();
	std::vector<int> symbols;
	int selected_index = -1;
	auto add_symbol = [&](const char *label, int symbol) {
		if (std::find(symbols.begin(), symbols.end(), symbol) != symbols.end())
			return;
		symbols.push_back(symbol);
		int index = choice.add(
			label, 0, nullptr,
			reinterpret_cast<void *>(static_cast<intptr_t>(symbol) + 1));
		if (symbol == selected_symbol)
			selected_index = index;
	};
	for (const auto &symbol : kLjiiSymbolChoices)
		add_symbol(symbol.label, symbol.value);
	for (size_t i = 0; i < ljii_default_font_count(); i++) {
		const LjiiDefaultFontInfo *font = ljii_default_font_info(i);
		if (!font || font->request.exact_cartridge == 0 ||
		    !ljii_default_font_available(font->id, cartridges, orientation))
			continue;
		int value = font->request.symbol_set;
		char label[48];
		snprintf(label, sizeof(label), "Cartridge font set (%d%c)",
		         value / 32, '@' + value % 32);
		add_symbol(label, value);
	}
	if (std::find(symbols.begin(), symbols.end(), selected_symbol) == symbols.end()) {
		char label[40];
		snprintf(label, sizeof(label), "Configured set (%d%c)",
		         selected_symbol / 32, '@' + selected_symbol % 32);
		add_symbol(label, selected_symbol);
	}
	choice.value(selected_index >= 0 ? selected_index : 0);
}

struct LjiiSettingsChoices {
	Fl_Choice *orientation;
	Fl_Choice *font;
	Fl_Choice *symbol;
	Fl_Choice *cartridge_1;
	Fl_Choice *cartridge_2;
};

static void ljii_rebuild_font_choices(LjiiSettingsChoices &choices)
{
	int previous_font = ljii_font_choice_id(*choices.font);
	int symbol = ljii_symbol_choice_value(*choices.symbol);
	LjiiCartridgeSlots cartridges = ljii_selected_cartridges(
		*choices.cartridge_1, *choices.cartridge_2);
	int orientation = choices.orientation->value() == 1 ? 1 : 0;
	populate_ljii_font_choice(
		*choices.font, previous_font, cartridges, orientation);
	populate_ljii_symbol_choice(
		*choices.symbol, symbol, cartridges, orientation);
}

static void cb_ljii_rebuild_fonts(Fl_Widget *, void *data)
{
	ljii_rebuild_font_choices(*static_cast<LjiiSettingsChoices *>(data));
}

static void cb_ljii_select_font(Fl_Widget *, void *data)
{
	auto &choices = *static_cast<LjiiSettingsChoices *>(data);
	int font_id = ljii_font_choice_id(*choices.font);
	const LjiiDefaultFontInfo *font = find_ljii_default_font(font_id);
	int symbol = font && font->request.exact_cartridge != 0
	    ? font->request.symbol_set
	    : ljii_symbol_choice_value(*choices.symbol);
	populate_ljii_symbol_choice(
		*choices.symbol, symbol,
		ljii_selected_cartridges(*choices.cartridge_1, *choices.cartridge_2),
		choices.orientation->value() == 1 ? 1 : 0);
}

static bool show_jet_settings_dialog(PrinterConfig &cfg)
{
	Fl_Window win(730, 355, "HP LaserJet II Settings");
	win.set_modal();

	Fl_Spinner copies(190, 25, 510, 25, "Copies:");
	copies.type(FL_INT_INPUT);
	copies.range(1, 99);
	copies.step(1);
	copies.value(cfg.copies);

	Fl_Choice orientation(190, 60, 510, 25, "Orientation:");
	orientation.add("Portrait");
	orientation.add("Landscape");
	orientation.value(cfg.pcl_orientation == 1 ? 1 : 0);

	Fl_Choice font(190, 95, 510, 25, "Font:");

	Fl_Choice symbol(190, 130, 510, 25, "Symbol Set:");

	Fl_Spinner form(190, 165, 510, 25, "Form Length (lines):");
	form.type(FL_INT_INPUT);
	form.range(5, 128);
	form.step(1);
	form.value(cfg.page_length_lines);

	Fl_Choice cartridge_1(190, 200, 510, 25, "Cartridge Slot 1:");
	populate_ljii_cartridge_choice(cartridge_1, cfg.pcl_cartridge_slot_1);

	Fl_Choice cartridge_2(190, 235, 510, 25, "Cartridge Slot 2:");
	populate_ljii_cartridge_choice(cartridge_2, cfg.pcl_cartridge_slot_2);

	LjiiCartridgeSlots cartridges = ljii_selected_cartridges(
		cartridge_1, cartridge_2);
	populate_ljii_font_choice(
		font, cfg.pcl_font, cartridges, orientation.value());
	populate_ljii_symbol_choice(
		symbol, cfg.pcl_symbol_set, cartridges, orientation.value());
	LjiiSettingsChoices choices = {
		&orientation, &font, &symbol, &cartridge_1, &cartridge_2
	};
	orientation.callback(cb_ljii_rebuild_fonts, &choices);
	cartridge_1.callback(cb_ljii_rebuild_fonts, &choices);
	cartridge_2.callback(cb_ljii_rebuild_fonts, &choices);
	font.callback(cb_ljii_select_font, &choices);

	bool ok = false;
	Fl_Return_Button btn_ok(540, 305, 80, 30, "OK");
	btn_ok.callback([](Fl_Widget *w, void *d) {
		*static_cast<bool *>(d) = true;
		w->window()->hide();
	}, &ok);
	Fl_Button btn_cancel(630, 305, 80, 30, "Cancel");
	btn_cancel.callback([](Fl_Widget *w, void *) {
		w->window()->hide();
	});

	win.end();
	win.show();
	while (win.shown()) Fl::wait();
	if (!ok) return false;

	cfg.copies = (int)copies.value();
	cfg.pcl_orientation = orientation.value() == 1 ? 1 : 0;
	cfg.pcl_font = ljii_font_choice_id(font);
	cfg.pcl_symbol_set = ljii_symbol_choice_value(symbol);
	cfg.page_length_lines = (int)form.value();
	cfg.pcl_cartridge_slot_1 = ljii_cartridge_choice_id(cartridge_1);
	cfg.pcl_cartridge_slot_2 = ljii_cartridge_choice_id(cartridge_2);
	return true;
}

static PrinterConfig g_iw_cfg;
static PrinterConfig g_fx_cfg;
static PrinterConfig g_bj_cfg;
static PrinterConfig g_jet_cfg;
static bool g_printer_cfg_loaded = false;

static void printer_cfg_load()
{
	if (g_printer_cfg_loaded) return;
	g_printer_cfg_loaded = true;
	g_iw_cfg = default_config_for(PrinterModel::ImageWriter);
	g_fx_cfg = default_config_for(PrinterModel::EpsonFX);
	g_bj_cfg = default_config_for(PrinterModel::CanonBJ10e);
	g_jet_cfg = default_config_for(PrinterModel::HpJet);

	g_iw_cfg.charset           = prefs_get_int("printer_iw", "charset", g_iw_cfg.charset);
	int iw_pitch_x100          = prefs_get_int("printer_iw", "pitch", static_cast<int>(g_iw_cfg.pitch_cpi * 100.0f));
	g_iw_cfg.pitch_cpi         = static_cast<float>(iw_pitch_x100) / 100.0f;
	g_iw_cfg.page_length_lines = prefs_get_int("printer_iw", "pglen", g_iw_cfg.page_length_lines);
	g_iw_cfg.perf_skip         = prefs_get_int("printer_iw", "perf_skip", g_iw_cfg.perf_skip);
	g_iw_cfg.auto_lf           = prefs_get_int("printer_iw", "auto_lf", g_iw_cfg.auto_lf ? 1 : 0);
	g_iw_cfg.font_mode         = prefs_get_int("printer_iw", "font_mode", g_iw_cfg.font_mode);
	g_iw_cfg.slashed_zero      = prefs_get_int("printer_iw", "slashed", g_iw_cfg.slashed_zero ? 1 : 0);

	g_fx_cfg.charset           = prefs_get_int("printer_fx", "charset", g_fx_cfg.charset);
	int fx_pitch_x100          = prefs_get_int("printer_fx", "pitch", static_cast<int>(g_fx_cfg.pitch_cpi * 100.0f));
	g_fx_cfg.pitch_cpi         = static_cast<float>(fx_pitch_x100) / 100.0f;
	g_fx_cfg.perf_skip         = prefs_get_int("printer_fx", "perf_skip", g_fx_cfg.perf_skip);
	g_fx_cfg.auto_lf           = prefs_get_int("printer_fx", "auto_lf", g_fx_cfg.auto_lf ? 1 : 0);
	g_fx_cfg.emphasized        = prefs_get_int("printer_fx", "emph", g_fx_cfg.emphasized ? 1 : 0);
	g_fx_cfg.slashed_zero      = prefs_get_int("printer_fx", "slashed", g_fx_cfg.slashed_zero ? 1 : 0);

	g_bj_cfg.charset           = prefs_get_int("printer_bj10e", "charset", g_bj_cfg.charset);
	int bj_pitch_x100          = prefs_get_int("printer_bj10e", "pitch", static_cast<int>(g_bj_cfg.pitch_cpi * 100.0f));
	g_bj_cfg.pitch_cpi         = static_cast<float>(bj_pitch_x100) / 100.0f;
	g_bj_cfg.perf_skip         = prefs_get_int("printer_bj10e", "perf_skip", g_bj_cfg.perf_skip);
	g_bj_cfg.auto_lf           = prefs_get_int("printer_bj10e", "auto_lf", g_bj_cfg.auto_lf ? 1 : 0);
	g_bj_cfg.emphasized        = prefs_get_int("printer_bj10e", "emph", g_bj_cfg.emphasized ? 1 : 0);
	g_bj_cfg.font_mode         = prefs_get_int("printer_bj10e", "font_mode", g_bj_cfg.font_mode);
	g_bj_cfg.slashed_zero      = prefs_get_int("printer_bj10e", "slashed", g_bj_cfg.slashed_zero ? 1 : 0);
	g_bj_cfg.dip_switches      = prefs_get_int("printer_bj10e", "dip_switches", g_bj_cfg.dip_switches);
	g_bj_cfg.double_width      = prefs_get_int("printer_bj10e", "double_width", g_bj_cfg.double_width ? 1 : 0);
	g_bj_cfg.double_high       = prefs_get_int("printer_bj10e", "double_high", g_bj_cfg.double_high ? 1 : 0);
	g_bj_cfg.proportional      = prefs_get_int("printer_bj10e", "proportional", g_bj_cfg.proportional ? 1 : 0);
	g_bj_cfg.auto_lf           = (g_bj_cfg.dip_switches & (1 << 2)) != 0;
	g_bj_cfg.page_length_lines = (g_bj_cfg.dip_switches & (1 << 3)) ? 72 : 66;

	g_jet_cfg.copies = prefs_get_int("printer_jet", "copies", g_jet_cfg.copies);
	if (g_jet_cfg.copies < 1) g_jet_cfg.copies = 1;
	if (g_jet_cfg.copies > 99) g_jet_cfg.copies = 99;
	g_jet_cfg.pcl_orientation = prefs_get_int(
		"printer_jet", "orientation", g_jet_cfg.pcl_orientation) == 1 ? 1 : 0;
	g_jet_cfg.pcl_font = prefs_get_int("printer_jet", "font", g_jet_cfg.pcl_font);
	g_jet_cfg.pcl_symbol_set = prefs_get_int(
		"printer_jet", "symbol_set", g_jet_cfg.pcl_symbol_set);
	if (g_jet_cfg.pcl_symbol_set < 0 ||
	    g_jet_cfg.pcl_symbol_set / 32 > 0x07ff ||
	    g_jet_cfg.pcl_symbol_set % 32 > 30)
		g_jet_cfg.pcl_symbol_set = kLjiiSymbolChoices[0].value;
	g_jet_cfg.page_length_lines = prefs_get_int(
		"printer_jet", "form_lines", g_jet_cfg.page_length_lines);
	if (g_jet_cfg.page_length_lines < 5) g_jet_cfg.page_length_lines = 5;
	if (g_jet_cfg.page_length_lines > 128) g_jet_cfg.page_length_lines = 128;
	g_jet_cfg.pcl_cartridge_slot_1 = prefs_get_int(
		"printer_jet", "cartridge_slot_1", g_jet_cfg.pcl_cartridge_slot_1);
	if (!ljii_valid_cartridge(g_jet_cfg.pcl_cartridge_slot_1))
		g_jet_cfg.pcl_cartridge_slot_1 = 0;
	g_jet_cfg.pcl_cartridge_slot_2 = prefs_get_int(
		"printer_jet", "cartridge_slot_2", g_jet_cfg.pcl_cartridge_slot_2);
	if (!ljii_valid_cartridge(g_jet_cfg.pcl_cartridge_slot_2))
		g_jet_cfg.pcl_cartridge_slot_2 = 0;
	LjiiCartridgeSlots cartridges = {{ g_jet_cfg.pcl_cartridge_slot_1,
	                                    g_jet_cfg.pcl_cartridge_slot_2 }};
	if (!ljii_default_font_available(
		    g_jet_cfg.pcl_font, cartridges, g_jet_cfg.pcl_orientation))
		g_jet_cfg.pcl_font = 0;
}

static void printer_cfg_save_iw()
{
	prefs_set_int("printer_iw", "charset", g_iw_cfg.charset);
	prefs_set_int("printer_iw", "pitch", static_cast<int>(g_iw_cfg.pitch_cpi * 100.0f));
	prefs_set_int("printer_iw", "pglen", g_iw_cfg.page_length_lines);
	prefs_set_int("printer_iw", "perf_skip", g_iw_cfg.perf_skip);
	prefs_set_int("printer_iw", "auto_lf", g_iw_cfg.auto_lf ? 1 : 0);
	prefs_set_int("printer_iw", "font_mode", g_iw_cfg.font_mode);
	prefs_set_int("printer_iw", "slashed", g_iw_cfg.slashed_zero ? 1 : 0);
}

static void printer_cfg_save_fx()
{
	prefs_set_int("printer_fx", "charset", g_fx_cfg.charset);
	prefs_set_int("printer_fx", "pitch", static_cast<int>(g_fx_cfg.pitch_cpi * 100.0f));
	prefs_set_int("printer_fx", "perf_skip", g_fx_cfg.perf_skip);
	prefs_set_int("printer_fx", "auto_lf", g_fx_cfg.auto_lf ? 1 : 0);
	prefs_set_int("printer_fx", "emph", g_fx_cfg.emphasized ? 1 : 0);
	prefs_set_int("printer_fx", "slashed", g_fx_cfg.slashed_zero ? 1 : 0);
}

static void printer_cfg_save_bj()
{
	prefs_set_int("printer_bj10e", "charset", g_bj_cfg.charset);
	prefs_set_int("printer_bj10e", "pitch", static_cast<int>(g_bj_cfg.pitch_cpi * 100.0f));
	prefs_set_int("printer_bj10e", "perf_skip", g_bj_cfg.perf_skip);
	prefs_set_int("printer_bj10e", "auto_lf", g_bj_cfg.auto_lf ? 1 : 0);
	prefs_set_int("printer_bj10e", "emph", g_bj_cfg.emphasized ? 1 : 0);
	prefs_set_int("printer_bj10e", "font_mode", g_bj_cfg.font_mode);
	prefs_set_int("printer_bj10e", "slashed", g_bj_cfg.slashed_zero ? 1 : 0);
	prefs_set_int("printer_bj10e", "dip_switches", g_bj_cfg.dip_switches);
	prefs_set_int("printer_bj10e", "double_width", g_bj_cfg.double_width ? 1 : 0);
	prefs_set_int("printer_bj10e", "double_high", g_bj_cfg.double_high ? 1 : 0);
	prefs_set_int("printer_bj10e", "proportional", g_bj_cfg.proportional ? 1 : 0);
}

static void printer_cfg_save_jet()
{
	prefs_set_int("printer_jet", "copies", g_jet_cfg.copies);
	prefs_set_int("printer_jet", "orientation", g_jet_cfg.pcl_orientation);
	prefs_set_int("printer_jet", "font", g_jet_cfg.pcl_font);
	prefs_set_int("printer_jet", "symbol_set", g_jet_cfg.pcl_symbol_set);
	prefs_set_int("printer_jet", "form_lines", g_jet_cfg.page_length_lines);
	prefs_set_int("printer_jet", "cartridge_slot_1",
	              g_jet_cfg.pcl_cartridge_slot_1);
	prefs_set_int("printer_jet", "cartridge_slot_2",
	              g_jet_cfg.pcl_cartridge_slot_2);
}

static void cb_iw_dip_switches(Fl_Widget *, void *) {
	printer_cfg_load();
	if (show_iw_dip_dialog(g_iw_cfg)) {
		printer_cfg_save_iw();
		if (g_mach.pdf_printer &&
		    g_mach.pdf_model == static_cast<int>(PrinterModel::ImageWriter))
			g_mach.pdf_printer->apply_config(g_iw_cfg);
	}
}

static void cb_fx_dip_switches(Fl_Widget *, void *) {
	printer_cfg_load();
	if (show_fx_dip_dialog(g_fx_cfg)) {
		printer_cfg_save_fx();
		if (g_mach.pdf_printer &&
		    g_mach.pdf_model == static_cast<int>(PrinterModel::EpsonFX))
			g_mach.pdf_printer->apply_config(g_fx_cfg);
	}
}

static void cb_bj_dip_switches(Fl_Widget *, void *) {
	printer_cfg_load();
	if (show_bj_settings_dialog(g_bj_cfg)) {
		printer_cfg_save_bj();
		if (g_mach.pdf_printer &&
		    g_mach.pdf_model == static_cast<int>(PrinterModel::CanonBJ10e))
			g_mach.pdf_printer->apply_config(g_bj_cfg);
	}
}

static void cb_jet_settings(Fl_Widget *, void *) {
	printer_cfg_load();
	if (show_jet_settings_dialog(g_jet_cfg)) {
		printer_cfg_save_jet();
		if (g_mach.pdf_printer &&
		    g_mach.pdf_model == static_cast<int>(PrinterModel::HpJet))
			g_mach.pdf_printer->apply_config(g_jet_cfg);
	}
}

static PrinterConfig &config_for_model(PrinterModel pm)
{
	printer_cfg_load();
	if (pm == PrinterModel::ImageWriter) return g_iw_cfg;
	if (pm == PrinterModel::CanonBJ10e) return g_bj_cfg;
	if (pm == PrinterModel::HpJet) return g_jet_cfg;
	return g_fx_cfg;
}

static void cb_printer_pdf(Fl_Widget *, void *v) {
	int model = (int)(intptr_t)v;
	char default_pdf[1024]; make_data_path("printer.pdf", default_pdf, sizeof(default_pdf));
	const char *path = fl_file_chooser("PDF Output", "*.pdf", default_pdf);
	if (!path) return;

	auto pm = static_cast<PrinterModel>(model);
	machine_pdf_start(&g_mach, path, model);
	if (g_mach.pdf_printer)
		g_mach.pdf_printer->apply_config(config_for_model(pm));
}

static void cb_printer_pdf_serial(Fl_Widget *, void *v) {
	int model = (int)(intptr_t)v;
	char default_pdf[1024]; make_data_path("printer.pdf", default_pdf, sizeof(default_pdf));
	const char *path = fl_file_chooser("PDF Output", "*.pdf", default_pdf);
	if (!path) return;

	auto pm = static_cast<PrinterModel>(model);
	machine_pdf_start_serial(&g_mach, path, model);
	if (g_mach.pdf_printer)
		g_mach.pdf_printer->apply_config(config_for_model(pm));
}

static void cb_printer_pdf_finish(Fl_Widget *, void *) {
	machine_pdf_finish(&g_mach);
	if (g_mach.cent_backend == CentBackend::Pdf)
		g_mach.cent_backend = CentBackend::File;
}

static void cb_serial_pty(Fl_Widget *, void *) {
	if (reconnect_uart(UartBackend::Pty, 0, nullptr))
		fl_message("Serial PTY: %s", g_mach.uart.path.c_str());
	else
		fl_alert("Cannot open serial PTY");
}

static void cb_serial_tcp(Fl_Widget *, void *) {
	const char *port = fl_input("TCP port:", "9600");
	if (port) {
		if (reconnect_uart(UartBackend::Tcp, atoi(port), nullptr))
			fl_message("Serial TCP: %s", g_mach.uart.path.c_str());
		else
			fl_alert("Cannot listen on TCP port %s", port);
	}
}

static void cb_serial_device(Fl_Widget *, void *) {
	const char *path = fl_file_chooser("Serial Device", "*", "/dev/cuau0");
	if (path) {
		if (reconnect_uart(UartBackend::Serial, 0, path))
			fl_message("Serial: %s", g_mach.uart.path.c_str());
		else
			fl_alert("Cannot open serial device %s", path);
	}
}

static void cb_serial_disconnect(Fl_Widget *, void *) {
	uart_destroy(&g_mach.uart);
}

static void cb_speed(Fl_Widget *, void *v) {
	g_speed = (int)(intptr_t)v;
}

/* ---- NVRAM path helper ---- */

static void make_nvram_path(const char *rom, char *out, size_t sz) {
	const char *base = strrchr(rom, '/');
#ifdef _WIN32
	const char *base2 = strrchr(rom, '\\');
	if (base2 && (!base || base2 > base)) base = base2;
#endif
	base = base ? base + 1 : rom;
	make_data_path("", out, sz);
	size_t len = strlen(out);
	snprintf(out + len, sz - len, "%s.nvram", base);
}

/* ---- main ---- */

int main(int argc, char *argv[])
{
#ifdef _WIN32
	WSADATA wsa;
	WSAStartup(MAKEWORD(2, 2), &wsa);
#else
	signal(SIGPIPE, SIG_IGN);
#endif

	auto uart_backend = UartBackend::Pty;
	int tcp_port = 0;
	const char *serial_path = nullptr;
	auto cent_backend = CentBackend::File;
	const char *cent_path = nullptr;
	const char *model_name = nullptr;
	const char *bios_name = nullptr;
	const char *romdir = nullptr;
	const char *rom_path = nullptr;
	g_pccard_path[0] = 0;
	g_floppy_path[0] = 0;

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--tcp") && i+1 < argc)
			{ uart_backend = UartBackend::Tcp; tcp_port = atoi(argv[++i]); }
		else if (!strcmp(argv[i], "--serial") && i+1 < argc)
			{ uart_backend = UartBackend::Serial; serial_path = argv[++i]; }
		else if (!strcmp(argv[i], "--lpt") && i+1 < argc)
			{ cent_backend = CentBackend::Lpt; cent_path = argv[++i]; }
		else if (!strcmp(argv[i], "--ppi") && i+1 < argc)
			{ cent_backend = CentBackend::Ppi; cent_path = argv[++i]; }
		else if (!strcmp(argv[i], "--pccard") && i+1 < argc)
			{ snprintf(g_pccard_path, sizeof(g_pccard_path), "%s", argv[++i]); }
		else if (!strcmp(argv[i], "--floppy") && i+1 < argc)
			{ snprintf(g_floppy_path, sizeof(g_floppy_path), "%s", argv[++i]); }
		else if (!strcmp(argv[i], "--remote") && i+1 < argc)
			g_remote_port = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--model") && i+1 < argc)
			model_name = argv[++i];
		else if (!strcmp(argv[i], "--bios") && i+1 < argc)
			bios_name = argv[++i];
		else if (!strcmp(argv[i], "--romdir") && i+1 < argc)
			romdir = argv[++i];
		else if (!strcmp(argv[i], "--rom") && i+1 < argc)
			rom_path = argv[++i];
		else if (!rom_path) {
			if (model_find(argv[i]))
				model_name = argv[i];
			else
				rom_path = argv[i];
		}
	}

	/* romdir: search --romdir > data dir > install dir > ./roms */
	romdir = find_rom_dir(romdir);

	/* scan romdir */
	static char found_path[1024];
	if (!rom_path && romdir) {
		const rom_entry_t *found = nullptr;
		for (int i = 0; i < rom_db_count; i++) {
			if (model_name && strcmp(rom_db[i].model, model_name) != 0) continue;
			if (bios_name && rom_db[i].bios && strcmp(rom_db[i].bios, bios_name) != 0) continue;
			snprintf(found_path, sizeof(found_path), "%s/%s", romdir, rom_db[i].filename);
			struct stat st;
			if (stat(found_path, &st) == 0) {
				found = &rom_db[i];
				break;
			}
		}
		if (!model_name && !rom_path) {
			fprintf(stderr, "Available ROMs");
			if (romdir) fprintf(stderr, " in %s", romdir);
			fprintf(stderr, ":\n");
			DIR *d = opendir(romdir);
			if (d) {
				struct dirent *de;
				while ((de = readdir(d)) != nullptr) {
					for (int i = 0; i < rom_db_count; i++) {
						if (strcmp(de->d_name, rom_db[i].filename) != 0) continue;
						const model_t *md = model_find(rom_db[i].model);
						fprintf(stderr, "  %-10s %-6s  %s  (%s)\n",
							rom_db[i].model, rom_db[i].bios ? rom_db[i].bios : "",
							de->d_name, md ? md->description : "");
					}
				}
				closedir(d);
			}
			fprintf(stderr, "\nUsage: %s [model | rom] [options]\n"
				"  --model NAME  --bios VER  --romdir PATH  --rom FILE\n"
				"  --tcp PORT  --serial DEV  --lpt DEV  --ppi DEV\n"
				"  --pccard FILE  --floppy FILE\n"
			"  --remote PORT\n", argv[0]);
			return 1;
		}
		if (found) {
			rom_path = found_path;
			if (!model_name) model_name = found->model;
		}
	}

	if (!rom_path) { fprintf(stderr, "No ROM found\n"); return 1; }

	/* identify ROM */
	const rom_entry_t *rom_entry = nullptr;
	{
		int rf = open(rom_path, O_RDONLY);
		if (rf >= 0) {
			struct stat rst;
			fstat(rf, &rst);
			size_t n = (size_t)rst.st_size;
			uint8_t *tmp = (uint8_t *)mmap(nullptr, n, PROT_READ, MAP_PRIVATE, rf, 0);
			if (tmp != MAP_FAILED) {
				rom_entry = rom_find_by_crc(crc32_buf(tmp, n));
				if (rom_entry && !model_name) model_name = rom_entry->model;
				munmap(tmp, n);
			}
			close(rf);
		}
	}

	if (!model_name) model_name = "dwT400";
	g_model = model_find(model_name);
	if (!g_model) { fprintf(stderr, "Unknown model: %s\n", model_name); return 1; }
	fprintf(stderr, "Model: %s (%s)\n", g_model->name, g_model->description);

	make_nvram_path(rom_path, g_nvram_path, sizeof(g_nvram_path));

	if (machine_init(&g_mach, g_model, uart_backend, tcp_port, serial_path,
	                 cent_backend, cent_path) < 0) {
		fprintf(stderr, "Machine: initialization failed\n");
		uart_destroy(&g_mach.uart);
		return 1;
	}
	if (g_floppy_path[0] && fdc_load_disk(&g_mach.fdc, g_floppy_path) < 0)
		fprintf(stderr, "FDC: cannot open %s\n", g_floppy_path);
	prefs_init();
	if (machine_open_nvram(&g_mach, g_nvram_path) < 0) {
		fprintf(stderr, "NVRAM: cannot allocate RAM\n");
		fdc_destroy(&g_mach.fdc);
		uart_destroy(&g_mach.uart);
		return 1;
	}
	g_mach.cpu.debug_cb = debug_monitor_cb;
	g_mach.cpu.debug_ctx = nullptr;
	if (machine_load_rom(&g_mach, rom_path, rom_entry) < 0) {
		machine_close_nvram(&g_mach);
		fdc_destroy(&g_mach.fdc);
		uart_destroy(&g_mach.uart);
		return 1;
	}
	if (g_pccard_path[0] && machine_open_pccard(&g_mach, g_pccard_path) < 0) {
		fprintf(stderr, "PC Card: cannot open %s\n", g_pccard_path);
		g_pccard_path[0] = 0;
	}
	machine_reset(&g_mach);

	/* ---- FLTK window ---- */

	g_lcd_h = g_mach.lcd_height;
	int win_w = LCD_WIDTH * SCALE;
	int win_h = MENUBAR_H + g_lcd_h * SCALE;
	char title[128];
	snprintf(title, sizeof(title), "dreamulator - %s", g_model->description);

	g_docked_regs = prefs_get_int("dock", "regs", 0);
	g_docked_dis = prefs_get_int("dock", "dis", 0);
	g_docked_mem = prefs_get_int("dock", "mem", 0);
	g_docked_periph = prefs_get_int("dock", "periph", 0);
	g_dock_h = prefs_get_int("dock", "height", 300);

	Fl_Double_Window *win = new Fl_Double_Window(win_w, win_h, title);
	g_main_win = win;
	win->callback(cb_quit);

	Fl_Menu_Bar *menu = new Fl_Menu_Bar(0, 0, win_w, MENUBAR_H);
	menu->add("&File/Quit",  FL_ALT+FL_F+4,  cb_quit);

	menu->add("&Machine/Power Button", FL_End,      cb_power);
	menu->add("&Machine/Clear NVRAM and Restart", 0, cb_clear_nvram);
	menu->add("&Machine/Main Battery Low", 0,       cb_battery, &g_mach.main_battery_low, FL_MENU_TOGGLE);
	menu->add("&Machine/Coin Battery Low", 0,       cb_battery, &g_mach.coin_battery_low, FL_MENU_TOGGLE);
	menu->add("&Machine/PCMCIA Battery Low", 0,     cb_battery, &g_mach.pccard_battery_low, FL_MENU_TOGGLE);

	if (g_model->has_pccard) {
		menu->add("M&edia/Insert PC Card...",  0, cb_insert_pccard);
		menu->add("M&edia/New PC Card...",     0, cb_new_pccard);
		menu->add("M&edia/Eject PC Card",      0, cb_eject_pccard);
	}
	if (g_model->has_floppy) {
		menu->add("M&edia/Insert Floppy...",   0, cb_insert_floppy);
		menu->add("M&edia/New Floppy...",      0, cb_new_floppy);
		menu->add("M&edia/Eject Floppy",       0, cb_eject_floppy);
	}
	//menu->add("&Printer/Parallel/IBM X24E...",     0, cb_printer_pdf, (void *)0);
	//menu->add("&Printer/Parallel/IBM XIII...",      0, cb_printer_pdf, (void *)1);
	//menu->add("&Printer/Parallel/Epson LQ...",     0, cb_printer_pdf, (void *)2);
	menu->add("&Printer/Parallel/Epson FX-80...",          0, cb_printer_pdf, (void *)3);
	menu->add("&Printer/Parallel/Canon BJ-10e...", 0, cb_printer_pdf, (void *)4);
	menu->add("&Printer/Parallel/HP LaserJet II...",       0, cb_printer_pdf, (void *)5);
	menu->add("&Printer/Parallel/Apple ImageWriter II...", 0, cb_printer_pdf, (void *)6);
	menu->add("&Printer/Serial/Apple ImageWriter II...",   0, cb_printer_pdf_serial, (void *)6);
	menu->add("&Printer/Finish PDF",               0, cb_printer_pdf_finish);
	menu->add("&Printer/Settings/ImageWriter II DIP Switches...", 0, cb_iw_dip_switches);
	menu->add("&Printer/Settings/Epson FX-80 DIP Switches...",   0, cb_fx_dip_switches);
	menu->add("&Printer/Settings/Canon BJ-10e Settings...",      0, cb_bj_dip_switches);
	menu->add("&Printer/Settings/HP LaserJet II Settings...",   0, cb_jet_settings);

	menu->add("&Serial/Connect PTY",       0, cb_serial_pty);
	menu->add("&Serial/Connect TCP...",    0, cb_serial_tcp);
	menu->add("&Serial/Connect Device...", 0, cb_serial_device);
	menu->add("&Serial/Disconnect",        0, cb_serial_disconnect);

	menu->add("&Debug/CPU Registers",      FL_F+1,              cb_show_regs);
	menu->add("&Debug/Disassembly",        FL_F+2,              cb_show_dis);
	menu->add("&Debug/Memory Editor",      FL_F+3,              cb_show_mem);
	menu->add("&Debug/Peripherals",        FL_F+4,              cb_show_periph,
	          0, FL_MENU_DIVIDER);
	menu->add("&Debug/Dock Registers",     FL_SHIFT+(FL_F+1),   cb_dock_regs,
	          0, FL_MENU_TOGGLE | (g_docked_regs ? FL_MENU_VALUE : 0));
	menu->add("&Debug/Dock Disassembly",   FL_SHIFT+(FL_F+2),   cb_dock_dis,
	          0, FL_MENU_TOGGLE | (g_docked_dis ? FL_MENU_VALUE : 0));
	menu->add("&Debug/Dock Memory",        FL_SHIFT+(FL_F+3),   cb_dock_mem,
	          0, FL_MENU_TOGGLE | (g_docked_mem ? FL_MENU_VALUE : 0));
	menu->add("&Debug/Dock Peripherals",   FL_SHIFT+(FL_F+4),   cb_dock_periph,
	          0, FL_MENU_TOGGLE | (g_docked_periph ? FL_MENU_VALUE : 0));

	menu->add("S&peed/Normal (1x)",   0, cb_speed, (void *)1, FL_MENU_RADIO | FL_MENU_VALUE);
	menu->add("S&peed/Double (2x)",   0, cb_speed, (void *)2, FL_MENU_RADIO);
	menu->add("S&peed/Half (0.5x)",   0, cb_speed, (void *)0, FL_MENU_RADIO);
	menu->add("S&peed/Unthrottled",   0, cb_speed, (void *)3, FL_MENU_RADIO);

	g_lcd = new LcdWidget(0, MENUBAR_H, win_w, g_lcd_h * SCALE);

	int dock_y = MENUBAR_H + g_lcd_h * SCALE;
	g_dock = new Fl_Tile(0, dock_y, win_w, g_dock_h);
	g_dock->box(FL_FLAT_BOX);
	g_dock->end();
	g_dock->hide();

	win->resizable(nullptr);
	win->size_range(LCD_WIDTH * SCALE, win_h, 0, 0);
	win->end();
	int fl_argc = 1;
	win->show(fl_argc, argv);
	g_lcd->take_focus();

#ifdef HAS_PORTAUDIO
	Pa_Initialize();
	PaError err = Pa_OpenDefaultStream(&g_audio, 0, 1, paFloat32,
		SAMPLE_RATE, AUDIO_BUF, pa_callback, &g_mach);
	if (err == paNoError)
		Pa_StartStream(g_audio);
	else
		fprintf(stderr, "PortAudio: %s\n", Pa_GetErrorText(err));
#endif

	/* ---- run ---- */

	Fl::add_handler([](int event) -> int {
		if (event != FL_SHORTCUT && event != FL_KEYDOWN) return 0;
		int key = Fl::event_key();
		if (key < FL_F+1 || key > FL_F+4) return 0;
		bool shift = Fl::event_state() & FL_SHIFT;
		if (!shift) {
			if (key == FL_F+1) { cb_show_regs(nullptr, nullptr); return 1; }
			if (key == FL_F+2) { cb_show_dis(nullptr, nullptr); return 1; }
			if (key == FL_F+3) { cb_show_mem(nullptr, nullptr); return 1; }
			if (key == FL_F+4) { cb_show_periph(nullptr, nullptr); return 1; }
		} else {
			if (key == FL_F+1) { cb_dock_regs(nullptr, nullptr); return 1; }
			if (key == FL_F+2) { cb_dock_dis(nullptr, nullptr); return 1; }
			if (key == FL_F+3) { cb_dock_mem(nullptr, nullptr); return 1; }
			if (key == FL_F+4) { cb_dock_periph(nullptr, nullptr); return 1; }
		}
		return 0;
	});

	if (g_remote_port > 0)
		remote_init(&g_mach, g_remote_port);

	Fl::add_timeout(1.0/60.0, emu_tick, nullptr);
	Fl::run();
	final_exit(0);
}
