#include <stdio.h>
#include <string.h>
#include <SDL2/SDL.h>
#include "mos6502.h"
#include "cartridge.h"
#include "bus.h"
#include "ppu.h"
#include "apu.h"

static uint8_t sdl_to_nes_controller(void) {
    const uint8_t *keys = SDL_GetKeyboardState(NULL);
    uint8_t state = 0;
    if (keys[SDL_SCANCODE_X])      state |= 0x01;
    if (keys[SDL_SCANCODE_Z])      state |= 0x02;
    if (keys[SDL_SCANCODE_RSHIFT]) state |= 0x04;
    if (keys[SDL_SCANCODE_RETURN]) state |= 0x08;
    if (keys[SDL_SCANCODE_UP])     state |= 0x10;
    if (keys[SDL_SCANCODE_DOWN])   state |= 0x20;
    if (keys[SDL_SCANCODE_LEFT])   state |= 0x40;
    if (keys[SDL_SCANCODE_RIGHT])  state |= 0x80;
    return state;
}

int main(int argc, char* argv[]) {
    setvbuf(stdout, NULL, _IONBF, 0);
    const char *rom_file = "roms/Donkey_Kong.nes";
    if (argc > 1) rom_file = argv[1];

    cartridge_t cart;
    mos6502_t cpu;
    nes_ppu_t ppu;
    apu_t apu;

    if (!cartridge_load(&cart, rom_file)) {
        fprintf(stderr, "error: failed to load cartridge %s\n", rom_file);
        return 1;
    }

    ppu_init(&ppu);
    ppu.mirroring_mode = (cart.header.flags6 & 0x01) != 0 ? 1 : 0;
    ppu.cart = &cart;
    apu_init(&apu);
    apu.prg_rom = cart.prg_rom;
    apu.prg_rom_size = cart.prg_rom_size;
    bus_init(&cart, &ppu, &apu);
    bus_set_cpu(&cpu);

    mos6502_reset(&cpu);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
        fprintf(stderr, "error: SDL could not initialize! SDL_Error: %s\n", SDL_GetError());
        cartridge_free(&cart);
        return 1;
    }

    SDL_AudioSpec audio_desired, audio_obtained;
    SDL_zero(audio_desired);
    audio_desired.freq = APU_SAMPLE_RATE;
    audio_desired.format = AUDIO_S16SYS;
    audio_desired.channels = 1;
    audio_desired.samples = 512;
    apu.dev_id = SDL_OpenAudioDevice(NULL, 0, &audio_desired, &audio_obtained, 0);
    if (apu.dev_id <= 0) {
        fprintf(stderr, "warning: audio device open failed: %s\n", SDL_GetError());
    } else {
        SDL_PauseAudioDevice(apu.dev_id, 0);
    }

    SDL_Window* window = SDL_CreateWindow(
        "NES Emulator",
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        SCREEN_WIDTH * 2, SCREEN_HEIGHT * 2,
        SDL_WINDOW_SHOWN
    );

    if (!window) {
        fprintf(stderr, "error: window creation failed\n");
        if (apu.dev_id > 0) SDL_CloseAudioDevice(apu.dev_id);
        SDL_Quit();
        cartridge_free(&cart);
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    SDL_Texture* texture = SDL_CreateTexture(
        renderer, SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_STREAMING,
        SCREEN_WIDTH, SCREEN_HEIGHT
    );

    // Burn frames to let the PPU stabilize
    for (int burn = 0; burn < 40; burn++) {
        ppu.frame_complete = false;
        int guard = 0;
        while (!ppu.frame_complete) {
            if (++guard > 300000) {
                fprintf(stderr, "timeout during burn frame %d\n", burn);
                break;
            }
            if (bus_is_dma_active()) bus_process_dma();
            mos6502_clock(&cpu, &ppu.nmi_pending);
            for (int i = 0; i < 3; i++) {
                ppu_clock(&ppu, &cart);
            }
            apu_clock(&apu);
        }
    }

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);

    uint32_t last_time = SDL_GetTicks();
    uint32_t fps_last = SDL_GetTicks();
    int fps_frames = 0;
    bool quit = false;
    SDL_Event e;

    while (!quit) {
        while (SDL_PollEvent(&e) != 0) {
            if (e.type == SDL_QUIT) quit = true;
        }

        bus_set_controller_state(0, sdl_to_nes_controller());
        ppu.frame_complete = false;

        int guard = 0;
        while (!ppu.frame_complete) {
            if (++guard > 300000) {
                fprintf(stderr, "timeout during frame render\n");
                break;
            }
            if (bus_is_dma_active()) bus_process_dma();
            mos6502_clock(&cpu, &ppu.nmi_pending);
            for (int i = 0; i < 3; i++) {
                ppu_clock(&ppu, &cart);
            }
            apu_clock(&apu);
        }


        if (apu.buffer_count > 0 && apu.dev_id > 0) {
            int queued = SDL_GetQueuedAudioSize(apu.dev_id);
            if (queued < APU_SAMPLE_RATE * 2) {
                SDL_QueueAudio(apu.dev_id, apu.buffer, apu.buffer_count * sizeof(int16_t));
            }
            apu.buffer_count = 0;
        }

        SDL_UpdateTexture(texture, NULL, ppu.framebuffer, SCREEN_WIDTH * sizeof(uint32_t));
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);

        fps_frames++;
        uint32_t now = SDL_GetTicks();
        if (now - fps_last >= 1000) {
            char title[64];
            snprintf(title, sizeof(title), "NES Emulator - %d FPS", fps_frames);
            SDL_SetWindowTitle(window, title);
            fps_frames = 0;
            fps_last = now;
        }

        uint32_t current_time = SDL_GetTicks();
        int32_t delay = 16 - (int32_t)(current_time - last_time);
        if (delay > 0) SDL_Delay(delay);
        last_time = SDL_GetTicks();
    }

    if (apu.dev_id > 0) SDL_CloseAudioDevice(apu.dev_id);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    cartridge_free(&cart);
    return 0;
}
