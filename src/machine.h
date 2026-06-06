// license:BSD-3-Clause
// copyright-holders:Stephen Hurd, MAMEDev (Wilbert Pol, Sandro Ronco)
#ifndef MACHINE_H
#define MACHINE_H

#include <array>
#include <cstddef>
#include <cstdio>
#include <memory>
#include "v20.h"
#include "uart.h"
#include "fdc.h"

class PrinterSim;
class PdfWriter;

constexpr int XTAL      = 19660000;
constexpr int CPU_CLOCK = XTAL / 2;

constexpr int ROM_SIZE  = 0x100000;
constexpr int MAX_RAM   = 256 * 1024;
constexpr int NUM_BANKS = 8;
constexpr int BANK_SIZE = 0x20000;

constexpr int LCD_WIDTH = 480;
constexpr int MAX_LCD_H = 128;

struct model_t {
	const char *name;
	const char *description;
	uint32_t ram_size;
	int      lcd_height;
	bool     has_pccard;
	bool     has_floppy;
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
	std::array<std::array<uint8_t, 13>, 2> reg{};
	std::array<uint8_t, 13> ram{};
	uint8_t  mode = 0;
	uint8_t  reset = 0;
};

enum class CentBackend { File, Lpt, Ppi, Pdf };

struct machine_t {
	machine_t();
	~machine_t();
	machine_t(machine_t &&) noexcept;
	machine_t &operator=(machine_t &&) noexcept;
	machine_t(const machine_t &) = delete;
	machine_t &operator=(const machine_t &) = delete;

	const model_t *model = nullptr;
	v20_t    cpu;

	uint8_t  *rom = nullptr;
	int      rom_fd = -1;
	uint8_t  *ram = nullptr;
	uint32_t ram_size = 0;
	int      nvram_fd = -1;

	std::array<uint8_t, NUM_BANKS>  bank_select{};
	std::array<uint8_t*, NUM_BANKS> bank_rd{};
	std::array<uint8_t*, NUM_BANKS> bank_wr{};

	uint8_t  lcd_memory_start = 0;
	bool     lcd_on = false;
	int      lcd_height = 0;

	uint8_t  irq_enabled = 0;
	uint8_t  irq_active = 0;

	uint8_t  keyboard_row = 0;
	uint8_t  keyboard_row_reset = 0;
	std::array<uint8_t, 10> kb_rows{};
	uint8_t  kb_hold[10][8]{};
	uint8_t  kb_last_scan_row = 0xFF;

	uint8_t  buzzer_low = 0;
	uint8_t  buzzer_high = 0;
	bool     buzzer_on = false;
	float    beeper_phase = 0.0f;

	uint8_t  port30 = 0;
	uint8_t  cent_data = 0;
	CentBackend cent_backend = CentBackend::File;
	int      cent_fd = -1;
	FilePtr  printer;

	std::unique_ptr<PdfWriter>   pdf_writer;
	std::unique_ptr<PrinterSim>  pdf_printer;
	int      pdf_model = 3;
	int      pdf_idle_cycles = 0;
	bool     pdf_active = false;
	bool     pdf_via_serial = false;

	uint8_t  *pccard = nullptr;
	uint32_t pccard_size = 0;
	int      pccard_fd = -1;

	uart_t   uart;
	fdc_t    fdc;

	rtc_t    rtc;

	int      kb_timer_cycles = 0;
	int      kb_timer_period = 0;
	int      f9_timer_cycles = 0;
	int      rtc_timer_cycles = 0;

	bool     bank_bit3_selects_ram = false;
	bool     main_battery_low = false;
	bool     coin_battery_low = false;
};

[[nodiscard]] int  machine_init(machine_t *m, const model_t *model,
                  UartBackend uart_backend, int tcp_port,
                  const char *serial_path,
                  CentBackend cent_backend, const char *cent_path);
void machine_reset(machine_t *m);
[[nodiscard]] int  machine_load_rom(machine_t *m, const char *path,
                      const rom_entry_t *entry);
void machine_step(machine_t *m, int cycles);
void machine_render_lcd(machine_t *m, uint32_t *pixels);
void machine_key_down(machine_t *m, int row, int bit);
void machine_key_up(machine_t *m, int row, int bit);
void machine_power_button(machine_t *m, bool pressed);
void machine_close_rom(machine_t *m);
[[nodiscard]] int  machine_open_nvram(machine_t *m, const char *path);
void machine_close_nvram(machine_t *m);
[[nodiscard]] int  machine_open_pccard(machine_t *m, const char *path);
void machine_close_pccard(machine_t *m);

void machine_pdf_start(machine_t *m, const char *path, int model);
void machine_pdf_finish(machine_t *m);
void machine_pdf_start_serial(machine_t *m, const char *path, int model);
bool machine_pdf_check_idle(machine_t *m, int timeout_seconds = 5);

#endif
