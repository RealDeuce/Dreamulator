// license:BSD-3-Clause
// copyright-holders:Stephen Hurd, MAMEDev (smf, Robbbert)
#ifndef UART_H
#define UART_H

#include <cstdint>
#include <string>

enum class UartBackend { Pty, Tcp, Serial };
enum class I8251State  { NextMode, NextSync1, NextSync2, NextCmd };

using uart_signal_fn = void (*)(void *ctx, bool state);

struct uart_t {
	I8251State prog_state = I8251State::NextMode;
	uint8_t  mode = 0;
	uint8_t  command = 0;
	uint8_t  status = 0;

	uint8_t  tx_data = 0;
	uint8_t  rx_data = 0;
	bool     tx_pending = false;

	int      br_factor = 0;
	int      baud_divider = 0;
	int      char_cycles = 0;
	int      tx_timer = 0;
	int      poll_timer = 0;

	bool     cts = false;
	bool     dsr = false;
	bool     prev_txrdy = false;
	bool     prev_rxrdy = false;

	UartBackend backend = UartBackend::Pty;
	int      fd = -1;
	int      listen_fd = -1;
	std::string path;

	uart_signal_fn txrdy_cb = nullptr;
	uart_signal_fn rxrdy_cb = nullptr;
	void    *cb_ctx = nullptr;
};

[[nodiscard]] int     uart_init(uart_t *u, UartBackend backend, int tcp_port, const char *serial_path);
void    uart_reset(uart_t *u);
void    uart_destroy(uart_t *u);
uint8_t uart_data_read(uart_t *u);
void    uart_data_write(uart_t *u, uint8_t data);
uint8_t uart_status_read(uart_t *u);
void    uart_control_write(uart_t *u, uint8_t data);
void    uart_set_baud(uart_t *u, int divider);
void    uart_tick(uart_t *u, int cycles);

#endif
