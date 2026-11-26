# candyspeak's Erlang tools build into the tree's top-level ebin/, which with
# ebin/csp_tools.app makes them the OTP application csp_tools: `-pa ebin` from
# the top, and application:load(csp_tools) finds it. The modules find the tree
# by code:which, not code:lib_dir, so the checkout's own name does not matter.
# Sources stay here; leex writes its .erl next to the input. The parser is
# candyspeak_parse (an LR engine) on candyspeak_parse_tab, which `make lr` at
# the top writes from candyspeak.y, the grammar C parses with too.
ERLC="$(shell which erlc)"
EBIN=../ebin
ERLC_FLAGS=-MMD -MP -MF .$<.d -I ../.. +debug_info -o $(EBIN)
XRL_SRC=$(wildcard *.xrl)
XRL_GENERATES=$(XRL_SRC:%.xrl=%.erl)
MODULES=$(XRL_GENERATES:%.erl=%) candyspeak_parse candyspeak_parse_act \
	candyspeak_parse_tab candyspeak candyspeak_varp candyspeak_c \
	csp_to_c csp_disasm csp_asm csp_list csp_node csp_patch csp_to_candyspeak \
	candyspeak_eval csp_tools csp_cc csp_img csp_cmd csp_ports
ALL_OBJECTS=$(MODULES:%=$(EBIN)/%.beam)

empty=
space=$(empty) $(empty)
comma=,
VERSION=$(shell git describe --always --tags)
APP_TARGET=$(EBIN)/csp_tools.app

.PRECIOUS: $(XRL_GENERATES)

all: $(ALL_OBJECTS) $(APP_TARGET)

clean:
	rm -f $(ALL_OBJECTS) $(APP_TARGET) *.core .*.d $(XRL_GENERATES)

$(EBIN):
	mkdir -p $@

$(EBIN)/%.beam: %.erl | $(EBIN)
	$(ERLC) $(ERLC_FLAGS) $<

# The words for /dump's and /patch's ERR codes, from the C header that numbers
# them: `RE_NAME = N,  // text' -> reply_text(N) -> "text".
csp_reply.hrl: ../include/csp_reply.h
	@{ echo '%% Generated from include/csp_reply.h -- do not edit.'; \
	   sed -n 's|^ *RE_[A-Z_]* = \([0-9]*\),\{0,1\} *// \(.*\)$$|reply_text(\1) -> "\2";|p' $<; \
	   echo 'reply_text(_) -> none.'; } > $@

$(EBIN)/csp_cmd.beam: csp_reply.hrl

# candyspeak.erl calls the parser, so it waits for it
$(EBIN)/candyspeak.beam: $(EBIN)/candyspeak_parse.beam

$(APP_TARGET): csp_tools.app.src Makefile | $(EBIN)
	sed -e 's;{vsn,.*git};{vsn,"$(VERSION)"};' \
	    -e 's;"@@MODULES@@";$(subst $(space),$(comma),$(strip $(MODULES)));' \
	    csp_tools.app.src > $@

%.erl: %.xrl
	$(ERLC) -o . $<

.%.d: ;

-include .*.d
