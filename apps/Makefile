# The applications under apps/, each built for a board.
#
#   make -C apps                          what there is
#   make -C apps wintersbone              build it for its own board
#   make -C apps wintersbone BOARD=wintersbone_s3
#   make -C apps wintersbone-upload       build, then load it
#   make -C apps wintersbone-test         its tests, on the host
#
# Anything else on the command line goes on to Makefile.board as it is:
# WATCH=1, MAP=..., DEV=/dev/ttyACM1, EXTRA="-D...". See README.md.

TOP := ..

APPS := lock wintersbone

# The board each one is built for unless BOARD= says another.
lock_BOARD        := mega_bare
wintersbone_BOARD := wintersbone

# How Makefile.board is told which program it is. lock names its pins per board
# (apps/lock/<board>/pins.csp, APP=); wintersbone's boards are its own, and
# their pins.csp is the board's (boards/<board>/pins.csp, PROG=).
lock_HOW        = APP=lock
wintersbone_HOW = PROG=apps/wintersbone/main.csp

board = $(or $(BOARD),$($(1)_BOARD))
mk    = $(MAKE) --no-print-directory -C $(TOP) -f Makefile.board \
	BOARD=$(call board,$(1)) $($(1)_HOW)

list:
	@for a in $(APPS); do \
	     echo "  $$a	(BOARD=$$(sed -n "s/^$${a}_BOARD *:= *//p" Makefile))"; \
	 done

$(APPS):
	$(call mk,$@)

$(APPS:%=%-upload):
	$(call mk,$(@:%-upload=%)) upload

$(APPS:%=%-test):
	$(MAKE) --no-print-directory -C $(TOP) csp
	cd $(TOP) && escript tests/run_tests.escript apps/$(@:%-test=%)/tests

.PHONY: list $(APPS) $(APPS:%=%-upload) $(APPS:%=%-test)
