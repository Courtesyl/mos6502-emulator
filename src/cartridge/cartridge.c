#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cartridge.h"

bool cartridge_load(cartridge_t *cart, const char *filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        printf("error: could not open file %s\n", filename);
        return false;
    }

    if (fread(&cart->header, sizeof(iNES_header_t), 1, f) != 1) {
        printf("error: failed to read header\n");
        fclose(f);
        return false;
    }

    if (memcmp(cart->header.name, "NES\x1A", 4) != 0) {
        printf("error: invalid ines format signature\n");
        fclose(f);
        return false;
    }

    cart->prg_rom_size = cart->header.prg_rom_chunks * 16384;
    cart->chr_rom_size = cart->header.chr_rom_chunks * 8192;

    if (cart->header.flags6 & 0x04) {
        fseek(f, 512, SEEK_CUR);
        printf("trainer skipped (512 bytes)\n");
    }

    if (cart->prg_rom_size > 0) {
        cart->prg_rom = (uint8_t*)malloc(cart->prg_rom_size);
        fread(cart->prg_rom, 1, cart->prg_rom_size, f);
    } else {
        cart->prg_rom = NULL;
    }

    if (cart->chr_rom_size > 0) {
        cart->chr_rom = (uint8_t*)malloc(cart->chr_rom_size);
        fread(cart->chr_rom, 1, cart->chr_rom_size, f);
        cart->chr_ram_enabled = false;
    } else {
        cart->chr_rom = NULL;
        cart->chr_ram_enabled = true;
        memset(cart->chr_ram, 0, sizeof(cart->chr_ram));
    }

    cart->mapper_id = (cart->header.flags7 & 0xF0) | ((cart->header.flags6 & 0xF0) >> 4);
    cart->vertical_mirroring = (cart->header.flags6 & 0x01) != 0;

    // Init mapper state
    cart->mapper_shift = 0;
    cart->mapper_shift_count = 0;
    cart->mmc1_control = 0x0C;
    cart->mmc1_prg_bank = 0;
    cart->mmc1_chr_bank0 = 0;
    cart->mmc1_chr_bank1 = 0;
    cart->uxrom_prg_bank = 0;
    cart->cnrom_chr_bank = 0;
    cart->aorom_prg_bank = (cart->prg_rom_size / 0x8000) - 1;
    cart->aorom_mirroring = false;

    printf("cartridge %s loaded: prg=%dkb chr=%dkb mapper=%d\n",
        filename, cart->prg_rom_size / 1024, cart->chr_rom_size / 1024, cart->mapper_id);

    fclose(f);
    return true;
}

void cartridge_free(cartridge_t *cart) {
    if (cart->prg_rom) free(cart->prg_rom);
    if (cart->chr_rom) free(cart->chr_rom);
}

uint8_t cartridge_read_prg(cartridge_t *cart, uint16_t addr) {
    addr &= 0x7FFF;
    uint32_t index;

    switch (cart->mapper_id) {
        case 0: // NROM
            if (cart->prg_rom_size <= 16384) {
                index = (addr - 0x4000) & (cart->prg_rom_size - 1);
                if (addr >= 0x4000) index = addr - 0x4000;
                else return cart->prg_rom[index & (cart->prg_rom_size - 1)];
            }
            return cart->prg_rom[addr & (cart->prg_rom_size - 1)];

        case 1: { // MMC1
            int last_mmc1_bank = (cart->prg_rom_size / 16384) - 1;
            if (addr < 0x4000) {
                // 0x8000-0xBFFF: switchable
                int prg_mode = (cart->mmc1_control >> 2) & 0x03;
                if (prg_mode <= 1) {
                    // 32KB mode
                    index = (addr & 0x7FFF) | ((cart->mmc1_prg_bank & 0x0E) << 14);
                } else if (prg_mode == 2) {
                    // Fixed first, switchable second
                    if (addr < 0x4000) index = addr;
                    else index = (addr & 0x3FFF) | (cart->mmc1_prg_bank << 14);
                } else {
                    // Switchable first, fixed last
                    if (addr >= 0x4000) index = (addr & 0x3FFF) | (last_mmc1_bank << 14);
                    else index = (addr & 0x3FFF) | (cart->mmc1_prg_bank << 14);
                }
            } else {
                // 0xC000-0xFFFF: fixed or switchable depending on mode
                if ((cart->mmc1_control & 0x0C) == 0x0C) index = (addr & 0x3FFF) | (last_mmc1_bank << 14);
                else if ((cart->mmc1_control & 0x0C) == 0x08) index = (addr & 0x3FFF) | (cart->mmc1_prg_bank << 14);
                else index = (addr & 0x7FFF) | ((cart->mmc1_prg_bank & 0x0E) << 14);
            }
            return cart->prg_rom[index % cart->prg_rom_size];
        }

        case 2: // UxROM
            if (addr < 0x4000)
                return cart->prg_rom[(cart->uxrom_prg_bank * 0x4000 + (addr & 0x3FFF)) % cart->prg_rom_size];
            else
                return cart->prg_rom[(((cart->prg_rom_size / 0x4000) - 1) * 0x4000 + (addr & 0x3FFF)) % cart->prg_rom_size];

        case 3: // CNROM
            return cart->prg_rom[addr & (cart->prg_rom_size - 1)];

        case 7: // AOROM
            return cart->prg_rom[(cart->aorom_prg_bank * 0x8000 + (addr & 0x7FFF)) % cart->prg_rom_size];

        default: // NROM fallback
            if (cart->prg_rom_size <= 16384 && addr >= 0x4000) addr &= 0x3FFF;
            return cart->prg_rom[addr % cart->prg_rom_size];
    }
}

void cartridge_write_prg(cartridge_t *cart, uint16_t addr, uint8_t data) {
    if (cart->mapper_id == 1 && addr < 0x8000) return;
    addr &= 0x7FFF;

    switch (cart->mapper_id) {
        case 1: { // MMC1
            if (data & 0x80) {
                cart->mapper_shift = 0;
                cart->mapper_shift_count = 0;
                cart->mmc1_control |= 0x0C;
                return;
            }
            cart->mapper_shift >>= 1;
            cart->mapper_shift |= (data & 0x01) << 4;
            cart->mapper_shift_count++;
            if (cart->mapper_shift_count < 5) return;

            uint8_t val = cart->mapper_shift;
            cart->mapper_shift = 0;
            cart->mapper_shift_count = 0;

            if (addr < 0x2000) {
                // 0x8000-0x9FFF: Control
                cart->mmc1_control = val & 0x0F;
            } else if (addr < 0x4000) {
                // 0xA000-0xBFFF: CHR bank 0
                cart->mmc1_chr_bank0 = val;
            } else if (addr < 0x6000) {
                // 0xC000-0xDFFF: CHR bank 1
                cart->mmc1_chr_bank1 = val;
            } else {
                // 0xE000-0xFFFF: PRG bank
                cart->mmc1_prg_bank = val & 0x0F;
            }
            break;
        }

        case 2: // UxROM
            cart->uxrom_prg_bank = data & 0x0F;
            break;

        case 3: // CNROM
            cart->cnrom_chr_bank = data & 0x03;
            break;

        case 7: { // AOROM
            cart->aorom_prg_bank = data & 0x07;
            cart->aorom_mirroring = (data >> 4) & 0x01;
            break;
        }
    }
}

uint8_t cartridge_read_chr(cartridge_t *cart, uint16_t addr) {
    if (addr >= 0x2000) return 0;

    if (cart->chr_ram_enabled) {
        return cart->chr_ram[addr & 0x1FFF];
    }

    if (cart->chr_rom == NULL) return 0;

    switch (cart->mapper_id) {
        case 1: { // MMC1
            int chr_mode = (cart->mmc1_control >> 4) & 0x01;
            uint32_t index;
            if (chr_mode == 0) {
                // 8KB mode
                index = (cart->mmc1_chr_bank0 & 0x1E) * 0x1000 + (addr & 0x1FFF);
            } else {
                // 4KB mode
                if (addr < 0x1000)
                    index = cart->mmc1_chr_bank0 * 0x1000 + (addr & 0x0FFF);
                else
                    index = cart->mmc1_chr_bank1 * 0x1000 + (addr & 0x0FFF);
            }
            return cart->chr_rom[index % cart->chr_rom_size];
        }

        case 3: // CNROM
            return cart->chr_rom[(cart->cnrom_chr_bank * 0x2000 + (addr & 0x1FFF)) % cart->chr_rom_size];

        default: // NROM, UxROM
            return cart->chr_rom[addr % cart->chr_rom_size];
    }
}

void cartridge_write_chr(cartridge_t *cart, uint16_t addr, uint8_t data) {
    if (addr >= 0x2000) return;
    if (cart->chr_ram_enabled) {
        cart->chr_ram[addr & 0x1FFF] = data;
    }
}
