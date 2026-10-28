# candyspeak's Erlang tools build into the tree's top-level ebin/, which with
# ebin/candyspeak.app makes the checkout an OTP application: `-pa ebin` from
# the top, or the parent directory on ERL_LIBS, and code:lib_dir(candyspeak)
# finds it. Sources stay here; leex and yecc write their .erl next to the input.
ERLC="$(shell which erlc)"
EBIN=../ebin
ERLC_FLAGS=-MMD -MP -MF .$<.d -I ../.. +debug_info -o $(EBIN)
YRL_SRC=$(wildcard *.yrl)
XRL_SRC=$(wildcard *.xrl)
YRL_XRL_GENERATES=$(YRL_SRC:%.yrl=%.erl) $(XRL_SRC:%.xrl=%.erl)
MODULES=$(YRL_XRL_GENERATES:%.erl=%) candyspeak candyspeak_varp candyspeak_c \
	csp_to_c csp_disasm csp_node csp_to_candyspeak candyspeak_eval
ALL_OBJECTS=$(MODULES:%=$(EBIN)/%.beam)

empty=
space=$(empty) $(empty)
comma=,

.PRECIOUS: $(YRL_XRL_GENERATES)

all: $(ALL_OBJECTS) $(EBIN)/candyspeak.app

clean:
	rm -f $(ALL_OBJECTS) $(EBIN)/candyspeak.app *.core .*.d $(YRL_XRL_GENERATES)

$(EBIN):
	mkdir -p $@

$(EBIN)/%.beam: %.erl | $(EBIN)
	$(ERLC) $(ERLC_FLAGS) $<

# candyspeak.erl calls the parser, so it waits for it
$(EBIN)/candyspeak.beam: $(EBIN)/candyspeak_parse.beam

$(EBIN)/candyspeak.app: Makefile | $(EBIN)
	@printf '{application,candyspeak,\n [{description,"candyspeak tools: parser, varp, C backend, disassembler"},\n  {vsn,"1"},\n  {modules,[%s]},\n  {registered,[]},\n  {applications,[kernel,stdlib]}]}.\n' \
	  "$(subst $(space),$(comma),$(strip $(MODULES)))" > $@

%.erl: %.yrl
	$(ERLC) -o . $<

%.erl: %.xrl
	$(ERLC) -o . $<

.%.d: ;

-include .*.d
