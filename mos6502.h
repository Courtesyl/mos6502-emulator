#ifndef MOS6502_H
#define MOS6502_H

#include <stdint.h>
#include <stdbool.h>

//register status flags

typedef enum {
    FLAG_CARRY  = (1 << 0),
    FLAG_ZERO   = (1 << 1),
    FLAG_INTERRUPT  = (1 << 2),
    FLAG_DECIMAL  = (1 << 3),
    FLAG_BREAK  = (1 << 4),
    FLAG_UNUSED   = (1 << 5),
    FLAG_OVERFLOW  = (1 << 6),
    FLAG_NEGATIVE  = (1 << 7),
} mos6502_flags;

// cpu struct

typedef struct {
    uint8_t a;
    uint8_t x;
    uint8_t y;

    uint8_t stkp;
    uint16_t pc;

    uint8_t status;

    uint32_t cycles;
}mos6502_t;

void mos6502_reset(mos6502_t *cpu);

void mos6502_clock(mos6502_t *cpu);

void mos6502_irq(mos6502_t *cpu);

void mos6502_nmi(mos6502_t *cpu);

uint8_t mos6502_read(uint16_t address);
void mos6502_write(uint16_t address, uint8_t data);

#endif //MOS6502_H
