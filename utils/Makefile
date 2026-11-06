# candyspeak's Erlang tools build into the tree's top-level ebin/, which with
# ebin/csp_tools.app makes them the OTP application csp_tools: `-pa ebin` from
# the top, and application:load(csp_tools) finds it. The modules find the tree
# by code:which, not code:lib_dir, so the checkout's own name does not matter.
# Sources stay here; leex and yecc write their .erl next to the input.
ERLC="$(shell which erlc)"
EBIN=../ebin
ERLC_FLAGS=-MMD -MP -MF .$<.d -I ../.. +debug_info -o $(EBIN)
YRL_SRC=$(wildcard *.yrl)
XRL_SRC=$(wildcard *.xrl)
YRL_XRL_GENERATES=$(YRL_SRC:%.yrl=%.erl) $(XRL_SRC:%.xrl=%.erl)
MODULES=$(YRL_XRL_GENERATES:%.erl=%) candyspeak candyspeak_varp candyspeak_c \
	csp_to_c csp_disasm csp_asm csp_list csp_node csp_to_candyspeak candyspeak_eval \
	csp_tools
ALL_OBJECTS=$(MODULES:%=$(EBIN)/%.beam)

empty=
space=$(empty) $(empty)
comma=,
VERSION=$(shell git describe --always --tags)
APP_TARGET=$(EBIN)/csp_tools.app

.PRECIOUS: $(YRL_XRL_GENERATES)

all: $(ALL_OBJECTS) $(APP_TARGET)

clean:
	rm -f $(ALL_OBJECTS) $(APP_TARGET) *.core .*.d $(YRL_XRL_GENERATES)

$(EBIN):
	mkdir -p $@

$(EBIN)/%.beam: %.erl | $(EBIN)
	$(ERLC) $(ERLC_FLAGS) $<

# candyspeak.erl calls the parser, so it waits for it
$(EBIN)/candyspeak.beam: $(EBIN)/candyspeak_parse.beam

$(APP_TARGET): csp_tools.app.src Makefile | $(EBIN)
	sed -e 's;{vsn,.*git};{vsn,"$(VERSION)"};' \
	    -e 's;"@@MODULES@@";$(subst $(space),$(comma),$(strip $(MODULES)));' \
	    csp_tools.app.src > $@

%.erl: %.yrl
	$(ERLC) -o . $<

%.erl: %.xrl
	$(ERLC) -o . $<

.%.d: ;

-include .*.d
