#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <SDL2/sdl.h>

#define MEM_SZ        0xFFF // 4096
#define ROM_LOC       0x200 // 512
#define MAX_ROM_SZ    (MEM_SZ - ROM_LOC)
#define INSTR_SZ      8
#define MAX_INSTR_CNT (MAX_ROM_SZ / INSTR_SZ)
#define IDENT_SIZE    65

typedef struct {
    char chars[IDENT_SIZE];
} Identifier;

void Identifier_init(Identifier* ident, char* chars, size_t len) {
    memset(ident, '\0', sizeof(Identifier));
    memcpy(ident->chars, chars, len);
}

Identifier Identifier_create(char* chars) {
    Identifier ident;
    memset(&ident, '\0', sizeof(Identifier));
    Identifier_init(&ident, chars, strlen(chars));
    return ident;
}

bool Identifier_isEqual(Identifier* ident1, Identifier* ident2) {
    return (strncmp(ident1->chars, ident2->chars, IDENT_SIZE) == 0);
}

typedef struct {
    int        opCount;
    Identifier name;
    Identifier ops[3];
} Instruction;

void Instruction_init(Instruction* instr, Identifier name) {
    memset(instr, '\0', sizeof(Instruction));
    instr->opCount = 0;
    instr->name = name;
}

void Instruction_addOp(Instruction* instr, Identifier op) {
    instr->ops[instr->opCount++] = op;
}

typedef struct {
    Identifier name;
    uint16_t   addr;
} Label;

bool isWhitespace(char c) {
    return c == ' ' || c == '\t'; // || c == '\n';
}

bool isNewline(char c) {
    return c == '\n';
}

bool isWordchar(char c) {
    return (c >= '0' && c <= '9') ||
           (c >= 'A' && c <= 'Z') ||
           (c >= 'a' && c <= 'z');
}

bool isNumeric(Identifier* ident) {
    for (size_t i = 0; i < IDENT_SIZE; i++) {
        char c = ident->chars[i];
        if ((c < '0' || c > '9') && c != '\0') return false;
    }

    return true;
}

uint16_t getLabelAddress(Identifier* name, Label* labels, size_t labelCount) {
    for (size_t i = 0; i < labelCount; i++) {
        if (Identifier_isEqual(name, &labels[i].name)) {
            return labels[i].addr + ROM_LOC;
        }
    }

    return 0; // there's gotta be a better return value here
}

typedef enum {
    IDENT_TYPE_LITERAL,
    IDENT_TYPE_REGISTER,
    IDENT_TYPE_LABEL,
    IDENT_TYPE_I,
    IDENT_TYPE_UNKNOWN
} IdentType;

IdentType getIdentifierType(Identifier* ident, Label* labels, size_t labelCount) {
    uint16_t labelAddr = getLabelAddress(ident, labels, labelCount);
    if (labelAddr != 0) {
        return IDENT_TYPE_LABEL;
    } else if (ident->chars[0] == 'V') {
        return IDENT_TYPE_REGISTER;
    } else if (ident->chars[0] == 'I') {
        return IDENT_TYPE_I;
    } else if (isNumeric(ident)) {
        return IDENT_TYPE_LITERAL;
    }

    fprintf(stderr, "ERROR: Unknown identifier \"%s\"\n", ident->chars);
    exit(1);
}

void printIdentifierType(IdentType t) {
    switch (t) {
        case IDENT_TYPE_LITERAL: puts("Literal"); break;
        case IDENT_TYPE_REGISTER: puts("Register"); break;
        case IDENT_TYPE_LABEL: puts("Label"); break;
        case IDENT_TYPE_I: puts("I"); break;
        case IDENT_TYPE_UNKNOWN: puts("Unknown"); break;
    }
}

uint16_t getLiteralValue(Identifier* ident) {
    return atoi(ident->chars);
}

uint16_t getRegisterIndex(Identifier* ident) {
    return atoi(&ident->chars[1]);
}

size_t getWord(const char* source, size_t start, char* tokbuf) {
    size_t end = start;
    while (isWordchar(source[end]) && (end - start < 64)) {
        tokbuf[end-start] = source[end];
        end++;
    }
    return end - start;
}


typedef struct {
    uint16_t memory[MEM_SZ];
    size_t   offset;
} Rom;

void Rom_init(Rom* rom) {
    rom->offset = 0;
}

void Rom_appendInstruction(Rom* rom, uint16_t instruction) {
    printf("0x%04X (%ld): 0x%04X\n", (uint16_t)(rom->offset*sizeof(uint16_t)), rom->offset, instruction);
    rom->memory[rom->offset] = instruction;
    rom->offset++;
}

void Rom_dump(Rom* rom) {
    printf("Instruction count: %ld\n", rom->offset);
    for (int i = 0; i < rom->offset; i++) {
        if (i % 16 == 0) printf("0x%04lX: ", i * sizeof(uint16_t));
        printf("%04X ", rom->memory[i]);
        if ((i+1) % 16 == 0) printf("\n");
    }
    printf("\n");
}

void Rom_prepare(Rom* rom) {
    Rom_dump(rom);
    for (int i = 0; i < rom->offset; i++) {
        uint16_t num = rom->memory[i];
        uint16_t high = (num & 0xFF00) >> 8;
        uint16_t low  = (num & 0x00FF) << 8;
        num = high | low;
        rom->memory[i] = num;
    }
    Rom_dump(rom);
}

int main(int argc, const char* argv[]) {
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

    char c;
    size_t start = 0;
    char tokbuf[65];
    size_t instructionCount = 0;
    Instruction program[MAX_INSTR_CNT];
    Instruction* instr = NULL;

    Label labels[128];
    int labelCount = 0;

    while (start < size) {
        c = source[start];
        if (isWhitespace(c)) {
            start++;
            continue;
        }
        else if (isNewline(c)) {
            instr = NULL;
            start++;
            continue;
        }
        else if (c == ';') {
            while (source[start] != '\n') {
                start++;
            }
            continue;
        }
        else if (c == ',') {
            start++;
            continue;
        }
        else {
            memset(tokbuf, '\0', IDENT_SIZE);
            size_t len = getWord(source, start, tokbuf);
            // printf("[%ld]: \"%s\"\n", start, tokbuf);
            size_t end = start + len;
            if (source[end] == ':') {
                Label l;
                Identifier_init(&l.name, tokbuf, IDENT_SIZE);
                l.addr = instructionCount * 2;
                labels[labelCount] = l;
                labelCount++;

                end++;
            }
            else {
                Identifier ident = Identifier_create(tokbuf);
                if (instr == NULL) {
                    instr = &program[instructionCount];
                    instructionCount++;
                    Instruction_init(instr, ident);
                }
                else {
                    Instruction_addOp(instr, ident);
                }
            }
            start = end;
            continue;
        }
    }

    puts("===");
    for (int i = 0; i < instructionCount; ++i) {
        Instruction instr = program[i];

        printf("[%d] ", i);
        printf("%s", instr.name.chars);

        for (int j = 0; j < instr.opCount; ++j) {
            printf(" %s", (instr.ops[j].chars));
        }

        printf("\n");
    }

    puts("===");
    for (int i = 0; i < labelCount; ++i) {
        printf("Label: %s (0x%04X)\n", labels[i].name.chars, labels[i].addr + ROM_LOC);
    }

    puts("===");
    Rom rom;
    Rom_init(&rom);
    for (int i = 0; i < instructionCount; ++i) {
        Instruction instr = program[i];
        Identifier  cmp;
        uint16_t    op;

        printf("[%d] ", i);

        cmp = Identifier_create("CLS");
        if (Identifier_isEqual(&instr.name, &cmp)) {
            op = 0x00E0;
            Rom_appendInstruction(&rom, op);
            continue;
        }

        cmp = Identifier_create("RET");
        if (Identifier_isEqual(&instr.name, &cmp)) {
            op = 0x00EE;
            Rom_appendInstruction(&rom, op);
            continue;
        }

        cmp = Identifier_create("JP");
        if (Identifier_isEqual(&instr.name, &cmp)) {
            op = 0x1000;
            IdentType t = getIdentifierType(&instr.ops[0], labels, labelCount);
            if (t == IDENT_TYPE_LABEL) {
                uint16_t addr = (getLabelAddress(&instr.ops[0], labels, labelCount) & 0xFFF);
                op |= addr;
            } else {
                fprintf(stderr, "Nope! Jump needs a label\n");
                exit(1);
            }
            Rom_appendInstruction(&rom, op);
            continue;
        }

        cmp = Identifier_create("LD");
        if (Identifier_isEqual(&instr.name, &cmp)) {
            IdentType t1 = getIdentifierType(&instr.ops[0], labels, labelCount);
            IdentType t2 = getIdentifierType(&instr.ops[1], labels, labelCount);

            if (t1 == IDENT_TYPE_REGISTER && t2 == IDENT_TYPE_LITERAL) {
                op = 0x6000;
                op |= (getRegisterIndex(&instr.ops[0]) & 0xF) << 8;
                op |= (getLiteralValue(&instr.ops[1]) & 0xFF);
            }
            else if (t1 == IDENT_TYPE_I && t2 == IDENT_TYPE_LABEL) {
                op = 0xA000;
                op |= (getLabelAddress(&instr.ops[1], labels, labelCount) & 0xFFF);
            }
            else {
                fprintf(stderr, "nope. types:\n");
                printIdentifierType(t1);
                printIdentifierType(t2);
                exit(1);
            }
            Rom_appendInstruction(&rom, op);
            continue;
        }

        cmp = Identifier_create("RND");
        if (Identifier_isEqual(&instr.name, &cmp)) {
            IdentType t1 = getIdentifierType(&instr.ops[0], labels, labelCount);
            IdentType t2 = getIdentifierType(&instr.ops[1], labels, labelCount);

            if (t1 == IDENT_TYPE_REGISTER && t2 == IDENT_TYPE_LITERAL) {
                op = 0xC000;
                op |= (getRegisterIndex(&instr.ops[0]) & 0xF) << 8;
                op |= (getLiteralValue(&instr.ops[1]) & 0xFF);
            }
            else {
                fprintf(stderr, "RND requires register and literal\n");
                exit(1);
            }

            Rom_appendInstruction(&rom, op);
            continue;
        }

        cmp = Identifier_create("SE");
        if (Identifier_isEqual(&instr.name, &cmp)) {
            IdentType t1 = getIdentifierType(&instr.ops[0], labels, labelCount);
            IdentType t2 = getIdentifierType(&instr.ops[1], labels, labelCount);

            if (t1 == IDENT_TYPE_REGISTER && t2 == IDENT_TYPE_LITERAL) {
                op = 0x3000;
                op |= (getRegisterIndex(&instr.ops[0]) & 0xF) << 8;
                op |= (getLiteralValue(&instr.ops[1]) & 0xFF);
            }
            else if (t1 == IDENT_TYPE_REGISTER && t2 == IDENT_TYPE_REGISTER) {
                op = 0x5000;
                op |= (getRegisterIndex(&instr.ops[0]) & 0xF) << 8;
                op |= (getRegisterIndex(&instr.ops[1]) & 0xF) << 4;
            }
            else {
                printf("SE requires two register operands or register and literal\n");
                exit(1);
            }

            Rom_appendInstruction(&rom, op);
            continue;
        }

        cmp = Identifier_create("DRW");
        if (Identifier_isEqual(&instr.name, &cmp)) {
            IdentType t1 = getIdentifierType(&instr.ops[0], labels, labelCount);
            IdentType t2 = getIdentifierType(&instr.ops[1], labels, labelCount);
            IdentType t3 = getIdentifierType(&instr.ops[2], labels, labelCount);

            if (t1 == IDENT_TYPE_REGISTER && t2 == IDENT_TYPE_REGISTER && t3 == IDENT_TYPE_LITERAL) {
                op = 0xD000;
                op |= (getRegisterIndex(&instr.ops[0]) & 0xF) << 8;
                op |= (getRegisterIndex(&instr.ops[1]) & 0xF) << 4;
                op |= (getLiteralValue(&instr.ops[2]) & 0xF);
            }
            else {
                printf("DRW requires two register operands and a literal\n");
                exit(1);
            }

            Rom_appendInstruction(&rom, op);
            continue;
        }

        cmp = Identifier_create("ADD");
        if (Identifier_isEqual(&instr.name, &cmp)) {
            IdentType t1 = getIdentifierType(&instr.ops[0], labels, labelCount);
            IdentType t2 = getIdentifierType(&instr.ops[1], labels, labelCount);

            if (t1 == IDENT_TYPE_REGISTER && t2 == IDENT_TYPE_LITERAL) {
                op = 0x7000;
                op |= (getRegisterIndex(&instr.ops[0]) & 0xF) << 8;
                op |= (getLiteralValue(&instr.ops[1]) & 0xFF);
            }
            else {
                printf("ADD requires a register and a literal operand\n");
                exit(1);
            }

            Rom_appendInstruction(&rom, op);
            continue;
        }

        cmp = Identifier_create("DATA");
        if (Identifier_isEqual(&instr.name, &cmp)) {
            Identifier data = instr.ops[0];

            if (data.chars[0] == '0' && data.chars[1] == 'b') {
                char* end;
                long val = strtol(&data.chars[2], &end, 2);
                op = (val & 0xFFFF);
            }
            else {
                fprintf(stderr, "Non-binary literals not currently supported for DATA\n");
                exit(1);
            }

            Rom_appendInstruction(&rom, op);
            continue;
        }

        printf("\n");
    }

    Rom_prepare(&rom);

    SDL_RWops* rw = SDL_RWFromFile("out.ch8", "w");
    if (rw != NULL) {
        bool complete = (SDL_RWwrite(rw, rom.memory, sizeof(uint16_t), rom.offset) == rom.offset);
        SDL_RWclose(rw);
        if (!complete) {
            fprintf(stderr, "Failed to write output: %s\n", SDL_GetError());
            exit(1);
        }
    }

    return 0;
}