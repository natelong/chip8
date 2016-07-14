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

    if (argc != 2) {
        printf("Missing argument: filename.\nUsage: assembler <filename>\n");
        return 1;
    }

    const char* filename = argv[1];
    const char* source;

    int64_t size;
    { // Load File
        SDL_RWops* sourceFile = SDL_RWFromFile(filename, "rb");
        if (sourceFile != NULL) {
            size = SDL_RWsize(sourceFile);

            char* sourceWritable = (char*)malloc(size);

            if (SDL_RWread(sourceFile, sourceWritable, size, 1) > 0) {
                SDL_RWclose(sourceFile);
            } else {
                printf("Couldn't read sourceFile\n");
                SDL_RWclose(sourceFile);
                return 1;
            }

            source = sourceWritable;
        } else {
            printf("Couldn't open file\n");
            return 1;
        }
    }

    printf("Source: \n%s\n", source);

    return 0;
}