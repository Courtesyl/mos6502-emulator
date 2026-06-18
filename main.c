#include <stdio.h>
#include "mos6502.h"

extern void bus_write(uint16_t address, uint8_t data);
extern uint8_t bus_read(uint16_t address);

int main() {
    mos6502_t cpu;

    // 1. Write the Reset Vector into addresses 0xFFFC and 0xFFFD
    // We want the CPU to jump to address 0x8000 upon start
    bus_write(0xFFFC, 0x00); // Low byte of address 0x8000
    bus_write(0xFFFD, 0x80); // High byte of address 0x8000

    // 2. Write the program itself, starting at address 0x8000
    bus_write(0x8000, 0xA9); // Opcode: LDA Immediate
    bus_write(0x8001, 0x05); // Data: load number 5
    
    bus_write(0x8002, 0x8D); // Opcode: STA Absolute
    bus_write(0x8003, 0x00); // Low byte of destination address (0x00)
    bus_write(0x8004, 0x20); // High byte of destination address (0x20) -> total 0x2000

    // 3. Wake up the CPU!
    printf("Initializing processor (RESET)...\n");
    mos6502_reset(&cpu);
    printf("CPU is awake. PC points to: 0x%04X\n\n", cpu.pc);

    // 4. Start ticking clock cycles!
    // We need to execute two instructions.
    // RESET took 8 cycles. LDA takes 2 cycles. STA takes 4 cycles.
    // We will loop until the CPU consumes all remaining cycles for each stage.
    
    printf("--- Executing LDA #$05 ---\n");
    while (cpu.cycles > 0) mos6502_clock(&cpu); // Consume 8 cycles of the reset stage
    
    mos6502_clock(&cpu); // This tick fetches LDA and sets cpu.cycles = 2
    printf("Fetched LDA. Remaining cycles for this instruction: %d\n", cpu.cycles);
    while (cpu.cycles > 0) mos6502_clock(&cpu); // Wait for execution to finish
    printf("LDA executed! Register A = %d, PC = 0x%04X\n\n", cpu.a, cpu.pc);

    printf("--- Executing STA $2000 ---\n");
    mos6502_clock(&cpu); // This tick fetches STA and sets cpu.cycles = 4
    while (cpu.cycles > 0) mos6502_clock(&cpu); // Wait for execution to finish
    printf("STA executed! PC = 0x%04X\n\n", cpu.pc);

    // 5. Verify the final result! Did the number 5 get saved to 0x2000?
    uint8_t result = bus_read(0x2000);
    printf("Checking memory at address 0x2000: value is %d\n", result);

    if (result == 5) {
        printf("\nSUCCESS!!! Our emulator works! YOU HAVE BUILT A LIVING CPU!\n");
    } else {
        printf("\nSomething went wrong, value was not saved...\n");
    }

    return 0;
}