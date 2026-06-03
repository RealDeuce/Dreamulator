// license:BSD-3-Clause
// copyright-holders:Stephen Hurd, MAMEDev (smf, Robbbert)
#include "uart.h"
#include "dbg_periph.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <errno.h>
#include <poll.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define CPU_CLK   (19660000 / 2)
#define BASE_CLK  (19200 * 16)
#define POLL_INTERVAL 4096

#define ST_TXRDY  0x01
#define ST_RXRDY  0x02
#define ST_TXE    0x04
#define ST_PE     0x08
#define ST_OE     0x10
#define ST_FE     0x20
#define ST_SYNDET 0x40
#define ST_DSR    0x80

static void set_nonblock(int fd)
{
	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
}

static int open_pty(uart_t *u)
{
	int master = posix_openpt(O_RDWR | O_NOCTTY);
	if (master < 0) return -1;
	if (grantpt(master) < 0 || unlockpt(master) < 0)
		{ close(master); return -1; }

	char *name = ptsname(master);
	if (!name) { close(master); return -1; }
	snprintf(u->path, sizeof(u->path), "%s", name);

	set_nonblock(master);

	struct termios t;
	if (tcgetattr(master, &t) == 0) {
		cfmakeraw(&t);
		cfsetispeed(&t, B9600);
		cfsetospeed(&t, B9600);
		t.c_cflag |= CLOCAL;
		tcsetattr(master, TCSANOW, &t);
	}

	u->fd = master;
	u->listen_fd = -1;
	fprintf(stderr, "UART: %s\n", u->path);
	return 0;
}

static int open_tcp(uart_t *u, int port)
{
	int sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0) return -1;

	int opt = 1;
	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr.sin_port = htons((uint16_t)port);

	if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0 ||
	    listen(sock, 1) < 0) {
		close(sock);
		return -1;
	}

	set_nonblock(sock);
	u->listen_fd = sock;
	u->fd = -1;
	snprintf(u->path, sizeof(u->path), "tcp://127.0.0.1:%d", port);
	fprintf(stderr, "UART: %s\n", u->path);
	return 0;
}

static int open_serial(uart_t *u, const char *path)
{
	int fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
	if (fd < 0) return -1;

	struct termios t;
	if (tcgetattr(fd, &t) == 0) {
		cfmakeraw(&t);
		cfsetispeed(&t, B9600);
		cfsetospeed(&t, B9600);
		t.c_cflag |= CLOCAL | CREAD;
		tcsetattr(fd, TCSANOW, &t);
	}

	snprintf(u->path, sizeof(u->path), "%s", path);
	u->fd = fd;
	u->listen_fd = -1;
	fprintf(stderr, "UART: %s\n", u->path);
	return 0;
}

static void update_char_cycles(uart_t *u)
{
	int clk = BASE_CLK >> u->baud_divider;
	int baud = u->br_factor ? clk / u->br_factor : clk;
	if (baud <= 0) baud = 9600;
	u->char_cycles = CPU_CLK * 10 / baud;
}

static bool tx_enabled(uart_t *u)
{
	return (u->command & 0x01) && u->cts;
}

static void fire_txrdy(uart_t *u)
{
	bool cur = tx_enabled(u) && (u->status & ST_TXRDY);
	if (cur != u->prev_txrdy) {
		u->prev_txrdy = cur;
		if (u->txrdy_cb) u->txrdy_cb(u->cb_ctx, cur);
	}
}

static void fire_rxrdy(uart_t *u)
{
	bool cur = !!(u->status & ST_RXRDY);
	if (cur != u->prev_rxrdy) {
		u->prev_rxrdy = cur;
		if (u->rxrdy_cb) u->rxrdy_cb(u->cb_ctx, cur);
	}
}

static void modem_out(uart_t *u)
{
	if (u->backend == UART_TCP || u->fd < 0) return;

	int bits = 0;
	if (!(u->command & 0x20)) bits |= TIOCM_RTS;
	if (!(u->command & 0x02)) bits |= TIOCM_DTR;
	ioctl(u->fd, TIOCMSET, &bits);
}

static void modem_in(uart_t *u)
{
	if (u->backend == UART_TCP || u->fd < 0) return;

	int bits = 0;
	if (ioctl(u->fd, TIOCMGET, &bits) == 0) {
		u->cts = !!(bits & TIOCM_CTS);
		u->dsr = !!(bits & TIOCM_DSR);
	}
}

/* ---- public API ---- */

int uart_init(uart_t *u, int backend, int tcp_port, const char *serial_path)
{
	memset(u, 0, sizeof(*u));
	u->backend = backend;
	u->fd = -1;
	u->listen_fd = -1;
	u->cts = true;
	u->dsr = true;
	u->br_factor = 1;

	int rc;
	switch (backend) {
	case UART_TCP:    rc = open_tcp(u, tcp_port); break;
	case UART_SERIAL: rc = open_serial(u, serial_path ? serial_path : "/dev/cuau0"); break;
	default:          rc = open_pty(u); break;
	}

	if (rc < 0)
		fprintf(stderr, "UART: backend init failed\n");

	uart_reset(u);
	return 0;
}

void uart_reset(uart_t *u)
{
	u->prog_state = I8251_NEXT_MODE;
	u->mode = 0;
	u->command = 0;
	u->status = ST_TXRDY | ST_TXE;
	u->tx_pending = false;
	u->tx_timer = 0;
	u->poll_timer = POLL_INTERVAL;
	u->br_factor = 1;
	u->prev_txrdy = false;
	u->prev_rxrdy = false;
	update_char_cycles(u);
}

void uart_destroy(uart_t *u)
{
	if (u->fd >= 0) close(u->fd);
	if (u->listen_fd >= 0) close(u->listen_fd);
	u->fd = -1;
	u->listen_fd = -1;
}

uint8_t uart_data_read(uart_t *u)
{
	u->status &= ~ST_RXRDY;
	fire_rxrdy(u);
	return u->rx_data;
}

void uart_data_write(uart_t *u, uint8_t data)
{
	u->tx_data = data;
	u->tx_pending = true;
	u->status &= ~(ST_TXRDY | ST_TXE);
	u->tx_timer = u->char_cycles;
	fire_txrdy(u);
}

uint8_t uart_status_read(uart_t *u)
{
	uint8_t s = u->status;
	if (!u->dsr) s |= ST_DSR;
	u->status &= ~ST_SYNDET;
	return s;
}

void uart_control_write(uart_t *u, uint8_t data)
{
	switch (u->prog_state) {
	case I8251_NEXT_MODE:
		u->mode = data;
		if ((data & 0x03) == 0) {
			u->br_factor = 1;
			u->prog_state = (data & 0x80)
				? I8251_NEXT_SYNC1
				: I8251_NEXT_SYNC1;
		} else {
			switch (data & 0x03) {
			case 1: u->br_factor = 1;  break;
			case 2: u->br_factor = 16; break;
			case 3: u->br_factor = 64; break;
			}
			u->prog_state = I8251_NEXT_CMD;
		}
		update_char_cycles(u);
		break;

	case I8251_NEXT_SYNC1:
		u->prog_state = (u->mode & 0x80) ? I8251_NEXT_SYNC2 : I8251_NEXT_CMD;
		break;

	case I8251_NEXT_SYNC2:
		u->prog_state = I8251_NEXT_CMD;
		break;

	case I8251_NEXT_CMD:
		if (data & 0x40) { uart_reset(u); return; }
		u->command = data;
		if (data & 0x10)
			u->status &= ~(ST_PE | ST_OE | ST_FE);
		modem_out(u);
		fire_txrdy(u);
		break;
	}
}

void uart_set_baud(uart_t *u, int divider)
{
	if (divider > 4) divider = 4;
	u->baud_divider = divider;
	update_char_cycles(u);

	if (u->backend != UART_TCP && u->fd >= 0) {
		static const speed_t sp[] = { B19200, B9600, B4800, B2400, B1200 };
		struct termios t;
		if (tcgetattr(u->fd, &t) == 0) {
			cfsetispeed(&t, sp[divider]);
			cfsetospeed(&t, sp[divider]);
			tcsetattr(u->fd, TCSANOW, &t);
		}
	}
}

void uart_tick(uart_t *u, int cycles)
{
	/* TCP: accept pending connections */
	if (u->backend == UART_TCP && u->fd < 0 && u->listen_fd >= 0) {
		struct pollfd pf = { .fd = u->listen_fd, .events = POLLIN };
		if (poll(&pf, 1, 0) > 0 && (pf.revents & POLLIN)) {
			int c = accept(u->listen_fd, NULL, NULL);
			if (c >= 0) {
				set_nonblock(c);
				u->fd = c;
				fprintf(stderr, "UART TCP: client connected\n");
			}
		}
	}

	/* TX timer */
	if (u->tx_pending) {
		u->tx_timer -= cycles;
		if (u->tx_timer <= 0) {
			u->tx_pending = false;
			periph_log_serial_tx(u->tx_data);
			if (u->fd >= 0) {
				uint8_t b = u->tx_data;
				ssize_t n = write(u->fd, &b, 1);
				if (n < 0 && (errno == EPIPE || errno == ECONNRESET) &&
				    u->backend == UART_TCP) {
					close(u->fd);
					u->fd = -1;
					fprintf(stderr, "UART TCP: client disconnected\n");
				}
				(void)n;
			}
			u->status |= ST_TXE | ST_TXRDY;
			fire_txrdy(u);
		}
	}

	/* RX poll (throttled) */
	u->poll_timer -= cycles;
	if (u->poll_timer <= 0) {
		u->poll_timer += POLL_INTERVAL;

		/* modem input lines */
		modem_in(u);
		fire_txrdy(u);

		/* receive data */
		if (u->fd >= 0 && (u->command & 0x04)) {
			struct pollfd pf = { .fd = u->fd, .events = POLLIN };
			if (poll(&pf, 1, 0) > 0 && (pf.revents & POLLIN)) {
				uint8_t b;
				ssize_t n = read(u->fd, &b, 1);
				if (n == 1) {
					if (u->status & ST_RXRDY)
						u->status |= ST_OE;
					u->rx_data = b;
					periph_log_serial_rx(b);
					u->status |= ST_RXRDY;
					fire_rxrdy(u);
				} else if (n == 0 && u->backend == UART_TCP) {
					close(u->fd);
					u->fd = -1;
					fprintf(stderr, "UART TCP: client disconnected\n");
				}
			}
		}
	}
}
