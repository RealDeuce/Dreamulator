// license:BSD-3-Clause
// copyright-holders:Stephen Hurd, MAMEDev (Olivier Galibert, Wilbert Pol)
#ifndef FDC_H
#define FDC_H

#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>

constexpr int FDC_HEADS       = 2;
constexpr int FDC_CYLINDERS   = 80;
constexpr int FDC_SECTORS     = 18;
constexpr int FDC_SECTOR_SIZE = 512;
constexpr int FDC_DISK_SIZE   = FDC_HEADS * FDC_CYLINDERS * FDC_SECTORS * FDC_SECTOR_SIZE;

enum class FdcPhase { Cmd, Exec, Result };

struct FClose { void operator()(FILE *f) const { if (f) fclose(f); } };
using FilePtr = std::unique_ptr<FILE, FClose>;

struct fdc_t {
	FdcPhase phase = FdcPhase::Cmd;

	std::array<uint8_t, 9> cmd{};
	int      cmd_len = 0, cmd_pos = 0;

	std::array<uint8_t, 8> res{};
	int      res_len = 0, res_pos = 0;

	std::array<uint8_t, FDC_SECTOR_SIZE> sec_buf{};
	int      data_remaining = 0;
	int      data_pos = 0;
	bool     data_is_read = false;

	uint8_t  dor = 0;
	int      pcn = 0;
	uint8_t  st0 = 0;
	bool     irq_pending = false;

	int      reset_count = 0;
	int      reset_pos = 0;
	std::array<uint8_t, 2> reset_data{};
	bool     reset_active = false;

	bool     sis_active = false;
	int      sis_pos = 0;

	int      shim_reads = 0;
	int      shim_writes = 0;
	std::array<uint8_t, 9> shim_cmd{};
	int      shim_cmd_len = 0;
	int      shim_cmd_pos = 0;

	FilePtr  disk;
	bool     disk_wp = false;
};

void    fdc_init(fdc_t *f);
void    fdc_destroy(fdc_t *f);
[[nodiscard]] int     fdc_load_disk(fdc_t *f, const char *path);
uint8_t fdc_read(fdc_t *f, int port);
void    fdc_write(fdc_t *f, int port, uint8_t data);

#endif
