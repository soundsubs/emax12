# emax12 -- Schwung audio_fx module
# Native build (for testing DSP on your dev machine) and the target
# binary name expected by the install script.
#
# Move's Cortex-A72 is aarch64 -- for the actual on-device binary you
# need an ARM64 cross-compile, not this native build. See scripts/build.sh
# for that path (Docker-based, matching the pattern schwung-rex uses).

# emax12 -- Schwung audio_fx module
# Native build (for testing DSP + plugin loading on your dev machine) and
# the target shared-library name Schwung's chain host expects.
#
# Move's Cortex-A72 is aarch64 -- for the actual on-device binary you
# need an ARM64 cross-compile, not this native build. See scripts/build.sh
# for that path (Docker-based cross-compile to a .so).

CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra -std=c11 -D_POSIX_C_SOURCE=200809L -fPIC
LDLIBS  := -lm

SRC := src/emax_dsp.c src/emax_audio_fx.c
OBJ := $(SRC:.c=.o)
LIB := Emax_FX.so

.PHONY: all clean test

all: $(LIB)

$(LIB): $(OBJ)
	$(CC) $(CFLAGS) -shared -o $@ $(OBJ) $(LDLIBS)

%.o: %.c src/emax_dsp.h src/audio_fx_api_v2.h
	$(CC) $(CFLAGS) -c -o $@ $<

# Headless DSP correctness check -- no plugin-loading dependency, just
# runs the signal chain over a synthetic sine sweep and reports basic
# sanity numbers (peak level, no NaN/Inf). Useful before touching the
# plugin wrapper at all.
test: src/emax_dsp.c src/emax_dsp.h test/test_dsp.c
	$(CC) -O2 -Wall -Wextra -std=c11 -D_POSIX_C_SOURCE=200809L -o test_dsp src/emax_dsp.c test/test_dsp.c -lm
	./test_dsp

clean:
	rm -f $(OBJ) $(LIB) test_dsp
