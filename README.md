# dreamulator

> **Disclaimer:** The code in this repository is 100% AI slop.  The "author"
> has never opened any of the source files to see what horrors lie within.

A standalone emulator for the Nakajima ES-2xx series electronic typewriters,
sold as the NTS DreamWriter, Walther ES-210, Dator 3000, and others.

Built from scratch using the [MAME](https://www.mamedev.org/) nakajies driver
as a hardware reference and [VirtualT](https://sourceforge.net/projects/virtualt/)
as a frontend reference.  The emulation core, peripheral devices, and frontend
are all original code.

## Supported Models

| Model | Description | RAM | LCD | PC Card | Floppy |
|-------|-------------|-----|-----|---------|--------|
| `wales210` | Walther ES-210 | 128 KB | 8 lines | Yes | — |
| `dw325` | DreamWriter 325 | 128 KB | 8 lines | Yes | — |
| `dator3k` | Dator 3000 | 128 KB | 8 lines | Yes | — |
| `es210_es` | Nakajima ES-210 (ES) | 128 KB | 8 lines | Yes | — |
| `dwT100` | DreamWriter T100 | 128 KB | 8 lines | — | — |
| `dwT400` | DreamWriter T400 | 256 KB | 8 lines | Yes | — |
| `dw450` | DreamWriter 450 | 256 KB | 8 lines | Yes | — |
| `dwT200` | DreamWriter T200 | 256 KB | 16 lines | Yes | 1.44 MB |

## Quick Start

```sh
mkdir build && cd build
cmake ..
cmake --build .
./dreamulator dwT400
```

ROM files go in a `roms/` directory.  The emulator identifies them by CRC32
and selects the correct model automatically.  Run with no arguments to list
available ROMs.

## Usage

```
dreamulator [model | rom] [options]

Options:
  --model NAME      Machine model (default: auto-detect)
  --bios VER        BIOS version (e.g. v3.1, v2.1)
  --rom FILE        Explicit ROM file path
  --romdir PATH     ROM search directory (default: ./roms)
  --pccard FILE     PC Card SRAM image
  --floppy FILE     Floppy disk image (T200 only)
  --serial DEV      UART via serial device (/dev/cuau0, /dev/ttyUSB0)
  --tcp PORT        UART via TCP
  --lpt DEV         Centronics via lpt device
  --ppi DEV         Centronics via ppi device (FreeBSD)
  --remote PORT     Enable remote debug control
```

## Emulated Hardware

- **CPU:** NEC V20HL (8086/80186 + V20 extensions) at 9.83 MHz with
  cycle-accurate instruction timing
- **Memory:** Banked ROM/RAM/PC Card with 8 × 128 KB windows
- **Display:** 480 × 64 (8-line) or 480 × 128 (16-line) monochrome LCD
- **Keyboard:** 10 × 8 matrix with scan timer and IRQ
- **RTC:** Ricoh RP5C01 with all 4 register modes, initialized from host clock
- **UART:** i8251 with full programming state machine; PTY, TCP, and serial
  device backends with baud rate and modem signal support
- **Centronics:** Parallel port with file, lpt, and ppi (FreeBSD) backends
- **Beeper:** Square wave via PortAudio (optional)
- **FDC:** N82077AA with firmware shim layer for DreamWriter T200
- **PC Card:** PCMCIA SRAM slot, mmap-backed images up to 1 MB
- **NVRAM:** Battery-backed RAM via mmap — always persistent, no save needed
- **Power:** Graceful shutdown triggers firmware save/suspend before exit

## GUI

Built with [FLTK](https://www.fltk.org/).  The menu bar provides runtime
control:

- **File** — Quit (with firmware-driven graceful shutdown)
- **Machine** — Power button, reset, battery status toggles
- **Media** — Insert/eject/create PC Card and floppy images, printer output
- **Serial** — Connect PTY, TCP, or serial device; disconnect
- **Speed** — Normal, double, half, unthrottled
- **Debug** — CPU registers, disassembly, memory editor, peripheral monitor

## Debugging

### GUI Debug Windows

| Window | Shortcut | Features |
|--------|----------|----------|
| CPU Registers | Ctrl+R | View/edit registers and flags, 8 breakpoints, step/run/stop (F5–F8), instruction trace |
| Disassembly | Ctrl+D | Live 8086/80186/V20 disassembly with hex dump and PC marker |
| Memory Editor | Ctrl+M | Hex/ASCII dump at any 20-bit physical or seg:off address |
| Peripherals | Ctrl+P | Serial TX/RX log, parallel output log, I/O port read/write log |

### Remote Control

Start with `--remote PORT`, connect with `nc localhost PORT`:

```
$ nc localhost 9999
dreamulator remote control. Type 'help' for commands.
> status
dwT400 PC=C000:320B RUNNING
> regs
AX=6074 BX=C772 CX=0000 DX=7A4A SI=0006 DI=7D4C
BP=0FC2 SP=0FBC CS=C000 DS=0000 ES=0CEF SS=0000 IP=320B
FLAGS=0206 [IP]
> dis C000:0000 5
C000:0000  CLI
C000:0001  MOV AL, 01h
C000:0003  OUT 16h, AL
C000:0005  MOV AL, 00h
C000:0007  OUT 17h, AL
> stop
Stopped at C000:320B
> step
Stepped to C000:320D
> run
Running
```

Commands: `help`, `status`, `regs`, `reg`, `mem`, `wmem`, `dis`, `bp`, `cbp`,
`lbp`, `run`, `stop`, `step`, `reset`, `key`, `io`, `wio`, `quit`.

## Dependencies

- **FLTK 1.3+** (required)
- **PortAudio** (optional — builds without audio if not found)
- **CMake 3.14+**

## Keyboard

The DreamWriter keyboard maps to standard PC keys.  Special keys:

| DreamWriter | PC Key |
|-------------|--------|
| CAN | Escape |
| ORGN | Page Up |
| WP | Page Down |
| BACK | Backspace |
| Power On/Off | End |

## Serial Port / DreamLink

The UART supports three backends for serial communication:

- **PTY** (default) — allocates a pseudo-terminal; connect with `cu` or `minicom`
- **TCP** (`--tcp PORT`) — rate-transparent byte passthrough
- **Serial device** (`--serial /dev/cuau0`) — real RS-232 with native baud rate
  and modem control signals

The DreamLink file transfer protocol operates at 9600 baud, 8N1.

## License

BSD-3-Clause.  See [LICENSE](LICENSE) for details.

The emulation design is informed by the MAME nakajies driver (BSD-3-Clause,
by Wilbert Pol, Sandro Ronco, and others).  Debugging tools are inspired by
VirtualT (BSD-2-Clause, by Ken Pettit and Stephen Hurd).

No code was copied from either project; the implementation is original.

- [MAME nakajies driver](https://github.com/mamedev/mame/blob/master/src/mame/nakajima/nakajies.cpp)
- [VirtualT](https://sourceforge.net/projects/virtualt/)

The ROM files in `roms/` are firmware images from Nakajima/NTS hardware.
Their license status is unknown.  If you are a rights holder and would like
them removed, please open an issue or contact the maintainer.
