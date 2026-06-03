CC ?= cc
CFLAGS = -std=c11 -Wall -Wextra -O2 -g
CFLAGS += $(shell pkg-config --cflags sdl2)
LDFLAGS = $(shell pkg-config --libs sdl2)

SRCS = src/main.c src/v20.c src/machine.c
OBJS = $(SRCS:.c=.o)
BIN = dreamulator

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(BIN)

.PHONY: clean
