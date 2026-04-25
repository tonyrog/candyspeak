# build of csp - candy speak

CFLAGS=-MMD -MP -MF .$<.d 
OBJS = csp_linux.o csp_rt.o csp_format.o csp_dump.o
LIBS =

# -O3 -std=c99
CFLAGS+= -Wall -g -Wswitch  -Wenum-compare -Wenum-conversion  -Wswitch
LFFLAGS = -g

all:	csp

csp:	$(OBJS)
	$(CC) $(LDFLAGS) $(OBJS) $(LIBS) -o $@

%.o:	%.c
	$(CC) $(CFLAGS) -c -fPIC $<

.%.d:	;

-include .*.d
