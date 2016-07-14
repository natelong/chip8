#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <SDL2/sdl.h>

#include "font.h"

#ifndef CHIP8_DEBUG
#define CHIP8_DEBUG 0
#endif

#define debug_print(...) \
    do { if (CHIP8_DEBUG > 0) fprintf(stdout, __VA_ARGS__); } while (0)

// For use later in framerate capping
const int SCREEN_FPS   = 60;
const int SCREEN_TICKS_PER_FRAME = 1000 / SCREEN_FPS;

const int WINDOW_WIDTH  = 640;
const int WINDOW_HEIGHT = 320;
const int SCREEN_WIDTH  = 64;
const int SCREEN_HEIGHT = 32;
const int PIXEL_WIDTH   = WINDOW_WIDTH / SCREEN_WIDTH;
const int PIXEL_HEIGHT  = WINDOW_HEIGHT / SCREEN_HEIGHT;

const int MEMORY_SIZE  = 4096;
const int ROM_OFFSET   = 0x200; // 512
const int MAX_ROM_SIZE = MEMORY_SIZE - ROM_OFFSET;

const SDL_Keycode MIN_COMMAND_KEY = SDLK_j;
const SDL_Keycode MAX_COMMAND_KEY = SDLK_l;
const int COMMAND_KEY_COUNT = MAX_COMMAND_KEY - MIN_COMMAND_KEY + 1;

bool drawFlag = false;

bool currKeys[COMMAND_KEY_COUNT];
bool prevKeys[COMMAND_KEY_COUNT];

uint16_t opcode;
uint8_t  memory[MEMORY_SIZE];
uint8_t  registers[16];
uint16_t I;
uint16_t pc;

uint8_t  gfx[SCREEN_WIDTH * SCREEN_HEIGHT];
uint8_t  delay_timer;
uint8_t  sound_timer;

uint16_t stack[16];
uint16_t sp;

uint8_t  key[16];
uint8_t  keyValues[16] =
   {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};

void preamble() {
    debug_print("%04X: (%04X) ", pc, opcode);
}

void printRegisters() {
    for (int i = 0; i < 16; i++) {
        printf(" V%X  ", i);
    }
    puts("  I");
    for (int i = 0; i < 16; i++) {
        printf("%04X ", registers[i]);
    }
    printf("[%04X]\n", I);
}

int getKeyIndex(SDL_Keycode key) {
    if (key >= SDLK_0 && key <= SDLK_9) return key - SDLK_0;
    if (key >= SDLK_a && key <= SDLK_f) return key - SDLK_a + SDLK_9 - SDLK_0 + 1;
    return -1;
}

void clearDisplay() {
    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        for (int x = 0; x < SCREEN_WIDTH; x++) {
            gfx[y * SCREEN_WIDTH + x] = 0;
        }
    }
}

bool isDown(SDL_Keycode k) {
    if (k < MIN_COMMAND_KEY || k > MAX_COMMAND_KEY) return false;
    return currKeys[k - MIN_COMMAND_KEY] && !prevKeys[k - MIN_COMMAND_KEY];
}

/**
 * Shortcut string equality, to be used only with string literals
 * for second argument.
 */
bool streq(const char* a, const char* b) {
    return strncmp(a, b, strlen(b)) == 0;
}

int main(int argc, const char* argv[]) {
    const char* filename = "roms/Maze.ch8";

    uint16_t breakpoint;

    if (argc > 1) {
        for (int i = 0; i < argc - 1; i++) {
            if (streq(argv[i], "-b") && i + 1 < argc) {
                breakpoint = strtol(argv[i+1], NULL, 16);
                printf("breakpoint set at 0x%04X\n", breakpoint);
            }
        }

        filename = argv[argc-1];
    }

    SDL_Window* window;
    SDL_Surface* surface;
    SDL_Init(SDL_INIT_VIDEO);

    printf("Window size: %dx%d\n", WINDOW_WIDTH, WINDOW_HEIGHT);
    printf("Screen size: %dx%d\n", SCREEN_WIDTH, SCREEN_HEIGHT);
    printf("Pixel size:  %dx%d\n", PIXEL_WIDTH, PIXEL_HEIGHT);

    { // Initialize graphics stuff
        window = SDL_CreateWindow(
                "CHIP-8",
                SDL_WINDOWPOS_UNDEFINED,
                SDL_WINDOWPOS_UNDEFINED,
                WINDOW_WIDTH,
                WINDOW_HEIGHT,
                SDL_WINDOW_ALLOW_HIGHDPI | SDL_RENDERER_PRESENTVSYNC
            );

        if (window == NULL) {
            printf("Couldn't create window\n");
            return 1;
        }

        surface = SDL_GetWindowSurface(window);
    }

    { // Initialize emulator
        pc     = ROM_OFFSET; // Program counter starts at 0x200
        opcode = 0;     // Reset current opcode
        I      = 0;     // Reset index register
        sp     = 0;     // Reset stack pointer

        clearDisplay();
        for (int i = 0; i < 16; i++) {
            stack[i] = 0;
        }
        for (int i = 0; i < 16; i++) {
            registers[i] = 0;
        }
        for (size_t i = 0; i < MEMORY_SIZE; i++) {
            memory[i] = 0;
        }
        size_t count = 0;
        for (size_t i = 0; i < CHIP8_FONT_SIZE; i++) {
            memory[i] = CHIP8_FONT[i];
            ++count;
        }
        printf("Loaded %ld bytes of font data\n", count);

        delay_timer = 0;
        sound_timer = 0;
    }

    { // Load ROM
        SDL_RWops* rom = SDL_RWFromFile(filename, "rb");
        if (rom != NULL) {
            int64_t size = SDL_RWsize(rom);
            printf("Size of ROM: %lld\n", size);

            if (size > MAX_ROM_SIZE) {
                printf("ROM too big!\n");
                return 1;
            }

            uint8_t* data = memory + ROM_OFFSET;

            if (SDL_RWread(rom, data, size, 1) > 0) {
                printf("Loaded ROM\n");
            } else {
                printf("Couldn't read file\n");
            }
            SDL_RWclose(rom);
        } else {
            printf("Couldn't open file\n");
        }
    }


    bool running = true;
    bool infinite = false;
    bool waitingForInput = false;
    uint32_t frameCount = 0;
    bool breakpointTriggered = false;

    for (int i = 0; i < 4; i++) {
        currKeys[i] = false;
        prevKeys[i] = false;
    }

    while (running) {
        for (int i = 0; i < COMMAND_KEY_COUNT; i++) {
            prevKeys[i] = currKeys[i];
        }

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
                break;
            }

            if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
                SDL_Keycode k = event.key.keysym.sym;
                if (k >= MIN_COMMAND_KEY && k <= MAX_COMMAND_KEY) {
                    currKeys[k - MIN_COMMAND_KEY] = (event.type == SDL_KEYDOWN);
                }

                int index = getKeyIndex(k);
                if (index == -1) continue;
                key[index] = (event.type == SDL_KEYDOWN);
                break;
            }
        }

        if (isDown(SDLK_l)) {
            printRegisters();
        }

        if (!infinite) { // run emulator
            if ((breakpoint != 0 && pc == breakpoint) || breakpointTriggered) {
                if (!breakpointTriggered) printf("=== Breakpoint triggered at 0x%04X ===\n", pc);

                if (isDown(SDLK_k)) {
                    breakpointTriggered = false;
                } else {
                    breakpointTriggered = true;
                }

                if (breakpointTriggered && !isDown(SDLK_j)) continue;
            }


            opcode = memory[pc] << 8 | memory[pc + 1];

            uint16_t addr = (opcode & 0x0FFF);
            uint8_t  x    = (opcode & 0x0F00) >> 8;
            uint8_t  y    = (opcode & 0x00F0) >> 4;
            uint8_t  z    = (opcode & 0x000F);
            uint8_t  yz   = (opcode & 0x00FF);

            if (!waitingForInput) {
                preamble();
            }

            switch (opcode & 0xF000) {
                case 0x0000: {
                    switch (yz) {
                        case 0x00E0: {
                            debug_print("CLS\n");
                            clearDisplay();
                            pc += 2;
                        }
                        break;

                        case 0x00EE: {
                            debug_print("RET\n");
                            pc = stack[sp];
                            --sp;
                            pc += 2;
                        }
                        break;

                        default:
                            printf("Unknown opcode: 0x%04X at 0x%04x\n", opcode, pc);
                        return 1;
                    }
                }
                break;

                case 0x1000: {
                    debug_print("JP   0x%04X\n", addr);
                    if (addr == pc) {
                        printf("Infinite loop detected; stopping VM\n");
                        infinite = true;
                    }

                    pc = addr;
                }
                break;

                case 0x2000: {
                    debug_print("CALL 0x%04X\n", addr);
                    ++sp;
                    stack[sp] = pc;
                    pc = addr;
                }
                break;

                case 0x3000: {
                    debug_print("SE   V%X,\t%d\n", x, yz);
                    if (registers[x] == yz) pc += 2;
                    pc += 2;
                }
                break;

                case 0x4000: {
                    debug_print("SNE  V%X,\t%d\n", x, yz);
                    if (registers[x] != yz) pc += 2;
                    pc += 2;
                }
                break;

                case 0x5000: {
                    debug_print("SE   V%X,\tv%X\n", x, y);
                    if (registers[x] == registers[y]) pc += 2;
                    pc += 2;
                }

                case 0x6000: {
                    debug_print("LD   V%X,\t%d\n", x, yz);
                    registers[x] = yz;
                    pc += 2;
                }
                break;

                case 0x7000: {
                    debug_print("ADD  V%X,\t%d\n", x, yz);
                    registers[x] += yz;
                    pc += 2;
                }
                break;

                case 0x8000: {
                    switch (z) {
                        case 0x0: {
                            debug_print("LD   V%X,\tV%X\n", x, y);
                            registers[x] = registers[y];
                            pc += 2;
                        }
                        break;

                        case 0x2: {
                            debug_print("AND  V%X,\tV%X\n", x, y);
                            registers[x] &= registers[y];
                            pc += 2;
                        }
                        break;

                        case 0x4: {
                            debug_print("ADD  V%X,\tV%X\n", x, y);
                            uint8_t  a = registers[x];
                            uint8_t  b = registers[y];
                            uint16_t c = a + b;
                            registers[0xF] = c > 0xFF;
                            registers[x]   = c & 0xFF;
                            pc += 2;
                        }
                        break;

                        case 0x5: {
                            debug_print("SUB  V%X,\tV%X\n", x, y);
                            registers[0xF] = registers[x] > registers[y];
                            registers[x]  -= registers[y];
                            pc += 2;
                        }
                        break;

                        case 0x6: {
                            debug_print("SHR  V%X,\t{V%X}\n", x, y);
                            registers[0xF] = registers[x] % 0x1 == 1;
                            registers[x]   = registers[x] >> 1;
                            pc += 2;
                        }
                        break;

                        default:
                            printf("Unknown opcode: 0x%04X at 0x%04x\n", opcode, pc);
                        return 1;
                    }
                }
                break;

                case 0xA000: {
                    I = opcode & 0x0FFF;
                    debug_print("LD   I,\t%d\n", I);
                    pc += 2;
                }
                break;

                case 0xB000: {
                    debug_print("JP   V0\t%d\n", addr);
                    pc = addr + registers[0];
                }
                break;

                case 0xC000: {
                    uint8_t rnd = rand() % 255;
                    debug_print("RND  V%X,\t%d\n", x, yz);
                    registers[x] = rnd & yz;
                    pc += 2;
                }
                break;

                case 0xD000: {
                    uint16_t xpos = registers[x];
                    uint16_t ypos = registers[y];
                    uint16_t pixel;

                    debug_print("DRW  V%X,\tV%X,\t%d\n", x, y, z);

                    registers[0xF] = 0;
                    for (int yline = 0; yline < z; ++yline) {
                        pixel = memory[I + yline];
                        for (int xline = 0; xline < 8; ++xline) {
                            if ((pixel & (0x80 >> xline)) != 0) {
                                if (gfx[(xpos + xline + ((ypos + yline) * 64))] == 1) {
                                    registers[0xF] = 1;
                                }
                                gfx[xpos + xline + ((ypos + yline) * 64)] ^= 1;
                            }
                        }
                    }

                    drawFlag = true;
                    pc += 2;
                }
                break;

                case 0xE000: {
                    switch (yz) {
                        case 0x9E: {
                            debug_print("SKP  V%X\n", x);
                            if (registers[x] > 0xF) {
                                printf("Invalid key index: %d\n", registers[x]);
                                return 1;
                            }

                            if (key[registers[x]]) pc += 2;
                            pc += 2;
                        }
                        break;

                        case 0xA1: {
                            debug_print("SKNP V%X\n", x);
                            if (registers[x] > 0xF) {
                                printf("Invalid key index: %d\n", registers[x]);
                                return 1;
                            }

                            if (!key[registers[x]]) pc += 2;
                            pc += 2;
                        }
                        break;

                        default:
                            printf("Unknown opcode: 0x%04X at 0x%04x\n", opcode, pc);
                            return 1;
                    }
                }
                break;

                case 0xF000: {
                    switch (yz) {
                        case 0x07: {
                            debug_print("LD   V%X,\tDT\n", x);
                            registers[x] = delay_timer;
                            pc += 2;
                        }
                        break;

                        case 0x0A: {
                            if (waitingForInput) {
                                for (int i = 0; i < 16; i++) {
                                    if (key[i]) {
                                        registers[x] = keyValues[i];
                                        waitingForInput = false;
                                        pc += 2;
                                        break;
                                    }
                                }
                            } else {
                                debug_print("LD   V%X\tK\n", x);
                                waitingForInput = true;
                            }
                        }
                        break;

                        case 0x15: {
                            debug_print("LD   DT,\tV%X\n", x);
                            delay_timer = registers[x];
                            pc += 2;
                        }
                        break;

                        case 0x18: {
                            debug_print("LD   ST, V%X\n", x);
                            sound_timer = registers[x];
                            pc += 2;
                        }
                        break;

                        case 0x1E: {
                            debug_print("ADD  I\tV%X\n", x);
                            I += registers[x];
                            pc += 2;
                        }
                        break;

                        case 0x29: {
                            debug_print("LD   F, V%X\n", x);
                            switch (registers[x]) {
                                case 0x0: I =  0 * 5; break;
                                case 0x1: I =  1 * 5; break;
                                case 0x2: I =  2 * 5; break;
                                case 0x3: I =  3 * 5; break;
                                case 0x4: I =  4 * 5; break;
                                case 0x5: I =  5 * 5; break;
                                case 0x6: I =  6 * 5; break;
                                case 0x7: I =  7 * 5; break;
                                case 0x8: I =  8 * 5; break;
                                case 0x9: I =  9 * 5; break;
                                case 0xA: I = 10 * 5; break;
                                case 0xB: I = 11 * 5; break;
                                case 0xC: I = 12 * 5; break;
                                case 0xD: I = 13 * 5; break;
                                case 0xE: I = 14 * 5; break;
                                case 0xF: I = 15 * 5; break;
                                default:
                                    printf("Invalid value for instruction Fx29: %d", x);
                                    return 1;
                            }
                            pc += 2;
                        }
                        break;

                        case 0x33: {
                            debug_print("LD   B, V%X\n", x);
                            memory[I]   = (registers[x] % 1000) / 100;
                            memory[I+1] = (registers[x] % 100) / 10;
                            memory[I+2] = (registers[x] % 10);
                            pc += 2;
                        }
                        break;

                        case 0x55: {
                            debug_print("LD   [I]\tV%X\n", x);
                            for (int i = 0; i <= x; ++i) {
                                memory[I + i] = registers[i];
                            }
                            pc += 2;
                        }
                        break;

                        case 0x65: {
                            debug_print("LD   V%X\t[I]\n", x);
                            for (int i = 0; i <= x; ++i) {
                                registers[i] = memory[I + i];
                            }
                            pc += 2;
                        }
                        break;

                        default:
                            printf("Unknown opcode: 0x%04X at 0x%04x\n", opcode, pc);
                            return 1;
                    }
                }
                break;

                default:
                    printf("Unknown opcode: 0x%04X at 0x%04x\n", opcode, pc);
                    return 1;
            }

            if (delay_timer > 0) {
                --delay_timer;
            }

            if (sound_timer > 0) {
                if (sound_timer == 1) {
                    printf("BEEP!\n");
                }
                --sound_timer;
            }
        }

        { // Update Graphics
            // if (drawFlag || frameCount % 60 == 0) {
            if (frameCount % 60 == 0) {
                SDL_Rect pixel;
                pixel.w = PIXEL_WIDTH;
                pixel.h = PIXEL_HEIGHT;
                uint32_t black = SDL_MapRGB(surface->format, 0, 0, 0);
                uint32_t white = SDL_MapRGB(surface->format, 255, 255, 255);

                for (int y = 0; y < SCREEN_HEIGHT; ++y) {
                    for (int x = 0; x < SCREEN_WIDTH; ++x) {
                        pixel.x = x * PIXEL_WIDTH;
                        pixel.y = y * PIXEL_HEIGHT;
                        if (!gfx[y * SCREEN_WIDTH + x]) {
                            SDL_FillRect(surface, &pixel, black);
                        } else {
                            SDL_FillRect(surface, &pixel, white);
                        }
                    }
                }
                SDL_UpdateWindowSurface(window);
            }

            ++frameCount;
            // if (frameCount % 60 == 0) {
            //     printf("Test frame: \n");
            //     for (int y = 0; y < SCREEN_HEIGHT; ++y) {
            //         for (int x = 0; x < SCREEN_WIDTH; ++x) {
            //             if (gfx[y * SCREEN_WIDTH + x]) {
            //                 printf("X");
            //             } else {
            //                 printf(" ");
            //             }
            //         }
            //         printf("\n");
            //     }
            // }
        }

        { // Set keys

        }
    }

    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}