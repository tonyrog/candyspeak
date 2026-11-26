// The C engine (src/csp_lr.inc, included) on the token streams
// check.escript writes: a case is a line per token, `code line len', its
// len bytes of text on the next line (len -1: no text), and `end'. Each
// case comes back as one line: the parse as a tree, (action child ...)
// with a token Tcode or Tcode"text", or `error <line>' -- what
// candyspeak_parse gives for the same tokens.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define LR_PEAK
#include "csp_lr.inc"
#include "csp_lr_act.h"

static const char* const act_name[] = {
#define A(n) #n,
#include "csp_lr_act.def"
#undef A
};

#define MAXTOK 100000
#define MAXNODE 300000

typedef struct {
    int code, line, len;
    char* text;
} tok_t;

typedef struct {
    int act;            // -1: a token
    int tok;            // the token's index
    int kid, next;      // first child, next sibling; -1 none
} node_t;

static tok_t toks[MAXTOK];
static int ntok, pos, at_end;
static node_t nodes[MAXNODE];
static int nnode;

static int next_tok(void* ctx, lr_val_t* v)
{
    (void) ctx;
    if (pos >= ntok) {
	at_end = 1;
	return 0;
    }
    nodes[nnode].act = -1;
    nodes[nnode].tok = pos;
    nodes[nnode].kid = nodes[nnode].next = -1;
    *v = (lr_val_t) nnode++;
    return toks[pos++].code;
}

static lr_val_t reduce(void* ctx, uint8_t act, lr_val_t* v, uint8_t n)
{
    int i, me = nnode++;

    (void) ctx;
    nodes[me].act = act;
    nodes[me].kid = n ? v[0] : -1;
    nodes[me].next = -1;
    for (i = 0; i + 1 < n; i++)
	nodes[v[i]].next = v[i + 1];
    return (lr_val_t) me;
}

static void put_tree(int x)
{
    node_t* d = &nodes[x];

    if (d->act < 0) {
	tok_t* t = &toks[d->tok];
	int i;
	printf("T%d", t->code);
	if (t->len >= 0) {
	    putchar('"');
	    for (i = 0; i < t->len; i++) {
		int c = (unsigned char) t->text[i];
		if (c == '"' || c == '\\')
		    putchar('\\');
		if (c == '\n')
		    fputs("\\n", stdout);
		else
		    putchar(c);
	    }
	    putchar('"');
	}
	return;
    }
    printf("(%s", act_name[d->act]);
    for (x = d->kid; x >= 0; x = nodes[x].next) {
	putchar(' ');
	put_tree(x);
    }
    putchar(')');
}

int main(void)
{
    char line[64];
    lr_val_t root;
    int peak = 0, r;

    while (fgets(line, sizeof(line), stdin)) {
	if (strncmp(line, "end", 3) == 0) {
	    nnode = 0;
	    pos = 0;
	    at_end = 0;
	    r = lr_parse(next_tok, reduce, 0, &root);
	    if (r == LR_OK) {
		put_tree(root);
		putchar('\n');
	    }
	    else {
		// the line of the token it stopped at, as candyspeak_parse says it
		printf("%s %d\n", r == LR_DEEP ? "deep" : "error",
		       at_end ? 999999 : toks[pos - 1].line);
	    }
	    if (lr_peak > peak)
		peak = lr_peak;
	    while (ntok > 0)
		free(toks[--ntok].text);
	    continue;
	}
	{
	    tok_t* t = &toks[ntok++];
	    sscanf(line, "%d %d %d", &t->code, &t->line, &t->len);
	    t->text = malloc(t->len > 0 ? t->len : 1);
	    if (t->len > 0 && fread(t->text, 1, t->len, stdin) != (size_t) t->len)
		return 1;
	    if (t->len >= 0)
		fgetc(stdin);           // the newline after the text
	}
    }
    fprintf(stderr, "lrtest: deepest stack %d\n", peak);
    return 0;
}
