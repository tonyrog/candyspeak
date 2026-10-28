# The panel needs three trees on the code path: candyspeak's own ebin/ (the
# parser and candyspeak_varp, built by utils/Makefile), wse (the remote DOM)
# and this. WSE defaults to ~/erlang/wse. uart is for a node on a serial line,
# and only then.

WSE   ?= $(HOME)/erlang/wse
UART  ?= $(HOME)/erlang/uart
ROOT   = ../..

ERLC   = erlc
EBIN   = ebin
PA     = -pa $(EBIN) -pa $(ROOT)/ebin -pa $(WSE)/ebin -pa $(UART)/ebin

SRC    = $(wildcard src/*.erl)
BEAM   = $(SRC:src/%.erl=$(EBIN)/%.beam)

all: candyspeak $(BEAM) $(EBIN)/csp_panel.app

# utils/Makefile knows what is stale; asking it every time costs nothing and
# keeps one set of rules for the tree's beams.
candyspeak:
	@$(MAKE) --no-print-directory -s -C $(ROOT)/utils

$(EBIN):
	mkdir -p $@

$(EBIN)/%.beam: src/%.erl | $(EBIN)
	$(ERLC) -o $(EBIN) $<

# Only so that `-config panel.config' reaches the panel: the environment of an
# application nobody loaded is not kept. See csp_panel:views/0.
$(EBIN)/csp_panel.app: Makefile | $(EBIN)
	@printf '{application,csp_panel,\n [{description,"candyspeak panel"},\n  {vsn,"1"},\n  {modules,[%s]},\n  {registered,[]},\n  {applications,[kernel,stdlib]},\n  {env,[]}]}.\n' \
	  "$(subst $(space),$(comma),$(strip $(SRC:src/%.erl=%)))" > $@

empty =
space = $(empty) $(empty)
comma = ,

run: all
	erl -s can $(PA) -eval 'csp_panel:start().'  -config panel.config

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

.PHONY: all candyspeak run test varp varp-check clean
