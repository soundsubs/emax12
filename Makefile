# emax12 -- Schwung audio_fx module
# Native build (for testing DSP on your dev machine) and the target
# binary name expected by the install script.
#
# Move's Cortex-A72 is aarch64 -- for the actual on-device binary you
# need an ARM64 cross-compile, not this native build. See scripts/build.sh
# for that path (Docker-based, matching the pattern schwung-rex uses).

CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra -std=c11 -D_POSIX_C_SOURCE=200809L
LDLIBS  := -ljack -lm

SRC := src/emax_dsp.c src/main_jack.c
OBJ := $(SRC:.c=.o)
BIN := emax12

.PHONY: all clean test

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDLIBS)

%.o: %.c src/emax_dsp.h
	$(CC) $(CFLAGS) -c -o $@ $<

# Headless DSP correctness check -- no JACK dependency, just runs the
# signal chain over a synthetic sine sweep and reports basic sanity
# numbers (peak level, no NaN/Inf). Useful before you even touch a JACK
# server.
test: src/emax_dsp.c src/emax_dsp.h test/test_dsp.c
	$(CC) $(CFLAGS) -o test_dsp src/emax_dsp.c test/test_dsp.c -lm
	./test_dsp

clean:
	rm -f $(OBJ) $(BIN) test_dsp
