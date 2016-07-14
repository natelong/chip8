#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <SDL2/sdl.h>

const int MEMORY_SIZE  = 4096;
const int ROM_OFFSET   = 0x200; // 512
const int MAX_ROM_SIZE = MEMORY_SIZE - ROM_OFFSET;

typedef struct {
    uint8_t  memory[MEMORY_SIZE];
    uint16_t pc;
} Emulator;

void preamble(uint16_t pc, uint16_t opcode) {
    printf("%04X: (%04X) ", pc, opcode);
}

int main(int argc, const char* argv[]) {
    Emulator emu;

    const char* filename = "roms/Maze.ch8";
    if (argc == 2) {
        filename = argv[1];
    }

    int64_t size;
    { // Load ROM
        SDL_RWops* rom = SDL_RWFromFile(filename, "rb");
        if (rom != NULL) {
            size = SDL_RWsize(rom);

            if (size > MAX_ROM_SIZE) {
                printf("ROM too big!\n");
                SDL_RWclose(rom);
                return 1;
            }

            uint8_t* data = emu.memory + ROM_OFFSET;

            if (SDL_RWread(rom, data, size, 1) > 0) {
                SDL_RWclose(rom);
            } else {
                printf("Couldn't read file\n");
                SDL_RWclose(rom);
                return 1;
            }
        } else {
            printf("Couldn't open file\n");
            return 1;
        }
    }

    uint16_t opcode;
    uint16_t addr;
    uint8_t  x;
    uint8_t  y;
    uint8_t  z;
    uint8_t  yz;
    for (emu.pc = 0x200; emu.pc < ROM_OFFSET + size; emu.pc += 2) {
        opcode = emu.memory[emu.pc] << 8 | emu.memory[emu.pc + 1];

        preamble(emu.pc, opcode);

        addr = (opcode & 0x0FFF);
        x    = (opcode & 0x0F00) >> 8;
        y    = (opcode & 0x00F0) >> 4;
        z    = (opcode & 0x000F);
        yz   = (opcode & 0x00FF);

        switch (opcode & 0xF000) {
            case 0x0000: {
                switch (opcode) {
                    case 0x00E0: printf("CLS\n"); break;
                    case 0x00EE: printf("RET\n"); break;
                    default:     printf("Unknown opcode\n");
                }
            }
            break;
            case 0x1000: printf("JP   0x%04X\n", addr); break;
            case 0x2000: printf("CALL 0x%04X\n", addr); break;
            case 0x3000: printf("SE   V%d,\t%d\n", x, yz); break;
            case 0x4000: printf("SNE  V%d,\t%d\n", x, yz); break;
            case 0x5000: printf("SE   V%d,\tV%d\n", x, y); break;
            case 0x6000: printf("LD   V%d,\t%d\n", x, yz); break;
            case 0x7000: printf("ADD  V%d,\t%d\n", x, yz); break;
            case 0x8000: {
                switch (z) {
                    case 0x0: printf("LD   V%d,\tV%d\n", x, y); break;
                    case 0x1: printf("OR   V%d,\tV%d\n", x, y); break;
                    case 0x2: printf("AND  V%d,\tV%d\n", x, y); break;
                    case 0x3: printf("XOR  V%d,\tV%d\n", x, y); break;
                    case 0x4: printf("ADD  V%d,\tV%d\n", x, y); break;
                    case 0x5: printf("SUB  V%d,\tV%d\n", x, y); break;
                    case 0x6: printf("SHR  V%d,\t{V%d}\n", x, y); break;
                    case 0x7: printf("SUBN V%d,\tV%d\n", x, y); break;
                    case 0xE: printf("SHL  V%d,\t{V%d}\n", x, y); break;
                    default:     printf("Unknown opcode\n");
                }
            }
            break;
            case 0x9000: printf("SNE  V%d, V%d\n", x, y);
            case 0xA000: printf("LD   I,\t%d\n", addr); break;
            case 0xB000: printf("JP   V0\t%d\n", addr); break;
            case 0xC000: printf("RND  V%d,\t%d\n", x, yz); break;
            case 0xD000: printf("DRW  V%d,\tV%d,\t%d\n", x, y, z); break;
            case 0xE000: {
                switch (yz) {
                    case 0x9E: printf("SKP  V%d\n", x); break;
                    case 0xA1: printf("SKNP V%d\n", x); break;
                }
            }
            break;
            case 0xF000: {
                switch (yz) {
                    case 0x07: printf("LD   V%d,\tDT\n", x); break;
                    case 0x0A: printf("LD   V%d\tK\n", x); break;
                    case 0x15: printf("LD   DT,\tV%d\n", x); break;
                    case 0x18: printf("LD   ST,\tV%d\n", x); break;
                    case 0x1E: printf("ADD  I,\tV%d\n", x); break;
                    case 0x29: printf("LD   F,\tV%d\n", x); break;
                    case 0x33: printf("LD   B,\tV%d\n", x); break;
                    case 0x55: printf("LD   [I]\tV%d\n", x); break;
                    case 0x65: printf("LD   V%d\t[I]\n", x); break;
                    default:     printf("Unknown opcode\n");
                }
            }
            break;
            default: printf("Unknown opcode\n");
        }
    }

    return 0;
}