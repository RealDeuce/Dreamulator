// license:BSD-3-Clause
// copyright-holders:Stephen Hurd, MAMEDev (smf, Robbbert)
#ifndef UART_H
#define UART_H

#include <stdint.h>
#include <stdbool.h>

enum { UART_PTY = 0, UART_TCP = 1, UART_SERIAL = 2 };

typedef void (*uart_signal_fn)(void *ctx, bool state);

typedef struct uart uart_t;

struct uart {
	enum { I8251_NEXT_MODE, I8251_NEXT_SYNC1, I8251_NEXT_SYNC2,
	       I8251_NEXT_CMD } prog_state;
	uint8_t  mode;
	uint8_t  command;
	uint8_t  status;

	uint8_t  tx_data;
	uint8_t  rx_data;
	bool     tx_pending;

	int      br_factor;
	int      baud_divider;
	int      char_cycles;
	int      tx_timer;
	int      poll_timer;

	bool     cts;
	bool     dsr;
	bool     prev_txrdy;
	bool     prev_rxrdy;

	int      backend;
	int      fd;
	int      listen_fd;
	char     path[256];

	uart_signal_fn txrdy_cb;
	uart_signal_fn rxrdy_cb;
	void    *cb_ctx;
};

int     uart_init(uart_t *u, int backend, int tcp_port, const char *serial_path);
void    uart_reset(uart_t *u);
void    uart_destroy(uart_t *u);
uint8_t uart_data_read(uart_t *u);
void    uart_data_write(uart_t *u, uint8_t data);
uint8_t uart_status_read(uart_t *u);
void    uart_control_write(uart_t *u, uint8_t data);
void    uart_set_baud(uart_t *u, int divider);
void    uart_tick(uart_t *u, int cycles);

#endif
