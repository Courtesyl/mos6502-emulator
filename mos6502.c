#include "mos6502.h"

extern uint8_t bus_read(uint16_t address);
extern void bus_write(uint16_t address, uint8_t data);

void mos6502_reset(mos6502_t *cpu){
    cpu->a = 0;
    cpu->x = 0;
    cpu->y = 0;

    cpu->stkp = 0xFD;
    cpu->status = 0x00 | FLAG_UNUSED;

    uint16_t lo = bus_read(0xFFFC);
    uint16_t hi = bus_read(0xFFFD);

    cpu->pc = (hi << 8) | lo;
    cpu->cycles = 8;
}

uint16_t get_address_absolute(mos6502_t *cpu){
    uint16_t lo = bus_read(cpu->pc);
    uint16_t hi = bus_read(cpu->pc + 1);

    
    cpu->pc += 2;
    return (hi << 8) | lo;
}

uint16_t get_address_zeropage(mos6502_t *cpu){
    uint8_t data = bus_read(cpu->pc);
    cpu->pc++;
    return data;
}

uint16_t get_address_absolute_x(mos6502_t *cpu){
    uint16_t addr = get_address_absolute(cpu);
    return addr + cpu->x;
}

void mos6502_clock(mos6502_t *cpu){
    if(cpu->cycles > 0){
        cpu->cycles--;
        return;
    }
    uint8_t opcode = bus_read(cpu->pc);
    cpu->pc++;

    switch (opcode)
    {
    case 0xA9: //lda
       { uint8_t data = bus_read(cpu->pc);
        cpu->pc++;
        cpu->a = data;

        if(cpu->a == 0x00){
            cpu->status |= FLAG_ZERO;
        }else{
            cpu->status &= ~FLAG_ZERO;
        }
        if(cpu->a & 0x80){
            cpu->status |= FLAG_NEGATIVE;
        }else{
            cpu->status &= ~FLAG_NEGATIVE;
        }
        cpu->cycles = 2;
        break;
       }
    case 0xA2: //ldx    
       { uint8_t data = bus_read(cpu->pc);
        cpu->pc++;
        cpu->x = data;

        if(cpu->x == 0x00){
            cpu->status |= FLAG_ZERO;
        }else{
            cpu->status &= ~FLAG_ZERO;
        }
        if(cpu->x & 0x80){
            cpu->status |= FLAG_NEGATIVE;
        }else{
            cpu->status &= ~FLAG_NEGATIVE;
        }
        cpu->cycles = 2;
        break;
    }

    case 0xAD: //lda absolute
    {
        uint16_t addr = get_address_absolute(cpu);
        uint8_t data = bus_read(addr);

        cpu->a = data;

        if(cpu->a == 0x00){
            cpu->status |= FLAG_ZERO;
        }else{
            cpu->status &= ~FLAG_ZERO;
        }
        if(cpu->a & 0x80){
            cpu->status |= FLAG_NEGATIVE;
        }else{
            cpu->status &= ~FLAG_NEGATIVE;
        }
        cpu->cycles = 4;
        break;
    }

    case 0x8D: //sta absolute
    {
        uint16_t addr = get_address_absolute(cpu);
        uint8_t data = cpu->a;

        bus_write(addr, data);
        cpu->cycles = 4;

        break;
    }

    case 0xA5: //lda zero page
    {
        uint16_t addr = get_address_zeropage(cpu);
        uint8_t data = bus_read(addr);

        cpu->a = data;

        if(cpu->a == 0x00){
            cpu->status |= FLAG_ZERO;
        }else{
            cpu->status &= ~FLAG_ZERO;
        }
        if(cpu->a & 0x80){
            cpu->status |= FLAG_NEGATIVE;
        }else{
            cpu->status &= ~FLAG_NEGATIVE;
        }
        cpu->cycles = 3;
        break;
    }

    case 0xBD: //lda absolute x
    {
        uint16_t addr = get_address_absolute_x(cpu);
        uint8_t data = bus_read(addr);

        cpu->a = data;

        if(cpu->a == 0x00){
            cpu->status |= FLAG_ZERO;
        }else{
            cpu->status &= ~FLAG_ZERO;
        }
        if(cpu->a & 0x80){
            cpu->status |= FLAG_NEGATIVE;
        }else{
            cpu->status &= ~FLAG_NEGATIVE;
        }
        cpu->cycles = 4;
        break;
    }

    default:
        cpu->cycles = 1;
        break;
    }
}

