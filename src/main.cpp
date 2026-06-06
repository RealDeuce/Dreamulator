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
#include <FL/Fl_Return_Button.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Group.H>
#ifdef HAS_PORTAUDIO
#include <portaudio.h>
#endif
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include "machine.h"
#include "print/printer.h"
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
public:
	LcdWidget(int x, int y, int w, int h) : Fl_Widget(x, y, w, h) {}

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
		if (event == FL_FOCUS || event == FL_UNFOCUS) return 1;

		if (event == FL_KEYDOWN || event == FL_KEYUP) {
			int key = Fl::event_key();
			bool down = (event == FL_KEYDOWN);

			if (key == FL_End) {
				machine_power_button(&g_mach, down);
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

static void cb_reset(Fl_Widget *, void *) {
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

	const char *path = fl_file_chooser("Save New PC Card As", "*", "card.bin");
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
	const char *path = fl_file_chooser("Save New Floppy As", "*.img", "floppy.img");
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

	Fl_Box hdr1(10, 5, 320, 20, "SW1 - Character & Page Settings");
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

	Fl_Check_Button perf(10, 120, 320, 25, "Perforation Skip (SW1-5)");
	perf.value(cfg.perf_skip > 0 ? 1 : 0);

	Fl_Check_Button autolf(10, 145, 320, 25, "Auto LF after CR (SW1-8)");
	autolf.value(cfg.auto_lf ? 1 : 0);

	Fl_Box hdr2(10, 175, 320, 20, "Front Panel");
	hdr2.labelfont(FL_BOLD);
	hdr2.align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

	Fl_Choice font(160, 200, 170, 25, "Print Quality:");
	font.add("Draft");
	font.add("Standard");
	font.add("Near Letter Quality");
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

	Fl_Box hdr1(10, 5, 320, 20, "SW1 - Character & Page Settings");
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

	Fl_Check_Button perf(10, 90, 320, 25, "Perforation Skip (SW1-5)");
	perf.value(cfg.perf_skip > 0 ? 1 : 0);

	Fl_Check_Button autolf(10, 115, 320, 25, "Auto LF after CR (SW1-8)");
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

static PrinterConfig g_iw_cfg;
static PrinterConfig g_fx_cfg;
static bool g_printer_cfg_loaded = false;

static void printer_cfg_load()
{
	if (g_printer_cfg_loaded) return;
	g_printer_cfg_loaded = true;
	g_iw_cfg = default_config_for(PrinterModel::ImageWriter);
	g_fx_cfg = default_config_for(PrinterModel::EpsonFX);

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

static PrinterConfig &config_for_model(PrinterModel pm)
{
	printer_cfg_load();
	if (pm == PrinterModel::ImageWriter) return g_iw_cfg;
	return g_fx_cfg;
}

static void cb_printer_pdf(Fl_Widget *, void *v) {
	int model = (int)(intptr_t)v;
	const char *path = fl_file_chooser("PDF Output", "*.pdf", "printer.pdf");
	if (!path) return;

	auto pm = static_cast<PrinterModel>(model);
	machine_pdf_start(&g_mach, path, model);
	if (g_mach.pdf_printer)
		g_mach.pdf_printer->apply_config(config_for_model(pm));
}

static void cb_printer_pdf_serial(Fl_Widget *, void *v) {
	int model = (int)(intptr_t)v;
	const char *path = fl_file_chooser("PDF Output", "*.pdf", "printer.pdf");
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
	snprintf(out, sz, "%s.nvram", rom);
}

/* ---- main ---- */

int main(int argc, char *argv[])
{
	signal(SIGPIPE, SIG_IGN);

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

	/* romdir default */
	if (!romdir) {
		struct stat st;
		if (stat("roms", &st) == 0 && S_ISDIR(st.st_mode))
			romdir = "roms";
	}

	/* scan romdir */
	static char found_path[1024];
	if (!rom_path && romdir) {
		const rom_entry_t *found = nullptr;
		DIR *d = opendir(romdir);
		if (d) {
			struct dirent *de;
			while ((de = readdir(d)) != nullptr) {
				for (int i = 0; i < rom_db_count; i++) {
					if (strcmp(de->d_name, rom_db[i].filename) != 0) continue;
					if (model_name && strcmp(rom_db[i].model, model_name) != 0) continue;
					if (bios_name && rom_db[i].bios && strcmp(rom_db[i].bios, bios_name) != 0) continue;
					if (!found || (model_name && strcmp(rom_db[i].model, model_name) == 0)) {
						found = &rom_db[i];
						snprintf(found_path, sizeof(found_path), "%s/%s", romdir, de->d_name);
					}
				}
			}
			closedir(d);
		}
		if (!model_name && !rom_path) {
			fprintf(stderr, "Available ROMs");
			if (romdir) fprintf(stderr, " in %s", romdir);
			fprintf(stderr, ":\n");
			d = opendir(romdir);
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
	menu->add("&Machine/Reset",        0,           cb_reset);
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
	//menu->add("&Printer/Parallel/Canon BJ-10e...", 0, cb_printer_pdf, (void *)4);
	//menu->add("&Printer/Parallel/HP JET...",       0, cb_printer_pdf, (void *)5);
	menu->add("&Printer/Parallel/Apple ImageWriter II...", 0, cb_printer_pdf, (void *)6);
	menu->add("&Printer/Serial/Apple ImageWriter II...",   0, cb_printer_pdf_serial, (void *)6);
	menu->add("&Printer/Finish PDF",               0, cb_printer_pdf_finish);
	menu->add("&Printer/Settings/ImageWriter II DIP Switches...", 0, cb_iw_dip_switches);
	menu->add("&Printer/Settings/Epson FX-80 DIP Switches...",   0, cb_fx_dip_switches);

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
