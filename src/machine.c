#include "machine.h"
#include <stdio.h>
#include <string.h>

static uint8_t mem_read(void *ctx, uint32_t addr);
static void    mem_write(void *ctx, uint32_t addr, uint8_t val);
static uint8_t io_read(void *ctx, uint16_t port);
static void    io_write(void *ctx, uint16_t port, uint8_t val);
static void    update_bank(machine_t *m, int bank);
static void    update_irqs(machine_t *m);

int machine_init(machine_t *m)
{
	memset(m, 0, sizeof(*m));
	m->bank_bit3_selects_ram = true;

	v20_init(&m->cpu);
	m->cpu.mem_read  = mem_read;
	m->cpu.mem_write = mem_write;
	m->cpu.io_read   = io_read;
	m->cpu.io_write  = io_write;
	m->cpu.ctx       = m;

	m->kb_timer_period = CPU_CLOCK / (XTAL / 20480);
	return 0;
}

void machine_reset(machine_t *m)
{
	v20_reset(&m->cpu);

	m->irq_enabled      = 0;
	m->irq_active        = 0;
	m->lcd_memory_start  = 0;
	m->lcd_on            = true;
	m->keyboard_row      = 0;
	m->keyboard_row_reset = 0xFF;
	m->uart_control      = 0;
	m->buzzer_low        = 0;
	m->buzzer_high       = 0;
	m->buzzer_on         = false;
	m->f9_timer_cycles   = 0;
	m->kb_timer_cycles   = m->kb_timer_period;

	memset(m->bank_select, 0, sizeof(m->bank_select));
	for (int i = 0; i < NUM_BANKS; i++)
		update_bank(m, i);
}

int machine_load_rom(machine_t *m, const char *path)
{
	FILE *f = fopen(path, "rb");
	if (!f) { fprintf(stderr, "Cannot open ROM: %s\n", path); return -1; }

	memset(m->rom, 0xFF, ROM_SIZE);
	size_t n = fread(m->rom, 1, ROM_SIZE, f);
	fclose(f);

	if (n == ROM_SIZE / 2) {
		memmove(m->rom + ROM_SIZE / 2, m->rom, ROM_SIZE / 2);
		memset(m->rom, 0xFF, ROM_SIZE / 2);
	}

	fprintf(stderr, "Loaded %zu bytes from %s\n", n, path);
	return 0;
}

/* ---- banking ---- */

static void update_bank(machine_t *m, int bank)
{
	uint8_t sel = m->bank_select[bank];
	int ram_pages = RAM_SIZE / BANK_SIZE;

	if (!(sel & 0x10)) {
		if (sel & 0x08) {
			int page = m->bank_bit3_selects_ram
				? bank % ram_pages
				: 1 % ram_pages;
			m->bank_rd[bank] = m->ram + page * BANK_SIZE;
			m->bank_wr[bank] = m->ram + page * BANK_SIZE;
		} else {
			int entry = ((sel & 0x0F) ^ 0x0F) % (ROM_SIZE / BANK_SIZE);
			m->bank_rd[bank] = m->rom + entry * BANK_SIZE;
			m->bank_wr[bank] = NULL;
		}
	} else {
		if (!(sel & 0x08)) {
			int page = ((sel & 0x0F) ^ 0x0F) % ram_pages;
			m->bank_rd[bank] = m->ram + page * BANK_SIZE;
			m->bank_wr[bank] = m->ram + page * BANK_SIZE;
		} else {
			m->bank_rd[bank] = NULL;
			m->bank_wr[bank] = NULL;
		}
	}
}

/* ---- memory ---- */

static uint8_t mem_read(void *ctx, uint32_t addr)
{
	machine_t *m = ctx;
	addr &= 0xFFFFF;
	int bank = addr >> 17;
	int off  = addr & 0x1FFFF;
	return m->bank_rd[bank] ? m->bank_rd[bank][off] : 0xFF;
}

static void mem_write(void *ctx, uint32_t addr, uint8_t val)
{
	machine_t *m = ctx;
	addr &= 0xFFFFF;
	int bank = addr >> 17;
	int off  = addr & 0x1FFFF;
	if (m->bank_wr[bank])
		m->bank_wr[bank][off] = val;
}

/* ---- IRQ ---- */

static void update_irqs(machine_t *m)
{
	uint8_t en = m->irq_enabled;
	uint8_t rev = 0;
	for (int i = 0; i < 8; i++)
		if (en & (1 << i)) rev |= (0x80 >> i);

	uint8_t pending = m->irq_active & ~rev;

	if (pending) {
		uint8_t vec = 0xFF;
		uint8_t t = pending;
		while (vec >= 0xF8 && !(t & 1)) { t >>= 1; vec--; }
		v20_irq(&m->cpu, true, vec);
	} else {
		v20_irq(&m->cpu, false, 0);
	}
}

/* ---- keyboard timer ---- */

static void kb_timer_tick(machine_t *m)
{
	if (!(m->keyboard_row_reset & 0x01)) {
		m->irq_active |= 0x20;
		update_irqs(m);
		return;
	}

	if (m->keyboard_row > 9) {
		m->keyboard_row = 0;
		m->irq_active |= 0x20;
	} else {
		m->keyboard_row++;
		m->irq_active |= 0x10;
	}
	update_irqs(m);
}

/* ---- I/O ---- */

static uint8_t io_read(void *ctx, uint16_t port)
{
	machine_t *m = ctx;

	switch (port) {
	case 0x0060: return m->irq_enabled;

	case 0x00A0: return 0x80;

	case 0x00B0: {
		uint8_t r = m->keyboard_row;
		return (r > 0 && r <= 10) ? m->kb_rows[r - 1] : 0;
	}

	case 0x00C0: return 0x00;
	case 0x00C1: return 0x05;

	case 0x00D0: case 0x00D1: case 0x00D2: case 0x00D3:
	case 0x00D4: case 0x00D5: case 0x00D6: case 0x00D7:
	case 0x00D8: case 0x00D9: case 0x00DA: case 0x00DB:
	case 0x00DC: case 0x00DD: case 0x00DE: case 0x00DF:
		return 0;
	}

	return 0xFF;
}

static void io_write(void *ctx, uint16_t port, uint8_t val)
{
	machine_t *m = ctx;

	switch (port) {
	case 0x0000:
		m->lcd_on = true;
		m->lcd_memory_start = val;
		break;

	case 0x0010: case 0x0011: case 0x0012: case 0x0013:
	case 0x0014: case 0x0015: case 0x0016: case 0x0017:
		m->bank_select[port - 0x10] = val;
		update_bank(m, port - 0x10);
		break;

	case 0x0030: m->uart_control = val; break;
	case 0x0040: break;

	case 0x0050: m->buzzer_low  = val; break;
	case 0x0051: m->buzzer_high = val; break;
	case 0x0052: m->buzzer_on = (val == 0x7F); break;

	case 0x0053:
		m->f9_timer_cycles = val
			? (int)((uint64_t)val * CPU_CLOCK / (XTAL / 20480))
			: 0;
		break;

	case 0x0060:
		m->irq_enabled = val;
		update_irqs(m);
		break;

	case 0x0061: {
		bool was = m->keyboard_row_reset & 0x01;
		bool now = val & 0x01;
		m->keyboard_row_reset = val;
		if (!now || !was) {
			m->keyboard_row = 0;
			m->irq_active &= ~0x30;
			update_irqs(m);
		}
		break;
	}

	case 0x0070:
		if (val & 0x01) {
			m->lcd_on = false;
			m->cpu.halted = true;
		}
		break;

	case 0x0090:
		m->irq_active &= ~val;
		update_irqs(m);
		break;

	case 0x00C0: case 0x00C1: break;

	case 0x00D0: case 0x00D1: case 0x00D2: case 0x00D3:
	case 0x00D4: case 0x00D5: case 0x00D6: case 0x00D7:
	case 0x00D8: case 0x00D9: case 0x00DA: case 0x00DB:
	case 0x00DC: case 0x00DD: case 0x00DE: case 0x00DF:
		break;
	}
}

/* ---- stepping ---- */

void machine_step(machine_t *m, int cycles)
{
	int rem = cycles;
	while (rem > 0) {
		int chunk = rem;
		if (chunk > m->kb_timer_cycles)
			chunk = m->kb_timer_cycles;
		if (m->f9_timer_cycles > 0 && chunk > m->f9_timer_cycles)
			chunk = m->f9_timer_cycles;
		if (chunk <= 0) chunk = 1;

		int ran = v20_exec(&m->cpu, chunk);
		rem -= ran;

		m->kb_timer_cycles -= ran;
		if (m->kb_timer_cycles <= 0) {
			kb_timer_tick(m);
			m->kb_timer_cycles += m->kb_timer_period;
		}

		if (m->f9_timer_cycles > 0) {
			m->f9_timer_cycles -= ran;
			if (m->f9_timer_cycles <= 0) {
				m->f9_timer_cycles = 0;
				m->irq_active |= 0x40;
				update_irqs(m);
			}
		}
	}
}

/* ---- LCD ---- */

void machine_render_lcd(machine_t *m, uint32_t *px)
{
	const uint32_t bg = 0xFF8A9294, fg = 0xFF5C5358;

	if (!m->lcd_on) {
		for (int i = 0; i < LCD_WIDTH * LCD_HEIGHT; i++)
			px[i] = bg;
		return;
	}

	uint32_t base = (uint32_t)m->lcd_memory_start << 9;

	for (int y = 0; y < LCD_HEIGHT; y++) {
		for (int x = 0; x < 60; x++) {
			uint32_t addr = base + y * 64 + x;
			uint8_t d = (addr < RAM_SIZE) ? m->ram[addr] : 0;
			for (int p = 0; p < 8; p++) {
				int idx = y * LCD_WIDTH + x * 8 + p;
				if (idx < LCD_WIDTH * LCD_HEIGHT)
					px[idx] = (d & 0x80) ? fg : bg;
				d <<= 1;
			}
		}
	}
}

/* ---- keyboard ---- */

void machine_key_down(machine_t *m, int row, int bit)
{
	if (row >= 0 && row < 10 && bit >= 0 && bit < 8)
		m->kb_rows[row] |= (uint8_t)(1 << bit);
}

void machine_key_up(machine_t *m, int row, int bit)
{
	if (row >= 0 && row < 10 && bit >= 0 && bit < 8)
		m->kb_rows[row] &= (uint8_t)~(1 << bit);
}

/* ---- power ---- */

void machine_power_button(machine_t *m, bool pressed)
{
	if (!pressed) {
		v20_nmi(&m->cpu, false);
		return;
	}

	for (int i = 0; i < 10; i++)
		m->ram[0x6D06 + i] = m->kb_rows[i];

	if (!m->lcd_on) {
		m->lcd_on = true;
		m->cpu.halted = false;
		machine_reset(m);
		return;
	}

	m->irq_active |= 0x01;
	update_irqs(m);
}
