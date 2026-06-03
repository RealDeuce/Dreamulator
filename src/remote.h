// license:BSD-3-Clause
// copyright-holders:Stephen Hurd
// Inspired by VirtualT (Copyright 2008 Ken Pettit, BSD-2-Clause)
#ifndef REMOTE_H
#define REMOTE_H

struct v20;
struct machine;

void remote_init(struct machine *mach, int port);
void remote_shutdown(void);

#endif
