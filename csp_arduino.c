#include <Arduino.h>
#include <stdlib.h>

#include "csp.h"

uint32_t csp_time_ms(void)
{
    return millis();

unsigned long csp_time_us(void)
{
    return micros();
}

void csp_setup(csp_parser_t* par, csp_state_t* st)
{
    int i;
    value_t res = 0;
    
    // setup in and inout (inout startup as input)
    for (i = 0; i < par->ni; i++) {
	index_t ix = st->input[i];
	int j = INDEX(ix);
	switch(par->decl[j].t) {
	case DECL_DIGITAL:
            if (par->decl[j].in) {
		if (par->decl[j].pullup)
		    pinMode(par->decl[j].pin, INPUT_PULLUP);
		else
		    pinMode(par->decl[j].pin, INPUT);
	    }
	    break;
	case DECL_ANALOG:
	    if (par->decl[j].in && par->decl[j].res)
		res = max(res, par->decl[j].res);
	    break;
	default:
	    break;
	}
    }

    if (res)
	analogReadResolution(res);


    // setup output (that is NOT inout)
    for (i = 0; i < par->no; i++) {
	index_t ix = st->output[i];
	int j = INDEX(ix);
	if (par->decl[j].in) continue;
	switch(par->decl[j].t) {
	case DECL_DIGITAL:
	    if (par->decl[j].out)
		pinMode(par->decl[j].pin, OUTPUT);
	    break;
	case DECL_ANALOG:
	    if (par->decl[j].out && par->decl[j].pwm)
		pinMode(s->decl[j].pin, OUTPUT);
	    break;
	default:
	    break;
	}
    }
}

void csp_input(csp_rt_t* st)
{
    int i;
    
    for (i = 0; i < st->ni; i++) {
	index_t ix = st->input[i];
	int j = INDEX(ix);
	switch(st->decl[j].t) {
	case DECL_DIGITAL:
	    if (st->decl[j].in)
		st->val[ix] = digitalRead(st->decl[j].pin);
	    break;
	case DECL_ANALOG:
	    if (st->decl[j].in)
		st->val[ix] = analogRead(st->decl[j].pin);
	    break;
	default:
	    break;
	}
    }
    for (i = 0; i< st->nt; i++) {
	index_t ix = st->timer[i];
	int j = INDEX(ix);
	if (st->decl[j].running) {
	    unsigned long now_ms = csp_time_ms();
	    if ((now_ms-st->decl[j].t0_ms) >= st->decl[j].period_ms) {
		st->decl[j].t0_ms = now_ms;
		st->val[ix] = 1;
		if (!st->val[ix])
		    csp_set_value(st, ix, 1);
	    }
	    break;
	}
    }
}

void csp_output(csp_rt_t* st)
{
    int i;

    for (i = 0; i < st->no; ++i) {
	index_t ix = st->output[i];
	int j = INDEX(ix);
	switch(st->decl[j].t) {
	case DECL_DIGITAL:
	    if (st->decl[j].out) {
		if (st->decl[j].in) {
		    pinMode(st->decl[j].pin, OUTPUT);
		    digitalWrite(st->decl[j].pin, st->val[ix]);
		    // prepare for next input
		    if (st->decl[j].pullup)
			pinMode(st->decl[j].pin, INPUT_PULLUP);
		    else
			pinMode(st->decl[j].pin, INPUT);
		}
		else { // plain out
		    digitalWrite(st->decl[j].pin, st->val[ix]);
		}
	    }
	    break;
	case DECL_ANALOG:
	    if ((st->decl[j].out) && (st->decl[j].pwm)) {
		int val = map(st->val[ix],
			      0, (1<<st->decl[j].res)-1,
			      0, 255);
		analogWrite(st->decl[j].pin, val);
	    }
	    break;
	default:
	    break;
	}
    }
    for (i = 0; i < st->nt; ++i) {
	index_t ix = st->timer[i];
	int j = INDEX(ix);
	if (!st->decl[j].running && st->val[ix]) {
	    // start
	    st->decl[i].running = 1;
	    st->decl[i].t0_ms = now_ms;
	    st->val[ix] = 0; // not timeout
	}
    }
}
