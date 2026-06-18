#include "mos6502.h"
#include "bus.h"


static void set_zn(mos6502_t *cpu, uint8_t val) {
    cpu->status &= ~(FLAG_ZERO | FLAG_NEGATIVE);
    if (val == 0) cpu->status |= FLAG_ZERO;
    if (val & 0x80) cpu->status |= FLAG_NEGATIVE;
}

static uint8_t read_byte(mos6502_t *cpu) {
    return bus_read(cpu->pc++);
}

static uint16_t read_word(mos6502_t *cpu) {
    uint16_t lo = bus_read(cpu->pc++);
    uint16_t hi = bus_read(cpu->pc++);
    return (hi << 8) | lo;
}

static void push(mos6502_t *cpu, uint8_t val) {
    bus_write(0x0100 + cpu->stkp, val);
    cpu->stkp--;
}

static uint8_t pull(mos6502_t *cpu) {
    cpu->stkp++;
    return bus_read(0x0100 + cpu->stkp);
}

static void branch_if(mos6502_t *cpu, bool cond) {
    int8_t offset = (int8_t)read_byte(cpu);
    if (cond) {
        uint16_t old_pc = cpu->pc;
        cpu->pc += offset;
        cpu->cycles += 1;
        if ((old_pc & 0xFF00) != (cpu->pc & 0xFF00))
            cpu->cycles += 1;
    }
    cpu->cycles += 2;
}

static void adc_core(mos6502_t *cpu, uint8_t m) {
    uint16_t sum = cpu->a + m + (cpu->status & FLAG_CARRY ? 1 : 0);
    cpu->status &= ~(FLAG_CARRY | FLAG_ZERO | FLAG_OVERFLOW | FLAG_NEGATIVE);
    if (sum > 0xFF) cpu->status |= FLAG_CARRY;
    if ((sum & 0xFF) == 0) cpu->status |= FLAG_ZERO;
    if (((cpu->a ^ m) & 0x80) == 0 && ((cpu->a ^ (uint8_t)sum) & 0x80) != 0)
        cpu->status |= FLAG_OVERFLOW;
    if (sum & 0x80) cpu->status |= FLAG_NEGATIVE;
    cpu->a = (uint8_t)sum;
}

static void sbc_core(mos6502_t *cpu, uint8_t m) {
    adc_core(cpu, ~m);
}

static uint8_t read_abs_x(mos6502_t *cpu, bool *page_crossed) {
    uint16_t addr = read_word(cpu);
    uint16_t result = addr + cpu->x;
    if (page_crossed) *page_crossed = (addr & 0xFF00) != (result & 0xFF00);
    return bus_read(result);
}

static uint8_t read_abs_y(mos6502_t *cpu, bool *page_crossed) {
    uint16_t addr = read_word(cpu);
    uint16_t result = addr + cpu->y;
    if (page_crossed) *page_crossed = (addr & 0xFF00) != (result & 0xFF00);
    return bus_read(result);
}

static uint8_t read_izx(mos6502_t *cpu) {
    uint8_t z = read_byte(cpu) + cpu->x;
    uint16_t addr = bus_read(z) | (bus_read((uint8_t)(z + 1)) << 8);
    return bus_read(addr);
}

static uint8_t read_izy(mos6502_t *cpu, bool *page_crossed) {
    uint8_t z = read_byte(cpu);
    uint16_t addr = bus_read(z) | (bus_read((uint8_t)(z + 1)) << 8);
    uint16_t result = addr + cpu->y;
    if (page_crossed) *page_crossed = (addr & 0xFF00) != (result & 0xFF00);
    return bus_read(result);
}

void mos6502_reset(mos6502_t *cpu) {
    cpu->a = 0;
    cpu->x = 0;
    cpu->y = 0;
    cpu->stkp = 0xFD;
    cpu->status = FLAG_UNUSED;
    cpu->pc = bus_read(0xFFFC) | (bus_read(0xFFFD) << 8);
    cpu->cycles = 7;
}

void mos6502_irq(mos6502_t *cpu) {
    if (cpu->status & FLAG_INTERRUPT) return;
    push(cpu, (uint8_t)(cpu->pc >> 8));
    push(cpu, (uint8_t)(cpu->pc));
    push(cpu, cpu->status & ~FLAG_BREAK);
    cpu->status |= FLAG_INTERRUPT;
    cpu->pc = bus_read(0xFFFE) | (bus_read(0xFFFF) << 8);
    cpu->cycles = 7;
}

void mos6502_nmi(mos6502_t *cpu) {
    push(cpu, (uint8_t)(cpu->pc >> 8));
    push(cpu, (uint8_t)(cpu->pc));
    push(cpu, cpu->status & ~FLAG_BREAK);
    cpu->status |= FLAG_INTERRUPT;
    cpu->pc = bus_read(0xFFFA) | (bus_read(0xFFFB) << 8);
    cpu->cycles = 7;
}

uint8_t mos6502_read(uint16_t address) {
    return bus_read(address);
}

void mos6502_write(uint16_t address, uint8_t data) {
    bus_write(address, data);
}

void mos6502_clock(mos6502_t *cpu, bool *nmi_pending) {
    if (cpu->cycles > 0) {
        cpu->cycles--;
        return;
    }

    if (*nmi_pending) {
        *nmi_pending = false;
        mos6502_nmi(cpu);
        return;
    }

    uint8_t opcode = bus_read(cpu->pc++);

    switch (opcode) {

    // ---- ADC ----
    case 0x69: { // ADC immediate
        uint8_t m = read_byte(cpu);
        adc_core(cpu, m);
        cpu->cycles = 2;
        break;
    }
    case 0x65: { // ADC zero page
        uint8_t addr = read_byte(cpu);
        adc_core(cpu, bus_read(addr));
        cpu->cycles = 3;
        break;
    }
    case 0x75: { // ADC zero page,X
        uint8_t addr = read_byte(cpu) + cpu->x;
        adc_core(cpu, bus_read(addr));
        cpu->cycles = 4;
        break;
    }
    case 0x6D: { // ADC absolute
        adc_core(cpu, bus_read(read_word(cpu)));
        cpu->cycles = 4;
        break;
    }
    case 0x7D: { // ADC absolute,X
        bool crossed = false;
        uint8_t m = read_abs_x(cpu, &crossed);
        adc_core(cpu, m);
        cpu->cycles = 4;
        if (crossed) cpu->cycles++;
        break;
    }
    case 0x79: { // ADC absolute,Y
        bool crossed = false;
        uint8_t m = read_abs_y(cpu, &crossed);
        adc_core(cpu, m);
        cpu->cycles = 4;
        if (crossed) cpu->cycles++;
        break;
    }
    case 0x61: { // ADC (indirect,X)
        adc_core(cpu, read_izx(cpu));
        cpu->cycles = 6;
        break;
    }
    case 0x71: { // ADC (indirect),Y
        bool crossed = false;
        uint8_t m = read_izy(cpu, &crossed);
        adc_core(cpu, m);
        cpu->cycles = 5;
        if (crossed) cpu->cycles++;
        break;
    }

    // ---- AND ----
    case 0x29: {
        cpu->a &= read_byte(cpu);
        set_zn(cpu, cpu->a);
        cpu->cycles = 2;
        break;
    }
    case 0x25: {
        cpu->a &= bus_read(read_byte(cpu));
        set_zn(cpu, cpu->a);
        cpu->cycles = 3;
        break;
    }
    case 0x35: {
        cpu->a &= bus_read(read_byte(cpu) + cpu->x);
        set_zn(cpu, cpu->a);
        cpu->cycles = 4;
        break;
    }
    case 0x2D: {
        cpu->a &= bus_read(read_word(cpu));
        set_zn(cpu, cpu->a);
        cpu->cycles = 4;
        break;
    }
    case 0x3D: {
        bool crossed = false;
        cpu->a &= read_abs_x(cpu, &crossed);
        set_zn(cpu, cpu->a);
        cpu->cycles = 4;
        if (crossed) cpu->cycles++;
        break;
    }
    case 0x39: {
        bool crossed = false;
        cpu->a &= read_abs_y(cpu, &crossed);
        set_zn(cpu, cpu->a);
        cpu->cycles = 4;
        if (crossed) cpu->cycles++;
        break;
    }
    case 0x21: {
        cpu->a &= read_izx(cpu);
        set_zn(cpu, cpu->a);
        cpu->cycles = 6;
        break;
    }
    case 0x31: {
        bool crossed = false;
        cpu->a &= read_izy(cpu, &crossed);
        set_zn(cpu, cpu->a);
        cpu->cycles = 5;
        if (crossed) cpu->cycles++;
        break;
    }

    // ---- ASL ----
    case 0x0A: { // ASL accumulator
        cpu->status &= ~FLAG_CARRY;
        if (cpu->a & 0x80) cpu->status |= FLAG_CARRY;
        cpu->a <<= 1;
        set_zn(cpu, cpu->a);
        cpu->cycles = 2;
        break;
    }
    case 0x06: { // ASL zero page
        uint8_t addr = read_byte(cpu);
        uint8_t val = bus_read(addr);
        cpu->status &= ~FLAG_CARRY;
        if (val & 0x80) cpu->status |= FLAG_CARRY;
        val <<= 1;
        bus_write(addr, val);
        set_zn(cpu, val);
        cpu->cycles = 5;
        break;
    }
    case 0x16: { // ASL zero page,X
        uint8_t addr = read_byte(cpu) + cpu->x;
        uint8_t val = bus_read(addr);
        cpu->status &= ~FLAG_CARRY;
        if (val & 0x80) cpu->status |= FLAG_CARRY;
        val <<= 1;
        bus_write(addr, val);
        set_zn(cpu, val);
        cpu->cycles = 6;
        break;
    }
    case 0x0E: { // ASL absolute
        uint16_t addr = read_word(cpu);
        uint8_t val = bus_read(addr);
        cpu->status &= ~FLAG_CARRY;
        if (val & 0x80) cpu->status |= FLAG_CARRY;
        val <<= 1;
        bus_write(addr, val);
        set_zn(cpu, val);
        cpu->cycles = 6;
        break;
    }
    case 0x1E: { // ASL absolute,X
        uint16_t addr = read_word(cpu) + cpu->x;
        uint8_t val = bus_read(addr);
        cpu->status &= ~FLAG_CARRY;
        if (val & 0x80) cpu->status |= FLAG_CARRY;
        val <<= 1;
        bus_write(addr, val);
        set_zn(cpu, val);
        cpu->cycles = 7;
        break;
    }

    // ---- BCC ----
    case 0x90: {
        branch_if(cpu, !(cpu->status & FLAG_CARRY));
        break;
    }

    // ---- BCS ----
    case 0xB0: {
        branch_if(cpu, cpu->status & FLAG_CARRY);
        break;
    }

    // ---- BEQ ----
    case 0xF0: {
        branch_if(cpu, cpu->status & FLAG_ZERO);
        break;
    }

    // ---- BIT ----
    case 0x24: { // BIT zero page
        uint8_t val = bus_read(read_byte(cpu));
        cpu->status &= ~(FLAG_ZERO | FLAG_OVERFLOW | FLAG_NEGATIVE);
        if ((cpu->a & val) == 0) cpu->status |= FLAG_ZERO;
        if (val & 0x40) cpu->status |= FLAG_OVERFLOW;
        if (val & 0x80) cpu->status |= FLAG_NEGATIVE;
        cpu->cycles = 3;
        break;
    }
    case 0x2C: { // BIT absolute
        uint8_t val = bus_read(read_word(cpu));
        cpu->status &= ~(FLAG_ZERO | FLAG_OVERFLOW | FLAG_NEGATIVE);
        if ((cpu->a & val) == 0) cpu->status |= FLAG_ZERO;
        if (val & 0x40) cpu->status |= FLAG_OVERFLOW;
        if (val & 0x80) cpu->status |= FLAG_NEGATIVE;
        cpu->cycles = 4;
        break;
    }

    // ---- BMI ----
    case 0x30: {
        branch_if(cpu, cpu->status & FLAG_NEGATIVE);
        break;
    }

    // ---- BNE ----
    case 0xD0: {
        branch_if(cpu, !(cpu->status & FLAG_ZERO));
        break;
    }

    // ---- BPL ----
    case 0x10: {
        branch_if(cpu, !(cpu->status & FLAG_NEGATIVE));
        break;
    }

    // ---- BRK ----
    case 0x00: {
        read_byte(cpu);
        push(cpu, (uint8_t)(cpu->pc >> 8));
        push(cpu, (uint8_t)(cpu->pc));
        push(cpu, cpu->status | FLAG_BREAK | FLAG_UNUSED);
        cpu->status |= FLAG_INTERRUPT;
        cpu->pc = bus_read(0xFFFE) | (bus_read(0xFFFF) << 8);
        cpu->cycles = 7;
        break;
    }

    // ---- BVC ----
    case 0x50: {
        branch_if(cpu, !(cpu->status & FLAG_OVERFLOW));
        break;
    }

    // ---- BVS ----
    case 0x70: {
        branch_if(cpu, cpu->status & FLAG_OVERFLOW);
        break;
    }

    // ---- CLC ----
    case 0x18: {
        cpu->status &= ~FLAG_CARRY;
        cpu->cycles = 2;
        break;
    }

    // ---- CLD ----
    case 0xD8: {
        cpu->status &= ~FLAG_DECIMAL;
        cpu->cycles = 2;
        break;
    }

    // ---- CLI ----
    case 0x58: {
        cpu->status &= ~FLAG_INTERRUPT;
        cpu->cycles = 2;
        break;
    }

    // ---- CLV ----
    case 0xB8: {
        cpu->status &= ~FLAG_OVERFLOW;
        cpu->cycles = 2;
        break;
    }

    // ---- CMP ----
    case 0xC9: { // CMP immediate
        uint8_t val = read_byte(cpu);
        uint16_t diff = cpu->a - val;
        cpu->status &= ~(FLAG_CARRY | FLAG_ZERO | FLAG_NEGATIVE);
        if (cpu->a >= val) cpu->status |= FLAG_CARRY;
        if ((diff & 0xFF) == 0) cpu->status |= FLAG_ZERO;
        if (diff & 0x80) cpu->status |= FLAG_NEGATIVE;
        cpu->cycles = 2;
        break;
    }
    case 0xC5: {
        uint8_t val = bus_read(read_byte(cpu));
        uint16_t diff = cpu->a - val;
        cpu->status &= ~(FLAG_CARRY | FLAG_ZERO | FLAG_NEGATIVE);
        if (cpu->a >= val) cpu->status |= FLAG_CARRY;
        if ((diff & 0xFF) == 0) cpu->status |= FLAG_ZERO;
        if (diff & 0x80) cpu->status |= FLAG_NEGATIVE;
        cpu->cycles = 3;
        break;
    }
    case 0xD5: {
        uint8_t val = bus_read(read_byte(cpu) + cpu->x);
        uint16_t diff = cpu->a - val;
        cpu->status &= ~(FLAG_CARRY | FLAG_ZERO | FLAG_NEGATIVE);
        if (cpu->a >= val) cpu->status |= FLAG_CARRY;
        if ((diff & 0xFF) == 0) cpu->status |= FLAG_ZERO;
        if (diff & 0x80) cpu->status |= FLAG_NEGATIVE;
        cpu->cycles = 4;
        break;
    }
    case 0xCD: {
        uint8_t val = bus_read(read_word(cpu));
        uint16_t diff = cpu->a - val;
        cpu->status &= ~(FLAG_CARRY | FLAG_ZERO | FLAG_NEGATIVE);
        if (cpu->a >= val) cpu->status |= FLAG_CARRY;
        if ((diff & 0xFF) == 0) cpu->status |= FLAG_ZERO;
        if (diff & 0x80) cpu->status |= FLAG_NEGATIVE;
        cpu->cycles = 4;
        break;
    }
    case 0xDD: {
        bool crossed = false;
        uint8_t val = read_abs_x(cpu, &crossed);
        uint16_t diff = cpu->a - val;
        cpu->status &= ~(FLAG_CARRY | FLAG_ZERO | FLAG_NEGATIVE);
        if (cpu->a >= val) cpu->status |= FLAG_CARRY;
        if ((diff & 0xFF) == 0) cpu->status |= FLAG_ZERO;
        if (diff & 0x80) cpu->status |= FLAG_NEGATIVE;
        cpu->cycles = 4;
        if (crossed) cpu->cycles++;
        break;
    }
    case 0xD9: {
        bool crossed = false;
        uint8_t val = read_abs_y(cpu, &crossed);
        uint16_t diff = cpu->a - val;
        cpu->status &= ~(FLAG_CARRY | FLAG_ZERO | FLAG_NEGATIVE);
        if (cpu->a >= val) cpu->status |= FLAG_CARRY;
        if ((diff & 0xFF) == 0) cpu->status |= FLAG_ZERO;
        if (diff & 0x80) cpu->status |= FLAG_NEGATIVE;
        cpu->cycles = 4;
        if (crossed) cpu->cycles++;
        break;
    }
    case 0xC1: {
        uint8_t val = read_izx(cpu);
        uint16_t diff = cpu->a - val;
        cpu->status &= ~(FLAG_CARRY | FLAG_ZERO | FLAG_NEGATIVE);
        if (cpu->a >= val) cpu->status |= FLAG_CARRY;
        if ((diff & 0xFF) == 0) cpu->status |= FLAG_ZERO;
        if (diff & 0x80) cpu->status |= FLAG_NEGATIVE;
        cpu->cycles = 6;
        break;
    }
    case 0xD1: {
        bool crossed = false;
        uint8_t val = read_izy(cpu, &crossed);
        uint16_t diff = cpu->a - val;
        cpu->status &= ~(FLAG_CARRY | FLAG_ZERO | FLAG_NEGATIVE);
        if (cpu->a >= val) cpu->status |= FLAG_CARRY;
        if ((diff & 0xFF) == 0) cpu->status |= FLAG_ZERO;
        if (diff & 0x80) cpu->status |= FLAG_NEGATIVE;
        cpu->cycles = 5;
        if (crossed) cpu->cycles++;
        break;
    }

    // ---- CPX ----
    case 0xE0: {
        uint8_t val = read_byte(cpu);
        uint16_t diff = cpu->x - val;
        cpu->status &= ~(FLAG_CARRY | FLAG_ZERO | FLAG_NEGATIVE);
        if (cpu->x >= val) cpu->status |= FLAG_CARRY;
        if ((diff & 0xFF) == 0) cpu->status |= FLAG_ZERO;
        if (diff & 0x80) cpu->status |= FLAG_NEGATIVE;
        cpu->cycles = 2;
        break;
    }
    case 0xE4: {
        uint8_t val = bus_read(read_byte(cpu));
        uint16_t diff = cpu->x - val;
        cpu->status &= ~(FLAG_CARRY | FLAG_ZERO | FLAG_NEGATIVE);
        if (cpu->x >= val) cpu->status |= FLAG_CARRY;
        if ((diff & 0xFF) == 0) cpu->status |= FLAG_ZERO;
        if (diff & 0x80) cpu->status |= FLAG_NEGATIVE;
        cpu->cycles = 3;
        break;
    }
    case 0xEC: {
        uint8_t val = bus_read(read_word(cpu));
        uint16_t diff = cpu->x - val;
        cpu->status &= ~(FLAG_CARRY | FLAG_ZERO | FLAG_NEGATIVE);
        if (cpu->x >= val) cpu->status |= FLAG_CARRY;
        if ((diff & 0xFF) == 0) cpu->status |= FLAG_ZERO;
        if (diff & 0x80) cpu->status |= FLAG_NEGATIVE;
        cpu->cycles = 4;
        break;
    }

    // ---- CPY ----
    case 0xC0: {
        uint8_t val = read_byte(cpu);
        uint16_t diff = cpu->y - val;
        cpu->status &= ~(FLAG_CARRY | FLAG_ZERO | FLAG_NEGATIVE);
        if (cpu->y >= val) cpu->status |= FLAG_CARRY;
        if ((diff & 0xFF) == 0) cpu->status |= FLAG_ZERO;
        if (diff & 0x80) cpu->status |= FLAG_NEGATIVE;
        cpu->cycles = 2;
        break;
    }
    case 0xC4: {
        uint8_t val = bus_read(read_byte(cpu));
        uint16_t diff = cpu->y - val;
        cpu->status &= ~(FLAG_CARRY | FLAG_ZERO | FLAG_NEGATIVE);
        if (cpu->y >= val) cpu->status |= FLAG_CARRY;
        if ((diff & 0xFF) == 0) cpu->status |= FLAG_ZERO;
        if (diff & 0x80) cpu->status |= FLAG_NEGATIVE;
        cpu->cycles = 3;
        break;
    }
    case 0xCC: {
        uint8_t val = bus_read(read_word(cpu));
        uint16_t diff = cpu->y - val;
        cpu->status &= ~(FLAG_CARRY | FLAG_ZERO | FLAG_NEGATIVE);
        if (cpu->y >= val) cpu->status |= FLAG_CARRY;
        if ((diff & 0xFF) == 0) cpu->status |= FLAG_ZERO;
        if (diff & 0x80) cpu->status |= FLAG_NEGATIVE;
        cpu->cycles = 4;
        break;
    }

    // ---- DEC ----
    case 0xC6: {
        uint8_t addr = read_byte(cpu);
        uint8_t val = bus_read(addr) - 1;
        bus_write(addr, val);
        set_zn(cpu, val);
        cpu->cycles = 5;
        break;
    }
    case 0xD6: {
        uint8_t addr = read_byte(cpu) + cpu->x;
        uint8_t val = bus_read(addr) - 1;
        bus_write(addr, val);
        set_zn(cpu, val);
        cpu->cycles = 6;
        break;
    }
    case 0xCE: {
        uint16_t addr = read_word(cpu);
        uint8_t val = bus_read(addr) - 1;
        bus_write(addr, val);
        set_zn(cpu, val);
        cpu->cycles = 6;
        break;
    }
    case 0xDE: {
        uint16_t addr = read_word(cpu) + cpu->x;
        uint8_t val = bus_read(addr) - 1;
        bus_write(addr, val);
        set_zn(cpu, val);
        cpu->cycles = 7;
        break;
    }

    // ---- DEX ----
    case 0xCA: {
        cpu->x--;
        set_zn(cpu, cpu->x);
        cpu->cycles = 2;
        break;
    }

    // ---- DEY ----
    case 0x88: {
        cpu->y--;
        set_zn(cpu, cpu->y);
        cpu->cycles = 2;
        break;
    }

    // ---- EOR ----
    case 0x49: {
        cpu->a ^= read_byte(cpu);
        set_zn(cpu, cpu->a);
        cpu->cycles = 2;
        break;
    }
    case 0x45: {
        cpu->a ^= bus_read(read_byte(cpu));
        set_zn(cpu, cpu->a);
        cpu->cycles = 3;
        break;
    }
    case 0x55: {
        cpu->a ^= bus_read(read_byte(cpu) + cpu->x);
        set_zn(cpu, cpu->a);
        cpu->cycles = 4;
        break;
    }
    case 0x4D: {
        cpu->a ^= bus_read(read_word(cpu));
        set_zn(cpu, cpu->a);
        cpu->cycles = 4;
        break;
    }
    case 0x5D: {
        bool crossed = false;
        cpu->a ^= read_abs_x(cpu, &crossed);
        set_zn(cpu, cpu->a);
        cpu->cycles = 4;
        if (crossed) cpu->cycles++;
        break;
    }
    case 0x59: {
        bool crossed = false;
        cpu->a ^= read_abs_y(cpu, &crossed);
        set_zn(cpu, cpu->a);
        cpu->cycles = 4;
        if (crossed) cpu->cycles++;
        break;
    }
    case 0x41: {
        cpu->a ^= read_izx(cpu);
        set_zn(cpu, cpu->a);
        cpu->cycles = 6;
        break;
    }
    case 0x51: {
        bool crossed = false;
        cpu->a ^= read_izy(cpu, &crossed);
        set_zn(cpu, cpu->a);
        cpu->cycles = 5;
        if (crossed) cpu->cycles++;
        break;
    }

    // ---- INC ----
    case 0xE6: {
        uint8_t addr = read_byte(cpu);
        uint8_t val = bus_read(addr) + 1;
        bus_write(addr, val);
        set_zn(cpu, val);
        cpu->cycles = 5;
        break;
    }
    case 0xF6: {
        uint8_t addr = read_byte(cpu) + cpu->x;
        uint8_t val = bus_read(addr) + 1;
        bus_write(addr, val);
        set_zn(cpu, val);
        cpu->cycles = 6;
        break;
    }
    case 0xEE: {
        uint16_t addr = read_word(cpu);
        uint8_t val = bus_read(addr) + 1;
        bus_write(addr, val);
        set_zn(cpu, val);
        cpu->cycles = 6;
        break;
    }
    case 0xFE: {
        uint16_t addr = read_word(cpu) + cpu->x;
        uint8_t val = bus_read(addr) + 1;
        bus_write(addr, val);
        set_zn(cpu, val);
        cpu->cycles = 7;
        break;
    }

    // ---- INX ----
    case 0xE8: {
        cpu->x++;
        set_zn(cpu, cpu->x);
        cpu->cycles = 2;
        break;
    }

    // ---- INY ----
    case 0xC8: {
        cpu->y++;
        set_zn(cpu, cpu->y);
        cpu->cycles = 2;
        break;
    }

    // ---- JMP ----
    case 0x4C: { // JMP absolute
        cpu->pc = read_word(cpu);
        cpu->cycles = 3;
        break;
    }
    case 0x6C: { // JMP indirect
        uint16_t addr = read_word(cpu);
        uint16_t lo = bus_read(addr);
        uint16_t hi = bus_read((addr & 0xFF00) | ((addr + 1) & 0x00FF));
        cpu->pc = (hi << 8) | lo;
        cpu->cycles = 5;
        break;
    }

    // ---- JSR ----
    case 0x20: {
        uint16_t addr = read_word(cpu);
        push(cpu, (uint8_t)((cpu->pc - 1) >> 8));
        push(cpu, (uint8_t)((cpu->pc - 1)));
        cpu->pc = addr;
        cpu->cycles = 6;
        break;
    }

    // ---- LDA ----
    case 0xA9: {
        cpu->a = read_byte(cpu);
        set_zn(cpu, cpu->a);
        cpu->cycles = 2;
        break;
    }
    case 0xA5: {
        cpu->a = bus_read(read_byte(cpu));
        set_zn(cpu, cpu->a);
        cpu->cycles = 3;
        break;
    }
    case 0xB5: {
        cpu->a = bus_read(read_byte(cpu) + cpu->x);
        set_zn(cpu, cpu->a);
        cpu->cycles = 4;
        break;
    }
    case 0xAD: {
        cpu->a = bus_read(read_word(cpu));
        set_zn(cpu, cpu->a);
        cpu->cycles = 4;
        break;
    }
    case 0xBD: {
        bool crossed = false;
        cpu->a = read_abs_x(cpu, &crossed);
        set_zn(cpu, cpu->a);
        cpu->cycles = 4;
        if (crossed) cpu->cycles++;
        break;
    }
    case 0xB9: {
        bool crossed = false;
        cpu->a = read_abs_y(cpu, &crossed);
        set_zn(cpu, cpu->a);
        cpu->cycles = 4;
        if (crossed) cpu->cycles++;
        break;
    }
    case 0xA1: {
        cpu->a = read_izx(cpu);
        set_zn(cpu, cpu->a);
        cpu->cycles = 6;
        break;
    }
    case 0xB1: {
        bool crossed = false;
        cpu->a = read_izy(cpu, &crossed);
        set_zn(cpu, cpu->a);
        cpu->cycles = 5;
        if (crossed) cpu->cycles++;
        break;
    }

    // ---- LDX ----
    case 0xA2: {
        cpu->x = read_byte(cpu);
        set_zn(cpu, cpu->x);
        cpu->cycles = 2;
        break;
    }
    case 0xA6: {
        cpu->x = bus_read(read_byte(cpu));
        set_zn(cpu, cpu->x);
        cpu->cycles = 3;
        break;
    }
    case 0xB6: {
        cpu->x = bus_read(read_byte(cpu) + cpu->y);
        set_zn(cpu, cpu->x);
        cpu->cycles = 4;
        break;
    }
    case 0xAE: {
        cpu->x = bus_read(read_word(cpu));
        set_zn(cpu, cpu->x);
        cpu->cycles = 4;
        break;
    }
    case 0xBE: {
        uint16_t addr = read_word(cpu);
        uint16_t result = addr + cpu->y;
        bool crossed = (addr & 0xFF00) != (result & 0xFF00);
        cpu->x = bus_read(result);
        set_zn(cpu, cpu->x);
        cpu->cycles = 4;
        if (crossed) cpu->cycles++;
        break;
    }

    // ---- LDY ----
    case 0xA0: {
        cpu->y = read_byte(cpu);
        set_zn(cpu, cpu->y);
        cpu->cycles = 2;
        break;
    }
    case 0xA4: {
        cpu->y = bus_read(read_byte(cpu));
        set_zn(cpu, cpu->y);
        cpu->cycles = 3;
        break;
    }
    case 0xB4: {
        cpu->y = bus_read(read_byte(cpu) + cpu->x);
        set_zn(cpu, cpu->y);
        cpu->cycles = 4;
        break;
    }
    case 0xAC: {
        cpu->y = bus_read(read_word(cpu));
        set_zn(cpu, cpu->y);
        cpu->cycles = 4;
        break;
    }
    case 0xBC: {
        bool crossed = false;
        uint16_t addr = read_word(cpu);
        uint16_t result = addr + cpu->x;
        if ((addr & 0xFF00) != (result & 0xFF00)) crossed = true;
        cpu->y = bus_read(result);
        set_zn(cpu, cpu->y);
        cpu->cycles = 4;
        if (crossed) cpu->cycles++;
        break;
    }

    // ---- LSR ----
    case 0x4A: { // LSR accumulator
        cpu->status &= ~FLAG_CARRY;
        if (cpu->a & 0x01) cpu->status |= FLAG_CARRY;
        cpu->a >>= 1;
        set_zn(cpu, cpu->a);
        cpu->cycles = 2;
        break;
    }
    case 0x46: {
        uint8_t addr = read_byte(cpu);
        uint8_t val = bus_read(addr);
        cpu->status &= ~FLAG_CARRY;
        if (val & 0x01) cpu->status |= FLAG_CARRY;
        val >>= 1;
        bus_write(addr, val);
        set_zn(cpu, val);
        cpu->cycles = 5;
        break;
    }
    case 0x56: {
        uint8_t addr = read_byte(cpu) + cpu->x;
        uint8_t val = bus_read(addr);
        cpu->status &= ~FLAG_CARRY;
        if (val & 0x01) cpu->status |= FLAG_CARRY;
        val >>= 1;
        bus_write(addr, val);
        set_zn(cpu, val);
        cpu->cycles = 6;
        break;
    }
    case 0x4E: {
        uint16_t addr = read_word(cpu);
        uint8_t val = bus_read(addr);
        cpu->status &= ~FLAG_CARRY;
        if (val & 0x01) cpu->status |= FLAG_CARRY;
        val >>= 1;
        bus_write(addr, val);
        set_zn(cpu, val);
        cpu->cycles = 6;
        break;
    }
    case 0x5E: {
        uint16_t addr = read_word(cpu) + cpu->x;
        uint8_t val = bus_read(addr);
        cpu->status &= ~FLAG_CARRY;
        if (val & 0x01) cpu->status |= FLAG_CARRY;
        val >>= 1;
        bus_write(addr, val);
        set_zn(cpu, val);
        cpu->cycles = 7;
        break;
    }

    // ---- NOP ----
    case 0xEA: {
        cpu->cycles = 2;
        break;
    }

    // ---- ORA ----
    case 0x09: {
        cpu->a |= read_byte(cpu);
        set_zn(cpu, cpu->a);
        cpu->cycles = 2;
        break;
    }
    case 0x05: {
        cpu->a |= bus_read(read_byte(cpu));
        set_zn(cpu, cpu->a);
        cpu->cycles = 3;
        break;
    }
    case 0x15: {
        cpu->a |= bus_read(read_byte(cpu) + cpu->x);
        set_zn(cpu, cpu->a);
        cpu->cycles = 4;
        break;
    }
    case 0x0D: {
        cpu->a |= bus_read(read_word(cpu));
        set_zn(cpu, cpu->a);
        cpu->cycles = 4;
        break;
    }
    case 0x1D: {
        bool crossed = false;
        cpu->a |= read_abs_x(cpu, &crossed);
        set_zn(cpu, cpu->a);
        cpu->cycles = 4;
        if (crossed) cpu->cycles++;
        break;
    }
    case 0x19: {
        bool crossed = false;
        cpu->a |= read_abs_y(cpu, &crossed);
        set_zn(cpu, cpu->a);
        cpu->cycles = 4;
        if (crossed) cpu->cycles++;
        break;
    }
    case 0x01: {
        cpu->a |= read_izx(cpu);
        set_zn(cpu, cpu->a);
        cpu->cycles = 6;
        break;
    }
    case 0x11: {
        bool crossed = false;
        cpu->a |= read_izy(cpu, &crossed);
        set_zn(cpu, cpu->a);
        cpu->cycles = 5;
        if (crossed) cpu->cycles++;
        break;
    }

    // ---- PHA ----
    case 0x48: {
        push(cpu, cpu->a);
        cpu->cycles = 3;
        break;
    }

    // ---- PHP ----
    case 0x08: {
        push(cpu, cpu->status | FLAG_BREAK | FLAG_UNUSED);
        cpu->cycles = 3;
        break;
    }

    // ---- PLA ----
    case 0x68: {
        cpu->a = pull(cpu);
        set_zn(cpu, cpu->a);
        cpu->cycles = 4;
        break;
    }

    // ---- PLP ----
    case 0x28: {
        cpu->status = pull(cpu);
        cpu->status |= FLAG_UNUSED;
        cpu->cycles = 4;
        break;
    }

    // ---- ROL ----
    case 0x2A: { // ROL accumulator
        uint8_t old_carry = cpu->status & FLAG_CARRY ? 1 : 0;
        cpu->status &= ~FLAG_CARRY;
        if (cpu->a & 0x80) cpu->status |= FLAG_CARRY;
        cpu->a = (cpu->a << 1) | old_carry;
        set_zn(cpu, cpu->a);
        cpu->cycles = 2;
        break;
    }
    case 0x26: {
        uint8_t addr = read_byte(cpu);
        uint8_t val = bus_read(addr);
        uint8_t old_carry = cpu->status & FLAG_CARRY ? 1 : 0;
        cpu->status &= ~FLAG_CARRY;
        if (val & 0x80) cpu->status |= FLAG_CARRY;
        val = (val << 1) | old_carry;
        bus_write(addr, val);
        set_zn(cpu, val);
        cpu->cycles = 5;
        break;
    }
    case 0x36: {
        uint8_t addr = read_byte(cpu) + cpu->x;
        uint8_t val = bus_read(addr);
        uint8_t old_carry = cpu->status & FLAG_CARRY ? 1 : 0;
        cpu->status &= ~FLAG_CARRY;
        if (val & 0x80) cpu->status |= FLAG_CARRY;
        val = (val << 1) | old_carry;
        bus_write(addr, val);
        set_zn(cpu, val);
        cpu->cycles = 6;
        break;
    }
    case 0x2E: {
        uint16_t addr = read_word(cpu);
        uint8_t val = bus_read(addr);
        uint8_t old_carry = cpu->status & FLAG_CARRY ? 1 : 0;
        cpu->status &= ~FLAG_CARRY;
        if (val & 0x80) cpu->status |= FLAG_CARRY;
        val = (val << 1) | old_carry;
        bus_write(addr, val);
        set_zn(cpu, val);
        cpu->cycles = 6;
        break;
    }
    case 0x3E: {
        uint16_t addr = read_word(cpu) + cpu->x;
        uint8_t val = bus_read(addr);
        uint8_t old_carry = cpu->status & FLAG_CARRY ? 1 : 0;
        cpu->status &= ~FLAG_CARRY;
        if (val & 0x80) cpu->status |= FLAG_CARRY;
        val = (val << 1) | old_carry;
        bus_write(addr, val);
        set_zn(cpu, val);
        cpu->cycles = 7;
        break;
    }

    // ---- ROR ----
    case 0x6A: { // ROR accumulator
        uint8_t old_carry = cpu->status & FLAG_CARRY ? 0x80 : 0;
        cpu->status &= ~FLAG_CARRY;
        if (cpu->a & 0x01) cpu->status |= FLAG_CARRY;
        cpu->a = (cpu->a >> 1) | old_carry;
        set_zn(cpu, cpu->a);
        cpu->cycles = 2;
        break;
    }
    case 0x66: {
        uint8_t addr = read_byte(cpu);
        uint8_t val = bus_read(addr);
        uint8_t old_carry = cpu->status & FLAG_CARRY ? 0x80 : 0;
        cpu->status &= ~FLAG_CARRY;
        if (val & 0x01) cpu->status |= FLAG_CARRY;
        val = (val >> 1) | old_carry;
        bus_write(addr, val);
        set_zn(cpu, val);
        cpu->cycles = 5;
        break;
    }
    case 0x76: {
        uint8_t addr = read_byte(cpu) + cpu->x;
        uint8_t val = bus_read(addr);
        uint8_t old_carry = cpu->status & FLAG_CARRY ? 0x80 : 0;
        cpu->status &= ~FLAG_CARRY;
        if (val & 0x01) cpu->status |= FLAG_CARRY;
        val = (val >> 1) | old_carry;
        bus_write(addr, val);
        set_zn(cpu, val);
        cpu->cycles = 6;
        break;
    }
    case 0x6E: {
        uint16_t addr = read_word(cpu);
        uint8_t val = bus_read(addr);
        uint8_t old_carry = cpu->status & FLAG_CARRY ? 0x80 : 0;
        cpu->status &= ~FLAG_CARRY;
        if (val & 0x01) cpu->status |= FLAG_CARRY;
        val = (val >> 1) | old_carry;
        bus_write(addr, val);
        set_zn(cpu, val);
        cpu->cycles = 6;
        break;
    }
    case 0x7E: {
        uint16_t addr = read_word(cpu) + cpu->x;
        uint8_t val = bus_read(addr);
        uint8_t old_carry = cpu->status & FLAG_CARRY ? 0x80 : 0;
        cpu->status &= ~FLAG_CARRY;
        if (val & 0x01) cpu->status |= FLAG_CARRY;
        val = (val >> 1) | old_carry;
        bus_write(addr, val);
        set_zn(cpu, val);
        cpu->cycles = 7;
        break;
    }

    // ---- RTI ----
    case 0x40: {
        cpu->status = pull(cpu);
        cpu->status |= FLAG_UNUSED;
        uint8_t lo = pull(cpu);
        uint8_t hi = pull(cpu);
        cpu->pc = (hi << 8) | lo;
        cpu->cycles = 6;
        break;
    }

    // ---- RTS ----
    case 0x60: {
        uint8_t lo = pull(cpu);
        uint8_t hi = pull(cpu);
        cpu->pc = ((hi << 8) | lo) + 1;
        cpu->cycles = 6;
        break;
    }

    // ---- SBC ----
    case 0xE9: {
        sbc_core(cpu, read_byte(cpu));
        cpu->cycles = 2;
        break;
    }
    case 0xE5: {
        sbc_core(cpu, bus_read(read_byte(cpu)));
        cpu->cycles = 3;
        break;
    }
    case 0xF5: {
        sbc_core(cpu, bus_read(read_byte(cpu) + cpu->x));
        cpu->cycles = 4;
        break;
    }
    case 0xED: {
        sbc_core(cpu, bus_read(read_word(cpu)));
        cpu->cycles = 4;
        break;
    }
    case 0xFD: {
        bool crossed = false;
        sbc_core(cpu, read_abs_x(cpu, &crossed));
        cpu->cycles = 4;
        if (crossed) cpu->cycles++;
        break;
    }
    case 0xF9: {
        bool crossed = false;
        sbc_core(cpu, read_abs_y(cpu, &crossed));
        cpu->cycles = 4;
        if (crossed) cpu->cycles++;
        break;
    }
    case 0xE1: {
        sbc_core(cpu, read_izx(cpu));
        cpu->cycles = 6;
        break;
    }
    case 0xF1: {
        bool crossed = false;
        sbc_core(cpu, read_izy(cpu, &crossed));
        cpu->cycles = 5;
        if (crossed) cpu->cycles++;
        break;
    }

    // ---- SEC ----
    case 0x38: {
        cpu->status |= FLAG_CARRY;
        cpu->cycles = 2;
        break;
    }

    // ---- SED ----
    case 0xF8: {
        cpu->status |= FLAG_DECIMAL;
        cpu->cycles = 2;
        break;
    }

    // ---- SEI ----
    case 0x78: {
        cpu->status |= FLAG_INTERRUPT;
        cpu->cycles = 2;
        break;
    }

    // ---- STA ----
    case 0x85: {
        bus_write(read_byte(cpu), cpu->a);
        cpu->cycles = 3;
        break;
    }
    case 0x95: {
        bus_write(read_byte(cpu) + cpu->x, cpu->a);
        cpu->cycles = 4;
        break;
    }
    case 0x8D: {
        bus_write(read_word(cpu), cpu->a);
        cpu->cycles = 4;
        break;
    }
    case 0x9D: {
        bus_write(read_word(cpu) + cpu->x, cpu->a);
        cpu->cycles = 5;
        break;
    }
    case 0x99: {
        bus_write(read_word(cpu) + cpu->y, cpu->a);
        cpu->cycles = 5;
        break;
    }
    case 0x81: {
        uint8_t z = read_byte(cpu) + cpu->x;
        uint16_t addr = bus_read(z) | (bus_read((uint8_t)(z + 1)) << 8);
        bus_write(addr, cpu->a);
        cpu->cycles = 6;
        break;
    }
    case 0x91: {
        uint8_t z = read_byte(cpu);
        uint16_t addr = bus_read(z) | (bus_read((uint8_t)(z + 1)) << 8);
        bus_write(addr + cpu->y, cpu->a);
        cpu->cycles = 6;
        break;
    }

    // ---- STX ----
    case 0x86: {
        bus_write(read_byte(cpu), cpu->x);
        cpu->cycles = 3;
        break;
    }
    case 0x96: {
        bus_write(read_byte(cpu) + cpu->y, cpu->x);
        cpu->cycles = 4;
        break;
    }
    case 0x8E: {
        bus_write(read_word(cpu), cpu->x);
        cpu->cycles = 4;
        break;
    }

    // ---- STY ----
    case 0x84: {
        bus_write(read_byte(cpu), cpu->y);
        cpu->cycles = 3;
        break;
    }
    case 0x94: {
        bus_write(read_byte(cpu) + cpu->x, cpu->y);
        cpu->cycles = 4;
        break;
    }
    case 0x8C: {
        bus_write(read_word(cpu), cpu->y);
        cpu->cycles = 4;
        break;
    }

    // ---- TAX ----
    case 0xAA: {
        cpu->x = cpu->a;
        set_zn(cpu, cpu->x);
        cpu->cycles = 2;
        break;
    }

    // ---- TAY ----
    case 0xA8: {
        cpu->y = cpu->a;
        set_zn(cpu, cpu->y);
        cpu->cycles = 2;
        break;
    }

    // ---- TSX ----
    case 0xBA: {
        cpu->x = cpu->stkp;
        set_zn(cpu, cpu->x);
        cpu->cycles = 2;
        break;
    }

    // ---- TXA ----
    case 0x8A: {
        cpu->a = cpu->x;
        set_zn(cpu, cpu->a);
        cpu->cycles = 2;
        break;
    }

    // ---- TXS ----
    case 0x9A: {
        cpu->stkp = cpu->x;
        cpu->cycles = 2;
        break;
    }

    // ---- TYA ----
    case 0x98: {
        cpu->a = cpu->y;
        set_zn(cpu, cpu->a);
        cpu->cycles = 2;
        break;
    }

    default: {
        cpu->cycles = 2;
        break;
    }
    }

    // Account for the fetch cycle we just consumed
    cpu->cycles--;
}
