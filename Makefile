# build of csp - candy speak

CC = gcc
CFLAGS=-MMD -MP -MF .$<.d
OBJS = csp_linux.o csp_rt.o csp_format.o csp_dump.o csp_eeprom.o csp_parse.o
LIBS =

# -O3 -std=c99
CFLAGS+= -Wall -g -Wswitch  -Wenum-compare -Wenum-conversion  -Wswitch
LDFLAGS = -g

all:	csp

debug: CFLAGS += -DDEBUG
debug: all

csp:	$(OBJS)
	$(CC) $(LDFLAGS) $(OBJS) $(LIBS) -o $@

clean:
	rm -f $(OBJS)

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
