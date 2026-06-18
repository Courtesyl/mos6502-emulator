#ifndef BUS_H
#define BUS_H

#include <stdint.h>
#include "cartridge.h"
typedef struct nes_ppu_t nes_ppu_t;
typedef struct mos6502_t mos6502_t;
typedef struct apu_t apu_t;

void bus_init(cartridge_t *cart, nes_ppu_t *ppu, apu_t *apu);
void bus_set_cpu(mos6502_t *cpu);

uint8_t bus_read(uint16_t address);
void bus_write(uint16_t address, uint8_t data);

// Controller API
void bus_set_controller_state(uint8_t controller, uint8_t state);

// OAM DMA
bool bus_is_dma_active(void);
void bus_process_dma(void);

#endif
