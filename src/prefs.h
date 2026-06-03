// license:BSD-3-Clause
// copyright-holders:Stephen Hurd
// Inspired by VirtualT (Copyright 2004-2008 Ken Pettit and Stephen Hurd, BSD-2-Clause)
#ifndef PREFS_H
#define PREFS_H

#include <FL/Fl_Preferences.H>

extern Fl_Preferences *g_prefs;

void prefs_init(void);
void prefs_save_window(const char *name, int x, int y, int w, int h);
void prefs_load_window(const char *name, int &x, int &y, int &w, int &h);
void prefs_set_int(const char *group, const char *key, int val);
int  prefs_get_int(const char *group, const char *key, int def);
void prefs_set_str(const char *group, const char *key, const char *val);
void prefs_get_str(const char *group, const char *key, char *buf, int bufsz, const char *def);

#endif
