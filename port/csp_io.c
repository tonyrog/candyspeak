// THE I/O SWEEP, for every port whose chip layer speaks csp_chip_io.h.
//
// What a port's csp_input/csp_output used to spell out for itself: walk
// st->nio, take a rule's configuration request down, and move each #digital
// and #analog between its value slot and the pin. The LPC, STM32 and Arduino
// ports each carried a copy of the same loop with only the leaf calls
// different; with the leaves behind csp_chip_* there is nothing left that
// differs, and this is the one copy.
//
// The declared WIDTH lives here too. The chip layer moves sixteen bits both
// ways, so `#analog Pot:10` is a shift by six whatever the converter has --
// and signedness, the default for an #analog, is half scale off the bottom.
// That was a scale function per port, each reading the declaration; three of
// them read it before loading it.

#include "csp.h"
#include "csp_chip_io.h"
#include "csp_lib.h"
#include "csp_io.h"

// DIR_IN is an enum, so the preprocessor cannot see it: a negative array size
// is the check that works in every C the ports are built with.
typedef char csp_chip_dir_agrees[((CSP_CHIP_IN == DIR_IN) &&
				  (CSP_CHIP_OUT == DIR_OUT)) ? 1 : -1];

// The declared width and signedness, out of ONE declaration lookup -- on AVR
// each decl() is a PROGMEM copy. The arithmetic itself is csp_lib's, which is
// what C generated from a .csp uses too.
static uint8_t io_shape(csp_rt_t* st, int di, uint8_t* sgn)
{
    const csp_decl_t* d = csp_decl_ref(st, di);

    *sgn = (CSP_MASK(csp_decl_get_vt(d), TYPE_BITS) != V_UNSIGNED);
    return (uint8_t)GET_RES(csp_decl_get_res(d));
}

static ivalue_t io_ain(csp_rt_t* st, int di, uint16_t raw)
{
    uint8_t sgn;
    uint8_t res = io_shape(st, di, &sgn);
    return (ivalue_t)csp_lib_ain(raw, res, sgn);
}

static uint16_t io_aout(csp_rt_t* st, int di, uint16_t val)
{
    uint8_t sgn;
    uint8_t res = io_shape(st, di, &sgn);
    return csp_lib_aout(val, res, sgn);
}

static void io_dcfg(value_t* vptr)
{
    csp_chip_dcfg(value_get_d_port(vptr), value_get_d_pin(vptr),
		  value_get_d_dir(vptr),
		  (uint8_t)((value_get_d_pullup(vptr) ? CSP_CHIP_PULLUP : 0) |
			    (value_get_d_pulldown(vptr) ? CSP_CHIP_PULLDOWN : 0)));
}

static void io_acfg(value_t* vptr)
{
    csp_chip_acfg(value_get_a_port(vptr), value_get_a_pin(vptr),
		  value_get_a_dir(vptr), value_get_a_pwm(vptr));
}

// Apply a configuration a rule asked for, and take the request down in BOTH
// slots -- the pair is copied on commit, so clearing one leaves a stale request
// in the other that spends a config call on some later cycle.
//
// d.cfg and a.cfg do NOT land on the same bit (digital has pullup/pulldown ahead
// of it, analog only pwm), so the flag is cleared through the member that set it.
static void io_reconfig(csp_rt_t* st, index_t ix, int analog)
{
    value_t* iptr;
    value_t* optr;

    csp_dio_slots(st, ix, &iptr, &optr);
    if (analog) {
	io_acfg(optr);
	value_set_a_cfg(iptr, 0);
	value_set_a_cfg(optr, 0);
    }
    else {
	io_dcfg(optr);
	value_set_d_cfg(iptr, 0);
	value_set_d_cfg(optr, 0);
    }
}

// An inout pin RESTS as an input: it is borrowed for the length of one write
// and handed straight back, which is what a pin shared with another device
// needs.
static void io_dout(value_t* vptr)
{
    uint8_t port = value_get_d_port(vptr);
    uint8_t pin  = value_get_d_pin(vptr);

    if (value_get_d_dir(vptr) & DIR_IN) {
	csp_chip_dcfg(port, pin, DIR_OUT, 0);
	csp_chip_dout(port, pin, value_get_d_val(vptr));
	io_dcfg(vptr);
    }
    else
	csp_chip_dout(port, pin, value_get_d_val(vptr));
}

// want is CSP_IO_CONFIG, DIR_IN or DIR_OUT. The configuration request is
// checked on both directions -- whichever list a pin is served in, a rule that
// turned it round is honoured BEFORE the pin is read or written, or the first
// sample after a flip comes off the old mode.
void csp_io_sweep(csp_rt_t* st, uint8_t want)
{
    int i;

    for (i = 0; i < st->nio; i++) {
	index_t ix = csp_io_at(st, i);              // binds the entry's object
	int di = INDEX(ix);
	value_t* vptr = csp_dio_slot(st, ix, DOUT);

	switch (decl(st, di, type)) {
	case DECL_DIGITAL:
	    if (want == CSP_IO_CONFIG) {
		io_dcfg(vptr);
		break;
	    }
	    if (value_get_d_cfg(vptr))
		io_reconfig(st, ix, 0);
	    if (!(value_get_d_dir(vptr) & want))
		break;
	    if (want & DIR_IN)
		csp_set_ivalue(st, ix,
			       csp_chip_din(value_get_d_port(vptr),
					    value_get_d_pin(vptr)));
	    else
		io_dout(vptr);
	    break;
	case DECL_ANALOG:
	    if (want == CSP_IO_CONFIG) {
		io_acfg(vptr);
		break;
	    }
	    if (value_get_a_cfg(vptr))
		io_reconfig(st, ix, 1);
	    if (!(value_get_a_dir(vptr) & want))
		break;
	    if (want & DIR_IN)
		csp_set_ivalue(st, ix,
			       io_ain(st, di,
				      csp_chip_ain(value_get_a_port(vptr),
						   value_get_a_pin(vptr))));
	    else
		csp_chip_aout(value_get_a_port(vptr), value_get_a_pin(vptr),
			      io_aout(st, di, value_get_a_val(vptr)));
	    break;
	default:
	    break;
	}
    }
    csp_ctx_reset(st);
}
