# Simply use arduino-cli
#
FQBN=arduino:avr:uno
DEV=/dev/serial/by-id/usb-Arduino_Srl_Arduino_Uno_55431313038351C04281-if00
BPATH="./build/tmp.arduino.avr.uno"
# The UNO is the tightest AVR target that still fits: 32 256 usable (optiboot is
# 512 bytes, against the Micro's 4K Caterina) and 2 048 of RAM.
MAX_FLASH=32256
MAX_RAM=2048
BOARD=-DCSP_BOARD=boards/uno.h
# -fno-inline-functions-called-once ONLY. This used to also carry -fno-inline,
# -fno-ipa-cp-clone, -fno-ipa-sra, -fno-partial-inlining and -fno-ipa-icf, and
# every one of those made the image BIGGER: measured 32 156 -> 30 492 bytes just
# by dropping them, which is the difference between 100 bytes of margin and
# 1 764. The IPA passes look wasteful up close -- constant-propagating `st` turns
# every st->field into a 4-byte absolute lds instead of a 2-byte indexed ldd --
# but they pay for themselves several times over in specialisation, and
# -fno-ipa-icf switches off exactly the identical-function folding this code
# leans on. The one flag that IS worth keeping stops gcc folding every
# single-call function into its caller, which buries csp_eval_rule and
# csp_rt_start inside main and makes `make size` useless. It costs nothing.
INLINE=-fno-inline-functions-called-once $(PROLOGUES)

# -mcall-prologues: 30 414 -> 28 446 on this target. AVR's ABI makes r2-r17 and
# r28-r29 call-saved, so every value a function keeps alive across a call has to
# be pushed in the prologue and popped in the epilogue -- and a 32-bit value is
# four registers. Our big functions carry 16-18 push/pop each because of it.
# This flag replaces those sequences with a call to __prologue_saves__ /
# __epilogue_restores__ in libgcc: one shared copy for the whole image. It costs
# cycles per call, so it is a variable -- PROLOGUES= trades it back.
PROLOGUES=-mcall-prologues

# Linker relaxation: call/jmp (4 bytes) becomes rcall/rjmp (2) wherever the
# target is within +/-2K. This belongs on the LINK step -- -mrelax in
# compiler.c.extra_flags does nothing, because arduino-cli links separately.
RELAX=--build-property "compiler.c.elf.extra_flags=-Wl,--relax"

OPTS=--build-property "compiler.c.extra_flags=-Os $(BOARD) $(INLINE) $(EXTRA)" \
     --build-property "compiler.cpp.extra_flags=-Os $(BOARD) $(INLINE) $(EXTRA)" \
     $(RELAX)

compile:
	arduino-cli compile -e --fqbn $(FQBN) $(OPTS) --build-path $(BPATH)

clean:
	arduino-cli compile --clean --fqbn $(FQBN) .

upload:
	arduino-cli upload -p $(DEV) --fqbn $(FQBN)

exec:
	$(MAKE) -f Makefile.uno compile EXTRA="-DCSP_EXEC_ONLY"
	@$(MAKE) -f Makefile.uno --no-print-directory check

# --- the guard ---------------------------------------------------------------
# `exec` runs this itself. It exists because the UNO margin is small enough that
# it can be spent without anyone noticing: arduino-cli only complains once the
# image no longer fits at all, and by then you have to bisect to find out which
# change did it. This prints the margin every build, so a change that eats 300
# bytes is visible the day it lands.
#
# RAM is reported but not enforced. With CSP_ARENA_MALLOC the pool is claimed at
# boot from whatever is free, so .data+.bss is not the whole story -- it is the
# floor, and what is left over is arena plus stack. On a 2K part that leftover,
# not flash, is what limits how large a program can be.
check:
	@f=$$(avr-size -A $(BPATH)/CandySpeak.ino.elf | \
	      awk '/^\.text/{t=$$2} /^\.data/{d=$$2} END{print t+d}'); \
	 r=$$(avr-size -A $(BPATH)/CandySpeak.ino.elf | \
	      awk '/^\.data/{d=$$2} /^\.bss/{b=$$2} END{print d+b}'); \
	 printf "flash %6d / %d   margin %+6d\n" $$f $(MAX_FLASH) $$(($(MAX_FLASH)-$$f)); \
	 printf "ram   %6d / %d   free   %6d  (arena + stack)\n" $$r $(MAX_RAM) $$(($(MAX_RAM)-$$r)); \
	 if [ $$f -gt $(MAX_FLASH) ]; then \
	   echo "FAIL: image does not fit an UNO"; exit 1; fi

size:
	@avr-nm --size-sort -S -C $(BPATH)/CandySpeak.ino.elf | \
	  awk '$$3=="T"||$$3=="t"{n=strtonum("0x"$$2); t+=n; printf "%6d  %s\n", n, $$4}' | \
	  sort -rn | head -100
	@avr-nm --size-sort -S -C $(BPATH)/CandySpeak.ino.elf | \
	  awk '$$3=="T"||$$3=="t"{n=strtonum("0x"$$2); t+=n; \
	    if ($$4 ~ /^(csp_|setup_|eval[0-9]|eval_op|est_|fn_|add_|op_info|tok_table|decl_table|build_dis_ip|can_mark_fields|main$$)/) ours+=n; \
	    else core+=n } \
	    END {printf "\n%6d  CandySpeak (main = setup+loop, inlined)\n%6d  core/libc\n%6d  TOTAL text\n", ours, core, t}'
