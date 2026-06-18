#include "apu.h"
#include <string.h>
#include <math.h>

static const uint8_t duty_table[4][8] = {
    {0, 0, 0, 0, 0, 0, 0, 1},
    {0, 0, 0, 0, 0, 0, 1, 1},
    {0, 0, 0, 0, 1, 1, 1, 1},
    {1, 1, 1, 1, 1, 1, 0, 0},
};

static const uint8_t length_table[32] = {
    10,254,20,2,40,4,80,6,160,8,60,10,14,12,26,14,
    12,16,24,18,48,20,96,22,192,24,72,26,16,28,32,30
};

static const uint16_t noise_periods[16] = {
    4,8,16,32,64,96,128,160,202,254,380,508,762,1016,2034,4068
};

static const uint16_t dmc_periods[16] = {
    428,380,340,320,286,254,226,214,190,160,142,128,106,84,72,54
};

static const uint8_t triangle_seq[32] = {
    0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,
    15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0
};

void apu_init(apu_t *apu) {
    memset(apu, 0, sizeof(*apu));
    apu->noise.shift_reg = 1;
}

static void clock_envelope(pulse_channel_t *p) {
    if (p->env_start) {
        p->env_start = false;
        p->env_counter = p->env_period;
        p->env_vol = 15;
    } else {
        if (p->env_counter == 0) {
            p->env_counter = p->env_period;
            if (p->env_vol > 0)
                p->env_vol--;
            else if (p->env_loop)
                p->env_vol = 15;
        } else {
            p->env_counter--;
        }
    }
}

static void clock_sweep(pulse_channel_t *p) {
    if (p->sweep_counter == 0 && p->sweep_enable && p->sweep_period > 0) {
        p->sweep_counter = p->sweep_period;
        uint16_t delta = p->timer_period >> p->sweep_shift;
        if (p->sweep_negate)
            p->timer_period -= delta;
        else
            p->timer_period += delta;
    } else if (p->sweep_counter > 0) {
        p->sweep_counter--;
    }
    if (p->sweep_reload) {
        p->sweep_reload = false;
        p->sweep_counter = p->sweep_period;
    }
}

static void clock_length(uint8_t *lc) {
    if (*lc > 0) (*lc)--;
}

static int8_t pulse_output(pulse_channel_t *p) {
    if (p->length_counter == 0) return 0;
    uint8_t vol = p->env_disable ? p->env_period : p->env_vol;
    uint16_t period = p->timer_period;
    if (period < 8 || period > 0x7FF) return 0;
    return duty_table[p->duty][p->duty_pos] ? (int8_t)vol : 0;
}

static int8_t triangle_output(triangle_channel_t *t) {
    if (t->length_counter == 0 || t->lin_counter == 0) return 0;
    if (t->timer_period < 2) return 0;
    return (int8_t)(triangle_seq[t->seq] >> 1);
}

static int8_t noise_output(noise_channel_t *n) {
    if (n->length_counter == 0) return 0;
    uint8_t vol = n->env_disable ? n->env_period : n->env_vol;
    return (n->shift_reg & 1) ? (int8_t)vol : 0;
}

static int8_t dmc_output(dmc_channel_t *d) {
    if (!d->enabled) return 0;
    return (int8_t)d->output_level;
}

void apu_write(apu_t *apu, uint16_t addr, uint8_t data) {
    pulse_channel_t *p;
    switch (addr) {
        case 0x4000: p = &apu->pulse1; goto write_pulse_ctrl;
        case 0x4004: p = &apu->pulse2; goto write_pulse_ctrl;
        write_pulse_ctrl: {
            p->duty = (data >> 6) & 3;
            p->env_loop = (data & 0x20) != 0;
            p->env_disable = (data & 0x10) != 0;
            p->env_period = data & 0x0F;
            if (p->env_disable) p->env_vol = data & 0x0F;
            break;
        }

        case 0x4001: p = &apu->pulse1; goto write_pulse_sweep;
        case 0x4005: p = &apu->pulse2; goto write_pulse_sweep;
        write_pulse_sweep: {
            p->sweep_enable = (data & 0x80) != 0;
            p->sweep_period = ((data >> 4) & 7);
            p->sweep_negate = (data & 0x08) != 0;
            p->sweep_shift = data & 0x07;
            p->sweep_reload = true;
            break;
        }

        case 0x4002: apu->pulse1.timer_period = (apu->pulse1.timer_period & 0x0700) | data; break;
        case 0x4006: apu->pulse2.timer_period = (apu->pulse2.timer_period & 0x0700) | data; break;

        case 0x4003: {
            apu->pulse1.timer_period = (apu->pulse1.timer_period & 0x00FF) | ((uint16_t)(data & 7) << 8);
            apu->pulse1.duty_pos = 0;
            apu->pulse1.env_start = true;
            apu->pulse1.length_counter = length_table[(data >> 3) & 0x1F];
            break;
        }
        case 0x4007: {
            apu->pulse2.timer_period = (apu->pulse2.timer_period & 0x00FF) | ((uint16_t)(data & 7) << 8);
            apu->pulse2.duty_pos = 0;
            apu->pulse2.env_start = true;
            apu->pulse2.length_counter = length_table[(data >> 3) & 0x1F];
            break;
        }

        case 0x4008: {
            apu->triangle.lin_counter_control = (data & 0x80) != 0;
            apu->triangle.lin_counter_period = data & 0x7F;
            apu->triangle.lin_counter_reload = true;
            break;
        }

        case 0x400A:
            apu->triangle.timer_period = (apu->triangle.timer_period & 0x0700) | data;
            break;

        case 0x400B: {
            apu->triangle.timer_period = (apu->triangle.timer_period & 0x00FF) | ((uint16_t)(data & 7) << 8);
            apu->triangle.seq = 0;
            apu->triangle.lin_counter_reload = true;
            apu->triangle.length_counter = length_table[(data >> 3) & 0x1F];
            break;
        }

        case 0x400C: {
            noise_channel_t *n = &apu->noise;
            n->env_loop = (data & 0x20) != 0;
            n->env_disable = (data & 0x10) != 0;
            n->env_period = data & 0x0F;
            if (n->env_disable) n->env_vol = data & 0x0F;
            break;
        }

        case 0x400E: {
            noise_channel_t *n = &apu->noise;
            n->mode = (data >> 7) & 1;
            n->timer_period = noise_periods[data & 0x0F];
            break;
        }

        case 0x400F: {
            noise_channel_t *n = &apu->noise;
            n->env_start = true;
            n->length_counter = length_table[(data >> 3) & 0x1F];
            break;
        }

        case 0x4010: {
            dmc_channel_t *d = &apu->dmc;
            d->irq_enable = (data & 0x80) != 0;
            d->loop = (data & 0x40) != 0;
            d->freq = data & 0x0F;
            d->timer_period = dmc_periods[d->freq];
            if (!d->irq_enable) d->irq_flag = false;
            break;
        }

        case 0x4011:
            apu->dmc.output_level = data & 0x7F;
            break;

        case 0x4012:
            apu->dmc.sample_addr = 0xC000 + ((uint16_t)data << 6);
            break;

        case 0x4013:
            apu->dmc.sample_len = ((uint16_t)data << 4) + 1;
            break;

        case 0x4015: {
            apu->pulse1.enabled = (data & 1) != 0;
            apu->pulse2.enabled = (data & 2) != 0;
            apu->triangle.enabled = (data & 4) != 0;
            apu->noise.enabled = (data & 8) != 0;
            apu->dmc.enabled = (data & 16) != 0;
            if (!apu->pulse1.enabled) apu->pulse1.length_counter = 0;
            if (!apu->pulse2.enabled) apu->pulse2.length_counter = 0;
            if (!apu->triangle.enabled) apu->triangle.length_counter = 0;
            if (!apu->noise.enabled) apu->noise.length_counter = 0;
            if (!apu->dmc.enabled) {
                apu->dmc.cur_len = 0;
                apu->dmc.irq_flag = false;
            } else {
                if (apu->dmc.cur_len == 0) {
                    apu->dmc.cur_addr = apu->dmc.sample_addr;
                    apu->dmc.cur_len = apu->dmc.sample_len;
                }
            }
            apu->status = (apu->status & 0xE0) | ((apu->dmc.irq_flag ? 1 : 0) << 7) | (apu->pulse1.length_counter > 0 ? 1 : 0) | (apu->pulse2.length_counter > 0 ? 2 : 0) | (apu->triangle.length_counter > 0 ? 4 : 0) | (apu->noise.length_counter > 0 ? 8 : 0) | (apu->dmc.cur_len > 0 ? 16 : 0);
            break;
        }

        case 0x4017: {
            apu->frame_mode = (data >> 7) & 1;
            apu->frame_irq = (data & 0x40) != 0;
            if (apu->frame_irq) {
                apu->frame_step = 0;
                apu->frame_cycles = 0;
            } else {
                apu->frame_step = 0;
                apu->frame_cycles = 0;
            }
            break;
        }
    }
}

uint8_t apu_read(apu_t *apu, uint16_t addr) {
    if (addr == 0x4015) {
        uint8_t s = apu->status;
        apu->dmc.irq_flag = false;
        apu->status &= 0x1F;
        apu->frame_irq = false;
        return s | 0x20;
    }
    return (apu->status & 0xE0) | ((uint8_t)(apu->cycle_count >> 8) & 0x1F);
}

static void clock_dmc(apu_t *apu) {
    dmc_channel_t *d = &apu->dmc;
    if (!d->enabled && d->cur_len == 0) return;

    if (d->timer_counter == 0) {
        d->timer_counter = d->timer_period;
        if (d->bits_rem == 0) {
            d->bits_rem = 8;
            if (d->sample_buf_full || d->cur_len > 0) {
                if (!d->sample_buf_full) {
                    uint32_t idx = (uint32_t)(d->cur_addr - 0x8000) % apu->prg_rom_size;
                    if (apu->prg_rom) d->sample_buf = apu->prg_rom[idx];
                    else d->sample_buf = 0;
                    d->sample_buf_full = true;
                    d->cur_addr++;
                    d->cur_len--;
                    if (d->cur_len == 0) {
                        if (d->loop) {
                            d->cur_addr = d->sample_addr;
                            d->cur_len = d->sample_len;
                        } else if (d->irq_enable) {
                            d->irq_flag = true;
                        }
                    }
                }
            }
        }
        if (d->bits_rem > 0 && d->sample_buf_full) {
            if (d->sample_buf & 1) {
                if (d->output_level <= 125) d->output_level += 2;
            } else {
                if (d->output_level >= 2) d->output_level -= 2;
            }
            d->sample_buf >>= 1;
            d->bits_rem--;
            if (d->bits_rem == 0) d->sample_buf_full = false;
        }
    } else {
        d->timer_counter--;
    }
}

static void frame_sequencer_clock(apu_t *apu) {
    apu->frame_cycles++;
    int step_len = (apu->frame_mode == 1 && apu->frame_step == 4) ? 3728 : 7457;
    if (apu->frame_cycles < step_len) return;
    apu->frame_cycles = 0;
    apu->frame_step++;
    if (apu->frame_mode == 1) {
        if (apu->frame_step > 4) apu->frame_step = 0;
    } else {
        if (apu->frame_step > 3) { apu->frame_step = 0; apu->cycle_count = 0; }
    }

    bool do_length = false;
    bool do_envelope = true;
    bool do_sweep = false;

    if (apu->frame_mode == 0) {
        if (apu->frame_step == 0 || apu->frame_step == 2) {
            do_length = true;
            do_sweep = true;
        }
        if (apu->frame_step == 3 && !apu->frame_irq) {
            do_length = true;
            do_sweep = true;
        }
    } else {
        if (apu->frame_step == 0 || apu->frame_step == 2 || apu->frame_step == 4)
            do_length = true;
    }

    if (do_length) {
        clock_length(&apu->pulse1.length_counter);
        clock_length(&apu->pulse2.length_counter);
        clock_length(&apu->triangle.length_counter);
        clock_length(&apu->noise.length_counter);
    }
    if (do_sweep) {
        clock_sweep(&apu->pulse1);
        clock_sweep(&apu->pulse2);
    }
    if (do_envelope) {
        clock_envelope(&apu->pulse1);
        clock_envelope(&apu->pulse2);
        pulse_channel_t *np = (pulse_channel_t*)&apu->noise;
        clock_envelope(np);
    }
    // Triangle linear counter clock
    if (do_envelope) {
        triangle_channel_t *t = &apu->triangle;
        if (t->lin_counter_reload) {
            t->lin_counter = t->lin_counter_period;
        } else if (t->lin_counter > 0) {
            t->lin_counter--;
        }
        if (!t->lin_counter_control) t->lin_counter_reload = false;
    }
}

static void clock_timer_pulse(pulse_channel_t *p) {
    if (p->timer_counter == 0) {
        p->timer_counter = p->timer_period;
        p->duty_pos = (p->duty_pos + 1) & 7;
    } else {
        p->timer_counter--;
    }
}

static void clock_timer_triangle(triangle_channel_t *t) {
    if (t->timer_counter == 0) {
        t->timer_counter = t->timer_period;
        t->seq = (t->seq + 1) & 31;
    } else {
        t->timer_counter--;
    }
}

static void clock_timer_noise(noise_channel_t *n) {
    if (n->timer_counter == 0) {
        n->timer_counter = n->timer_period;
        uint16_t fb;
        if (n->mode)
            fb = ((n->shift_reg >> 6) ^ (n->shift_reg >> 5)) & 1;
        else
            fb = ((n->shift_reg >> 0) ^ (n->shift_reg >> 1)) & 1;
        n->shift_reg = (n->shift_reg >> 1) | (fb << 14);
    } else {
        n->timer_counter--;
    }
}

void apu_clock(apu_t *apu) {
    static const int cycles_per_sample = 1789773 / APU_SAMPLE_RATE;
    apu->cycle_count++;
    frame_sequencer_clock(apu);
    clock_timer_pulse(&apu->pulse1);
    clock_timer_pulse(&apu->pulse2);
    clock_timer_triangle(&apu->triangle);
    clock_timer_noise(&apu->noise);
    clock_dmc(apu);

    apu->sample_acc += 1.0f;
    if (apu->sample_acc >= cycles_per_sample) {
        apu->sample_acc -= cycles_per_sample;
        int p1 = pulse_output(&apu->pulse1);
        int p2 = pulse_output(&apu->pulse2);
        int t = triangle_output(&apu->triangle);
        int n = noise_output(&apu->noise);
        int d = dmc_output(&apu->dmc);
        float mixed = (float)(p1 + p2) * 0.45f + (float)t * 0.10f + (float)n * 0.08f + (float)d * 0.20f;
        mixed = mixed > 127.0f ? 127.0f : (mixed < -128.0f ? -128.0f : mixed);
        if (apu->buffer_count < (int)(sizeof(apu->buffer) / sizeof(apu->buffer[0]))) {
            apu->buffer[apu->buffer_count++] = (int16_t)(mixed * 200.0f);
        }
    }
}
