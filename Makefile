# Host side: the engine's tests, the WAV renderer, the patch packer and the app's test.
# The FM-1 firmware itself, its simulator and its browser emulator build with fm1-base: ./build.sh (README.md).
CC ?= cc
# -ffp-contract=off: no fused multiply-add, as on the FM-1
CFLAGS ?= -O2 -std=c99 -Wall -Wextra -ffp-contract=off -Iapp/dsp
ENGINE = app/dsp/s1_synth.c app/dsp/s1_fx.c app/dsp/s1_patch.c app/dsp/s1_tables.c
HEADERS = $(wildcard app/dsp/*.h) $(wildcard host/*.h)
# fm1-base, for its plat.h (the app's test)
BASE ?= base

all: build/s1render build/sy1pack

build/s1render: host/render.c $(ENGINE) $(HEADERS)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ host/render.c $(ENGINE) -lm

build/sy1pack: host/sy1pack.c app/dsp/s1_patch.c app/dsp/s1_tables.c $(HEADERS)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ host/sy1pack.c app/dsp/s1_patch.c app/dsp/s1_tables.c

build/s1test: host/test.c $(ENGINE) $(HEADERS)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ host/test.c $(ENGINE) -lm

build/apptest: host/app_test.c app/app.c app/app.h app/bank.h app/category.h $(ENGINE) $(HEADERS)
	@mkdir -p build
	$(CC) $(CFLAGS) -Wno-unused-function -Wno-unused-parameter -Iapp -I$(BASE)/include -o $@ host/app_test.c $(ENGINE) -lm

# the engine must stay float-only: no double anywhere (the FM-1 has no double FPU, and fm1-base refuses it)
check-float:
	$(CC) $(CFLAGS) -Wdouble-promotion -Werror -fsyntax-only $(ENGINE)

test: check-float build/s1test
	build/s1test

# the app against a stand-in for the FM-1 (host/app_test.c); needs fm1-base's plat.h: make test-app BASE=...
test-app: build/apptest
	build/apptest

clean:
	rm -rf build

.PHONY: all test test-app test-web clean check-float

# the emulator page's patch reader against the C one (node)
test-web: build/sy1pack
	node host/web_loader_test.mjs
