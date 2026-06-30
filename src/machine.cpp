// license:BSD-3-Clause
// copyright-holders:Stephen Hurd, MAMEDev (Wilbert Pol, Sandro Ronco)
#include "machine.h"
#include "dbg_periph.h"
#include "paths.h"
#include "print/printer.h"
#include <algorithm>

machine_t::machine_t() = default;
machine_t::~machine_t() = default;
machine_t::machine_t(machine_t &&) noexcept = default;
machine_t &machine_t::operator=(machine_t &&) noexcept = default;
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#ifdef __FreeBSD__
#include <dev/ppbus/ppi.h>
#include <dev/ppbus/ppbconf.h>
#endif

/* ---- model table ---- */

const model_t models[] = {
	//                                    RAM       LCD  card   flop   nmi
	{ "wales210", "Walther ES-210",       128*1024, 64,  true,  false, false },
	{ "dw325",    "DreamWriter 325",      128*1024, 64,  true,  false, false },
	{ "dator3k",  "Dator 3000",           128*1024, 64,  true,  false, false },
	{ "es210_es", "Nakajima ES-210 (ES)", 128*1024, 64,  true,  false, false },
	{ "dwT100",   "DreamWriter T100",     128*1024, 64,  false, false, false },
	{ "dwT400",   "DreamWriter T400",     256*1024, 64,  true,  false, true  },
	{ "dw450",    "DreamWriter 450",      256*1024, 64,  true,  false, true  },
	{ "dwT200",   "DreamWriter T200",     256*1024, 128, true,  true,  true  },
};
const int model_count = sizeof(models) / sizeof(models[0]);

const model_t *model_find(const char *name)
{
	for (int i = 0; i < model_count; i++)
		if (strcmp(models[i].name, name) == 0)
			return &models[i];
	return nullptr;
}

/* ---- ROM database ---- */

const rom_entry_t rom_db[] = {
	{ "wales210", nullptr,    "wales210.ic303",        0xa8e8d991, 0x80000,  0 },
	{ "dw325",    "v1.02", "dr3_1_02uk.ic303",     0x027db9fe, 0x80000,  0 },
	{ "dw325",    "v1.03", "dr3_1_03.ic303",       0x21fd074e, 0x80000,  0 },
	{ "dw325",    "v2.0",  "nts_325_basic.ic303",  0xfeb40854, 0x80000,  0 },
	{ "dator3k",  nullptr,    "dator3000.ic303",       0xb67fffeb, 0x80000,  0 },
	{ "es210_es", nullptr,    "nakajima_es.ic303",     0x214d73ce, 0x80000,  0 },
	{ "dwT100",   "v2.3",  "t100_2.3.ic303",       0x8a16f12f, 0x80000,  0 },
	{ "dwT100",   "v1.0",  "t100_1.0.ic303",       0xb53f810a, 0x80000,  0 },
	{ "dwT200",   nullptr,    "drwrt200.bin",          0x3c39483c, 0x100000, 0 },
	{ "dwT400",   "v3.1",  "t4_ir_3.1_T4-067979",  0x5c062e52, 0x100000, 0 },
	{ "dwT400",   "v3.1",  "t4_ir_3.1_e588.ic303", 0x1724ceb2, 0x100000, 0 },
	{ "dwT400",   "v2.1",  "t4_ir_2.1.ic303",      0xf0f45fd2, 0x80000,  0x80000 },
	{ "dw450",    nullptr,    "t4_ir_35ba308.ic303",   0x3b5a580d, 0x100000, 0 },
};
const int rom_db_count = sizeof(rom_db) / sizeof(rom_db[0]);

const rom_entry_t *rom_find_by_crc(uint32_t crc)
{
	for (int i = 0; i < rom_db_count; i++)
		if (rom_db[i].crc32 == crc)
			return &rom_db[i];
	return nullptr;
}

const rom_entry_t *rom_find_for_model(const char *model, const char *bios)
{
	const rom_entry_t *fallback = nullptr;
	for (int i = 0; i < rom_db_count; i++) {
		if (strcmp(rom_db[i].model, model) != 0) continue;
		if (bios && rom_db[i].bios && strcmp(rom_db[i].bios, bios) == 0)
			return &rom_db[i];
		if (!fallback) fallback = &rom_db[i];
	}
	return fallback;
}

uint32_t crc32_buf(const uint8_t *data, size_t len)
{
	uint32_t crc = 0xFFFFFFFF;
	for (size_t i = 0; i < len; i++) {
		crc ^= data[i];
		for (int j = 0; j < 8; j++)
			crc = (crc >> 1) ^ (crc & 1 ? 0xEDB88320 : 0);
	}
	return ~crc;
}

static uint8_t mem_read(void *ctx, uint32_t addr);
static void    mem_write(void *ctx, uint32_t addr, uint8_t val);
static uint8_t io_read(void *ctx, uint16_t port);
static void    io_write(void *ctx, uint16_t port, uint8_t val);
static void    update_bank(machine_t *m, int bank);
static void    update_irqs(machine_t *m);
static void    rtc_init(rtc_t *r);

static void uart_txrdy_cb(void *ctx, bool state);
static void uart_rxrdy_cb(void *ctx, bool state);

int machine_init(machine_t *m, const model_t *model,
                 UartBackend uart_backend, int tcp_port,
                 const char *serial_path,
                 CentBackend cent_backend, const char *cent_path)
{
	*m = machine_t{};
	m->model = model;
	m->ram_size = model->ram_size;
	m->lcd_height = model->lcd_height;
	m->cent_backend = cent_backend;

	if (cent_backend != CentBackend::File && cent_path) {
		m->cent_fd = open(cent_path, O_RDWR);
		if (m->cent_fd < 0) {
			fprintf(stderr, "Centronics: cannot open %s\n", cent_path);
			m->cent_backend = CentBackend::File;
		} else {
			fprintf(stderr, "Centronics: %s\n", cent_path);
		}
	}

	v20_init(&m->cpu);
	m->cpu.mem_read  = mem_read;
	m->cpu.mem_write = mem_write;
	m->cpu.io_read   = io_read;
	m->cpu.io_write  = io_write;
	m->cpu.ctx       = m;

	m->kb_timer_period = CPU_CLOCK / (XTAL / 20480);

	if (uart_init(&m->uart, uart_backend, tcp_port, serial_path) < 0)
		return -1;
	fdc_init(&m->fdc);
	m->uart.txrdy_cb = uart_txrdy_cb;
	m->uart.rxrdy_cb = uart_rxrdy_cb;
	m->uart.cb_ctx   = m;

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
	machine_keys_all_up(m);
	m->port30            = 0;
	m->buzzer_low        = 0;
	m->buzzer_high       = 0;
	m->buzzer_on         = false;
	m->f9_timer_cycles   = 0;
	m->kb_timer_cycles   = m->kb_timer_period;
	m->rtc_timer_cycles  = CPU_CLOCK;

	rtc_init(&m->rtc);
	uart_reset(&m->uart);

	m->bank_select.fill(0);
	for (int i = 0; i < NUM_BANKS; i++)
		update_bank(m, i);
}

int machine_load_rom(machine_t *m, const char *path, const rom_entry_t *entry)
{
	int fd = open(path, O_RDONLY);
	if (fd < 0) { fprintf(stderr, "Cannot open ROM: %s\n", path); return -1; }

	struct stat st;
	fstat(fd, &st);
	size_t filesz = (size_t)st.st_size;

	uint8_t *filebuf = (uint8_t *)mmap(nullptr, filesz, PROT_READ,
	                                   MAP_PRIVATE, fd, 0);
	if (filebuf == MAP_FAILED) {
		fprintf(stderr, "ROM: mmap failed for %s\n", path);
		close(fd);
		return -1;
	}

	uint32_t crc = crc32_buf(filebuf, filesz);
	const rom_entry_t *detected = rom_find_by_crc(crc);

	m->rom = (uint8_t *)mmap(nullptr, ROM_SIZE, PROT_READ | PROT_WRITE,
	                         MAP_PRIVATE | MAP_ANON, -1, 0);
	if (m->rom == MAP_FAILED) {
		m->rom = nullptr;
		munmap(filebuf, filesz);
		close(fd);
		return -1;
	}
	m->rom_fd = fd;
	memset(m->rom, 0xFF, ROM_SIZE);

	uint32_t offset = 0;
	if (entry) offset = entry->load_offset;
	else if (detected) { offset = detected->load_offset; entry = detected; }

	if (offset > ROM_SIZE || filesz > ROM_SIZE - offset) {
		fprintf(stderr, "ROM: %s is too large for load offset 0x%05x (%zu bytes)\n",
		        path, offset, filesz);
		munmap(filebuf, filesz);
		munmap(m->rom, ROM_SIZE);
		m->rom = nullptr;
		close(fd);
		m->rom_fd = -1;
		return -1;
	}

	memcpy(m->rom + offset, filebuf, filesz);
	m->rom_base_page = offset / BANK_SIZE;
	m->rom_pages = std::max<uint32_t>(1, (uint32_t)((filesz + BANK_SIZE - 1) / BANK_SIZE));

	munmap(filebuf, filesz);
	mprotect(m->rom, ROM_SIZE, PROT_READ);

	if (detected)
		fprintf(stderr, "ROM: %s %s%s%s (CRC %08x, mmap)\n",
			detected->model,
			detected->bios ? "(" : "",
			detected->bios ? detected->bios : "",
			detected->bios ? ")" : "",
			crc);
	else
		fprintf(stderr, "ROM: unknown (CRC %08x), %zu bytes from %s\n",
			crc, filesz, path);

	return 0;
}

/* ---- banking ---- */

static void update_bank(machine_t *m, int bank)
{
	size_t b = (size_t)bank;
	uint8_t sel = m->bank_select[b];
	int inv_page = (sel ^ 0x07) & 0x07;
	int source = (sel >> 3) & 0x03;

	m->bank_rd[b] = nullptr;
	m->bank_wr[b] = nullptr;

	switch (source) {
	case 0: {
		uint32_t entry = m->rom_base_page + (uint32_t)inv_page % m->rom_pages;
		entry %= ROM_SIZE / BANK_SIZE;
		m->bank_rd[b] = m->rom + entry * BANK_SIZE;
		break;
	}
	case 1:
	case 2: {
		uint32_t installed_pages = std::max<uint32_t>(1, m->ram_size / BANK_SIZE);
		uint32_t page = (uint32_t)(2 - source) % installed_pages;
		m->bank_rd[b] = m->ram + page * BANK_SIZE;
		m->bank_wr[b] = m->ram + page * BANK_SIZE;
		break;
	}
	case 3: {
		uint32_t pc_off = (uint32_t)inv_page * BANK_SIZE;
		if (m->pccard && pc_off + BANK_SIZE <= m->pccard_size) {
			m->bank_rd[b] = m->pccard + pc_off;
			m->bank_wr[b] = m->pccard + pc_off;
		}
		break;
	}
	}
}

/* ---- memory ---- */

static uint8_t mem_read(void *ctx, uint32_t addr)
{
	machine_t *m = (machine_t *)ctx;
	addr &= 0xFFFFF;
	size_t bank = addr >> 17;
	int off  = addr & 0x1FFFF;
	return m->bank_rd[bank] ? m->bank_rd[bank][off] : 0xFF;
}

static void mem_write(void *ctx, uint32_t addr, uint8_t val)
{
	machine_t *m = (machine_t *)ctx;
	addr &= 0xFFFFF;
	size_t bank = addr >> 17;
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

/* ---- RTC (RP5C01) ---- */

static constexpr uint8_t rtc_wmask[2][13] = {
	{ 0xF,0x7,0xF,0x7,0xF,0x3,0x7,0xF,0x3,0xF,0x1,0xF,0xF },
	{ 0x0,0x0,0xF,0x7,0xF,0x3,0x0,0x0,0x0,0x0,0x1,0x3,0x0 },
};

static void rtc_init(rtc_t *r)
{
	*r = rtc_t{};
	r->reg[1][10] = 1;

	time_t now = time(nullptr);
	struct tm *t = localtime(&now);

	r->reg[0][0]  = (uint8_t)(t->tm_sec % 10);
	r->reg[0][1]  = (uint8_t)(t->tm_sec / 10);
	r->reg[0][2]  = (uint8_t)(t->tm_min % 10);
	r->reg[0][3]  = (uint8_t)(t->tm_min / 10);
	r->reg[0][4]  = (uint8_t)(t->tm_hour % 10);
	r->reg[0][5]  = (uint8_t)(t->tm_hour / 10);
	r->reg[0][6]  = (uint8_t)(t->tm_wday);
	r->reg[0][7]  = (uint8_t)(t->tm_mday % 10);
	r->reg[0][8]  = (uint8_t)(t->tm_mday / 10);
	r->reg[0][9]  = (uint8_t)((t->tm_mon + 1) % 10);
	r->reg[0][10] = (uint8_t)((t->tm_mon + 1) / 10);
	r->reg[0][11] = (uint8_t)((t->tm_year % 100) % 10);
	r->reg[0][12] = (uint8_t)((t->tm_year % 100) / 10);
	r->reg[1][11] = (uint8_t)(t->tm_year % 4);
}

static uint8_t rtc_read(rtc_t *r, uint8_t offset)
{
	offset &= 0x0F;

	if (offset == 0x0D) return r->mode & 0x0F;
	if (offset >= 0x0E) return 0;

	switch (r->mode & 3) {
	case 0: return r->reg[0][offset] & 0x0F;
	case 1: return r->reg[1][offset] & 0x0F;
	case 2: return r->ram[offset] & 0x0F;
	case 3: return (r->ram[offset] >> 4) & 0x0F;
	}
	return 0;
}

static void rtc_write(rtc_t *r, uint8_t offset, uint8_t data)
{
	offset &= 0x0F;
	data   &= 0x0F;

	if (offset == 0x0D) { r->mode = data; return; }
	if (offset == 0x0E) return;
	if (offset == 0x0F) {
		if (data & 1)
			std::fill_n(r->reg[1].begin(), 5, 0);
		r->reset = data;
		return;
	}

	switch (r->mode & 3) {
	case 0: r->reg[0][offset] = data & rtc_wmask[0][offset]; break;
	case 1: r->reg[1][offset] = data & rtc_wmask[1][offset]; break;
	case 2: r->ram[offset] = (r->ram[offset] & 0xF0) | data; break;
	case 3: r->ram[offset] = (uint8_t)((r->ram[offset] & 0x0F) | (data << 4)); break;
	}
}

static void rtc_tick(rtc_t *r)
{
	if (!(r->mode & 0x08)) return;

	static constexpr uint8_t mdays[] = {31,28,31,30,31,30,31,31,30,31,30,31};

	if (++r->reg[0][0] <= 9) return;
	r->reg[0][0] = 0;
	if (++r->reg[0][1] <= 5) return;
	r->reg[0][1] = 0;

	if (++r->reg[0][2] <= 9) return;
	r->reg[0][2] = 0;
	if (++r->reg[0][3] <= 5) return;
	r->reg[0][3] = 0;

	r->reg[0][4]++;
	if (r->reg[0][4] > 9) { r->reg[0][4] = 0; r->reg[0][5]++; }
	if (r->reg[0][5] * 10 + r->reg[0][4] < 24) return;
	r->reg[0][4] = 0;
	r->reg[0][5] = 0;

	r->reg[0][6] = (r->reg[0][6] + 1) % 7;

	int mon = r->reg[0][10] * 10 + r->reg[0][9];
	int mx = 31;
	if (mon >= 1 && mon <= 12) {
		mx = mdays[mon - 1];
		if (mon == 2 && r->reg[1][11] == 0) mx = 29;
	}

	r->reg[0][7]++;
	if (r->reg[0][7] > 9) { r->reg[0][7] = 0; r->reg[0][8]++; }
	if (r->reg[0][8] * 10 + r->reg[0][7] <= mx) return;
	r->reg[0][7] = 1;
	r->reg[0][8] = 0;

	r->reg[0][9]++;
	if (r->reg[0][9] > 9) { r->reg[0][9] = 0; r->reg[0][10]++; }
	if (r->reg[0][10] * 10 + r->reg[0][9] <= 12) return;
	r->reg[0][9] = 1;
	r->reg[0][10] = 0;

	r->reg[0][11]++;
	if (r->reg[0][11] > 9) {
		r->reg[0][11] = 0;
		r->reg[0][12] = (r->reg[0][12] + 1) % 10;
	}
	r->reg[1][11] = (r->reg[1][11] + 1) & 3;
}

/* ---- I/O ---- */

static uint8_t io_read_inner(machine_t *m, uint16_t port)
{
	switch (port) {
	case 0x0060: return m->irq_enabled;

	case 0x00A0: {
		uint8_t st = 0;
		if (!m->pccard) st |= 0x80;
		if (!m->pccard_battery_low) st |= 0x10;
		if (m->main_battery_low) st |= 0x08;
		if (m->coin_battery_low) st |= 0x04;
#ifdef __FreeBSD__
		if (m->cent_backend == CentBackend::Ppi && m->cent_fd >= 0) {
			uint8_t pst = 0;
			if (ioctl(m->cent_fd, PPIGSTATUS, &pst) == 0)
				st |= (pst & nBUSY) ? 0 : 0x02;
		}
#endif
		return st;
	}

	case 0x00B0: {
		uint8_t r = m->keyboard_row;
		if (r < 1 || r > 10) return 0;
		size_t idx = (size_t)r - 1;
		uint8_t held = 0;
		for (int b = 0; b < 8; b++)
			if (m->kb_hold[idx][b] > 0) held |= (uint8_t)(1 << b);
		uint8_t result = m->kb_rows[idx] | held;
		if (m->kb_last_scan_row != r) {
			m->kb_last_scan_row = r;
			for (int b = 0; b < 8; b++)
				if (m->kb_hold[idx][b] > 0) m->kb_hold[idx][b]--;
		}
		return result;
	}

	case 0x00C0: return uart_data_read(&m->uart);
	case 0x00C1: return uart_status_read(&m->uart);

	case 0x00D0: case 0x00D1: case 0x00D2: case 0x00D3:
	case 0x00D4: case 0x00D5: case 0x00D6: case 0x00D7:
	case 0x00D8: case 0x00D9: case 0x00DA: case 0x00DB:
	case 0x00DC: case 0x00DD: case 0x00DE: case 0x00DF:
		return rtc_read(&m->rtc, (uint8_t)(port - 0xD0));

	case 0x00E0: case 0x00E1: case 0x00E2: case 0x00E3:
	case 0x00E4: case 0x00E5: case 0x00E7:
		return fdc_read(&m->fdc, port - 0xE0);
	}

	return 0xFF;
}

static uint8_t io_read(void *ctx, uint16_t port)
{
	uint8_t val = io_read_inner((machine_t *)ctx, port);
	periph_log_io_read(port, val);
	return val;
}

static void io_write(void *ctx, uint16_t port, uint8_t val)
{
	periph_log_io_write(port, val);
	machine_t *m = (machine_t *)ctx;

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

	case 0x0030: {
		uint8_t old = m->port30;
		m->port30 = val;
		uart_set_baud(&m->uart, val & 7);
		if ((old & 0x08) && !(val & 0x08))
			uart_reset(&m->uart);
		if ((old & 0x20) && !(val & 0x20)) {
			periph_log_parallel(m->cent_data);
			switch (m->cent_backend) {
#ifdef __FreeBSD__
			case CentBackend::Ppi:
				if (m->cent_fd >= 0) {
					uint8_t d = m->cent_data;
					ioctl(m->cent_fd, PPISDATA, &d);
					uint8_t ctl = STROBE | SELECTIN | nINIT;
					ioctl(m->cent_fd, PPISCTRL, &ctl);
					ctl &= ~STROBE;
					ioctl(m->cent_fd, PPISCTRL, &ctl);
				}
				break;
#endif
			case CentBackend::Lpt:
				if (m->cent_fd >= 0)
					(void)write(m->cent_fd, &m->cent_data, 1);
				break;
			case CentBackend::Pdf:
				if (m->pdf_printer) {
					uint8_t b = m->cent_data;
					m->pdf_printer->feed(&b, 1);
					m->pdf_idle_cycles = 0;
					m->pdf_active = true;
				}
				break;
			default:
				if (!m->printer) {
					char pout[1024]; make_data_path("printer.out", pout, sizeof(pout));
					m->printer.reset(fopen(pout, "ab"));
				}
				if (m->printer) {
					fputc(m->cent_data, m->printer.get());
					fflush(m->printer.get());
				}
				break;
			}
			m->irq_active |= 0x02;
			update_irqs(m);
		}
		break;
	}
	case 0x0040: m->cent_data = val; break;

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

	case 0x00C0: uart_data_write(&m->uart, val); break;
	case 0x00C1: uart_control_write(&m->uart, val); break;

	case 0x00D0: case 0x00D1: case 0x00D2: case 0x00D3:
	case 0x00D4: case 0x00D5: case 0x00D6: case 0x00D7:
	case 0x00D8: case 0x00D9: case 0x00DA: case 0x00DB:
	case 0x00DC: case 0x00DD: case 0x00DE: case 0x00DF:
		rtc_write(&m->rtc, (uint8_t)(port - 0xD0), val);
		break;

	case 0x00E0: case 0x00E1: case 0x00E2: case 0x00E3:
	case 0x00E4: case 0x00E5: case 0x00E7:
		fdc_write(&m->fdc, port - 0xE0, val);
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
		if (chunk > m->rtc_timer_cycles)
			chunk = m->rtc_timer_cycles;
		if (chunk <= 0) chunk = 1;

		int ran = v20_exec(&m->cpu, chunk);
		rem -= ran;

		uart_tick(&m->uart, ran);

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

		m->rtc_timer_cycles -= ran;
		if (m->rtc_timer_cycles <= 0) {
			rtc_tick(&m->rtc);
			m->rtc_timer_cycles += CPU_CLOCK;
		}

		if (m->pdf_active)
			m->pdf_idle_cycles += ran;
	}
}

/* ---- LCD ---- */

void machine_render_lcd(machine_t *m, uint32_t *px)
{
	const uint32_t bg = 0xFF8A9294, fg = 0xFF5C5358;

	if (!m->lcd_on) {
		for (int i = 0; i < LCD_WIDTH * m->lcd_height; i++)
			px[i] = bg;
		return;
	}

	uint32_t base = (uint32_t)m->lcd_memory_start << 9;

	for (int y = 0; y < m->lcd_height; y++) {
		for (int x = 0; x < 60; x++) {
			uint32_t addr = base + (uint32_t)y * 64 + (uint32_t)x;
			uint8_t d = (addr < m->ram_size) ? m->ram[addr] : 0;
			for (int p = 0; p < 8; p++) {
				int idx = y * LCD_WIDTH + x * 8 + p;
				if (idx < LCD_WIDTH * m->lcd_height)
					px[idx] = (d & 0x80) ? fg : bg;
				d <<= 1;
			}
		}
	}
}

/* ---- keyboard ---- */

void machine_key_down(machine_t *m, int row, int bit)
{
	if (row >= 0 && (size_t)row < m->kb_rows.size() && bit >= 0 && bit < 8) {
		m->kb_rows[(size_t)row] |= (uint8_t)(1 << bit);
		m->kb_hold[row][bit] = 2;
	}
}

void machine_key_up(machine_t *m, int row, int bit)
{
	if (row >= 0 && (size_t)row < m->kb_rows.size() && bit >= 0 && bit < 8)
		m->kb_rows[(size_t)row] &= (uint8_t)~(1 << bit);
}

void machine_keys_all_up(machine_t *m)
{
	m->kb_rows.fill(0);
	memset(m->kb_hold, 0, sizeof(m->kb_hold));
}

/* ---- UART IRQ callbacks ---- */

static void uart_txrdy_cb(void *ctx, bool state)
{
	machine_t *m = (machine_t *)ctx;
	if (state) {
		m->irq_active |= 0x04;
		update_irqs(m);
	}
}

static void uart_rxrdy_cb(void *ctx, bool state)
{
	machine_t *m = (machine_t *)ctx;
	if (state) {
		m->irq_active |= 0x08;
		update_irqs(m);
	}
}

/* ---- power ---- */

void machine_power_button(machine_t *m, bool pressed)
{
	if (!pressed) {
		v20_nmi(&m->cpu, false);
		return;
	}

	for (size_t i = 0; i < m->kb_rows.size(); i++)
		m->ram[0x6D06 + i] = m->kb_rows[i];

	if (!m->lcd_on) {
		m->lcd_on = true;
		m->cpu.halted = false;
		machine_reset(m);
		return;
	}

	m->irq_active |= 0x01;
	v20_nmi(&m->cpu, true);
	update_irqs(m);
}

/* ---- NVRAM ---- */

int machine_open_nvram(machine_t *m, const char *path)
{
	int fd = open(path, O_RDWR | O_CREAT, 0644);
	if (fd < 0) {
		fprintf(stderr, "NVRAM: cannot open %s, using volatile RAM\n", path);
		m->ram = (uint8_t *)calloc(1, m->ram_size);
		return m->ram ? 0 : -1;
	}

	struct stat st;
	fstat(fd, &st);
	if ((uint32_t)st.st_size < m->ram_size)
		(void)ftruncate(fd, (off_t)m->ram_size);

	m->ram = (uint8_t *)mmap(nullptr, m->ram_size, PROT_READ | PROT_WRITE,
	                         MAP_SHARED, fd, 0);
	if (m->ram == MAP_FAILED) {
		m->ram = (uint8_t *)calloc(1, m->ram_size);
		close(fd);
		fprintf(stderr, "NVRAM: mmap failed, using volatile RAM\n");
		return m->ram ? 0 : -1;
	}

	m->nvram_fd = fd;
	fprintf(stderr, "NVRAM: %s (%u KB, mmap)\n", path, m->ram_size / 1024);
	return 0;
}

void machine_close_rom(machine_t *m)
{
	if (m->rom) {
		munmap(m->rom, ROM_SIZE);
		m->rom = nullptr;
	}
	if (m->rom_fd >= 0) {
		close(m->rom_fd);
		m->rom_fd = -1;
	}
}

void machine_close_nvram(machine_t *m)
{
	if (m->nvram_fd >= 0 && m->ram) {
		msync(m->ram, m->ram_size, MS_SYNC);
		munmap(m->ram, m->ram_size);
		close(m->nvram_fd);
	} else {
		free(m->ram);
	}
	m->ram = nullptr;
	m->nvram_fd = -1;
}

/* ---- PC Card ---- */

#define PCCARD_DEFAULT_SIZE (512 * 1024)

int machine_open_pccard(machine_t *m, const char *path)
{
	int fd = open(path, O_RDWR | O_CREAT, 0644);
	if (fd < 0) return -1;

	struct stat st;
	fstat(fd, &st);
	uint32_t sz = (uint32_t)st.st_size;
	bool is_new = (sz == 0);
	if (sz < PCCARD_DEFAULT_SIZE) {
		sz = PCCARD_DEFAULT_SIZE;
		(void)ftruncate(fd, (off_t)sz);
	}

	m->pccard = (uint8_t *)mmap(nullptr, sz, PROT_READ | PROT_WRITE,
	                            MAP_SHARED, fd, 0);
	if (m->pccard == MAP_FAILED) {
		m->pccard = nullptr;
		close(fd);
		return -1;
	}

	if (is_new) memset(m->pccard, 0xFF, sz);

	m->pccard_size = sz;
	m->pccard_fd = fd;

	fprintf(stderr, "PC Card: %s (%u KB, mmap%s)\n",
		path, sz / 1024, is_new ? ", new" : "");

	for (int i = 0; i < NUM_BANKS; i++)
		update_bank(m, i);

	return 0;
}

void machine_close_pccard(machine_t *m)
{
	if (!m->pccard) return;

	if (m->pccard_fd >= 0) {
		msync(m->pccard, m->pccard_size, MS_SYNC);
		munmap(m->pccard, m->pccard_size);
		close(m->pccard_fd);
	} else {
		free(m->pccard);
	}
	m->pccard = nullptr;
	m->pccard_size = 0;
	m->pccard_fd = -1;

	for (int i = 0; i < NUM_BANKS; i++)
		update_bank(m, i);
}

/* ---- PDF printer ---- */

static void uart_pdf_tx(void *ctx, uint8_t byte)
{
	auto *m = static_cast<machine_t *>(ctx);
	if (m->pdf_printer) {
		m->pdf_printer->feed(&byte, 1);
		m->pdf_idle_cycles = 0;
		if (!m->pdf_active)
			fprintf(stderr, "PDF printer (serial): first byte received\n");
		m->pdf_active = true;
	}
}

void machine_pdf_start(machine_t *m, const char *path, int model)
{
	machine_pdf_finish(m);

	auto pm = static_cast<PrinterModel>(model);
	const auto &prof = profile_for(pm);

	m->pdf_writer = std::make_unique<PdfWriter>(path);
	m->pdf_printer = create_printer(pm, *m->pdf_writer);
	PrinterConfig cfg = default_config_for(pm);
	m->pdf_printer->apply_config(cfg);
	m->pdf_model = model;
	m->pdf_active = false;
	m->pdf_idle_cycles = 0;
	m->pdf_via_serial = false;
	m->cent_backend = CentBackend::Pdf;

	fprintf(stderr, "PDF printer (parallel): %s -> %s\n", prof.name, path);
}

void machine_pdf_start_serial(machine_t *m, const char *path, int model)
{
	machine_pdf_finish(m);

	auto pm = static_cast<PrinterModel>(model);
	const auto &prof = profile_for(pm);

	m->pdf_writer = std::make_unique<PdfWriter>(path);
	m->pdf_printer = create_printer(pm, *m->pdf_writer);
	PrinterConfig cfg = default_config_for(pm);
	m->pdf_printer->apply_config(cfg);
	m->pdf_model = model;
	m->pdf_active = false;
	m->pdf_idle_cycles = 0;
	m->pdf_via_serial = true;
	m->uart.tx_byte_cb = uart_pdf_tx;
	m->uart.tx_byte_ctx = m;
	m->uart.cts = true;
	m->uart.dsr = true;

	fprintf(stderr, "PDF printer (serial): %s -> %s\n", prof.name, path);
}

void machine_pdf_finish(machine_t *m)
{
	if (m->pdf_printer) {
		m->pdf_printer->flush();
		m->pdf_printer.reset();
	}
	if (m->pdf_writer) {
		m->pdf_writer->finish();
		m->pdf_writer.reset();
	}
	if (m->pdf_via_serial) {
		m->uart.tx_byte_cb = nullptr;
		m->uart.tx_byte_ctx = nullptr;
	}
	m->pdf_active = false;
	m->pdf_idle_cycles = 0;
	m->pdf_via_serial = false;
}

bool machine_pdf_check_idle(machine_t *m, int timeout_seconds)
{
	if (!m->pdf_active || !m->pdf_printer)
		return false;
	if (m->pdf_idle_cycles > CPU_CLOCK * timeout_seconds) {
		machine_pdf_finish(m);
		return true;
	}
	return false;
}
