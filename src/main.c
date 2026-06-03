// license:BSD-3-Clause
// copyright-holders:Stephen Hurd
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <dirent.h>
#include <sys/stat.h>
#include "machine.h"

#define SCALE       2
#define SAMPLE_RATE 48000
#define AUDIO_BUF   512
#define BEEP_VOL    0.05f

struct keymap { SDL_Scancode sc; int row, bit; };

static const struct keymap keymap[] = {
	{ SDL_SCANCODE_LSHIFT,       0, 0 },
	{ SDL_SCANCODE_RSHIFT,       0, 1 },
	{ SDL_SCANCODE_LEFT,         0, 3 },
	{ SDL_SCANCODE_RETURN,       0, 4 },
	{ SDL_SCANCODE_LALT,         1, 0 },
	{ SDL_SCANCODE_GRAVE,        1, 1 },
	{ SDL_SCANCODE_ESCAPE,       1, 2 },
	{ SDL_SCANCODE_SPACE,        1, 3 },
	{ SDL_SCANCODE_5,            1, 6 },
	{ SDL_SCANCODE_LCTRL,        2, 0 },
	{ SDL_SCANCODE_CAPSLOCK,     2, 1 },
	{ SDL_SCANCODE_1,            2, 2 },
	{ SDL_SCANCODE_TAB,          2, 3 },
	{ SDL_SCANCODE_3,            3, 0 },
	{ SDL_SCANCODE_2,            3, 1 },
	{ SDL_SCANCODE_Q,            3, 2 },
	{ SDL_SCANCODE_W,            3, 3 },
	{ SDL_SCANCODE_E,            3, 4 },
	{ SDL_SCANCODE_S,            3, 6 },
	{ SDL_SCANCODE_D,            3, 7 },
	{ SDL_SCANCODE_4,            4, 0 },
	{ SDL_SCANCODE_Z,            4, 2 },
	{ SDL_SCANCODE_X,            4, 3 },
	{ SDL_SCANCODE_A,            4, 4 },
	{ SDL_SCANCODE_R,            4, 6 },
	{ SDL_SCANCODE_F,            4, 7 },
	{ SDL_SCANCODE_B,            5, 2 },
	{ SDL_SCANCODE_V,            5, 3 },
	{ SDL_SCANCODE_T,            5, 4 },
	{ SDL_SCANCODE_Y,            5, 5 },
	{ SDL_SCANCODE_G,            5, 6 },
	{ SDL_SCANCODE_C,            5, 7 },
	{ SDL_SCANCODE_6,            6, 0 },
	{ SDL_SCANCODE_DOWN,         6, 1 },
	{ SDL_SCANCODE_INSERT,       6, 2 },
	{ SDL_SCANCODE_RIGHT,        6, 3 },
	{ SDL_SCANCODE_BACKSLASH,    6, 4 },
	{ SDL_SCANCODE_SLASH,        6, 5 },
	{ SDL_SCANCODE_H,            6, 6 },
	{ SDL_SCANCODE_N,            6, 7 },
	{ SDL_SCANCODE_EQUALS,       7, 0 },
	{ SDL_SCANCODE_7,            7, 1 },
	{ SDL_SCANCODE_PAGEUP,       7, 2 },
	{ SDL_SCANCODE_UP,           7, 3 },
	{ SDL_SCANCODE_PAGEDOWN,     7, 4 },
	{ SDL_SCANCODE_U,            7, 5 },
	{ SDL_SCANCODE_M,            7, 6 },
	{ SDL_SCANCODE_K,            7, 7 },
	{ SDL_SCANCODE_8,            8, 0 },
	{ SDL_SCANCODE_MINUS,        8, 1 },
	{ SDL_SCANCODE_RIGHTBRACKET, 8, 2 },
	{ SDL_SCANCODE_LEFTBRACKET,  8, 3 },
	{ SDL_SCANCODE_APOSTROPHE,   8, 4 },
	{ SDL_SCANCODE_I,            8, 5 },
	{ SDL_SCANCODE_J,            8, 6 },
	{ SDL_SCANCODE_COMMA,        8, 7 },
	{ SDL_SCANCODE_0,            9, 0 },
	{ SDL_SCANCODE_9,            9, 1 },
	{ SDL_SCANCODE_BACKSPACE,    9, 2 },
	{ SDL_SCANCODE_P,            9, 3 },
	{ SDL_SCANCODE_SEMICOLON,    9, 4 },
	{ SDL_SCANCODE_L,            9, 5 },
	{ SDL_SCANCODE_O,            9, 6 },
	{ SDL_SCANCODE_PERIOD,       9, 7 },
	{ 0, -1, -1 }
};

static void audio_callback(void *userdata, Uint8 *stream, int len)
{
	machine_t *m = userdata;
	float *out = (float *)stream;
	int samples = len / (int)sizeof(float);

	uint16_t divisor = m->buzzer_low | ((uint16_t)m->buzzer_high << 8);
	float freq = (divisor && m->buzzer_on)
		? (float)(XTAL / 64) / (float)divisor
		: 0.0f;
	float step = freq / (float)SAMPLE_RATE;

	for (int i = 0; i < samples; i++) {
		if (freq > 0.0f) {
			out[i] = (m->beeper_phase < 0.5f) ? BEEP_VOL : -BEEP_VOL;
			m->beeper_phase += step;
			if (m->beeper_phase >= 1.0f)
				m->beeper_phase -= 1.0f;
		} else {
			out[i] = 0.0f;
			m->beeper_phase = 0.0f;
		}
	}
}

static char *make_nvram_path(const char *rom_path)
{
	size_t len = strlen(rom_path);
	char *p = malloc(len + 7);
	if (p) { memcpy(p, rom_path, len); memcpy(p + len, ".nvram", 7); }
	return p;
}

int main(int argc, char *argv[])
{
	signal(SIGPIPE, SIG_IGN);

	int uart_backend = UART_PTY;
	int tcp_port = 0;
	const char *serial_path = NULL;
	int cent_backend = CENT_FILE;
	const char *cent_path = NULL;
	const char *pccard_path = NULL;
	const char *floppy_path = NULL;
	const char *model_name = NULL;
	const char *bios_name = NULL;
	const char *romdir = NULL;
	const char *rom_path = NULL;

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--tcp") == 0 && i + 1 < argc) {
			uart_backend = UART_TCP;
			tcp_port = atoi(argv[++i]);
		} else if (strcmp(argv[i], "--serial") == 0 && i + 1 < argc) {
			uart_backend = UART_SERIAL;
			serial_path = argv[++i];
		} else if (strcmp(argv[i], "--lpt") == 0 && i + 1 < argc) {
			cent_backend = CENT_LPT;
			cent_path = argv[++i];
		} else if (strcmp(argv[i], "--ppi") == 0 && i + 1 < argc) {
			cent_backend = CENT_PPI;
			cent_path = argv[++i];
		} else if (strcmp(argv[i], "--pccard") == 0 && i + 1 < argc) {
			pccard_path = argv[++i];
		} else if (strcmp(argv[i], "--floppy") == 0 && i + 1 < argc) {
			floppy_path = argv[++i];
		} else if (strcmp(argv[i], "--model") == 0 && i + 1 < argc) {
			model_name = argv[++i];
		} else if (strcmp(argv[i], "--bios") == 0 && i + 1 < argc) {
			bios_name = argv[++i];
		} else if (strcmp(argv[i], "--romdir") == 0 && i + 1 < argc) {
			romdir = argv[++i];
		} else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
			rom_path = NULL;
			model_name = NULL;
			goto usage;
		} else if (!rom_path) {
			rom_path = argv[i];
		}
	}

	/* ---- resolve romdir ---- */
	if (!romdir) {
		struct stat st;
		if (stat("roms", &st) == 0 && S_ISDIR(st.st_mode))
			romdir = "roms";
	}

	/* ---- scan romdir for known ROMs ---- */
	if (!rom_path && romdir) {
		const rom_entry_t *found = NULL;
		static char found_path[1024];
		DIR *d = opendir(romdir);
		if (d) {
			struct dirent *de;
			while ((de = readdir(d)) != NULL) {
				for (int i = 0; i < rom_db_count; i++) {
					if (strcmp(de->d_name, rom_db[i].filename) != 0)
						continue;
					if (model_name && strcmp(rom_db[i].model, model_name) != 0)
						continue;
					if (bios_name && rom_db[i].bios &&
					    strcmp(rom_db[i].bios, bios_name) != 0)
						continue;
					if (!model_name && !found) {
						/* listing mode — print all */
					}
					if (found && model_name) break;
					found = &rom_db[i];
					snprintf(found_path, sizeof(found_path),
						"%s/%s", romdir, de->d_name);
				}
			}
			closedir(d);
		}

		if (!model_name && !rom_path) {
			/* list mode */
			fprintf(stderr, "Available ROMs");
			if (romdir) fprintf(stderr, " in %s", romdir);
			fprintf(stderr, ":\n");
			int any = 0;
			if (romdir) {
				d = opendir(romdir);
				if (d) {
					struct dirent *de2;
					while ((de2 = readdir(d)) != NULL) {
						for (int i = 0; i < rom_db_count; i++) {
							if (strcmp(de2->d_name, rom_db[i].filename) != 0)
								continue;
							const model_t *md = model_find(rom_db[i].model);
							fprintf(stderr, "  %-10s %-6s  %s  (%s)\n",
								rom_db[i].model,
								rom_db[i].bios ? rom_db[i].bios : "",
								de2->d_name,
								md ? md->description : "");
							any = 1;
						}
					}
					closedir(d);
				}
			}
			if (!any)
				fprintf(stderr, "  (none found)\n");
usage:
			fprintf(stderr, "\nUsage: %s [options] [rom]\n"
				"  --model NAME      Machine model (default: auto-detect)\n"
				"  --bios VER        BIOS version (e.g. v3.1, v2.1)\n"
				"  --romdir PATH     ROM search directory\n"
				"  --tcp PORT        UART via TCP\n"
				"  --serial DEV      UART via serial device\n"
				"  --lpt DEV         Centronics via lpt device\n"
				"  --ppi DEV         Centronics via ppi device\n"
				"  --pccard FILE     PC Card SRAM image\n"
			"  --floppy FILE     Floppy disk image (T200)\n"
				"\nModels: ", argv[0]);
			for (int i = 0; i < model_count; i++)
				fprintf(stderr, "%s%s", i ? ", " : "", models[i].name);
			fprintf(stderr, "\n");
			return 1;
		}

		if (found) {
			rom_path = found_path;
			if (!model_name) model_name = found->model;
		}
	}

	if (!rom_path) {
		fprintf(stderr, "No ROM found");
		if (model_name) fprintf(stderr, " for model %s", model_name);
		if (bios_name) fprintf(stderr, " bios %s", bios_name);
		if (romdir) fprintf(stderr, " in %s", romdir);
		fprintf(stderr, "\n");
		return 1;
	}

	/* ---- identify ROM and resolve model ---- */
	const model_t *model;
	const rom_entry_t *rom_entry = NULL;

	/* pre-read ROM to identify */
	{
		FILE *rf = fopen(rom_path, "rb");
		if (rf) {
			uint8_t tmp[ROM_SIZE];
			size_t n = fread(tmp, 1, ROM_SIZE, rf);
			fclose(rf);
			uint32_t crc = crc32_buf(tmp, n);
			rom_entry = rom_find_by_crc(crc);
			if (rom_entry && !model_name)
				model_name = rom_entry->model;
		}
	}

	if (!model_name) model_name = "dwT400";
	model = model_find(model_name);
	if (!model) {
		fprintf(stderr, "Unknown model: %s\n", model_name);
		return 1;
	}
	fprintf(stderr, "Model: %s (%s, %uKB RAM, %d-line LCD)\n",
		model->name, model->description,
		model->ram_size / 1024, model->lcd_height / 8);

	char *nvram_path = make_nvram_path(rom_path);

	machine_t m;
	if (machine_init(&m, model, uart_backend, tcp_port, serial_path,
	                 cent_backend, cent_path) != 0) return 1;
	if (pccard_path)
		machine_load_pccard(&m, pccard_path);
	if (floppy_path)
		fdc_load_disk(&m.fdc, floppy_path);
	if (machine_load_rom(&m, rom_path, rom_entry) != 0) return 1;
	machine_load_nvram(&m, nvram_path);
	machine_reset(&m);

	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
		fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
		return 1;
	}

	int win_w = LCD_WIDTH * SCALE;
	int win_h = m.lcd_height * SCALE;
	char title[64];
	snprintf(title, sizeof(title), "%s", model->description);

	SDL_Window *win = SDL_CreateWindow(title,
		SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
		win_w, win_h, 0);
	SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
	SDL_Texture *tex = SDL_CreateTexture(ren,
		SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
		LCD_WIDTH, m.lcd_height);

	SDL_AudioSpec want = {0}, have;
	want.freq     = SAMPLE_RATE;
	want.format   = AUDIO_F32;
	want.channels = 1;
	want.samples  = AUDIO_BUF;
	want.callback = audio_callback;
	want.userdata = &m;

	SDL_AudioDeviceID audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
	if (audio_dev)
		SDL_PauseAudioDevice(audio_dev, 0);
	else
		fprintf(stderr, "Audio init failed: %s\n", SDL_GetError());

	uint32_t pixels[LCD_WIDTH * MAX_LCD_H];
	int running = 1;
	Uint32 ticks;

	while (running) {
		ticks = SDL_GetTicks();
		SDL_Event ev;
		while (SDL_PollEvent(&ev)) {
			if (ev.type == SDL_QUIT) {
				running = 0;
				break;
			}
			if (ev.type != SDL_KEYDOWN && ev.type != SDL_KEYUP)
				continue;

			SDL_Scancode sc = ev.key.keysym.scancode;
			bool down = ev.type == SDL_KEYDOWN;

			if (sc == SDL_SCANCODE_END) {
				machine_power_button(&m, down);
				continue;
			}
			if (sc == SDL_SCANCODE_F12 && down) {
				running = 0;
				break;
			}

			for (const struct keymap *k = keymap; k->row >= 0; k++) {
				if (k->sc == sc) {
					if (down) machine_key_down(&m, k->row, k->bit);
					else      machine_key_up(&m, k->row, k->bit);
					break;
				}
			}
		}

		machine_step(&m, CPU_CLOCK / 60);

		machine_render_lcd(&m, pixels);
		SDL_UpdateTexture(tex, NULL, pixels, LCD_WIDTH * sizeof(uint32_t));
		SDL_RenderClear(ren);
		SDL_RenderCopy(ren, tex, NULL, NULL);
		SDL_RenderPresent(ren);

		Uint32 elapsed = SDL_GetTicks() - ticks;
		if (elapsed < 16)
			SDL_Delay(16 - elapsed);
	}

	machine_save_nvram(&m, nvram_path);
	if (pccard_path)
		machine_save_pccard(&m, pccard_path);
	free(m.pccard);
	fdc_destroy(&m.fdc);
	uart_destroy(&m.uart);

	if (audio_dev)
		SDL_CloseAudioDevice(audio_dev);
	SDL_DestroyTexture(tex);
	SDL_DestroyRenderer(ren);
	SDL_DestroyWindow(win);
	SDL_Quit();
	free(nvram_path);
	return 0;
}
