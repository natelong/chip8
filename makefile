CCFLAGS+=-Wall
OUTDIR=bin
SRCDIR=src
LIBS=-lSDL2

debug: CCFLAGS += -DCHIP8_DEBUG -g
debug: all

release: CCFLAGS += -O3
release: all

all: chip8 disassembler assembler

chip8: $(SRCDIR)/chip8.c
	mkdir -p $(OUTDIR)
	$(CC) -o $(OUTDIR)/chip8 $(CCFLAGS) $(LIBS) $(SRCDIR)/chip8.c

disassembler: $(SRCDIR)/disassembler.c
	mkdir -p $(OUTDIR)
	$(CC) -o $(OUTDIR)/disassembler $(CCFLAGS) $(LIBS) $(SRCDIR)/disassembler.c

assembler: $(SRCDIR)/assembler.c
	mkdir -p $(OUTDIR)
	$(CC) -o $(OUTDIR)/assembler $(CCFLAGS) $(LIBS) $(SRCDIR)/assembler.c

clean:
	rm -rf bin