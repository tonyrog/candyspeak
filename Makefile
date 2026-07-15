# build of csp - candy speak

CC = gcc
CFLAGS=-MMD -MP -MF .$<.d
OBJS = csp_linux.o csp_rt.o csp_dump.o csp_eeprom.o \
	csp_parse.o csp_print.o csp_strings.o rom.o

LIBS =

# -O3 -std=c99
CFLAGS += -Wall -g -Wdeclaration-after-statement -Wenum-compare -Wenum-conversion -Wswitch
LDFLAGS = -g

# `make ubsan` / `make san` fill SAN with these for a host build (targets below).
SAN =
SANFLAGS = -fsanitize=undefined,address -fno-omit-frame-pointer -O0
CFLAGS  += $(SAN)
LDFLAGS += $(SAN)

all:	csp

debug: CFLAGS += -DDEBUG
debug: all

# Sanitizer build (host): catches misaligned access -- the Cortex-M0 HardFault
# class that x86 tolerates silently -- plus out-of-bounds and other UB. Forces a
# clean rebuild so every object carries the flags. Then run e.g.:
#   UBSAN_OPTIONS=halt_on_error=0 ./csp examples/cpx.csp
ubsan:
	$(MAKE) clean
	$(MAKE) all SAN='$(SANFLAGS)'

# One-shot sanitizer check: clean rebuild with UBSan+ASan, run the whole unit
# suite (seq + the reactive-marked variants) under it, then restore a normal
# binary so leftover sanitized objects never break the next plain `make`.
san:
	$(MAKE) clean
	$(MAKE) all SAN='$(SANFLAGS)'
	ASAN_OPTIONS=detect_leaks=0 $(MAKE) test
	$(MAKE) clean
	$(MAKE) all

csp:	$(OBJS)
	$(CC) $(LDFLAGS) $(OBJS) $(LIBS) -o $@

# String table: a small C tool generates csp_strings.{c,h} from strings.tab.
strtab: strtab.c
	$(CC) -Wall -o $@ strtab.c

csp_strings.c csp_strings.h: strings.tab strtab
	./strtab strings.tab csp_strings.c csp_strings.h

# sources that use the shared RODATA strings need the generated header first
csp_rt.o csp_strings.o: csp_strings.h

clean:
	rm -f $(OBJS) strtab csp_strings.c csp_strings.h

test:	csp
	@chmod +x tests/run_tests.escript
	@cd $(CURDIR) && escript tests/run_tests.escript tests/unit

test-examples: csp
	@chmod +x tests/run_tests.escript
	@cd $(CURDIR) && escript tests/run_tests.escript examples

%.o:	%.c
	$(CC) $(CFLAGS) -c -fPIC $<

.%.d:	;

-include .*.d

.PHONY: all clean test test-examples debug ubsan san
