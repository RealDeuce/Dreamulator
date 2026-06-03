// license:BSD-3-Clause
// copyright-holders:Stephen Hurd
#include <FL/Fl.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Menu_Bar.H>
#include <FL/Fl_Widget.H>
#include <FL/Fl_File_Chooser.H>
#include <FL/fl_ask.H>
#include <FL/fl_draw.H>
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

extern "C" {
#include "machine.h"
}
#include "prefs.h"
#include "dbg_regs.h"
#include "dbg_dis.h"
#include "dbg_mem.h"
#include "remote.h"

#define SCALE       2
#define MENUBAR_H   25
#define SAMPLE_RATE 48000
#define AUDIO_BUF   512
#define BEEP_VOL    0.05f

/* ---- globals ---- */

static machine_t    g_mach;
#ifdef HAS_PORTAUDIO
static PaStream    *g_audio;
#endif
static int          g_speed = 1;
static int          g_remote_port = 0;
static DbgRegsWindow *g_dbg_regs = nullptr;
static DbgDisWindow  *g_dbg_dis  = nullptr;
static DbgMemWindow  *g_dbg_mem  = nullptr;
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
			return 1;
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
	uint16_t div = m->buzzer_low | ((uint16_t)m->buzzer_high << 8);
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

/* ---- emulation tick ---- */

static void emu_tick(void *data)
{
	LcdWidget *lcd = (LcdWidget *)data;
	int cycles;
	switch (g_speed) {
	case 0: cycles = CPU_CLOCK / 120; break;
	default:
	case 1: cycles = CPU_CLOCK / 60; break;
	case 2: cycles = CPU_CLOCK / 30; break;
	case 3: cycles = CPU_CLOCK / 6; break;
	}
	machine_step(&g_mach, cycles);
	lcd->redraw();
	Fl::repeat_timeout((g_speed == 3) ? 0.001 : 1.0/60.0, emu_tick, data);
}

/* ---- UART IRQ helpers (needed for reconnect) ---- */

static void uart_txrdy_glue(void *ctx, bool state) {
	machine_t *m = (machine_t *)ctx;
	if (state) { m->irq_active |= 0x04; /* update_irqs called by machine_step */ }
}
static void uart_rxrdy_glue(void *ctx, bool state) {
	machine_t *m = (machine_t *)ctx;
	if (state) { m->irq_active |= 0x08; }
}

static void reconnect_uart(int backend, int port, const char *path) {
	uart_destroy(&g_mach.uart);
	uart_init(&g_mach.uart, backend, port, path);
	g_mach.uart.txrdy_cb = uart_txrdy_glue;
	g_mach.uart.rxrdy_cb = uart_rxrdy_glue;
	g_mach.uart.cb_ctx   = &g_mach;
}

/* ---- menu callbacks ---- */

/* ---- debug callbacks ---- */

static void debug_monitor_cb(void *)
{
	if (g_dbg_regs && g_dbg_regs->visible()) g_dbg_regs->refresh();
	if (g_dbg_dis && g_dbg_dis->visible()) g_dbg_dis->refresh();
}

static void cb_show_regs(Fl_Widget *, void *) {
	if (!g_dbg_regs) g_dbg_regs = new DbgRegsWindow(&g_mach.cpu);
	g_dbg_regs->show();
	g_dbg_regs->refresh();
}

static void cb_show_dis(Fl_Widget *, void *) {
	if (!g_dbg_dis) g_dbg_dis = new DbgDisWindow(&g_mach.cpu);
	g_dbg_dis->show();
	g_dbg_dis->refresh();
}

static void cb_show_mem(Fl_Widget *, void *) {
	if (!g_dbg_mem) g_dbg_mem = new DbgMemWindow(&g_mach.cpu);
	g_dbg_mem->show();
	g_dbg_mem->refresh();
}

/* ---- main callbacks ---- */

static void cb_quit(Fl_Widget *, void *) {
	machine_close_pccard(&g_mach);
	machine_close_nvram(&g_mach);
	fdc_destroy(&g_mach.fdc);
	uart_destroy(&g_mach.uart);
	exit(0);
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
	machine_open_pccard(&g_mach, g_pccard_path);
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
	uint8_t *p = (uint8_t *)mmap(NULL, sizes[idx].size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
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
	fdc_load_disk(&g_mach.fdc, g_floppy_path);
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

static void cb_printer_file(Fl_Widget *, void *) {
	const char *path = fl_file_chooser("Printer Output", "*", "printer.out");
	if (path) {
		if (g_mach.printer) fclose((FILE *)g_mach.printer);
		g_mach.printer = fopen(path, "ab");
	}
}

static void cb_serial_pty(Fl_Widget *, void *) {
	reconnect_uart(UART_PTY, 0, nullptr);
	fl_message("Serial PTY: %s", g_mach.uart.path);
}

static void cb_serial_tcp(Fl_Widget *, void *) {
	const char *port = fl_input("TCP port:", "9600");
	if (port) {
		reconnect_uart(UART_TCP, atoi(port), nullptr);
		fl_message("Serial TCP: %s", g_mach.uart.path);
	}
}

static void cb_serial_device(Fl_Widget *, void *) {
	const char *path = fl_file_chooser("Serial Device", "*", "/dev/cuau0");
	if (path) {
		reconnect_uart(UART_SERIAL, 0, path);
		fl_message("Serial: %s", g_mach.uart.path);
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

	int uart_backend = UART_PTY;
	int tcp_port = 0;
	const char *serial_path = nullptr;
	int cent_backend = CENT_FILE;
	const char *cent_path = nullptr;
	const char *model_name = nullptr;
	const char *bios_name = nullptr;
	const char *romdir = nullptr;
	const char *rom_path = nullptr;
	g_pccard_path[0] = 0;
	g_floppy_path[0] = 0;

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--tcp") && i+1 < argc)
			{ uart_backend = UART_TCP; tcp_port = atoi(argv[++i]); }
		else if (!strcmp(argv[i], "--serial") && i+1 < argc)
			{ uart_backend = UART_SERIAL; serial_path = argv[++i]; }
		else if (!strcmp(argv[i], "--lpt") && i+1 < argc)
			{ cent_backend = CENT_LPT; cent_path = argv[++i]; }
		else if (!strcmp(argv[i], "--ppi") && i+1 < argc)
			{ cent_backend = CENT_PPI; cent_path = argv[++i]; }
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
		else if (!rom_path)
			rom_path = argv[i];
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
			fprintf(stderr, "\nUsage: %s [options] [rom]\n"
				"  --model NAME  --bios VER  --romdir PATH\n"
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
		FILE *rf = fopen(rom_path, "rb");
		if (rf) {
			uint8_t tmp[ROM_SIZE];
			size_t n = fread(tmp, 1, ROM_SIZE, rf);
			fclose(rf);
			rom_entry = rom_find_by_crc(crc32_buf(tmp, n));
			if (rom_entry && !model_name) model_name = rom_entry->model;
		}
	}

	if (!model_name) model_name = "dwT400";
	g_model = model_find(model_name);
	if (!g_model) { fprintf(stderr, "Unknown model: %s\n", model_name); return 1; }
	fprintf(stderr, "Model: %s (%s)\n", g_model->name, g_model->description);

	make_nvram_path(rom_path, g_nvram_path, sizeof(g_nvram_path));

	machine_init(&g_mach, g_model, uart_backend, tcp_port, serial_path,
	             cent_backend, cent_path);
	if (g_pccard_path[0]) machine_open_pccard(&g_mach, g_pccard_path);
	if (g_floppy_path[0]) fdc_load_disk(&g_mach.fdc, g_floppy_path);
	prefs_init();
	machine_open_nvram(&g_mach, g_nvram_path);
	g_mach.cpu.debug_cb = debug_monitor_cb;
	g_mach.cpu.debug_ctx = nullptr;
	machine_load_rom(&g_mach, rom_path, rom_entry);
	machine_reset(&g_mach);

	/* ---- FLTK window ---- */

	int lcd_h = g_mach.lcd_height;
	int win_w = LCD_WIDTH * SCALE;
	int win_h = MENUBAR_H + lcd_h * SCALE;
	char title[128];
	snprintf(title, sizeof(title), "dreamulator - %s", g_model->description);

	Fl_Double_Window *win = new Fl_Double_Window(win_w, win_h, title);
	win->callback([](Fl_Widget *, void *) { cb_quit(nullptr, nullptr); });

	Fl_Menu_Bar *menu = new Fl_Menu_Bar(0, 0, win_w, MENUBAR_H);
	menu->add("&File/Quit",  FL_CTRL+'q',  cb_quit);

	menu->add("&Machine/Power Button", FL_End,      cb_power);
	menu->add("&Machine/Reset",        0,           cb_reset);
	menu->add("&Machine/Main Battery Low", 0,       cb_battery, &g_mach.main_battery_low, FL_MENU_TOGGLE);
	menu->add("&Machine/Coin Battery Low", 0,       cb_battery, &g_mach.coin_battery_low, FL_MENU_TOGGLE);

	menu->add("M&edia/Insert PC Card...",  0, cb_insert_pccard);
	menu->add("M&edia/New PC Card...",     0, cb_new_pccard);
	menu->add("M&edia/Eject PC Card",      0, cb_eject_pccard);
	menu->add("M&edia/Insert Floppy...",   0, cb_insert_floppy);
	menu->add("M&edia/New Floppy...",      0, cb_new_floppy);
	menu->add("M&edia/Eject Floppy",       0, cb_eject_floppy);
	menu->add("M&edia/Printer Output...",  0, cb_printer_file);

	menu->add("&Serial/Connect PTY",       0, cb_serial_pty);
	menu->add("&Serial/Connect TCP...",    0, cb_serial_tcp);
	menu->add("&Serial/Connect Device...", 0, cb_serial_device);
	menu->add("&Serial/Disconnect",        0, cb_serial_disconnect);

	menu->add("&Debug/CPU Registers",  FL_CTRL+'r', cb_show_regs);
	menu->add("&Debug/Disassembly",    FL_CTRL+'d', cb_show_dis);
	menu->add("&Debug/Memory Editor",  FL_CTRL+'m', cb_show_mem);

	menu->add("S&peed/Normal (1x)",   0, cb_speed, (void *)1, FL_MENU_RADIO | FL_MENU_VALUE);
	menu->add("S&peed/Double (2x)",   0, cb_speed, (void *)2, FL_MENU_RADIO);
	menu->add("S&peed/Half (0.5x)",   0, cb_speed, (void *)0, FL_MENU_RADIO);
	menu->add("S&peed/Unthrottled",   0, cb_speed, (void *)3, FL_MENU_RADIO);

	LcdWidget *lcd = new LcdWidget(0, MENUBAR_H, win_w, lcd_h * SCALE);
	win->end();
	int fl_argc = 1;
	win->show(fl_argc, argv);
	lcd->take_focus();

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

	if (g_remote_port > 0)
		remote_init(&g_mach, g_remote_port);

	Fl::add_timeout(1.0/60.0, emu_tick, lcd);
	int ret = Fl::run();

	/* ---- cleanup ---- */

#ifdef HAS_PORTAUDIO
	if (g_audio) { Pa_StopStream(g_audio); Pa_CloseStream(g_audio); }
	Pa_Terminate();
#endif
	remote_shutdown();
	machine_close_pccard(&g_mach);
	machine_close_nvram(&g_mach);
	fdc_destroy(&g_mach.fdc);
	uart_destroy(&g_mach.uart);
	return ret;
}
