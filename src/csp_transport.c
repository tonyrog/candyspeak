// Weak do-nothing defaults for the three transports that are not CAN.
//
// A port implements what its hardware has and links this for the rest, the same
// way csp_can_none.c serves a board with no bus. The point is that a PROGRAM
// does not have to know: a `#buffer Imu:14 in i2c 3 0x68 0x3B` compiles, links
// and runs on a host with no I2C at all -- it simply never delivers, `Imu.rx`
// stays false, and the rules guarded on it do not fire.
//
// That is deliberate. The alternative -- a link error naming a hook -- would
// mean a program could not be developed on the host and moved to the board,
// which is most of what the host build is for.
//
// WEAK, so a port's own definition replaces this without a header saying which
// one won. The one trap is that a weak symbol in the SAME translation unit as
// its caller can be resolved at compile time; these are in a file of their own
// for that reason.

#include <stdint.h>
#include <stddef.h>
#include "csp.h"

#if defined(__GNUC__)
#define CSP_WEAK __attribute__((weak))
#else
#define CSP_WEAK
#endif

// --- UDP --------------------------------------------------------------------

CSP_WEAK int csp_udp_open(csp_rt_t* st, uint16_t port)
{
    (void)st; (void)port;
    return -1;                     // no stack: nothing to bind
}

CSP_WEAK int csp_udp_recv(csp_rt_t* st, uint16_t port, uint8_t* data,
			  uint16_t* len)
{
    (void)st; (void)port; (void)data; (void)len;
    return 0;                      // nothing pending, ever
}

CSP_WEAK int csp_udp_send(csp_rt_t* st, uint32_t addr, uint16_t port,
			  const uint8_t* data, uint16_t len)
{
    (void)st; (void)addr; (void)port; (void)data; (void)len;
    return -1;
}

// --- I2C --------------------------------------------------------------------

CSP_WEAK int csp_i2c_start(csp_rt_t* st, uint32_t xref, uint8_t* data,
			   uint16_t len, int is_read)
{
    (void)st; (void)xref; (void)data; (void)len; (void)is_read;
    // -1, not 0: a start that reports success and never completes leaves
    // BUF_F_BUSY set forever, and the buffer stops trying. Refusing keeps the
    // slot free, so a port that gains a bus later starts working with no other
    // change.
    return -1;
}

CSP_WEAK int csp_i2c_done(csp_rt_t* st, uint32_t xref, uint16_t* len)
{
    (void)st; (void)xref; (void)len;
    return -1;
}

// --- SPI --------------------------------------------------------------------

CSP_WEAK int csp_spi_start(csp_rt_t* st, uint32_t xref, uint8_t* data,
			   uint16_t len, int is_read)
{
    (void)st; (void)xref; (void)data; (void)len; (void)is_read;
    return -1;
}

CSP_WEAK int csp_spi_done(csp_rt_t* st, uint32_t xref, uint16_t* len)
{
    (void)st; (void)xref; (void)len;
    return -1;
}
