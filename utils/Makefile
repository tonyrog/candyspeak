ERLC="$(shell which erlc)"
ERLC_FLAGS=-MMD -MP -MF .$<.d -I ../.. +debug_info
YRL_SRC=$(wildcard *.yrl)
XRL_SRC=$(wildcard *.xrl)
YRL_XRL_GENERATES=$(YRL_SRC:%.yrl=%.erl) $(XRL_SRC:%.xrl=%.erl)
ERL_SOURCES=$(YRL_XRL_GENERATES)
ERL_OBJECTS=$(ERL_SOURCES:%.erl=%.beam)
ALL_OBJECTS=$(ERL_OBJECTS) candyspeak.beam
ERL_MODULES=$(ERL_SOURCES:%.erl=%)

.PRECIOUS: $(YRL_SRC:%.yrl=%.erl) $(XRL_SRC:%.xrl=%.erl)

all: $(ALL_OBJECTS)

clean:
	rm -f $(ALL_OBJECTS) *.core .*.d $(YRL_XRL_GENERATES)

%.beam: %.erl
	$(ERLC) $(ERLC_FLAGS) $<

%.erl: %.yrl
	$(ERLC) $<

%.erl: %.xrl
	$(ERLC) $<

.%.d: ;

-include .*.d

