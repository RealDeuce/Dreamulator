// license:BSD-3-Clause
// copyright-holders:Stephen Hurd
// Inspired by VirtualT (Copyright 2008 Ken Pettit, BSD-2-Clause)
#include <FL/Fl.H>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>

#include "v20.h"
#include "machine.h"
#include "dis86.h"
#include "remote.h"

static machine_t *g_mach;
static int g_listen_fd = -1;
static int g_client_fd = -1;
static char g_linebuf[1024];
static int g_linepos = 0;

static void set_nonblock(int fd)
{
	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
}

#ifdef __GNUC__
__attribute__((format(printf, 1, 2)))
#endif
static void rsend(const char *fmt, ...)
{
	if (g_client_fd < 0) return;
	char buf[2048];
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	if (n <= 0) return;
	size_t len = (n >= (int)sizeof(buf)) ? sizeof(buf) - 1 : (size_t)n;
	(void)write(g_client_fd, buf, len);
}

static void process_command(const char *cmd)
{
	v20_t *c = &g_mach->cpu;

	if (!*cmd) return;

	if (!strcmp(cmd, "help")) {
		rsend("Commands: help status regs reg mem wmem dis bp cbp lbp\n");
		rsend("  run stop step reset key io wio quit\n");
	}
	else if (!strcmp(cmd, "status")) {
		rsend("%s PC=%04X:%04X %s\n",
			g_mach->model->name, c->cs, c->ip,
			c->debug_stop ? "STOPPED" : "RUNNING");
	}
	else if (!strcmp(cmd, "regs")) {
		rsend("AX=%04X BX=%04X CX=%04X DX=%04X SI=%04X DI=%04X\n",
			c->ax, c->bx, c->cx, c->dx, c->si, c->di);
		rsend("BP=%04X SP=%04X CS=%04X DS=%04X ES=%04X SS=%04X IP=%04X\n",
			c->bp, c->sp, c->cs, c->ds, c->es, c->ss, c->ip);
		rsend("FLAGS=%04X [%s%s%s%s%s%s%s%s%s]\n", c->flags,
			c->flags&V20_OF?"O":"", c->flags&V20_DF?"D":"",
			c->flags&V20_IF?"I":"", c->flags&V20_TF?"T":"",
			c->flags&V20_SF?"S":"", c->flags&V20_ZF?"Z":"",
			c->flags&V20_AF?"A":"", c->flags&V20_PF?"P":"",
			c->flags&V20_CF?"C":"");
	}
	else if (!strncmp(cmd, "reg ", 4)) {
		const char *arg = cmd + 4;
		unsigned val;
		char name[8];
		if (sscanf(arg, "%7[^=]=%x", name, &val) == 2) {
			if (!strcasecmp(name,"ax")) c->ax=(uint16_t)val;
			else if (!strcasecmp(name,"bx")) c->bx=(uint16_t)val;
			else if (!strcasecmp(name,"cx")) c->cx=(uint16_t)val;
			else if (!strcasecmp(name,"dx")) c->dx=(uint16_t)val;
			else if (!strcasecmp(name,"si")) c->si=(uint16_t)val;
			else if (!strcasecmp(name,"di")) c->di=(uint16_t)val;
			else if (!strcasecmp(name,"bp")) c->bp=(uint16_t)val;
			else if (!strcasecmp(name,"sp")) c->sp=(uint16_t)val;
			else if (!strcasecmp(name,"cs")) c->cs=(uint16_t)val;
			else if (!strcasecmp(name,"ds")) c->ds=(uint16_t)val;
			else if (!strcasecmp(name,"es")) c->es=(uint16_t)val;
			else if (!strcasecmp(name,"ss")) c->ss=(uint16_t)val;
			else if (!strcasecmp(name,"ip")) c->ip=(uint16_t)val;
			else if (!strcasecmp(name,"flags")) c->flags=(uint16_t)val;
			else { rsend("Unknown register: %s\n", name); return; }
			rsend("OK\n");
		} else {
			rsend("Usage: reg NAME=VALUE\n");
		}
	}
	else if (!strncmp(cmd, "mem ", 4)) {
		unsigned seg=0, off=0, count=64;
		const char *arg = cmd + 4;
		if (sscanf(arg, "%x:%x %u", &seg, &off, &count) < 2) {
			unsigned addr=0;
			sscanf(arg, "%x %u", &addr, &count);
			seg = addr >> 4; off = addr & 0xF;
		}
		if (count > 256) count = 256;
		for (unsigned i = 0; i < count; i += 16) {
			uint32_t addr = ((uint32_t)seg << 4) + off + i;
			rsend("%05X ", addr & 0xFFFFF);
			for (unsigned j = 0; j < 16 && i+j < count; j++)
				rsend("%02X ", c->mem_read(c->ctx, (addr+j) & 0xFFFFF));
			rsend("\n");
		}
	}
	else if (!strncmp(cmd, "wmem ", 5)) {
		unsigned seg=0, off=0;
		const char *p = cmd + 5;
		int n;
		if (sscanf(p, "%x:%x%n", &seg, &off, &n) >= 2) {
			p += n;
			uint32_t addr = ((uint32_t)seg << 4) + off;
			unsigned byte;
			while (sscanf(p, " %x%n", &byte, &n) == 1) {
				c->mem_write(c->ctx, addr & 0xFFFFF, (uint8_t)byte);
				addr++; p += n;
			}
			rsend("OK\n");
		} else { rsend("Usage: wmem SEG:OFF byte...\n"); }
	}
	else if (!strncmp(cmd, "dis ", 4)) {
		unsigned seg=0, off=0, count=16;
		sscanf(cmd + 4, "%x:%x %u", &seg, &off, &count);
		if (count > 100) count = 100;
		for (unsigned i = 0; i < count; i++) {
			uint8_t code[8];
			for (unsigned j = 0; j < 8; j++)
				code[j] = c->mem_read(c->ctx, ((seg<<4)+off+j) & 0xFFFFF);
			char dis[128];
			int len = dis86(code, 8, (uint16_t)off, dis, sizeof(dis));
			rsend("%04X:%04X  %s\n", seg, off, dis);
			off += (unsigned)len;
		}
	}
	else if (!strncmp(cmd, "bp ", 3)) {
		unsigned seg=0, off=0;
		sscanf(cmd + 3, "%x:%x", &seg, &off);
		for (size_t i = 0; i < c->bp_enabled.size(); i++) {
			if (!c->bp_enabled[i]) {
				c->bp_seg[i] = (uint16_t)seg; c->bp_off[i] = (uint16_t)off;
				c->bp_enabled[i] = true;
				rsend("Breakpoint %zu set at %04X:%04X\n", i, seg, off);
				return;
			}
		}
		rsend("No free breakpoint slots\n");
	}
	else if (!strncmp(cmd, "cbp ", 4)) {
		unsigned seg=0, off=0;
		sscanf(cmd + 4, "%x:%x", &seg, &off);
		for (size_t i = 0; i < c->bp_enabled.size(); i++) {
			if (c->bp_enabled[i] && c->bp_seg[i]==seg && c->bp_off[i]==off) {
				c->bp_enabled[i] = false;
				rsend("Breakpoint %zu cleared\n", i);
				return;
			}
		}
		rsend("No breakpoint at %04X:%04X\n", seg, off);
	}
	else if (!strcmp(cmd, "lbp")) {
		for (size_t i = 0; i < c->bp_enabled.size(); i++)
			if (c->bp_enabled[i])
				rsend("BP%zu: %04X:%04X\n", i, c->bp_seg[i], c->bp_off[i]);
	}
	else if (!strcmp(cmd, "run")) {
		c->debug_stop = false;
		rsend("Running\n");
	}
	else if (!strcmp(cmd, "stop")) {
		c->debug_stop = true;
		rsend("Stopped at %04X:%04X\n", c->cs, c->ip);
	}
	else if (!strncmp(cmd, "step", 4)) {
		int count = 1;
		sscanf(cmd + 4, " %d", &count);
		for (int i = 0; i < count; i++) {
			c->debug_step = true;
			c->debug_stop = true;
			v20_exec(c, 100);
		}
		rsend("Stepped to %04X:%04X\n", c->cs, c->ip);
	}
	else if (!strcmp(cmd, "reset")) {
		machine_reset(g_mach);
		rsend("Reset\n");
	}
	else if (!strncmp(cmd, "io ", 3)) {
		unsigned port;
		if (sscanf(cmd + 3, "%x", &port) == 1)
			rsend("%02X\n", c->io_read(c->ctx, (uint16_t)port));
	}
	else if (!strncmp(cmd, "wio ", 4)) {
		unsigned port, val;
		if (sscanf(cmd + 4, "%x %x", &port, &val) == 2) {
			c->io_write(c->ctx, (uint16_t)port, (uint8_t)val);
			rsend("OK\n");
		}
	}
	else if (!strncmp(cmd, "key ", 4)) {
		int row, bit;
		char dir[8] = "down";
		sscanf(cmd + 4, "%d %d %7s", &row, &bit, dir);
		if (!strcmp(dir, "up"))
			machine_key_up(g_mach, row, bit);
		else
			machine_key_down(g_mach, row, bit);
		rsend("OK\n");
	}
	else if (!strcmp(cmd, "quit")) {
		close(g_client_fd);
		Fl::remove_fd((FL_SOCKET)g_client_fd);
		g_client_fd = -1;
		g_linepos = 0;
		fprintf(stderr, "Remote: client disconnected\n");
	}
	else {
		rsend("Unknown command: %s\nType 'help' for commands.\n", cmd);
	}
}

static void client_cb(FL_SOCKET fd, void *)
{
	char buf[256];
	ssize_t n = read(fd, buf, sizeof(buf));
	if (n <= 0) {
		close(fd);
		Fl::remove_fd((FL_SOCKET)fd);
		g_client_fd = -1;
		g_linepos = 0;
		fprintf(stderr, "Remote: client disconnected\n");
		return;
	}
	for (ssize_t i = 0; i < n; i++) {
		char ch = buf[i];
		if (ch == '\n' || ch == '\r') {
			if (g_linepos > 0) {
				g_linebuf[g_linepos] = 0;
				process_command(g_linebuf);
				g_linepos = 0;
			}
		} else if (g_linepos < (int)sizeof(g_linebuf) - 1) {
			g_linebuf[g_linepos++] = ch;
		}
	}
}

static void listen_cb(FL_SOCKET fd, void *)
{
	if (g_client_fd >= 0) {
		int tmp = accept(fd, nullptr, nullptr);
		if (tmp >= 0) {
			const char *msg = "Another client is connected.\n";
			(void)write(tmp, msg, strlen(msg));
			close(tmp);
		}
		return;
	}

	g_client_fd = accept(fd, nullptr, nullptr);
	if (g_client_fd < 0) return;

	set_nonblock(g_client_fd);
	Fl::add_fd((FL_SOCKET)g_client_fd, FL_READ, client_cb, nullptr);
	g_linepos = 0;
	fprintf(stderr, "Remote: client connected\n");
	rsend("dreamulator remote control. Type 'help' for commands.\n");
}

void remote_init(machine_t *mach, int port)
{
	g_mach = mach;

	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) return;

	int opt = 1;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr.sin_port = htons((uint16_t)port);

	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 ||
	    listen(fd, 1) < 0) {
		fprintf(stderr, "Remote: cannot bind port %d\n", port);
		close(fd);
		return;
	}

	set_nonblock(fd);
	g_listen_fd = fd;
	Fl::add_fd((FL_SOCKET)fd, FL_READ, listen_cb, nullptr);
	fprintf(stderr, "Remote: listening on port %d\n", port);
}

void remote_shutdown(void)
{
	if (g_client_fd >= 0) { Fl::remove_fd((FL_SOCKET)g_client_fd); close(g_client_fd); g_client_fd = -1; }
	if (g_listen_fd >= 0) { Fl::remove_fd((FL_SOCKET)g_listen_fd); close(g_listen_fd); g_listen_fd = -1; }
}
