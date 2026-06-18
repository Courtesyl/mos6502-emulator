#ifndef CARTRIDGE_H
#define CARTRIDGE_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    char name[4];
    uint8_t prg_rom_chunks;
    uint8_t chr_rom_chunks;
    uint8_t flags6;
    uint8_t flags7;
    uint8_t prg_ram_size;
    uint8_t flags9;
    uint8_t flags10;
    uint8_t padding[5];
} iNES_header_t;

typedef struct cartridge_t {
    iNES_header_t header;
    uint8_t *prg_rom;
    uint32_t prg_rom_size;
    uint8_t *chr_rom;
    uint32_t chr_rom_size;

    bool vertical_mirroring;
    uint8_t chr_ram[8192];
    bool chr_ram_enabled;

    uint8_t mapper_id;

    // Mapper state
    uint8_t mapper_shift;
    uint8_t mapper_shift_count;
    // MMC1
    uint8_t mmc1_control;
    uint8_t mmc1_prg_bank;
    uint8_t mmc1_chr_bank0;
    uint8_t mmc1_chr_bank1;
    // UxROM (mapper 2)
    uint8_t uxrom_prg_bank;
    // CNROM (mapper 3)
    uint8_t cnrom_chr_bank;
    // AOROM (mapper 7)
    uint8_t aorom_prg_bank;
    bool aorom_mirroring;
} cartridge_t;

bool cartridge_load(cartridge_t *cart, const char *filename);
void cartridge_free(cartridge_t *cart);

uint8_t cartridge_read_prg(cartridge_t *cart, uint16_t addr);
void cartridge_write_prg(cartridge_t *cart, uint16_t addr, uint8_t data);
uint8_t cartridge_read_chr(cartridge_t *cart, uint16_t addr);
void cartridge_write_chr(cartridge_t *cart, uint16_t addr, uint8_t data);

#endif
