# The panel needs three trees on the code path: candyspeak's own utils/ (the
# parser), wse (the remote DOM) and this. WSE defaults to ~/erlang/wse.

WSE   ?= $(HOME)/erlang/wse
ROOT   = ../..
UTILS  = $(ROOT)/utils

ERLC   = erlc
EBIN   = ebin
PA     = -pa $(EBIN) -pa $(UTILS)/ebin -pa $(WSE)/ebin

SRC    = $(wildcard src/*.erl)
BEAM   = $(SRC:src/%.erl=$(EBIN)/%.beam)

# candyspeak's scanner and parser. Real targets, not .PHONY: rebuilding leex
# and yecc on every start costs a couple of seconds and produces the same two
# files, so they are rebuilt only when the .xrl/.yrl actually change.
# candyspeak_varp is not the parser, but `make varp' and the tests call it, and
# left off this list its beam was never rebuilt: the tests ran yesterday's.
PARSER = $(UTILS)/ebin/candyspeak.beam \
	 $(UTILS)/ebin/candyspeak_scan.beam \
	 $(UTILS)/ebin/candyspeak_parse.beam \
	 $(UTILS)/ebin/candyspeak_varp.beam

all: $(PARSER) $(BEAM)

$(EBIN) $(UTILS)/ebin:
	mkdir -p $@

$(EBIN)/%.beam: src/%.erl | $(EBIN)
	$(ERLC) -o $(EBIN) $<

$(UTILS)/ebin/%.beam: $(UTILS)/%.erl | $(UTILS)/ebin
	$(ERLC) -o $(UTILS)/ebin $<

# leex and yecc write next to their input, so run them there
$(UTILS)/candyspeak_scan.erl: $(UTILS)/candyspeak_scan.xrl
	cd $(UTILS) && erl -noshell -eval 'leex:file("candyspeak_scan.xrl"),halt().'

$(UTILS)/candyspeak_parse.erl: $(UTILS)/candyspeak_parse.yrl
	cd $(UTILS) && erl -noshell -eval 'yecc:file("candyspeak_parse.yrl"),halt().'

# candyspeak.erl calls the parser, so it waits for it
$(UTILS)/ebin/candyspeak.beam: $(UTILS)/ebin/candyspeak_parse.beam

run: all
	erl $(PA) -eval 'csp_panel:start().'

# The stub wse must shadow the real one. Each -pa prepends, so the LAST one
# given wins -- test/ebin goes at the end, not the front.
test: all
	mkdir -p test/ebin
	$(ERLC) -o test/ebin test/wse.erl test/panel_test.erl
	CSP_EXE=$(abspath $(ROOT)/csp) \
	  erl -noshell $(PA) -pa test/ebin -s panel_test run

# Generate a varp model from a .csp and check it has transitions at all. The
# consistency probe is not optional: an inconsistent `next' makes every property
# provable, which looks authoritative and means nothing.
VARP_SH ?= $(HOME)/erlang/varp/priv/varp.sh
CSP     ?= demo/home.csp

# Output is <name>_gen.varp, NOT <name>.varp: the hand-written models in demo/
# are commentary and were overwritten once already by a careless default.
varp: all
	erl -noshell $(PA) -eval \
	  'candyspeak_varp:file("$(CSP)", "$(basename $(CSP))_gen.varp"), halt().'
	@echo "wrote $(basename $(CSP))_gen.varp"

varp-check: all
	erl -noshell $(PA) -eval \
	  'candyspeak_varp:check("../../examples/*.csp", "$(VARP_SH)"), halt().'

clean:
	rm -rf $(EBIN) test/ebin

.PHONY: all run test varp varp-check clean
