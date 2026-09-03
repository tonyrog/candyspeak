# Chip descriptions

What a PART is, as data. One directory per vendor, and inside it the names the
vendor uses — so you recognise them from the data sheet rather than from ours.

    chips/nxp/lpc2000.terms    the family: IAP, RAM base, how the PLL works
    chips/nxp/212x.terms       the group: sector geometry, default flash map
    chips/nxp/2129.terms       the part:  id, flash size, peripherals

`NNNx` is a group, `NNNN` is one part. A part names its group, a group names its
family, and each level states only what it changes — an LPC2129 file is four
lines because everything else is already true of a 212x.

## Why terms and not C

Because it is data. Thirty LPC variants differing in four fields is a table
someone has to be able to read and a script has to be able to check, and C
initialisers are neither. `utils/gen_chips.erl` turns these into C at build
time.

It also lets the generator do arithmetic the C compiler cannot: solving a PLL
against a board's crystal, and failing the BUILD when the numbers do not meet
instead of leaving a node running at the wrong clock.

The copy this replaces had the LPC1754 at 160K flash. The part has 128, and the
data sheet line was sitting in the sketch's own per-board Makefile the whole
time.

## Generated on demand, not checked in

    make chip CHIP=lpc2129      # csp_chip.c + csp_chip.h, that part only
    make chips                  # list what is known
    csp_chips.c                 # every part, for the host tools

A target carries the geometry of the part it IS. The host tools (`csp --devices`,
`csp --ld=lpc2129`) need to be able to name any of them, so they get the lot --
but out of the same terms, which is the point of moving them here.

## The three levels

| | says | changes when |
|---|---|---|
| family | how the part family works — IAP entry, command numbers, PLL equation | never |
| group  | sector geometry and a default flash map | you pick a different variant |
| chip   | id, flash and RAM size, which peripherals exist | you pick a different part |

## The flash map

Every group carries a default: which sectors are the runtime, which are
application slots, where settings live.

    {map, '212x',
     [{runtime, 0,  7},        %%  64K
      {app, "A", 8,  8},       %%  64K  one sector, one slot
      {app, "B", 9,  9},       %%  64K
      {store,   10, 16}]}.     %%  56K

It is a DEFAULT and a board may override it, because the board is what knows
whether settings go in flash or in an I2C EEPROM — and that answer changes the
map. A board with external EEPROM has no `store` region and spends those sectors
on applications instead.

Two rules that are not arbitrary:

**A slot should be whole sectors, and preferably one.** Erasing is per sector,
so a slot that shares a sector with anything else cannot be rewritten on its
own. The two 64K sectors in the middle of a 212x are why that part is pleasant
for A/B.

**The store goes in SMALL sectors.** Rewriting one tuning costs a whole sector.
8K is a better price than 32K, which is why the LPC1754 map puts `store` at
sectors 14..15 and leaves both big ones for the slots.

## Adding a part

If it is a variant of a group that exists, it is four lines:

    {chip, lpc2124, [{group,'212x'}, {id,16#0101FF13},
                     {flash_kb,256}, {ram_kb,16},
                     {peripherals,[uart,i2c,spi,adc,pwm,rtc,wdt]}]}.

If its sector table is its own, give it a group file too — that is what
`2103.terms` is: a part whose geometry nothing else shares.

A part with no `{map, ...}` is geometry only. Nothing can be written to it until
someone decides where things go, which is a decision and not a default:
`nregion == 0` makes every `csp_region_find` miss.
