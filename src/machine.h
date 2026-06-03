#ifndef MACHINE_H
#define MACHINE_H

#include "v20.h"

#define XTAL        19660000
#define CPU_CLOCK   (XTAL / 2)

#define ROM_SIZE    0x100000
#define RAM_SIZE    (256 * 1024)
#define NUM_BANKS   8
#define BANK_SIZE   0x20000

#define LCD_WIDTH   480
#define LCD_HEIGHT  64

typedef struct {
	uint8_t  reg[2][13];
	uint8_t  ram[13];
	uint8_t  mode;
	uint8_t  reset;
} rtc_t;

typedef struct machine machine_t;

struct machine {
	v20_t    cpu;

	uint8_t  rom[ROM_SIZE];
	uint8_t  ram[RAM_SIZE];

	uint8_t  bank_select[NUM_BANKS];
	uint8_t  *bank_rd[NUM_BANKS];
	uint8_t  *bank_wr[NUM_BANKS];

	uint8_t  lcd_memory_start;
	bool     lcd_on;

	uint8_t  irq_enabled;
	uint8_t  irq_active;

	uint8_t  keyboard_row;
	uint8_t  keyboard_row_reset;
	uint8_t  kb_rows[10];

	uint8_t  buzzer_low;
	uint8_t  buzzer_high;
	bool     buzzer_on;
	float    beeper_phase;

	uint8_t  uart_control;

	rtc_t    rtc;

	int      kb_timer_cycles;
	int      kb_timer_period;
	int      f9_timer_cycles;
	int      rtc_timer_cycles;

	bool     bank_bit3_selects_ram;
};

int  machine_init(machine_t *m);
void machine_reset(machine_t *m);
int  machine_load_rom(machine_t *m, const char *path);
void machine_step(machine_t *m, int cycles);
void machine_render_lcd(machine_t *m, uint32_t *pixels);
void machine_key_down(machine_t *m, int row, int bit);
void machine_key_up(machine_t *m, int row, int bit);
void machine_power_button(machine_t *m, bool pressed);
int  machine_load_nvram(machine_t *m, const char *path);
int  machine_save_nvram(machine_t *m, const char *path);

#endif
