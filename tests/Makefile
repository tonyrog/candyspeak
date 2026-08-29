
line_edit:	csp_line.o line_edit.o
	$(CC) -o$@ csp_line.o line_edit.o

csp_line.o:	../csp_line.c ../csp_line.h
	$(CC) -c -I .. ../csp_line.c

line_edit.o:	line_edit.c
	$(CC) -c line_edit.c

