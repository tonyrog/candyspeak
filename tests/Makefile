# The line editor, on its own.
#
# csp_line.c and a terminal emulator, and nothing else -- no runtime, no
# compiler, no parser. That is the whole point of having pulled the editor out
# into its own file with its own state: the thing under test links in two
# objects, so a break in it cannot be hidden by a break somewhere else.
#
# The rest of the suite is driven from the top-level `make test`.

CFLAGS := -Wall -Wextra -Wno-unused-parameter -g -I../include -I../gen -I../src

all:	line_edit

line_edit:	csp_line.o line_edit.o
	$(CC) -o$@ csp_line.o line_edit.o

csp_line.o:	../src/csp_line.c ../include/csp_line.h
	$(CC) $(CFLAGS) -c ../src/csp_line.c

line_edit.o:	line_edit.c ../include/csp_line.h
	$(CC) $(CFLAGS) -c line_edit.c

check:	line_edit
	./line_edit

clean:
	$(RM) line_edit csp_line.o line_edit.o

.PHONY: all check clean
