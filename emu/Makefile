CC ?= cc
CFLAGS ?= -O2 -g -Wall -Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable
CORE = snes/spc.c snes/dsp.c snes/apu.c snes/cpu.c snes/dma.c snes/ppu.c snes/cart.c snes/input.c \
       snes/statehandler.c snes/snes.c snes/snes_other.c snes/dsp1.c

# Recompiled code (generated from your ROM by tools/recomp.py) is linked in when present.
RECOMP_GEN := $(wildcard ../recomp/gen/bank_*.c)
ifneq ($(RECOMP_GEN),)
RC_OVR := $(filter-out ../recomp/overrides/_%,$(wildcard ../recomp/overrides/*.c))
RC_SRC = ../recomp/recomp.c $(RECOMP_GEN) $(RC_OVR)
RC_FLAGS = -DWITH_RECOMP -I ../recomp -I ../recomp/gen
RC_OBJ = $(patsubst ../recomp/%.c,obj/%.o,$(RC_SRC))
endif

all: smktrace smkplay

obj/%.o: ../recomp/%.c ../recomp/recomp_rt.h ../recomp/smk.h snes/cpu_core.inc snes/cpu.h
	@mkdir -p $(dir $@)
	$(CC) -O1 -g0 -Wno-unused-label -I snes $(RC_FLAGS) -c $< -o $@

smktrace: smktrace.c $(CORE) snes/*.h $(RC_OBJ)
	$(CC) $(CFLAGS) -I snes $(RC_FLAGS) -o $@ smktrace.c $(CORE) $(RC_OBJ) -lm

clean:
	rm -rf smktrace smktrace.exe smkplay smkplay.exe obj objwin

smkplay: smkplay.c netplay.c netplay.h hd2d.c hd2d.h options.c options.h font5x7.h $(CORE) snes/*.h $(RC_OBJ)
	$(CC) $(CFLAGS) -I snes $(RC_FLAGS) `sdl2-config --cflags` -o $@ smkplay.c netplay.c hd2d.c options.c $(CORE) $(RC_OBJ) `sdl2-config --libs` -lm

# Windows cross-build (mingw-w64 + SDL2 mingw devel package)
WINCC ?= x86_64-w64-mingw32-gcc
SDL2WIN ?= ../../ext/SDL2-2.30.8/x86_64-w64-mingw32
RC_OBJW = $(patsubst ../recomp/%.c,objwin/%.o,$(RC_SRC))
objwin/%.o: ../recomp/%.c ../recomp/recomp_rt.h ../recomp/smk.h snes/cpu_core.inc snes/cpu.h
	@mkdir -p $(dir $@)
	$(WINCC) -O1 -g0 -Wno-unused-label -I snes $(RC_FLAGS) -c $< -o $@
win: smkplay.exe smktrace.exe
smkplay.exe: smkplay.c netplay.c netplay.h hd2d.c hd2d.h options.c options.h font5x7.h $(CORE) snes/*.h $(RC_OBJW)
	$(WINCC) -O2 -I snes $(RC_FLAGS) -I $(SDL2WIN)/include/SDL2 -Dmain=SDL_main -o $@ smkplay.c netplay.c hd2d.c options.c $(CORE) $(RC_OBJW) \
	  -L $(SDL2WIN)/lib -lmingw32 -lSDL2main -lSDL2 -lws2_32 -mwindows -static-libgcc -lm
smktrace.exe: smktrace.c $(CORE) snes/*.h $(RC_OBJW)
	$(WINCC) -O2 -I snes $(RC_FLAGS) -o $@ smktrace.c $(CORE) $(RC_OBJW) -static -lm
