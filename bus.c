#include <stdint.h>

static uint8_t ram[65536];

uint8_t bus_read(uint16_t address){
    return ram[address];
}

void bus_write(uint16_t address, uint8_t data){
    ram[address] = data;
    return;
}