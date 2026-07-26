// bitflip -- flip a single bit in a file, in place. The "poke" primitive for
// the CRC destroyer harness (no perl/python needed).
//
//   bitflip <file> <bitindex>          flip one bit (bit = byte*8 + bitInByte)
//   bitflip <file> <byteindex> <bit>   flip bit <bit> (0..7) of byte <byteindex>
//
// Exit 0 on success, 1 on any error (bad args, out-of-range, I/O).
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv)
{
    FILE* f;
    long byte, bit;
    int c;

    if (argc == 3) {
	long idx = strtol(argv[2], NULL, 0);
	byte = idx >> 3;
	bit  = idx & 7;
    } else if (argc == 4) {
	byte = strtol(argv[2], NULL, 0);
	bit  = strtol(argv[3], NULL, 0);
    } else {
	fprintf(stderr, "usage: %s <file> <bitindex> | <file> <byte> <bit>\n",
		argv[0]);
	return 1;
    }
    if ((bit < 0) || (bit > 7) || (byte < 0))
	return 1;

    if ((f = fopen(argv[1], "r+b")) == NULL)
	return 1;
    if (fseek(f, byte, SEEK_SET) != 0)      { fclose(f); return 1; }
    if ((c = fgetc(f)) == EOF)              { fclose(f); return 1; }
    c ^= (1 << bit);
    if (fseek(f, byte, SEEK_SET) != 0)      { fclose(f); return 1; }
    if (fputc(c, f) == EOF)                 { fclose(f); return 1; }
    fclose(f);
    return 0;
}
