#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <SDL2/sdl.h>

#ifndef CHIP8_DEBUG
#define CHIP8_DEBUG 0
#endif

#define debug_print(...) \
    do { if (CHIP8_DEBUG > 0) fprintf(stdout, __VA_ARGS__); } while (0)

#define MEM_SZ        0xFFF // 4096
#define ROM_LOC       0x200 // 512
#define MAX_ROM_SZ    (MEM_SZ - ROM_LOC)
#define INSTR_SZ      8
#define MAX_INSTR_CNT (MAX_ROM_SZ / INSTR_SZ)
#define IDENT_SIZE    65
#define INSTR_OP_CNT  3

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

uint16_t Identifier_getLiteralValue(Identifier* ident) {
    uint16_t result = 0;

    if (ident->chars[0] == '0' && ident->chars[1] == 'b') {
        char* end;
        long val = strtol(&ident->chars[2], &end, 2);
        result = (val & 0xFFFF);
    }
    else if (ident->chars[0] == '0' && ident->chars[1] == 'x') {
        char* end;
        long val = strtol(&ident->chars[2], &end, 16);
        result = (val & 0xFFFF);
    }
    else {
        result = atoi(ident->chars);
    }

    return result;
}

uint16_t Identifier_getRegisterIndex(Identifier* ident) {
    char c = ident->chars[1];
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + '9' + 1;

    fprintf(stderr, "Can't get register index for %s\n", ident->chars);
    exit(EXIT_FAILURE);
}




typedef struct {
    Identifier name;
    Identifier ops[INSTR_OP_CNT];
    uint8_t    opCount;
    uint8_t    size;
} Instruction;

void Instruction_clear(Instruction* instr) {
    memset(instr, '\0', sizeof(Instruction));
    instr->opCount = 0;
}

void Instruction_init(Instruction* instr, Identifier name, uint8_t size) {
    Instruction_clear(instr);
    instr->name = name;
    instr->size = size;
}

void Instruction_addOp(Instruction* instr, Identifier op) {
    if (instr->opCount >= INSTR_OP_CNT) {
        fprintf(stderr, "Each instruction can have a maximum of %d operands (%s)\n", INSTR_OP_CNT, instr->name.chars);
        exit(EXIT_FAILURE);
    }
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
           (c >= 'a' && c <= 'z') ||
           c == '[' || c == ']' || c == '_';
}

bool isSymbol(char c) {
    return (c == ';' || c == ':' || c == ',');
}

bool isValidChar(char c) {
    return isWhitespace(c) || isNewline(c) || isWordchar(c) || isSymbol(c);
}

bool isHexChar(char c) {
    return (c >= '0' && c <= '9') ||
           (c >= 'A' && c <= 'F') ||
           (c >= 'a' && c <= 'f');
}

bool isHexStr(const char* str) {
    if (str[0] != '0' || str[1] != 'x') return false;

    size_t i = 2;
    while (str[i] != '\0') {
        if (!isHexChar(str[i])) return false;
        ++i;
    }

    return true;
}

bool isBinChar(char c) {
    return c == 0 || c == 1;
}

bool isBinStr(const char* str) {
    if (str[0] != '0' || str[1] != 'b') return false;

    size_t i = 2;
    while (str[i] != '\0') {
        if (!isBinChar(str[i])) return false;
        ++i;
    }

    return true;
}

bool isNumChar(char c) {
    return (c >= '0' && c <= '9');
}

bool isNumStr(const char* str) {
    size_t i = 0;
    while (str[i] != '\0') {
        if (!isNumChar(str[i])) return false;
        ++i;
    }

    return true;
}

bool isNumeric(Identifier* ident) {
    if (isBinStr(ident->chars)) return true;
    if (isHexStr(ident->chars)) return true;
    if (isNumStr(ident->chars)) return true;

    return false;
}

size_t getWord(const char* source, size_t start, char* tokbuf) {
    size_t end = start;
    while (isWordchar(source[end]) && (end - start < 64)) {
        tokbuf[end-start] = source[end];
        end++;
    }
    return end - start;
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
    IDENT_TYPE_I_REF,
    IDENT_TYPE_F,
    IDENT_TYPE_UNKNOWN
} IdentType;

IdentType getIdentifierType(Identifier* ident, Label* labels, size_t labelCount) {
    Identifier identifierI = Identifier_create("I");
    Identifier identifierIRef = Identifier_create("[I]");
    Identifier identifierF = Identifier_create("F");

    uint16_t labelAddr = getLabelAddress(ident, labels, labelCount);
    if (labelAddr != 0)                             return IDENT_TYPE_LABEL;
    if (ident->chars[0] == 'V')                     return IDENT_TYPE_REGISTER;
    if (Identifier_isEqual(ident, &identifierI))    return IDENT_TYPE_I;
    if (Identifier_isEqual(ident, &identifierIRef)) return IDENT_TYPE_I_REF;
    if (isNumeric(ident))                           return IDENT_TYPE_LITERAL;
    if (Identifier_isEqual(ident, &identifierF))    return IDENT_TYPE_F;

    fprintf(stderr, "ERROR: Unknown identifier \"%s\"\n", ident->chars);
    exit(EXIT_FAILURE);
}

void IdentType_print(IdentType t) {
    switch (t) {
        case IDENT_TYPE_LITERAL: puts("Literal"); break;
        case IDENT_TYPE_REGISTER: puts("Register"); break;
        case IDENT_TYPE_LABEL: puts("Label"); break;
        case IDENT_TYPE_I: puts("I"); break;
        case IDENT_TYPE_I_REF: puts("[I]"); break;
        case IDENT_TYPE_F: puts("F"); break;
        case IDENT_TYPE_UNKNOWN: puts("Unknown"); break;
    }
}

typedef struct {
    Identifier name;
    Identifier value;
} Constant;

void Constant_init(Constant* constant, Identifier name, Identifier value) {
    memset(constant, '\0', sizeof(Constant));
    constant->name = name;
    constant->value = value;
}

bool getConstantValue(Identifier* name, Constant* constants, size_t constantCount, Identifier* value) {
    for (size_t i = 0; i < constantCount; ++i) {
        if (Identifier_isEqual(name, &constants[i].name)) {
            *value = constants[i].value;
            return true;
        }
    }

    return false;
}


typedef struct {
    uint8_t memory[MEM_SZ];
    size_t  offset;
} Rom;

void Rom_init(Rom* rom) {
    rom->offset = 0;
}

size_t Rom_getInstructionCount(Rom* rom) {
    size_t count = rom->offset / sizeof(uint16_t);
    if (rom->offset % sizeof(uint16_t) > 0) count++;
    return count;
}

void Rom_appendInstruction(Rom* rom, uint16_t instruction) {
    debug_print("0x%04lX (%ld): 0x%04X\n", rom->offset, rom->offset, instruction);
    uint16_t* loc = (uint16_t*)&rom->memory[rom->offset];
    *loc = instruction;
    rom->offset += 2;
}

void Rom_appendByte(Rom* rom, uint8_t byte) {
    debug_print("0x%04lX (%ld): 0x%04X\n", rom->offset, rom->offset, byte);
    rom->memory[rom->offset] = byte;
    rom->offset += 1;
}

void Rom_dump(Rom* rom) {
    debug_print("Instruction count: %ld\n", rom->offset);
    size_t count = Rom_getInstructionCount(rom);
    uint16_t* memory = (uint16_t*)rom->memory;
    for (int i = 0; i < count; i++) {
        if (i % 16 == 0) debug_print("0x%04lX: ", i * sizeof(uint16_t));
        debug_print("%04X ", memory[i]);
        if ((i+1) % 16 == 0) debug_print("\n");
    }
    debug_print("\n");
}

void Rom_prepare(Rom* rom) {
    Rom_dump(rom);
    size_t count = Rom_getInstructionCount(rom);
    uint16_t* memory = (uint16_t*)rom->memory;

    for (int i = 0; i < count; i++) {
        uint16_t num = memory[i];
        uint16_t high = (num & 0xFF00) >> 8;
        uint16_t low  = (num & 0x00FF) << 8;
        num = high | low;
        memory[i] = num;
    }
    Rom_dump(rom);
}

int main(int argc, const char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Missing argument: filename.\nUsage: assembler <filename>\n");
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
                fprintf(stderr, "Couldn't read sourceFile\n");
                SDL_RWclose(sourceFile);
                return 1;
            }

            source = sourceWritable;
        } else {
            fprintf(stderr, "Couldn't open file\n");
            return 1;
        }
    }

    debug_print("Loaded %lld bytes of source code\n", size);

    char c;
    size_t start = 0;
    char tokbuf[65];
    size_t instructionCount = 0;
    size_t instructionOffset = 0;
    size_t line = 1;
    Instruction program[MAX_INSTR_CNT];
    Instruction* instr = NULL;
    Identifier byteIdent = Identifier_create("DB");
    Identifier equIdent  = Identifier_create("EQU");

    Label labels[128];
    int labelCount = 0;

    Constant constants[128];
    int constantCount = 0;

    while (start < size) {
        c = source[start];
        if (isWhitespace(c)) {
            start++;
            continue;
        }
        else if (isNewline(c)) {

            if (instr != NULL && Identifier_isEqual(&instr->ops[0], &equIdent)) {
                Constant c;
                Identifier name = instr->name;
                Identifier value = instr->ops[1];
                Constant_init(&c, name, value);
                constants[constantCount] = c;
                constantCount++;

                debug_print("Constant: %s: %s\n", c.name.chars, c.value.chars);

                instructionCount--;
                instructionOffset -= instr->size;
                Instruction_clear(instr);
            }

            instr = NULL;
            start++;
            line++;
            continue;
        }
        else if (c == ';') {
            while (source[start] != '\n' && start < size) {
                start++;
            }
            continue;
        }
        else if (c == ',') {
            start++;
            continue;
        }
        else if (!isValidChar(c)) {
            int invalidIndex = start;
            // walk back to previous newline
            int contextStart = start;
            while (!isNewline(source[contextStart]) && contextStart > 0) contextStart--;
            // walk forward to next newline
            int contextEnd = start;
            while (!isNewline(source[contextEnd]) && contextEnd < size) contextEnd++;
            int contextLength = contextEnd - contextStart;
            int column = invalidIndex - contextStart;

            fprintf(stderr, "Invalid character %c at %ld:%d\n", c, line, column);
            fprintf(stderr, "%.*s\n", contextLength, &source[contextStart]);
            fprintf(stderr, "%*s^\n", column-1, " ");
            exit(EXIT_FAILURE);
        }
        else {
            memset(tokbuf, '\0', IDENT_SIZE);
            size_t len = getWord(source, start, tokbuf);
            size_t end = start + len;
            if (source[end] == ':') {
                Label l;
                Identifier_init(&l.name, tokbuf, IDENT_SIZE);
                l.addr = instructionOffset;
                labels[labelCount] = l;
                labelCount++;

                end++;
            }
            else {
                Identifier ident = Identifier_create(tokbuf);
                if (instr == NULL) {
                    instr = &program[instructionCount];
                    instructionCount++;
                    uint8_t instrSize = (Identifier_isEqual(&ident, &byteIdent) ? 1 : 2);
                    instructionOffset += instrSize;
                    Instruction_init(instr, ident, instrSize);
                }
                else {
                    Instruction_addOp(instr, ident);
                }
            }
            start = end;

            continue;
        }
    }

    debug_print("===\n");
    for (int i = 0; i < instructionCount; ++i) {
        Instruction instr = program[i];

        debug_print("[%d] ", i);
        debug_print("%s", instr.name.chars);

        for (int j = 0; j < instr.opCount; ++j) {
            debug_print(" %s", (instr.ops[j].chars));
        }

        debug_print("\n");
    }

    debug_print("===\n");
    for (int i = 0; i < labelCount; ++i) {
        debug_print("Label: %s (0x%04X)\n", labels[i].name.chars, labels[i].addr + ROM_LOC);
    }

    debug_print("===\n");
    for (int i = 0; i < constantCount; ++i) {
        debug_print("Constant: %s = %s\n", constants[i].name.chars, constants[i].value.chars);
    }

    debug_print("===\n");
    Rom rom;
    Rom_init(&rom);
    for (int i = 0; i < instructionCount; ++i) {
        Instruction instr = program[i];
        Identifier  cmp;
        uint16_t    op;

        Identifier constIdent;
        for (int j = 0; j < instr.opCount; ++j) {
            if (getConstantValue(&instr.ops[j], constants, constantCount, &constIdent)) {
                instr.ops[j] = constIdent;
            }
        }

        debug_print("[%d] ", i);

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
                exit(EXIT_FAILURE);
            }
            Rom_appendInstruction(&rom, op);
            continue;
        }

        cmp = Identifier_create("CALL");
        if (Identifier_isEqual(&instr.name, &cmp)) {
            op = 0x2000;
            IdentType t = getIdentifierType(&instr.ops[0], labels, labelCount);
            if (t == IDENT_TYPE_LABEL) {
                uint16_t addr = (getLabelAddress(&instr.ops[0], labels, labelCount) & 0xFFF);
                op |= addr;
            } else {
                fprintf(stderr, "CALL requires a label operand\n");
                exit(EXIT_FAILURE);
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
                op |= (Identifier_getRegisterIndex(&instr.ops[0]) & 0xF) << 8;
                op |= (Identifier_getLiteralValue(&instr.ops[1]) & 0xFF);
            }
            else if (t1 == IDENT_TYPE_REGISTER && t2 == IDENT_TYPE_REGISTER) {
                op = 0x8000;
                op |= (Identifier_getRegisterIndex(&instr.ops[0]) & 0xF) << 8;
                op |= (Identifier_getRegisterIndex(&instr.ops[0]) & 0xF) << 4;
            }
            else if (t1 == IDENT_TYPE_I && t2 == IDENT_TYPE_LABEL) {
                op = 0xA000;
                op |= (getLabelAddress(&instr.ops[1], labels, labelCount) & 0xFFF);
            }
            else if (t1 == IDENT_TYPE_F && t2 == IDENT_TYPE_REGISTER) {
                op = 0xF029;
                op |= (Identifier_getRegisterIndex(&instr.ops[1]) & 0xF) << 8;
            }
            else if (t1 == IDENT_TYPE_I_REF && t2 == IDENT_TYPE_REGISTER) {
                op = 0xF055;
                op |= (Identifier_getRegisterIndex(&instr.ops[1]) & 0xF) << 8;
            }
            else if (t1 == IDENT_TYPE_REGISTER && t2 == IDENT_TYPE_I_REF) {
                op = 0xF065;
                op |= (Identifier_getRegisterIndex(&instr.ops[0]) & 0xF) << 8;
            }
            else {
                IdentType_print(t1);
                IdentType_print(t2);
                fprintf(stderr, "LD instruction requires one of the following combinations of operands:\n");
                fprintf(stderr, "  register, literal\n");
                fprintf(stderr, "  register, register\n");
                fprintf(stderr, "  I, label\n");
                fprintf(stderr, "  F, register\n");
                fprintf(stderr, "  [I], register\n");
                fprintf(stderr, "  register, [I]\n");
                exit(EXIT_FAILURE);
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
                op |= (Identifier_getRegisterIndex(&instr.ops[0]) & 0xF) << 8;
                op |= (Identifier_getLiteralValue(&instr.ops[1]) & 0xFF);
            }
            else {
                fprintf(stderr, "RND requires register and literal\n");
                exit(EXIT_FAILURE);
            }

            Rom_appendInstruction(&rom, op);
            continue;
        }

        cmp = Identifier_create("AND");
        if (Identifier_isEqual(&instr.name, &cmp)) {
            IdentType t1 = getIdentifierType(&instr.ops[0], labels, labelCount);
            IdentType t2 = getIdentifierType(&instr.ops[1], labels, labelCount);

            if (t1 == IDENT_TYPE_REGISTER && t2 == IDENT_TYPE_REGISTER) {
                op = 0x8002;
                op |= (Identifier_getRegisterIndex(&instr.ops[0]) & 0xF) << 8;
                op |= (Identifier_getRegisterIndex(&instr.ops[1]) & 0xF) << 4;
            }
            else {
                fprintf(stderr, "AND requires two register operands\n");
                exit(EXIT_FAILURE);
            }

            Rom_appendInstruction(&rom, op);
            continue;
        }

        cmp = Identifier_create("SKP");
        if (Identifier_isEqual(&instr.name, &cmp)) {
            IdentType t1 = getIdentifierType(&instr.ops[0], labels, labelCount);

            if (t1 == IDENT_TYPE_REGISTER) {
                op = 0xE09E;
                op |= (Identifier_getRegisterIndex(&instr.ops[0]) & 0xF) << 8;
            }
            else {
                fprintf(stderr, "SKP requires a register operand\n");
                exit(EXIT_FAILURE);
            }

            Rom_appendInstruction(&rom, op);
            continue;
        }

        cmp = Identifier_create("SKNP");
        if (Identifier_isEqual(&instr.name, &cmp)) {
            IdentType t1 = getIdentifierType(&instr.ops[0], labels, labelCount);

            if (t1 == IDENT_TYPE_REGISTER) {
                op = 0xE0A1;
                op |= (Identifier_getRegisterIndex(&instr.ops[0]) & 0xF) << 8;
            }
            else {
                fprintf(stderr, "SKNP requires a register operand\n");
                exit(EXIT_FAILURE);
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
                op |= (Identifier_getRegisterIndex(&instr.ops[0]) & 0xF) << 8;
                op |= (Identifier_getLiteralValue(&instr.ops[1]) & 0xFF);
            }
            else if (t1 == IDENT_TYPE_REGISTER && t2 == IDENT_TYPE_REGISTER) {
                op = 0x5000;
                op |= (Identifier_getRegisterIndex(&instr.ops[0]) & 0xF) << 8;
                op |= (Identifier_getRegisterIndex(&instr.ops[1]) & 0xF) << 4;
            }
            else {
                fprintf(stderr, "SE requires two register operands or register and literal\n");
                exit(EXIT_FAILURE);
            }

            Rom_appendInstruction(&rom, op);
            continue;
        }

        cmp = Identifier_create("SNE");
        if (Identifier_isEqual(&instr.name, &cmp)) {
            IdentType t1 = getIdentifierType(&instr.ops[0], labels, labelCount);
            IdentType t2 = getIdentifierType(&instr.ops[1], labels, labelCount);

            if (t1 == IDENT_TYPE_REGISTER && t2 == IDENT_TYPE_LITERAL) {
                op = 0x4000;
                op |= (Identifier_getRegisterIndex(&instr.ops[0]) & 0xF) << 8;
                op |= (Identifier_getLiteralValue(&instr.ops[1]) & 0xFF);
            }
            else if (t1 == IDENT_TYPE_REGISTER && t2 == IDENT_TYPE_REGISTER) {
                op = 0x9000;
                op |= (Identifier_getRegisterIndex(&instr.ops[0]) & 0xF) << 8;
                op |= (Identifier_getRegisterIndex(&instr.ops[1]) & 0xF) << 4;
            }
            else {
                fprintf(stderr, "SE requires two register operands or register and literal\n");
                exit(EXIT_FAILURE);
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
                op |= (Identifier_getRegisterIndex(&instr.ops[0]) & 0xF) << 8;
                op |= (Identifier_getRegisterIndex(&instr.ops[1]) & 0xF) << 4;
                op |= (Identifier_getLiteralValue(&instr.ops[2]) & 0xF);
            }
            else {
                fprintf(stderr, "DRW requires two register operands and a literal\n");
                exit(EXIT_FAILURE);
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
                op |= (Identifier_getRegisterIndex(&instr.ops[0]) & 0xF) << 8;
                op |= (Identifier_getLiteralValue(&instr.ops[1]) & 0xFF);
            }
            else if (t1 == IDENT_TYPE_I && t2 == IDENT_TYPE_REGISTER) {
                op = 0xF01E;
                op |= (Identifier_getRegisterIndex(&instr.ops[1]) & 0xF) << 8;
            }
            else if (t1 == IDENT_TYPE_REGISTER && t2 == IDENT_TYPE_REGISTER) {
                op = 0x8004;
                op |= (Identifier_getRegisterIndex(&instr.ops[0]) & 0xF) << 8;
                op |= (Identifier_getRegisterIndex(&instr.ops[0]) & 0xF) << 4;
            }
            else {
                IdentType_print(t1);
                IdentType_print(t2);
                fprintf(stderr, "ADD requires a register and a literal operand\n");
                exit(EXIT_FAILURE);
            }

            Rom_appendInstruction(&rom, op);
            continue;
        }

        cmp = Identifier_create("DB");
        if (Identifier_isEqual(&instr.name, &cmp)) {
            Identifier data = instr.ops[0];
            uint8_t byte = 0;

            if (data.chars[0] == '0' && data.chars[1] == 'b') {
                char* end;
                long val = strtol(&data.chars[2], &end, 2);
                byte = (val & 0xFF);
            }
            else if (data.chars[0] == '0' && data.chars[1] == 'x') {
                char* end;
                long val = strtol(&data.chars[2], &end, 16);
                byte = (val & 0xFF);
            }
            else {
                fprintf(stderr, "Only binary and hexadecimal literals currently supported for DB\n");
                exit(EXIT_FAILURE);
            }

            Rom_appendByte(&rom, byte);
            continue;
        }

        cmp = Identifier_create("DW");
        if (Identifier_isEqual(&instr.name, &cmp)) {
            Identifier data = instr.ops[0];

            if (data.chars[0] == '0' && data.chars[1] == 'b') {
                char* end;
                long val = strtol(&data.chars[2], &end, 2);
                op = (val & 0xFFFF);
            }
            else if (data.chars[0] == '0' && data.chars[1] == 'x') {
                char* end;
                long val = strtol(&data.chars[2], &end, 16);
                op = (val & 0xFFFF);
            }
            else {
                fprintf(stderr, "Only binary and hexadecimal literals currently supported for DW\n");
                exit(EXIT_FAILURE);
            }

            Rom_appendInstruction(&rom, op);
            continue;
        }

        debug_print("\n");
    }

    Rom_prepare(&rom);

    SDL_RWops* rw = SDL_RWFromFile("out.ch8", "w");
    if (rw != NULL) {
        size_t count  = Rom_getInstructionCount(&rom);
        bool complete = (SDL_RWwrite(rw, rom.memory, sizeof(uint16_t), count) == count);
        SDL_RWclose(rw);
        if (!complete) {
            fprintf(stderr, "Failed to write output: %s\n", SDL_GetError());
            exit(EXIT_FAILURE);
        }
    }

    return 0;
}