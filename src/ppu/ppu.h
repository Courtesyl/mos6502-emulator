#ifndef PPU_H
#define PPU_H

#include <stdint.h>
#include <stdbool.h>
#include "cartridge.h"

#define PPU_OAM_SIZE 256
#define SCREEN_WIDTH 256
#define SCREEN_HEIGHT 240

typedef struct nes_ppu_t {
    uint8_t vram[2048];
    uint8_t palette_vram[32];
    uint8_t oam[PPU_OAM_SIZE];

    // Registers
    uint8_t ppuctrl;
    uint8_t ppumask;
    uint8_t ppustatus;
    uint8_t oamaddr;

    // Internal PPU address/scroll registers
    uint16_t v;
    uint16_t t;
    uint8_t fine_x;
    uint8_t w;

    // Nametable mirroring mode
    // 0 = horizontal, 1 = vertical, 2 = one-screen lower
    int mirroring_mode;

    // PPUDATA read buffer
    uint8_t read_buffer;

    // NMI
    bool nmi_pending;

    // Timing
    int scanline;
    int dot;
    bool frame_complete;

    // Framebuffer (rendered pixel data)
    uint32_t framebuffer[SCREEN_WIDTH * SCREEN_HEIGHT];

    // Scroll snapshot for compatibility
    uint16_t scroll_snapshot_t;
    uint8_t scroll_snapshot_fine_x;

    // Secondary OAM for sprite evaluation (up to 8 sprites per scanline)
    uint8_t secondary_oam[32];
    int sprite_count;
    bool sprite_zero_hit_possible;

    // Cartridge pointer for CHR RAM access
    struct cartridge_t *cart;

    // Odd-frame flag (for PPU dot-skip on pre-render scanline)
    bool odd_frame;
} nes_ppu_t;

void ppu_init(nes_ppu_t *ppu);
void ppu_clock(nes_ppu_t *ppu, cartridge_t *cart);

uint8_t ppu_register_read(nes_ppu_t *ppu, uint16_t address);
void ppu_register_write(nes_ppu_t *ppu, uint16_t address, uint8_t data);

uint8_t ppu_read_vram(nes_ppu_t *ppu, uint16_t addr);
void ppu_write_vram(nes_ppu_t *ppu, uint16_t addr, uint8_t data);

#endif
