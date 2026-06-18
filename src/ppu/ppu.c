#include <stdio.h>
#include "ppu.h"
#include <string.h>

#define MIRROR_HORIZONTAL 0
#define MIRROR_VERTICAL 1
#define MIRROR_ONESCREEN_LOWER 2

static const uint32_t nes_palette[64] = {
    0x545454FF, 0x001E74FF, 0x081090FF, 0x300088FF,
    0x440064FF, 0x5C0030FF, 0x540400FF, 0x3C1800FF,
    0x202A00FF, 0x083A00FF, 0x004000FF, 0x003C00FF,
    0x00323CFF, 0x000000FF, 0x000000FF, 0x000000FF,
    0x989698FF, 0x084CC4FF, 0x3032ECFF, 0x5C1EE4FF,
    0x8814B0FF, 0xA01464FF, 0x982220FF, 0x783C00FF,
    0x545A00FF, 0x287200FF, 0x087C00FF, 0x007628FF,
    0x006678FF, 0x000000FF, 0x000000FF, 0x000000FF,
    0xECEEECFF, 0x3CBCFCFF, 0x8888FCFF, 0xB878F8FF,
    0xE064D8FF, 0xF86498FF, 0xF87044FF, 0xE48A00FF,
    0xB6A800FF, 0x7CC000FF, 0x4CCE00FF, 0x34C844FF,
    0x24B49CFF, 0x000000FF, 0x000000FF, 0x000000FF,
    0xFCFCFCFF, 0xA4E4FCFF, 0xC4C4FCFF, 0xDCBCFCFF,
    0xF4B4F4FF, 0xFCB4D0FF, 0xFCBCA0FF, 0xF0C878FF,
    0xDAD658FF, 0xB0E034FF, 0x88EC30FF, 0x76E67CFF,
    0x68DAC4FF, 0x000000FF, 0x000000FF, 0x000000FF,
};

static uint32_t color_from_pal(nes_ppu_t *ppu, uint8_t pal_entry) {
    return nes_palette[ppu->palette_vram[pal_entry & 0x1F] & 0x3F];
}

void ppu_init(nes_ppu_t *ppu) {
    memset(ppu->vram, 0, sizeof(ppu->vram));
    memset(ppu->palette_vram, 0, sizeof(ppu->palette_vram));
    memset(ppu->oam, 0, sizeof(ppu->oam));
    memset(ppu->framebuffer, 0, sizeof(ppu->framebuffer));

    ppu->ppuctrl = 0;
    ppu->ppumask = 0;
    ppu->ppustatus = 0;
    ppu->oamaddr = 0;

    ppu->v = 0;
    ppu->t = 0;
    ppu->fine_x = 0;
    ppu->w = 0;

    ppu->read_buffer = 0;
    ppu->nmi_pending = false;

    ppu->scanline = 0;
    ppu->dot = 0;
    ppu->frame_complete = false;
    ppu->scroll_snapshot_t = 0;
    ppu->scroll_snapshot_fine_x = 0;

    ppu->sprite_count = 0;
    ppu->sprite_zero_hit_possible = false;
    ppu->odd_frame = false;
}

static uint16_t mirror_nametable(nes_ppu_t *ppu, uint16_t addr) {
    switch (ppu->mirroring_mode) {
        case MIRROR_VERTICAL:
            return addr & 0x07FF;
        case MIRROR_ONESCREEN_LOWER:
            return addr & 0x03FF;
        default: // MIRROR_HORIZONTAL
            return (addr & 0x03FF) | ((addr & 0x0800) >> 1);
    }
}

uint8_t ppu_read_vram(nes_ppu_t *ppu, uint16_t addr) {
    addr &= 0x3FFF;
    if (addr < 0x2000 && ppu->cart) {
        return cartridge_read_chr(ppu->cart, addr);
    }
    if (addr >= 0x2000 && addr <= 0x3EFF)
        return ppu->vram[mirror_nametable(ppu, addr)];
    if (addr >= 0x3F00 && addr <= 0x3FFF) {
        uint16_t pal_addr = addr & 0x001F;
        if (pal_addr >= 0x10 && (pal_addr & 0x03) == 0) pal_addr &= 0x0F;
        return ppu->palette_vram[pal_addr];
    }
    return 0;
}

void ppu_write_vram(nes_ppu_t *ppu, uint16_t addr, uint8_t data) {
    addr &= 0x3FFF;
    if (addr < 0x2000 && ppu->cart) {
        cartridge_write_chr(ppu->cart, addr, data);
        return;
    }
    if (addr >= 0x2000 && addr <= 0x3EFF) {
        ppu->vram[mirror_nametable(ppu, addr)] = data;
        return;
    }
    if (addr >= 0x3F00 && addr <= 0x3FFF) {
        uint16_t pal_addr = addr & 0x001F;
        if (pal_addr >= 0x10 && (pal_addr & 0x03) == 0) pal_addr &= 0x0F;
        ppu->palette_vram[pal_addr] = data;
        return;
    }
}

void ppu_register_write(nes_ppu_t *ppu, uint16_t address, uint8_t data) {
    uint16_t reg = address & 0x0007;

    switch (reg) {
        case 0x0000: { // PPUCTRL
            uint8_t old = ppu->ppuctrl;
            ppu->ppuctrl = data;
            ppu->t = (ppu->t & 0xF3FF) | ((uint16_t)(data & 0x03) << 10);
            if (!(old & 0x80) && (data & 0x80) && (ppu->ppustatus & 0x80))
                ppu->nmi_pending = true;
            break;
        }

        case 0x0001: // PPUMASK
            ppu->ppumask = data;
            break;

        case 0x0003: // OAMADDR
            ppu->oamaddr = data;
            break;

        case 0x0004: // OAMDATA
            ppu->oam[ppu->oamaddr] = data;
            ppu->oamaddr++;
            break;

        case 0x0005: // PPUSCROLL
            if (ppu->w == 0) {
                ppu->t = (ppu->t & 0xFFE0) | (data >> 3);
                ppu->fine_x = data & 0x07;
                ppu->w = 1;
            } else {
                ppu->t = (ppu->t & 0x8C1F) | ((data & 0x07) << 12) | ((data & 0xF8) << 2);
                ppu->w = 0;
            }
            break;

        case 0x0006: // PPUADDR
            if (ppu->w == 0) {
                ppu->t = (ppu->t & 0x80FF) | ((uint16_t)(data & 0x3F) << 8);
                ppu->w = 1;
            } else {
                ppu->t = (ppu->t & 0x7F00) | data;
                ppu->v = ppu->t;
                ppu->w = 0;
            }
            break;

        case 0x0007: // PPUDATA
            ppu_write_vram(ppu, ppu->v, data);
            ppu->v += (ppu->ppuctrl & 0x04) ? 32 : 1;
            break;
    }
}

uint8_t ppu_register_read(nes_ppu_t *ppu, uint16_t address) {
    uint16_t reg = address & 0x0007;

    switch (reg) {
        case 0x0002: {
            uint8_t data = ppu->ppustatus;
            ppu->ppustatus &= ~0x80;
            ppu->w = 0;
            ppu->nmi_pending = false;
            return data;
        }

        case 0x0004:
            return ppu->oam[ppu->oamaddr];

        case 0x0007: {
            uint8_t data = ppu->read_buffer;
            ppu->read_buffer = ppu_read_vram(ppu, ppu->v);
            if (ppu->v >= 0x3F00)
                data = ppu->read_buffer;
            ppu->v += (ppu->ppuctrl & 0x04) ? 32 : 1;
            return data;
        }
    }
    return ppu->v >> 8;
}

// --- Decode tile pixels from CHR ---
static void decode_tile(cartridge_t *cart, uint16_t tile_addr, uint8_t out[8][8]) {
    for (int row = 0; row < 8; row++) {
        uint8_t b1 = cartridge_read_chr(cart, tile_addr + row);
        uint8_t b2 = cartridge_read_chr(cart, tile_addr + row + 8);
        for (int col = 0; col < 8; col++) {
            uint8_t p1 = (b1 >> (7 - col)) & 1;
            uint8_t p2 = (b2 >> (7 - col)) & 1;
            out[row][col] = (p2 << 1) | p1;
        }
    }
}

// --- Scroll helpers ---
static void inc_vert_scroll(nes_ppu_t *ppu) {
    if (!(ppu->ppumask & 0x18)) return;
    uint16_t fy = (ppu->v >> 12) & 7;
    uint16_t cy = (ppu->v >> 5) & 0x1F;
    fy++;
    if (fy < 8) {
        ppu->v = (ppu->v & 0x8FFF) | (fy << 12);
    } else {
        fy = 0;
        ppu->v = (ppu->v & 0x8FFF) | (fy << 12);
        cy++;
        if (cy < 30) {
            ppu->v = (ppu->v & 0xFC1F) | (cy << 5);
        } else {
            cy = 0;
            ppu->v = (ppu->v & 0xFC1F) | (cy << 5);
            ppu->v ^= 0x0800;
        }
    }
}

static void reload_horiz(nes_ppu_t *ppu) {
    if (!(ppu->ppumask & 0x18)) return;
    ppu->v = (ppu->v & 0x7BE0) | (ppu->t & 0x041F);
}

static void reload_vert(nes_ppu_t *ppu) {
    if (!(ppu->ppumask & 0x18)) return;
    ppu->v = (ppu->v & 0x841F) | (ppu->t & 0x7BE0);
}

// --- Evaluate sprites for a given scanline into secondary OAM ---
static void eval_sprites(nes_ppu_t *ppu, int scanline) {
    ppu->sprite_count = 0;
    ppu->sprite_zero_hit_possible = false;
    bool s8x16 = (ppu->ppuctrl & 0x20) != 0;
    int h = s8x16 ? 16 : 8;

    for (int i = 0; i < 64; i++) {
        int sy = ppu->oam[i * 4 + 0] + 1;
        if (sy > scanline || sy + h <= scanline) continue;
        if (ppu->sprite_count >= 8) break;
        int idx = ppu->sprite_count * 4;
        memcpy(&ppu->secondary_oam[idx], &ppu->oam[i * 4], 4);
        if (i == 0) ppu->sprite_zero_hit_possible = true;
        ppu->sprite_count++;
    }
}

// --- Render sprites for one scanline, blending onto the rendered background ---
static void render_sprite_scanline(nes_ppu_t *ppu, cartridge_t *cart, int scanline, uint32_t *fb_line) {
    if (!(ppu->ppumask & 0x10)) return;
    bool s8x16 = (ppu->ppuctrl & 0x20) != 0;
    uint16_t pt = (ppu->ppuctrl & 0x08) ? 0x1000 : 0x0000;
    uint32_t trans = color_from_pal(ppu, 0);

    for (int i = ppu->sprite_count - 1; i >= 0; i--) {
        int idx = i * 4;
        int sy = ppu->secondary_oam[idx + 0] + 1;
        int tile_idx = ppu->secondary_oam[idx + 1];
        uint8_t attr = ppu->secondary_oam[idx + 2];
        int sx = ppu->secondary_oam[idx + 3];
        int pal = attr & 3;
        bool priority = (attr & 0x20) != 0;
        bool flip_h = (attr & 0x40) != 0;
        bool flip_v = (attr & 0x80) != 0;

        if (sx >= 256) continue;

        uint16_t tile_addr;
        int actual_tile = tile_idx;
        int tile_row = scanline - sy;
        if (s8x16) {
            int bank = tile_idx & 1;
            actual_tile = tile_idx & 0xFE;
            tile_addr = (bank ? 0x1000 : 0x0000) + actual_tile * 16;
            if (tile_row >= 8) {
                tile_addr += 16;
                tile_row -= 8;
            }
        } else {
            tile_addr = pt + tile_idx * 16;
        }
        if (tile_row < 0 || tile_row >= 8) continue;

        if (flip_v) tile_row = 7 - tile_row;

        uint8_t tile_buf[8][8];
        decode_tile(cart, tile_addr, tile_buf);

        bool is_sprite_zero = (i == 0 && ppu->sprite_zero_hit_possible);

        for (int col = 0; col < 8; col++) {
            int x = sx + col;
            if (x >= 256) break;
            int src_col = flip_h ? (7 - col) : col;
            uint8_t ci = tile_buf[tile_row][src_col];
            if (ci == 0) continue;

            uint32_t bg = fb_line[x];
            if (is_sprite_zero && (ppu->ppumask & 0x18) == 0x18 && bg != trans && ci != 0)
                ppu->ppustatus |= 0x40;

            if (!priority || bg == trans)
                fb_line[x] = color_from_pal(ppu, 0x10 + pal * 4 + ci);
        }
    }
}

// --- Render one complete scanline (32 tiles + carry) ---
static void render_scanline(nes_ppu_t *ppu, cartridge_t *cart, int scanline) {
    if (!(ppu->ppumask & 0x08) && !(ppu->ppumask & 0x10)) return;

    uint32_t *fb_line = &ppu->framebuffer[scanline * 256];
    uint32_t trans = color_from_pal(ppu, 0);

    if (!(ppu->ppumask & 0x08)) {
        for (int x = 0; x < 256; x++) fb_line[x] = trans;
    } else {
        // Background rendering
        int coarse_x = ppu->v & 0x1F;
        int fine_x_local = ppu->fine_x;
        int nt = (ppu->v >> 10) & 3;
        uint16_t coarse_y = (ppu->v >> 5) & 0x1F;
        uint16_t fine_y = (ppu->v >> 12) & 7;
        uint16_t bg_pt = (ppu->ppuctrl & 0x10) ? 0x1000 : 0x0000;

        // We need to render (coarse_x + fine_x spanning 256 pixels)
        // which may require up to 33 tiles due to fine_x offset
        // Render pixels starting from 0 to 255
        int pixel = 0;
        int cur_cx = coarse_x;
        int cur_nt_x = nt & 1;
        int cur_nt = nt;

        while (pixel < 256) {
            uint16_t nt_addr = 0x2000 + cur_nt * 0x0400;
            uint16_t tda = nt_addr + coarse_y * 32 + cur_cx;
            uint8_t tile_id = ppu_read_vram(ppu, tda);
            uint8_t tile_buf[8][8];
            decode_tile(cart, bg_pt + tile_id * 16, tile_buf);

            uint16_t at_addr = nt_addr + 0x03C0 + (coarse_y / 4) * 8 + (cur_cx / 4);
            uint8_t ab = ppu_read_vram(ppu, at_addr);
            int shift = ((coarse_y / 2) % 2 * 2 + (cur_cx / 2) % 2) * 2;
            uint8_t pal_quad = (ab >> shift) & 3;

            for (int col = fine_x_local; col < 8 && pixel < 256; col++) {
                uint8_t ci = tile_buf[fine_y][col];
                fb_line[pixel++] = (ci == 0) ? trans : color_from_pal(ppu, pal_quad * 4 + ci);
            }
            fine_x_local = 0;

            cur_cx++;
            if (cur_cx == 32) {
                cur_cx = 0;
                cur_nt_x ^= 1;
                cur_nt = (cur_nt & 2) | cur_nt_x;
            }
        }
    }

    // Render sprites on top
    render_sprite_scanline(ppu, cart, scanline, fb_line);
}

void ppu_clock(nes_ppu_t *ppu, cartridge_t *cart) {
    // Odd-frame: skip dot 0 on pre-render scanline
    if (ppu->scanline == 261 && ppu->dot == 0 && ppu->odd_frame) {
        ppu->dot = 1;
    }

    // --- VBlank flag & NMI ---
    if (ppu->scanline == 241 && ppu->dot == 1) {
        ppu->ppustatus |= 0x80;
        if (ppu->ppuctrl & 0x80)
            ppu->nmi_pending = true;
    }

    // --- Pre-render scanline (261) ---
    if (ppu->scanline == 261) {
        if (ppu->dot == 1) {
            ppu->ppustatus &= ~0xE0;
            ppu->nmi_pending = false;
        }
        if (ppu->dot >= 280 && ppu->dot <= 304)
            reload_vert(ppu);
        if (ppu->dot == 256)
            inc_vert_scroll(ppu);
        if (ppu->dot == 257) {
            reload_horiz(ppu);
            eval_sprites(ppu, 0); // evaluate sprites for scanline 0
        }
    }

    // --- Visible scanlines (0-239) ---
    if (ppu->scanline >= 0 && ppu->scanline <= 239) {
        if (ppu->dot == 0) {
            // Render the entire scanline before pixel-accurate processing starts
            render_scanline(ppu, cart, ppu->scanline);
        }
        if (ppu->dot == 256)
            inc_vert_scroll(ppu);
        if (ppu->dot == 257) {
            reload_horiz(ppu);
            // Evaluate sprites for next scanline
            if (ppu->scanline < 239)
                eval_sprites(ppu, ppu->scanline + 1);
        }
    }

    // --- Advance ---
    ppu->dot++;
    if (ppu->dot > 340) {
        ppu->dot = 0;
        ppu->scanline++;
        if (ppu->scanline > 261) {
            ppu->scanline = 0;
            ppu->frame_complete = true;
            ppu->odd_frame = !ppu->odd_frame;
            ppu->scroll_snapshot_t = ppu->t;
            ppu->scroll_snapshot_fine_x = ppu->fine_x;
        }
    }
}
