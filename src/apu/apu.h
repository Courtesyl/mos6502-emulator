#ifndef APU_H
#define APU_H

#include <stdint.h>
#include <stdbool.h>
#include "biquad.h"

#define APU_SAMPLE_RATE 44100

typedef struct {
    bool enabled;
    uint8_t duty;
    uint8_t duty_pos;
    bool env_loop;
    bool env_disable;
    uint8_t env_period;
    uint8_t env_vol;
    uint8_t env_counter;
    bool env_start;
    bool sweep_enable;
    uint8_t sweep_period;
    bool sweep_negate;
    uint8_t sweep_shift;
    uint8_t sweep_counter;
    bool sweep_reload;
    uint16_t timer_period;
    uint16_t timer_counter;
    uint8_t length_counter;
} pulse_channel_t;

typedef struct {
    bool enabled;
    uint8_t lin_counter_period;
    uint8_t lin_counter;
    bool lin_counter_reload;
    bool lin_counter_control;
    uint16_t timer_period;
    uint16_t timer_counter;
    uint8_t seq;
    uint8_t length_counter;
} triangle_channel_t;

typedef struct {
    bool enabled;
    bool env_loop;
    bool env_disable;
    uint8_t env_period;
    uint8_t env_vol;
    uint8_t env_counter;
    bool env_start;
    uint8_t mode;
    uint16_t timer_period;
    uint16_t timer_counter;
    uint16_t shift_reg;
    uint8_t length_counter;
} noise_channel_t;

typedef struct {
    bool enabled;
    uint8_t freq;
    uint16_t timer_period;
    uint16_t timer_counter;
    uint8_t output_level;
    bool irq_enable;
    bool loop;
    uint16_t sample_addr;
    uint16_t sample_len;
    uint16_t cur_addr;
    uint16_t cur_len;
    uint8_t sample_buf;
    bool sample_buf_full;
    uint8_t bits_rem;
    bool irq_flag;
} dmc_channel_t;

typedef struct apu_t {
    pulse_channel_t pulse1, pulse2;
    triangle_channel_t triangle;
    noise_channel_t noise;
    dmc_channel_t dmc;
    uint8_t status;
    uint8_t frame_mode;
    uint8_t frame_step;
    int frame_cycles;
    bool frame_irq;
    uint32_t cycle_count;
    int16_t buffer[44100 / 60 + 1];
    int buffer_count;
    float sample_acc;
    int dev_id;
    uint8_t *prg_rom;
    uint32_t prg_rom_size;
    Biquad hpf;
} apu_t;

void apu_init(apu_t *apu);
void apu_write(apu_t *apu, uint16_t addr, uint8_t data);
uint8_t apu_read(apu_t *apu, uint16_t addr);
void apu_clock(apu_t *apu);

#endif
