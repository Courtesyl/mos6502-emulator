#include <stddef.h>
#include "bus.h"
#include "apu.h"
#include "ppu.h"
#include "mos6502.h"

static uint8_t cpu_ram[2048];
static cartridge_t *current_cart = NULL;
static nes_ppu_t *current_ppu = NULL;
static mos6502_t *current_cpu = NULL;
static apu_t *current_apu = NULL;

// Controller state
static uint8_t controller_buttons[2] = {0, 0};
static uint8_t controller_shift[2] = {0, 0};
static bool controller_strobe[2] = {false, false};

// OAM DMA
static bool dma_active = false;
static uint8_t dma_page = 0;
static uint8_t dma_offset = 0;

void bus_init(cartridge_t *cart, nes_ppu_t *ppu, apu_t *apu) {
    current_cart = cart;
    current_ppu = ppu;
    current_apu = apu;
    current_cpu = NULL;
    for (int i = 0; i < 2048; i++) cpu_ram[i] = 0x00;
    ppu->mirroring_mode = cart->vertical_mirroring ? 1 : 0;
}

void bus_set_cpu(mos6502_t *cpu) {
    current_cpu = cpu;
}

void bus_set_controller_state(uint8_t controller, uint8_t state) {
    if (controller < 2) {
        controller_buttons[controller] = state;
    }
}

bool bus_is_dma_active(void) {
    return dma_active;
}

void bus_process_dma(void) {
    if (!dma_active) return;

    uint16_t src_addr = ((uint16_t)dma_page << 8) | dma_offset;
    uint8_t data = bus_read(src_addr);
    current_ppu->oam[current_ppu->oamaddr] = data;
    current_ppu->oamaddr++;

    dma_offset++;
    if (dma_offset == 0) {
        dma_active = false;
    }
}

uint8_t bus_read(uint16_t address) {
    if (address <= 0x1FFF) {
        return cpu_ram[address & 0x07FF];
    }
    if (address >= 0x2000 && address <= 0x3FFF) {
        return ppu_register_read(current_ppu, address);
    }
    if (address >= 0x4000 && address <= 0x4013) {
        return apu_read(current_apu, address);
    }
    if (address == 0x4015) {
        return apu_read(current_apu, address);
    }
    if (address >= 0x4016 && address <= 0x4017) {
        uint8_t idx = address - 0x4016;
        uint8_t data = (controller_shift[idx] & 0x01) ? 0x01 : 0x00;
        if (!controller_strobe[idx]) {
            controller_shift[idx] = 0x80 | (controller_shift[idx] >> 1);
        }
        if (address == 0x4017)
            return data | 0x40;
        return data;
    }
    if (address >= 0x8000) {
        if (current_cart != NULL && current_cart->prg_rom != NULL) {
            return cartridge_read_prg(current_cart, address);
        }
    }
    return 0x00;
}

void bus_write(uint16_t address, uint8_t data) {
    if (address <= 0x1FFF) {
        cpu_ram[address & 0x07FF] = data;
        return;
    }
    if (address >= 0x2000 && address <= 0x3FFF) {
        ppu_register_write(current_ppu, address, data);
        return;
    }
    if (address >= 0x4000 && address <= 0x4013) {
        apu_write(current_apu, address, data);
        return;
    }
    if (address == 0x4015 || address == 0x4017) {
        apu_write(current_apu, address, data);
        return;
    }
    if (address == 0x4014) {
        dma_page = data;
        dma_offset = 0;
        dma_active = true;
        if (current_cpu) current_cpu->cycles += 513;
        return;
    }
    if (address == 0x4016) {
        bool strobe = (data & 0x01) != 0;
        controller_strobe[0] = strobe;
        controller_strobe[1] = strobe;
        if (strobe) {
            controller_shift[0] = controller_buttons[0];
            controller_shift[1] = controller_buttons[1];
        }
        return;
    }
    // Mapper register writes
    if (address >= 0x4020 && current_cart != NULL) {
        cartridge_write_prg(current_cart, address, data);
        if (current_cart->mapper_id == 7 && current_ppu != NULL) {
            current_ppu->mirroring_mode = current_cart->aorom_mirroring ? 2 : 0;
        }
        if (current_cart->mapper_id == 1 && current_ppu != NULL && (address & 0xE000) == 0x8000) {
            static const int mmc1_mirror[4] = {2, 2, 1, 0};
            current_ppu->mirroring_mode = mmc1_mirror[current_cart->mmc1_control & 0x03];
        }
        return;
    }
}
