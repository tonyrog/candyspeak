# build of csp - candy speak

CC = gcc
CFLAGS=-MMD -MP -MF .$<.d
OBJS = csp_linux.o csp_rt.o csp_dump.o csp_eeprom.o \
	csp_parse.o csp_print.o csp_strings.o rom.o

LIBS =

# -O3 -std=c99
CFLAGS += -Wall -g -Wdeclaration-after-statement -Wenum-compare -Wenum-conversion -Wswitch 
LDFLAGS = -g

all:	csp

debug: CFLAGS += -DDEBUG
debug: all

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

.PHONY: all clean test test-examples
