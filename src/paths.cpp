// license:BSD-3-Clause
// copyright-holders:Stephen Hurd
#include "paths.h"
#include <FL/Fl_Preferences.H>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#include <io.h>
#define mkdir(p, m) _mkdir(p)
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#endif
#else
#include <unistd.h>
#include <libgen.h>
#endif
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

static char g_data_dir[1024];
static char g_install_dir[1024];

static bool dir_exists(const char *path)
{
	struct stat st;
	return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static void ensure_dir(const char *path)
{
	if (!dir_exists(path))
		mkdir(path, 0755);
}

const char *get_data_dir()
{
	if (g_data_dir[0]) return g_data_dir;

	Fl_Preferences prefs(Fl_Preferences::USER, "dreamulator", "dreamulator");
	prefs.getUserdataPath(g_data_dir, (int)sizeof(g_data_dir));

	size_t len = strlen(g_data_dir);
	if (len > 0 && (g_data_dir[len - 1] == '/' || g_data_dir[len - 1] == '\\'))
		g_data_dir[len - 1] = '\0';

	ensure_dir(g_data_dir);
	return g_data_dir;
}

static void get_exe_dir(char *buf, size_t sz)
{
	buf[0] = '\0';
#ifdef _WIN32
	GetModuleFileNameA(NULL, buf, (DWORD)sz);
	char *sep = strrchr(buf, '\\');
	if (sep) *sep = '\0';
#elif defined(__APPLE__)
	uint32_t bsz = (uint32_t)sz;
	if (_NSGetExecutablePath(buf, &bsz) == 0) {
		char *sep = strrchr(buf, '/');
		if (sep) *sep = '\0';
	}
#elif defined(__FreeBSD__)
	ssize_t n = readlink("/proc/curproc/file", buf, sz - 1);
	if (n > 0) { buf[n] = '\0'; char *sep = strrchr(buf, '/'); if (sep) *sep = '\0'; }
#else
	ssize_t n = readlink("/proc/self/exe", buf, sz - 1);
	if (n > 0) { buf[n] = '\0'; char *sep = strrchr(buf, '/'); if (sep) *sep = '\0'; }
#endif
}

const char *get_install_dir()
{
	if (g_install_dir[0]) return g_install_dir;

	char exe_dir[1024];
	get_exe_dir(exe_dir, sizeof(exe_dir));
	if (!exe_dir[0]) return nullptr;

	char candidate[1024];
#ifdef _WIN32
	snprintf(candidate, sizeof(candidate), "%s\\roms", exe_dir);
	if (dir_exists(candidate)) {
		snprintf(g_install_dir, sizeof(g_install_dir), "%s", exe_dir);
		return g_install_dir;
	}
#else
	snprintf(candidate, sizeof(candidate), "%s/../share/dreamulator/roms", exe_dir);
	if (dir_exists(candidate)) {
		snprintf(g_install_dir, sizeof(g_install_dir), "%s/../share/dreamulator", exe_dir);
		return g_install_dir;
	}
#endif
#ifdef __APPLE__
	snprintf(candidate, sizeof(candidate), "%s/../Resources/roms", exe_dir);
	if (dir_exists(candidate)) {
		snprintf(g_install_dir, sizeof(g_install_dir), "%s/../Resources", exe_dir);
		return g_install_dir;
	}
#endif
	return nullptr;
}

const char *find_rom_dir(const char *override)
{
	static char rom_dir[1024];

	if (override && dir_exists(override)) {
		snprintf(rom_dir, sizeof(rom_dir), "%s", override);
		return rom_dir;
	}

	const char *data = get_data_dir();
	if (data) {
		snprintf(rom_dir, sizeof(rom_dir), "%s/roms", data);
		if (dir_exists(rom_dir)) return rom_dir;
	}

	const char *inst = get_install_dir();
	if (inst) {
		snprintf(rom_dir, sizeof(rom_dir), "%s/roms", inst);
		if (dir_exists(rom_dir)) return rom_dir;
	}

	if (dir_exists("roms"))
		return "roms";

	return nullptr;
}

void make_data_path(const char *filename, char *out, size_t sz)
{
	const char *data = get_data_dir();
	if (data)
		snprintf(out, sz, "%s/%s", data, filename);
	else
		snprintf(out, sz, "%s", filename);
}
