#pragma once

typedef struct {
    double a0, a1, a2, a3, a4;
    double x1, x2, y1, y2;
} Biquad;

enum {
    LPF,
    HPF,
    BPF,
    NOTCH,
    PEQ,
    LSH,
    HSH
};

double biquad(double sample, Biquad *b);
void biquad_init(Biquad* b, int type, double dbGain, double freq, double srate, double bandwidth);
