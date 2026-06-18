CC = gcc
CFLAGS = -Wall -Wextra -O2 -g
LDFLAGS =

MINGW_DIR = C:/msys64/mingw64
SDL_CFLAGS = -I$(MINGW_DIR)/include/SDL2
SDL_LIBS = -L$(MINGW_DIR)/lib -lmingw32 -lSDL2main -lSDL2

INC = -Isrc -Isrc/cpu -Isrc/ppu -Isrc/bus -Isrc/cartridge -Isrc/apu
SRCDIR = src
BUILDDIR = build

SRCS = $(SRCDIR)/main.c $(SRCDIR)/cpu/mos6502.c $(SRCDIR)/ppu/ppu.c $(SRCDIR)/bus/bus.c $(SRCDIR)/cartridge/cartridge.c $(SRCDIR)/apu/apu.c $(SRCDIR)/apu/biquad.c
OBJS = $(SRCS:$(SRCDIR)/%.c=$(BUILDDIR)/%.o)
TARGET = $(BUILDDIR)/mos.exe

.PHONY: all clean

all: $(TARGET)

$(BUILDDIR)/%.o: $(SRCDIR)/%.c
	@if not exist $(subst /,\,$(@D)) mkdir $(subst /,\,$(@D))
	$(CC) $(CFLAGS) $(INC) $(SDL_CFLAGS) -c -o $@ $<

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(SDL_LIBS)

clean:
	-if exist build rmdir /s /q build 1>nul 2>nul
