// license:BSD-3-Clause
// copyright-holders:Stephen Hurd
// Inspired by VirtualT (Copyright 2004-2008 Ken Pettit and Stephen Hurd, BSD-2-Clause)
#include <cstdlib>
#include <cstdio>
#include "prefs.h"

Fl_Preferences *g_prefs = nullptr;

void prefs_init(void)
{
	g_prefs = new Fl_Preferences(Fl_Preferences::USER, "dreamulator", "dreamulator");
}

void prefs_save_window(const char *name, int x, int y, int w, int h)
{
	if (!g_prefs) return;
	Fl_Preferences grp(g_prefs, name);
	grp.set("x", x);
	grp.set("y", y);
	grp.set("w", w);
	grp.set("h", h);
}

void prefs_load_window(const char *name, int &x, int &y, int &w, int &h)
{
	if (!g_prefs) return;
	Fl_Preferences grp(g_prefs, name);
	grp.get("x", x, x);
	grp.get("y", y, y);
	grp.get("w", w, w);
	grp.get("h", h, h);
}

void prefs_set_int(const char *group, const char *key, int val)
{
	if (!g_prefs) return;
	Fl_Preferences grp(g_prefs, group);
	grp.set(key, val);
}

int prefs_get_int(const char *group, const char *key, int def)
{
	if (!g_prefs) return def;
	Fl_Preferences grp(g_prefs, group);
	int val;
	grp.get(key, val, def);
	return val;
}

void prefs_set_str(const char *group, const char *key, const char *val)
{
	if (!g_prefs) return;
	Fl_Preferences grp(g_prefs, group);
	grp.set(key, val);
}

void prefs_get_str(const char *group, const char *key, char *buf, int bufsz, const char *def)
{
	if (!g_prefs) return;
	Fl_Preferences grp(g_prefs, group);
	char *val;
	grp.get(key, val, def);
	snprintf(buf, bufsz, "%s", val);
	free(val);
}
