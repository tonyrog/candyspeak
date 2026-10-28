# The panel needs three trees on the code path: candyspeak's own ebin/ (the
# parser and candyspeak_varp, built by utils/Makefile), wse (the remote DOM)
# and this. WSE defaults to ~/erlang/wse. uart is for a node on a serial line,
# and only then. ellm (and the rester it talks through) is for the chat, and
# only then: without it on the path there is no chat button.

WSE   ?= $(HOME)/erlang/wse
UART  ?= $(HOME)/erlang/uart
ELLM  ?= $(HOME)/erlang/ellm
RESTER ?= $(HOME)/erlang/rester
ROOT   = ../..

ERLC   = erlc
EBIN   = ebin
PA     = -pa $(EBIN) -pa $(ROOT)/ebin -pa $(WSE)/ebin -pa $(UART)/ebin \
	 -pa $(ELLM)/ebin -pa $(RESTER)/ebin

all: candyspeak
	@if [ -d "src" -a -f "src/Makefile" ]; then (cd src && $(MAKE) all); fi

# utils/Makefile knows what is stale; asking it every time costs nothing and
# keeps one set of rules for the tree's beams.
candyspeak:
	@$(MAKE) --no-print-directory -s -C $(ROOT)/utils

run: all
	erl -s can $(PA) -s csp_panel -config panel.config

# The stub wse must shadow the real one. Each -pa prepends, so the LAST one
# given wins -- test/ebin goes at the end, not the front.
test: all
	mkdir -p test/ebin
	$(ERLC) -o test/ebin test/wse.erl test/panel_test.erl test/fake_llm.erl
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
