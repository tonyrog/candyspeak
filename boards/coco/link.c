// CoCo's master protocol: CoCo.ino's command(), over csp_lib_getc/putc.
//
// Compiled into the same unit as the translated main.csp (port/
// csp_lib_prog.c), so it reads csp_in and writes csp_out by member. A write
// lands in the working copy and the program sees it next cycle, as a value
// typed at a prompt would.
//
// ONE BYTE IN, ONE BYTE OUT, and the byte out is the answer to the PREVIOUS
// byte in -- an SPI slave's shift register, which is what the original was.
// Over a UART the same thing is kept by sending the answer to the byte before:
// CoCo.ino's USE_SERIAL loop, c_prev. tools/coco_master.escript talks to it.
//
//   SYN                           -> ACK
//   STATUS, CAPTURE + 8 bytes     -> d_intf d_value a_intf a0 a1 a2 a3 hi
//                                    (hi: two top bits of each value, a0 first)
//   GET  ih il sub + 4 bytes      -> value, big-endian
//   SET  ih il sub v3 v2 v1 v0    -> the OLD value, big-endian
//
// The answer to the index's low byte is nonzero when the dictionary has that
// index -- CoCo.ino's short index, here just 1 -- and the master checks it.
//
// Reading the record to its end is the acknowledgement: Ack clears the flags
// and Wake goes low.

#define SPI_COMMAND_NONE    0x00
#define SPI_COMMAND_SYN     0x01
#define SPI_COMMAND_STATUS  0x02
#define SPI_COMMAND_CAPTURE 0x03
#define SPI_COMMAND_SET     0x04
#define SPI_COMMAND_GET     0x05

#define SPI_REPLY_ACK       0x81
#define SPI_REPLY_NACK      0x8F

// The dictionary, pds_index.h's numbers.
#define INDEX_READ_INPUT8                      0x6000
#define INDEX_POLARITY_INPUT8                  0x6002
#define INDEX_FILTER_CONSTANT_INPUT8           0x6003
#define INDEX_GLOBAL_INTERRUPT_ENABLED_DIGITAL 0x6005
#define INDEX_INTERRUPT_MASK_ANY_CHANGE8       0x6006
#define INDEX_INTERRUPT_MASK_LOW_TO_HIGH8      0x6007
#define INDEX_INTERRUPT_MASK_HIGH_TO_LOW8      0x6008
#define INDEX_ADC_READ16                       0x6401
#define INDEX_ADC_FLAGS                        0x6421
#define INDEX_GLOBAL_INTERRUPT_ENABLED_ANALOG  0x6423
#define INDEX_ADC_UPPER                        0x6424
#define INDEX_ADC_LOWER                        0x6425
#define INDEX_ADC_DELTA                        0x6426
#define INDEX_ADC_NDELTA                       0x6427
#define INDEX_ADC_PDELTA                       0x6428
#define INDEX_ADC_OFFSET                       0x6431
#define INDEX_ADC_SCALE                        0x6432
#define INDEX_ADC_MIN                          0x2768
#define INDEX_ADC_MAX                          0x2769
#define INDEX_ADC_INHIBIT                      0x276A
#define INDEX_ADC_DELAY                        0x276B

static uint8_t known(uint16_t ix)
{
    switch (ix) {
    case INDEX_READ_INPUT8:
    case INDEX_POLARITY_INPUT8:
    case INDEX_FILTER_CONSTANT_INPUT8:
    case INDEX_GLOBAL_INTERRUPT_ENABLED_DIGITAL:
    case INDEX_INTERRUPT_MASK_ANY_CHANGE8:
    case INDEX_INTERRUPT_MASK_LOW_TO_HIGH8:
    case INDEX_INTERRUPT_MASK_HIGH_TO_LOW8:
    case INDEX_ADC_READ16:
    case INDEX_ADC_FLAGS:
    case INDEX_GLOBAL_INTERRUPT_ENABLED_ANALOG:
    case INDEX_ADC_UPPER:
    case INDEX_ADC_LOWER:
    case INDEX_ADC_DELTA:
    case INDEX_ADC_NDELTA:
    case INDEX_ADC_PDELTA:
    case INDEX_ADC_OFFSET:
    case INDEX_ADC_SCALE:
    case INDEX_ADC_MIN:
    case INDEX_ADC_MAX:
    case INDEX_ADC_INHIBIT:
    case INDEX_ADC_DELAY:
	return 1;
    default:
	return 0;
    }
}

// Subindex 1..4 of an ADC index is one channel; 0 and 5.. are not there.
static Analog_t* channel(Main_t* m, uint8_t sub)
{
    switch (sub) {
    case 1: return &m->a1;
    case 2: return &m->a2;
    case 3: return &m->a3;
    case 4: return &m->a4;
    default: return 0;
    }
}

static uint32_t ain(uint8_t sub)
{
    switch (sub) {
    case 1: return csp_in.Ain1;
    case 2: return csp_in.Ain2;
    case 3: return csp_in.Ain3;
    case 4: return csp_in.Ain4;
    default: return 0;
    }
}

// Global (subindex 0) and digital (subindex 1) entries, then the analog ones.
// *ok is cleared for an index:subindex the dictionary does not have.
static uint32_t get_value(uint16_t ix, uint8_t sub, int* ok)
{
    Analog_t* a = channel(&csp_in, sub);

    *ok = 1;
    if (sub == 0) {
	switch (ix) {
	case INDEX_GLOBAL_INTERRUPT_ENABLED_DIGITAL: return csp_in.DigEna;
	case INDEX_GLOBAL_INTERRUPT_ENABLED_ANALOG:  return csp_in.AnaEna;
	}
    }
    else if (sub == 1) {
	switch (ix) {
	case INDEX_READ_INPUT8:                 return csp_in.DValue;
	case INDEX_POLARITY_INPUT8:             return csp_in.Polarity;
	case INDEX_FILTER_CONSTANT_INPUT8:      return csp_in.Filter;
	case INDEX_INTERRUPT_MASK_ANY_CHANGE8:  return csp_in.AnyChange;
	case INDEX_INTERRUPT_MASK_LOW_TO_HIGH8: return csp_in.LowHigh;
	case INDEX_INTERRUPT_MASK_HIGH_TO_LOW8: return csp_in.HighLow;
	}
    }
    if (a) {
	switch (ix) {
	case INDEX_ADC_READ16:  return ain(sub);
	case INDEX_ADC_FLAGS:   return a->flags;
	case INDEX_ADC_UPPER:   return a->upper;
	case INDEX_ADC_LOWER:   return a->lower;
	case INDEX_ADC_DELTA:   return a->delta;
	case INDEX_ADC_NDELTA:  return a->ndelta;
	case INDEX_ADC_PDELTA:  return a->pdelta;
	case INDEX_ADC_OFFSET:  return a->offset;
	case INDEX_ADC_SCALE:   return a->scale;
	case INDEX_ADC_MIN:     return a->min_val;
	case INDEX_ADC_MAX:     return a->max_val;
	case INDEX_ADC_INHIBIT: return a->inhibit_tm;
	case INDEX_ADC_DELAY:   return a->delay_tm;
	}
    }
    *ok = 0;
    return 0;
}

// The read-only entries (READ_INPUT8, ADC_READ16) are not settable.
static int set_value(uint16_t ix, uint8_t sub, uint32_t v)
{
    Analog_t* a = channel(&csp_out, sub);

    if (sub == 0) {
	switch (ix) {
	case INDEX_GLOBAL_INTERRUPT_ENABLED_DIGITAL: csp_out.DigEna = v; return 1;
	case INDEX_GLOBAL_INTERRUPT_ENABLED_ANALOG:  csp_out.AnaEna = v; return 1;
	}
    }
    else if (sub == 1) {
	switch (ix) {
	case INDEX_POLARITY_INPUT8:             csp_out.Polarity = v; return 1;
	case INDEX_FILTER_CONSTANT_INPUT8:      csp_out.Filter = v; return 1;
	case INDEX_INTERRUPT_MASK_ANY_CHANGE8:  csp_out.AnyChange = v; return 1;
	case INDEX_INTERRUPT_MASK_LOW_TO_HIGH8: csp_out.LowHigh = v; return 1;
	case INDEX_INTERRUPT_MASK_HIGH_TO_LOW8: csp_out.HighLow = v; return 1;
	}
    }
    if (a) {
	switch (ix) {
	case INDEX_ADC_FLAGS:   a->flags = v; return 1;
	case INDEX_ADC_UPPER:   a->upper = v; return 1;
	case INDEX_ADC_LOWER:   a->lower = v; return 1;
	case INDEX_ADC_DELTA:   a->delta = v; return 1;
	case INDEX_ADC_NDELTA:  a->ndelta = v; return 1;
	case INDEX_ADC_PDELTA:  a->pdelta = v; return 1;
	case INDEX_ADC_OFFSET:  a->offset = v; return 1;
	case INDEX_ADC_SCALE:   a->scale = v; return 1;
	case INDEX_ADC_MIN:     a->min_val = v; return 1;
	case INDEX_ADC_MAX:     a->max_val = v; return 1;
	case INDEX_ADC_INHIBIT: a->inhibit_tm = v; return 1;
	case INDEX_ADC_DELAY:   a->delay_tm = v; return 1;
	}
    }
    return 0;
}

static uint8_t  in_cmd = SPI_COMMAND_NONE;
static uint8_t  in_len;
static uint16_t in_index;
static uint8_t  in_subind;
static uint32_t in_value;
static uint32_t out_value;
static uint8_t  out_hi;
static uint8_t  rec[8];          // the record, taken when STATUS arrives

static void take_record(void)
{
    uint16_t a[4];
    int i;

    a[0] = csp_in.Ain1; a[1] = csp_in.Ain2;
    a[2] = csp_in.Ain3; a[3] = csp_in.Ain4;
    rec[0] = csp_in.DIntf;
    rec[1] = csp_in.DValue;
    rec[2] = csp_in.AIntf;
    out_hi = 0;
    for (i = 0; i < 4; i++) {
	rec[3 + i] = (uint8_t)a[i];
	out_hi = (uint8_t)((out_hi << 2) | ((a[i] >> 8) & 0x3));
    }
    rec[7] = out_hi;
}

// The answer to byte `inb`, which goes out with the NEXT byte.
static uint8_t command(uint8_t inb)
{
    int ok;

    switch (in_cmd) {
    case SPI_COMMAND_NONE:
	switch (inb) {
	case SPI_COMMAND_SYN:
	    return SPI_REPLY_ACK;
	case SPI_COMMAND_STATUS:
	case SPI_COMMAND_CAPTURE:
	    take_record();
	    in_len = 8;
	    break;
	case SPI_COMMAND_SET:
	case SPI_COMMAND_GET:
	    in_len = 7;
	    break;
	default:
	    return SPI_REPLY_NACK;
	}
	in_cmd = inb;
	return 0x80 | inb;

    case SPI_COMMAND_SET:
    case SPI_COMMAND_GET:
	switch (in_len--) {
	case 7: in_index = inb; return 0;
	case 6:
	    in_index = (uint16_t)((in_index << 8) | inb);
	    return known(in_index);
	case 5:
	    in_subind = inb;
	    out_value = get_value(in_index, in_subind, &ok);
	    return (in_cmd == SPI_COMMAND_GET) ? in_subind : 0;
	case 4: in_value = inb;                     return (uint8_t)(out_value >> 24);
	case 3: in_value = (in_value << 8) | inb;   return (uint8_t)(out_value >> 16);
	case 2: in_value = (in_value << 8) | inb;   return (uint8_t)(out_value >> 8);
	case 1:
	    in_value = (in_value << 8) | inb;
	    if (in_cmd == SPI_COMMAND_SET)
		(void)set_value(in_index, in_subind, in_value);
	    in_cmd = SPI_COMMAND_NONE;
	    return (uint8_t)out_value;
	}
	break;

    case SPI_COMMAND_STATUS:
    case SPI_COMMAND_CAPTURE:
	if (in_len == 0)
	    break;
	in_len--;
	if (in_len == 0) {
	    in_cmd = SPI_COMMAND_NONE;
	    csp_out.Ack = 1;
	}
	return rec[7 - in_len];
    }
    in_cmd = SPI_COMMAND_NONE;
    return 0;
}

void csp_lib_poll(void)
{
    static uint8_t prev = 0;
    int c;

    while ((c = csp_lib_getc()) >= 0) {
	uint8_t next = command((uint8_t)c);
	csp_lib_putc((char)prev);
	prev = next;
    }
}
