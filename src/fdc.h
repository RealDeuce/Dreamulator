// license:BSD-3-Clause
// copyright-holders:Stephen Hurd, MAMEDev (Olivier Galibert, Wilbert Pol)
#ifndef FDC_H
#define FDC_H

#include <cstdint>
#include <cstdio>

#define FDC_HEADS       2
#define FDC_CYLINDERS   80
#define FDC_SECTORS     18
#define FDC_SECTOR_SIZE 512
#define FDC_DISK_SIZE   (FDC_HEADS * FDC_CYLINDERS * FDC_SECTORS * FDC_SECTOR_SIZE)

enum { FDC_CMD, FDC_EXEC, FDC_RESULT };

struct fdc_t {
	int      phase;

	uint8_t  cmd[9];
	int      cmd_len, cmd_pos;

	uint8_t  res[8];
	int      res_len, res_pos;

	uint8_t  sec_buf[FDC_SECTOR_SIZE];
	int      data_remaining;
	int      data_pos;
	bool     data_is_read;

	uint8_t  dor;
	int      pcn;
	uint8_t  st0;
	bool     irq_pending;

	int      reset_count;
	int      reset_pos;
	uint8_t  reset_data[2];
	bool     reset_active;

	bool     sis_active;
	int      sis_pos;

	int      shim_reads;
	int      shim_writes;
	uint8_t  shim_cmd[9];
	int      shim_cmd_len;
	int      shim_cmd_pos;

	FILE     *disk;
	bool     disk_wp;
};

void    fdc_init(fdc_t *f);
void    fdc_destroy(fdc_t *f);
int     fdc_load_disk(fdc_t *f, const char *path);
uint8_t fdc_read(fdc_t *f, int port);
void    fdc_write(fdc_t *f, int port, uint8_t data);

#endif
