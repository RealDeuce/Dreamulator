#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include "machine.h"

#define SCALE     2
#define WINDOW_W  (LCD_WIDTH * SCALE)
#define WINDOW_H  (LCD_HEIGHT * SCALE)

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

int main(int argc, char *argv[])
{
	if (argc < 2) {
		fprintf(stderr, "Usage: %s <rom>\n", argv[0]);
		return 1;
	}

	machine_t m;
	if (machine_init(&m) != 0) return 1;
	if (machine_load_rom(&m, argv[1]) != 0) return 1;
	machine_reset(&m);

	if (SDL_Init(SDL_INIT_VIDEO) < 0) {
		fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
		return 1;
	}

	SDL_Window *win = SDL_CreateWindow("DreamWriter T400",
		SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
		WINDOW_W, WINDOW_H, 0);
	SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
	SDL_Texture *tex = SDL_CreateTexture(ren,
		SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
		LCD_WIDTH, LCD_HEIGHT);

	uint32_t pixels[LCD_WIDTH * LCD_HEIGHT];
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

	SDL_DestroyTexture(tex);
	SDL_DestroyRenderer(ren);
	SDL_DestroyWindow(win);
	SDL_Quit();
	return 0;
}
