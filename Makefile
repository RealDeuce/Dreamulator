CC ?= cc
CXX ?= c++
CFLAGS = -std=c11 -Wall -Wextra -O2 -g
CXXFLAGS = -std=c++11 -Wall -Wextra -O2 -g $(shell fltk-config --cxxflags)
CXXFLAGS += $(shell pkg-config --cflags portaudio-2.0)
LDFLAGS = $(shell fltk-config --ldflags) $(shell pkg-config --libs portaudio-2.0)

C_SRCS = src/v20.c src/machine.c src/uart.c src/fdc.c
CXX_SRCS = src/main.cpp
C_OBJS = $(C_SRCS:.c=.o)
CXX_OBJS = $(CXX_SRCS:.cpp=.o)
OBJS = $(C_OBJS) $(CXX_OBJS)
BIN = dreamulator

$(BIN): $(OBJS)
	$(CXX) -o $@ $(OBJS) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(BIN)

.PHONY: clean
