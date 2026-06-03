// license:BSD-3-Clause
// copyright-holders:Stephen Hurd, MAMEDev (Wilbert Pol, Sandro Ronco)
#ifndef MACHINE_H
#define MACHINE_H

#include <cstddef>
#include <cstdio>
#include "v20.h"
#include "uart.h"
#include "fdc.h"

#define XTAL        19660000
#define CPU_CLOCK   (XTAL / 2)

#define ROM_SIZE    0x100000
#define MAX_RAM     (256 * 1024)
#define NUM_BANKS   8
#define BANK_SIZE   0x20000

#define LCD_WIDTH   480
#define MAX_LCD_H   128

struct model_t {
	const char *name;
	const char *description;
	uint32_t ram_size;
	int      lcd_height;
	bool     has_pccard;
	bool     bank_bit3_selects_ram;
	bool     power_nmi;
};

extern const model_t models[];
extern const int     model_count;
const model_t *model_find(const char *name);

struct rom_entry_t {
	const char *model;
	const char *bios;
	const char *filename;
	uint32_t    crc32;
	uint32_t    size;
	uint32_t    load_offset;
};

extern const rom_entry_t rom_db[];
extern const int          rom_db_count;
const rom_entry_t *rom_find_by_crc(uint32_t crc);
const rom_entry_t *rom_find_for_model(const char *model, const char *bios);
uint32_t crc32_buf(const uint8_t *data, size_t len);

struct rtc_t {
	uint8_t  reg[2][13];
	uint8_t  ram[13];
	uint8_t  mode;
	uint8_t  reset;
};

struct machine_t {
	const model_t *model;
	v20_t    cpu;

	uint8_t  rom[ROM_SIZE];
	uint8_t  *ram;
	uint32_t ram_size;
	int      nvram_fd;

	uint8_t  bank_select[NUM_BANKS];
	uint8_t  *bank_rd[NUM_BANKS];
	uint8_t  *bank_wr[NUM_BANKS];

	uint8_t  lcd_memory_start;
	bool     lcd_on;
	int      lcd_height;

	uint8_t  irq_enabled;
	uint8_t  irq_active;

	uint8_t  keyboard_row;
	uint8_t  keyboard_row_reset;
	uint8_t  kb_rows[10];

	uint8_t  buzzer_low;
	uint8_t  buzzer_high;
	bool     buzzer_on;
	float    beeper_phase;

	uint8_t  port30;
	uint8_t  cent_data;
	int      cent_backend;
	int      cent_fd;
	FILE     *printer;

	uint8_t  *pccard;
	uint32_t pccard_size;
	int      pccard_fd;

	uart_t   uart;
	fdc_t    fdc;

	rtc_t    rtc;

	int      kb_timer_cycles;
	int      kb_timer_period;
	int      f9_timer_cycles;
	int      rtc_timer_cycles;

	bool     bank_bit3_selects_ram;
	bool     main_battery_low;
	bool     coin_battery_low;
};

enum { CENT_FILE = 0, CENT_LPT = 1, CENT_PPI = 2 };

int  machine_init(machine_t *m, const model_t *model,
                  int uart_backend, int tcp_port,
                  const char *serial_path,
                  int cent_backend, const char *cent_path);
void machine_reset(machine_t *m);
int  machine_load_rom(machine_t *m, const char *path,
                      const rom_entry_t *entry);
void machine_step(machine_t *m, int cycles);
void machine_render_lcd(machine_t *m, uint32_t *pixels);
void machine_key_down(machine_t *m, int row, int bit);
void machine_key_up(machine_t *m, int row, int bit);
void machine_power_button(machine_t *m, bool pressed);
int  machine_open_nvram(machine_t *m, const char *path);
void machine_close_nvram(machine_t *m);
int  machine_open_pccard(machine_t *m, const char *path);
void machine_close_pccard(machine_t *m);

#endif
