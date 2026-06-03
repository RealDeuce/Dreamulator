// license:BSD-3-Clause
// copyright-holders:Stephen Hurd, MAMEDev (Olivier Galibert, Wilbert Pol)
#include "fdc.h"
#include <cstdio>
#include <cstring>

#define MSR_RQM  0x80
#define MSR_DIO  0x40
#define MSR_NDM  0x20
#define MSR_CB   0x10
#define MSR_DB   0x0F

static int cmd_length(uint8_t c)
{
	switch (c & 0x1f) {
	case 0x03: case 0x0f: return 3;
	case 0x04: case 0x07: case 0x0a: case 0x12: return 2;
	case 0x05: case 0x06: case 0x09: case 0x0c: return 9;
	case 0x0d: return 6;
	case 0x13: return 4;
	default:   return 1;
	}
}

static int sector_size(uint8_t n, uint8_t dtl)
{
	int sz = n ? (int)(128U << (n & 3)) : dtl;
	if (sz > FDC_SECTOR_SIZE) sz = FDC_SECTOR_SIZE;
	return sz;
}

static long sector_offset(int cyl, int head, int sect)
{
	return ((long)(cyl * FDC_HEADS + head) * FDC_SECTORS + (sect - 1))
		* FDC_SECTOR_SIZE;
}

static void read_sector(fdc_t *f, int cyl, int head, int sect)
{
	f->sec_buf.fill(0);
	if (f->disk) {
		fseek(f->disk.get(), sector_offset(cyl, head, sect), SEEK_SET);
		(void)fread(f->sec_buf.data(), 1, FDC_SECTOR_SIZE, f->disk.get());
	}
}

static void write_sector(fdc_t *f, int cyl, int head, int sect)
{
	if (f->disk && !f->disk_wp) {
		fseek(f->disk.get(), sector_offset(cyl, head, sect), SEEK_SET);
		fwrite(f->sec_buf.data(), 1, FDC_SECTOR_SIZE, f->disk.get());
		fflush(f->disk.get());
	}
}

static void enter_result(fdc_t *f, int len)
{
	f->phase = FdcPhase::Result;
	f->res_len = len;
	f->res_pos = 0;
}

static void finish_command(fdc_t *f)
{
	int c = f->cmd[0] & 0x1f;
	int head = (f->cmd[1] >> 2) & 1;
	int cyl = f->cmd[2];
	int sect = f->cmd[4];

	switch (c) {
	case 0x03: /* SPECIFY */
	case 0x12: /* PERPENDICULAR MODE */
	case 0x13: /* CONFIGURE */
		f->phase = FdcPhase::Cmd;
		break;

	case 0x04: /* SENSE DRIVE STATUS */
		f->res[0] = 0x20 | (f->cmd[1] & 7);
		if (f->pcn == 0) f->res[0] |= 0x10;
		enter_result(f, 1);
		break;

	case 0x07: /* RECALIBRATE */
		f->pcn = 0;
		f->st0 = 0x20 | (f->cmd[1] & 7);
		f->irq_pending = true;
		f->phase = FdcPhase::Cmd;
		break;

	case 0x08: /* SENSE INTERRUPT STATUS */
		if (f->irq_pending) {
			f->res[0] = f->st0;
			f->irq_pending = false;
		} else {
			f->res[0] = 0x80;
		}
		f->res[1] = (uint8_t)f->pcn;
		enter_result(f, 2);
		break;

	case 0x0a: /* READ ID */
		f->res[0] = (uint8_t)((head << 2) | (f->cmd[1] & 3));
		f->res[1] = 0;
		f->res[2] = 0;
		f->res[3] = (uint8_t)f->pcn;
		f->res[4] = (uint8_t)head;
		f->res[5] = 1;
		f->res[6] = 2;
		enter_result(f, 7);
		break;

	case 0x0f: /* SEEK */
		f->pcn = f->cmd[2];
		f->st0 = 0x20 | (f->cmd[1] & 7);
		f->irq_pending = true;
		f->phase = FdcPhase::Cmd;
		break;

	case 0x06: case 0x0c: /* READ DATA / READ DELETED DATA */
		read_sector(f, cyl, head, sect);
		f->data_pos = 0;
		f->data_remaining = (int)sector_size(f->cmd[5], f->cmd[8]);
		f->data_is_read = true;
		f->phase = FdcPhase::Exec;
		break;

	case 0x05: case 0x09: /* WRITE DATA / WRITE DELETED DATA */
		f->data_pos = 0;
		f->data_remaining = (int)sector_size(f->cmd[5], f->cmd[8]);
		f->data_is_read = false;
		f->phase = FdcPhase::Exec;
		break;

	case 0x0d: /* FORMAT TRACK */
		f->data_pos = 0;
		f->data_remaining = (f->cmd[3] > FDC_SECTORS ? FDC_SECTORS : f->cmd[3]) * 4;
		f->data_is_read = false;
		f->phase = FdcPhase::Exec;
		break;

	default:
		f->res[0] = 0x80;
		enter_result(f, 1);
		break;
	}
}

static void data_complete(fdc_t *f)
{
	int c = f->cmd[0] & 0x1f;
	int head = (f->cmd[1] >> 2) & 1;
	int cyl = f->cmd[2];
	int sect = f->cmd[4];

	if (c == 0x05 || c == 0x09) {
		write_sector(f, cyl, head, sect);
	} else if (c == 0x0d) {
		f->sec_buf.fill(0);
		int sects = f->cmd[3];
		for (int s = 1; s <= sects && s <= FDC_SECTORS; s++)
			write_sector(f, cyl, head, s);
	}

	f->res[0] = (uint8_t)((head << 2) | (f->cmd[1] & 3));
	f->res[1] = 0;
	f->res[2] = 0;
	f->res[3] = (uint8_t)cyl;
	f->res[4] = (uint8_t)head;
	f->res[5] = (uint8_t)(sect + 1);
	f->res[6] = f->cmd[5];
	enter_result(f, 7);
}

/* ---- public API ---- */

void fdc_init(fdc_t *f)
{
	*f = fdc_t{};
}

void fdc_destroy(fdc_t *f)
{
	f->disk.reset();
}

int fdc_load_disk(fdc_t *f, const char *path)
{
	FilePtr fp{fopen(path, "r+b")};
	if (!fp) {
		fp.reset(fopen(path, "w+b"));
		if (!fp) return -1;
		std::array<uint8_t, FDC_SECTOR_SIZE> zero{};
		for (int i = 0; i < FDC_HEADS * FDC_CYLINDERS * FDC_SECTORS; i++)
			fwrite(zero.data(), 1, FDC_SECTOR_SIZE, fp.get());
		fflush(fp.get());
		fprintf(stderr, "FDC: new disk image %s\n", path);
	} else {
		fprintf(stderr, "FDC: loaded %s\n", path);
	}
	f->disk = std::move(fp);
	return 0;
}

uint8_t fdc_read(fdc_t *f, int port)
{
	switch (port) {
	case 0: return 0; /* SRA */
	case 1: return 0; /* SRB */
	case 2: return f->dor;

	case 4: /* MSR */
		if (f->reset_active)
			return MSR_RQM | MSR_DIO | MSR_CB;
		switch (f->phase) {
		case FdcPhase::Cmd:    return MSR_RQM;
		case FdcPhase::Exec:   return MSR_RQM | (f->data_is_read ? MSR_DIO : 0) | MSR_CB | MSR_NDM;
		case FdcPhase::Result: return MSR_RQM | MSR_DIO | MSR_CB;
		}
		return MSR_RQM;

	case 5: /* FIFO read */
		if (f->reset_active) {
			uint8_t d = f->reset_data[f->reset_pos & 1];
			if (++f->reset_pos >= 2) {
				f->reset_pos = 0;
				f->reset_active = false;
			}
			return d;
		}

		if (f->phase == FdcPhase::Exec && f->data_is_read) {
			uint8_t d = f->sec_buf[(size_t)f->data_pos++];
			f->data_remaining--;
			if (f->data_remaining <= 0)
				data_complete(f);
			return d;
		}

		if (f->phase == FdcPhase::Result && f->res_pos < f->res_len) {
			uint8_t d = f->res[(size_t)f->res_pos];
			if (f->sis_active) {
				if (f->sis_pos == 0)
					d &= ~0x04;
				f->sis_pos++;
				if (d == 0x80 || f->sis_pos >= 2) {
					f->sis_active = false;
					f->sis_pos = 0;
				}
			}
			f->res_pos++;
			if (f->res_pos >= f->res_len)
				f->phase = FdcPhase::Cmd;
			return d;
		}
		return 0xff;

	case 7: /* DIR */
		return 0;
	}
	return 0xff;
}

void fdc_write(fdc_t *f, int port, uint8_t data)
{
	switch (port) {
	case 2: { /* DOR */
		bool reset_released = !(f->dor & 0x04) && (data & 0x04);
		f->dor = data;
		if (reset_released) {
			f->phase = FdcPhase::Cmd;
			f->shim_cmd_len = 0;
			f->shim_cmd_pos = 0;
			f->shim_reads = 0;
			f->shim_writes = 0;
			f->reset_count = 4;
			f->reset_pos = 0;
			f->reset_active = false;
			f->sis_active = false;
			f->sis_pos = 0;
		}
		break;
	}

	case 4: /* DSR */
		break;

	case 5: /* FIFO write */
		/* reset-sense injection */
		if (data == 0x08 && f->reset_count > 0) {
			uint8_t st0 = 0xC0 | (uint8_t)(4 - f->reset_count);
			f->reset_data[0] = st0;
			f->reset_data[1] = (uint8_t)f->pcn;
			f->reset_count--;
			f->reset_pos = 0;
			f->reset_active = true;
			return;
		}

		/* data write phase */
		if (f->phase == FdcPhase::Exec && !f->data_is_read) {
			if ((f->cmd[0] & 0x1f) != 0x0d)
				f->sec_buf[(size_t)f->data_pos] = data;
			f->data_pos++;
			f->data_remaining--;
			if (f->data_remaining <= 0)
				data_complete(f);
			return;
		}

		/* command accumulation */
		if (f->phase != FdcPhase::Cmd) return;

		if (f->shim_cmd_pos == 0) {
			f->shim_cmd_len = cmd_length(data);
			f->sis_active = (data == 0x08);
			f->sis_pos = 0;
		}

		f->cmd[(size_t)f->shim_cmd_pos++] = data;

		if (f->shim_cmd_pos >= f->shim_cmd_len) {
			int c = f->cmd[0] & 0x1f;
			switch (c) {
			case 0x05:
				f->shim_writes = sector_size(f->cmd[5], f->cmd[8]);
				break;
			case 0x06:
				f->shim_reads = sector_size(f->cmd[5], f->cmd[8]);
				break;
			case 0x0d:
				f->shim_writes = (f->cmd[3] > FDC_SECTORS ? FDC_SECTORS : f->cmd[3]) * 4;
				break;
			}
			finish_command(f);
			f->shim_cmd_len = 0;
			f->shim_cmd_pos = 0;
		}
		break;

	case 7: /* CCR */
		break;
	}
}
