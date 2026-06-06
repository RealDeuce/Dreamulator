// license:BSD-3-Clause
// copyright-holders:Stephen Hurd
#ifndef PATHS_H
#define PATHS_H

#include <cstddef>

const char *get_data_dir();
const char *get_install_dir();
const char *find_rom_dir(const char *override);
void make_data_path(const char *filename, char *out, size_t sz);

#endif
