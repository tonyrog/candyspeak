// The image, as bytes.
//
// `csp -C` emits a C STRUCT, which is what a firmware links -- but a flash tool
// needs the same thing as a run of bytes. Rather than write a second emitter
// (two emitters is how a header and a section CRC come to disagree), this links
// the generated file and dumps what the compiler laid out: byte-for-byte what
// the target reads back, alignment and padding included.
//
// Not called directly: tools/csp-image compiles the program, builds this against
// the generated image and runs it. Reach for that.
//
//   tools/csp-image prog.csp      # tmp/prog.rom.c, .img and .hex
//   tools/csp-image -p prog.csp   # hex on stdout, straight into /upgrade
//
// Hex on stdout is the default because that is the format the receiver takes:
// `{ echo /upgrade A; mkimage; echo .; } | ./csp -i --flash=f.bin` is the whole
// upgrade path, host-side, with no board and no serial port. `-b FILE` writes
// the raw bytes instead.

#include <stdio.h>
#include <string.h>
#include "csp.h"

// The image the linked rom.c carries. `rom` is the default --prefix.
extern const csp_image_ref_t rom_image;

#define PER_LINE 32

int main(int argc, char* argv[])
{
    const uint8_t* p = rom_image.base;
    csp_image_header_t h;
    uint32_t i;
    FILE* f;

    memcpy(&h, p, sizeof(h));
    if ((h.magic[0] != CSP_IMAGE_MAGIC0) || (h.magic[1] != CSP_IMAGE_MAGIC1) ||
	(h.magic[2] != CSP_IMAGE_MAGIC2) || (h.magic[3] != CSP_IMAGE_MAGIC3)) {
	fprintf(stderr, "mkimage: not an image (bad magic)\n");
	return 1;
    }
    if (csp_crc16(0xFFFF, &h, sizeof(h) - sizeof(uint16_t), 0) != h.crc_hdr) {
	fprintf(stderr, "mkimage: header CRC bad\n");
	return 1;
    }
    if ((argc >= 3) && (strcmp(argv[1], "-b") == 0)) {
	if ((f = fopen(argv[2], "wb")) == NULL) {
	    fprintf(stderr, "mkimage: cannot write %s\n", argv[2]);
	    return 1;
	}
	if (fwrite(p, 1, h.size, f) != h.size) {
	    fclose(f);
	    return 1;
	}
	fclose(f);
	// Nothing on success: csp-image reports the size, the role and the
	// generation itself, and a helper that chatters on stderr is a helper
	// whose stderr gets redirected -- which is how a real failure came to
	// be silent.
	return 0;
    }
    // Hex, and nothing else on stdout: the receiver reads this as data.
    for (i = 0; i < h.size; i++) {
	printf("%02X", p[i]);
	if (((i + 1) % PER_LINE) == 0)
	    printf("\n");
    }
    if ((h.size % PER_LINE) != 0)
	printf("\n");
    return 0;
}
