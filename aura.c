#include "aura.h"
#include "astack.h"
#include "aparser.h"
#include "aword.h"
#include "atype.h"
#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>

#define AURA_MAXPROG 4096
#define AURA_LOCALFRAMESIZE 32
#define AURA_MAXFRAME 32

struct aura_stackframe {
	uint8_t n;
	uint8_t maxid;
	uint8_t map[AURA_MAXLOCALS];
	uint8_t t[AURA_LOCALFRAMESIZE];
	union aura_var l[AURA_LOCALFRAMESIZE];
};

struct aura_context {
	int stackframe;
	void *ud;
	aura_errfunction errfunc;
	struct aura_wordlist words;
	struct aura_locallist locals;
	struct aura_stack stack;
	struct aura_stackframe frame[AURA_MAXFRAME];
	union list_node * prog[AURA_MAXPROG];
};

static void
raise_error(struct aura_context *ctx, const char *msg) {
	auraS_settop(&ctx->stack, 0);
	ctx->stackframe = 0;
	ctx->errfunc(ctx->ud, msg);
}

static void
newframe(struct aura_context *ctx) {
	int frame = ctx->stackframe++;
	if (frame > AURA_MAXFRAME) {
		raise_error(ctx, "stackframe overflow");
	}
	struct aura_stackframe *f = &ctx->frame[frame];
	f->n = 0;
	f->maxid = 0;
}

static inline void
endframe(struct aura_context *ctx) {
	--ctx->stackframe;
}

static inline struct aura_stackframe *
currentframe(struct aura_context *ctx) {
	return &ctx->frame[ctx->stackframe-1];
}

static int
setlocal_index(struct aura_context *ctx, int localid) {
	struct aura_stackframe *f = currentframe(ctx);
	assert(localid >=0 && localid < AURA_INVALIDLOCAL);
	if (localid >= f->maxid) {
		memset(f->map + f->maxid, AURA_LOCALFRAMESIZE, localid + 1 - f->maxid);
		f->maxid = localid + 1;
	}
	if (f->map[localid] != AURA_LOCALFRAMESIZE)
		return f->map[localid];
	int index = f->n++;
	if (index >= AURA_LOCALFRAMESIZE)
		raise_error(ctx, "Too many locals");
	f->map[localid] = index;
	return index;
}

static int
getlocal_index(struct aura_context *ctx, int localid) {
	struct aura_stackframe *f = currentframe(ctx);
	if (localid < 0 || localid >= f->maxid)
		raise_error(ctx, "No local");
	int index = f->map[localid];
	if (index == AURA_LOCALFRAMESIZE)
		raise_error(ctx, "No local");
	return index;
}

void
aura_close(struct aura_context *ctx) {
	if (ctx == NULL)
		return;
	free(ctx);
}

static int
convert_word(struct aura_context *ctx, union list_node *data, const char * name, int sz) {
	int rt = AURA_TWORD;
	if (sz >= 2) {
		switch (name[0]) {
		case '\'' :
			name++;
			sz--;
			rt = AURA_TWORDREF;
			break;
		case '$' :
			name++;
			sz--;
			data->word = auraW_local(&ctx->locals, name, sz);
			if (data->word < 0) {
				raise_error(ctx, "Too many locals");
			}
			return AURA_TLOCAL;
		case '(' :
			if (sz == 2)
				raise_error(ctx, "() not allows");
			name++;
			sz-=2;
			if (auraW_localdef(&ctx->locals, name, sz, data->local) < 0)
				raise_error(ctx, "Too many locals in ()");
			return AURA_TLOCALSET;
		}
	}
	int id = auraW_index(&ctx->words, name, sz);
	if (id < 0)
		raise_error(ctx, "Too many words");
	data->word = id;
	return rt;
}

static void
convert_node(struct aura_context *ctx, union list_node *node, int index, const char *source) {
	union list_node * data = &node[node[index].index.offset];
	int i;
	switch (node[index].index.type) {
	case AURA_TLIST:
		for (i=0;i<data->list.n;i++) {
			convert_node(ctx, node, data->list.offset+i, source);
		}
		break;
	case AURA_TWORD:
		node[index].index.type = convert_word(ctx, data, source + data->atom.offset, data->atom.len);
		break;
	}
}

int
aura_load(struct aura_context *ctx, const char *source, int sz, char output[AURA_MAXCHUNKSIZE]) {
	if (sz > 0xffff)
		raise_error(ctx, "Source too long");
	int node_sz = AURA_MAXCHUNKSIZE / sizeof(union list_node);
	union list_node *node = (union list_node *)output;
	sz = auraP_parse(source, sz, node, node_sz);
	if (sz < 0) {
		raise_error(ctx, "Parse error");
	}
	convert_node(ctx, node, 0, source);
//	auraP_dump(node, source);

	return sz * sizeof(union list_node);	
}

void
aura_register(struct aura_context *ctx, const char *name, aura_cfunction func, void *ud) {
	if (auraW_register(&ctx->words, name, func, ud) < 0) {
		raise_error(ctx, "Duplicate word");
	}
}

static inline void
execute(struct aura_context *ctx, int word) {
	struct aura_word * w = &ctx->words.w[word];
	if (w->func != NULL) {
		w->func(ctx, w->u.ud);
	} else {
		raise_error(ctx, "Undefined Word");
	}
}

static void
set_locals(struct aura_context *ctx, const uint8_t locals[4]) {
	int n;
	for (n=0; n<4; n++) {
		if (locals[n] == AURA_INVALIDLOCAL) {
			break;
		}
	}
	auraS_checkstack(&ctx->stack, -n);
	struct aura_stackframe *f = currentframe(ctx);
	int top = (ctx->stack.top -= n);
	int i;
	for (i=0;i<n;i++) {
		int index = setlocal_index(ctx, locals[i]);
		f->t[index] = ctx->stack.type[top + i];
		f->l[index] = ctx->stack.v[top + i];
	}
}

static void
get_local(struct aura_context *ctx, int local) {
	int index = getlocal_index(ctx, local);
	struct aura_stackframe *f = currentframe(ctx);
	int top = ctx->stack.top++;
	ctx->stack.type[top] = f->t[index];
	ctx->stack.v[top] = f->l[index];
}

static void
execute_listword(struct aura_context *ctx, const union list_node *node, int pc, int progid) {
	const union list_node *ins = &node[pc];
	int t = ins->index.type;
	const union list_node *data = &node[ins->index.offset];
	if (t == AURA_TWORD) {
		execute(ctx, data->word);
	} else if (t == AURA_TLOCALSET) {
		set_locals(ctx, data->local);
	} else {
		if (!auraS_checkstack(&ctx->stack, 1)) {
			raise_error(ctx, "Stack overflow");
		}
		switch (t) {
		case AURA_TLIST:
			auraS_pushlist(&ctx->stack, data->list.offset, data->list.n, progid);
			break;
		case AURA_TINT:
			auraS_pushint(&ctx->stack, data->d);
			break;
		case AURA_TFLOAT:
			auraS_pushfloat(&ctx->stack, data->f);
			break;
		case AURA_TTRUE:
			auraS_pushboolean(&ctx->stack, 1);
			break;
		case AURA_TFALSE:
			auraS_pushboolean(&ctx->stack, 0);
			break;
		case AURA_TWORDREF:
			auraS_pushword(&ctx->stack, data->word);
			break;
		case AURA_TLOCAL:
			get_local(ctx, data->word);
			break;
		default:
			raise_error(ctx, "Unknown instruction");
			break;
		}
	}
}

static void
execute_slist(struct aura_context *ctx, const union list_node *node, int offset, int n, int progid) {
	int i;
	for (i=0;i<n;i++) {
		execute_listword(ctx, node, offset+i, progid);
	}
}

static void
execute_dlistword(struct aura_context *ctx, const uint8_t *type, const union aura_var *var, int pc) {
	int t = type[pc];
	if (t == AURA_TWORD) {
		execute(ctx, var[pc].word);
	} else {
		union aura_var v = var[pc];
		if (!auraS_checkstack(&ctx->stack, 1)) {
			raise_error(ctx, "Stack overflow");
		}
		switch (t) {
		case AURA_TLIST:
			auraS_pushlist(&ctx->stack, v.slist.offset, v.slist.size, v.slist.prog);
			break;
		case AURA_TDLIST:
			auraS_pushdlist(&ctx->stack, v.dlist.offset, v.dlist.size);
			break;
		case AURA_TINT:
			auraS_pushint(&ctx->stack, v.d);
			break;
		case AURA_TFLOAT:
			auraS_pushfloat(&ctx->stack, v.f);
			break;
		case AURA_TTRUE:
			auraS_pushboolean(&ctx->stack, 1);
			break;
		case AURA_TFALSE:
			auraS_pushboolean(&ctx->stack, 0);
			break;
		case AURA_TWORDREF:
			auraS_pushboolean(&ctx->stack, 0);
			break;
		default:
			raise_error(ctx, "Unknown instruction");
			break;
		}
	}
}

static void
execute_dlist(struct aura_context *ctx, union aura_var var) {
	int i;
	const uint8_t *t = &ctx->stack.list_t[var.dlist.offset];
	const union aura_var *v = &ctx->stack.list[var.dlist.offset];
	for (i=0;i<var.dlist.size;i++) {
		execute_dlistword(ctx, t, v, i);
	}
}

void
aura_run(struct aura_context *ctx, int progid, void *code) {
	if (progid < 0 || progid >= AURA_MAXPROG) {
		raise_error(ctx, "Too many progs");
	}
	union list_node *prog = (union list_node *)code;
	if (prog == NULL) {
		prog = ctx->prog[progid];
	} else if (ctx->prog[progid] == NULL) {
		ctx->prog[progid] = prog;
	} else if (ctx->prog[progid] != prog) {
		raise_error(ctx, "Duplicate prog");
	}
	if (prog == NULL) {
		raise_error(ctx, "No prog");
	}
	ctx->stack.list_n = 0;
	ctx->stackframe = 0;
	newframe(ctx);

	int t = prog[0].index.type;
	if (t != AURA_TLIST) {
		raise_error(ctx, "Invalid code");
	}
	const union list_node * node = &prog[prog[0].index.offset];

	execute_slist(ctx, prog, node->list.offset, node->list.n,  progid);

	endframe(ctx);
}

void
aura_error(struct aura_context *ctx, const char *msg) {
	raise_error(ctx, msg);
}

static void
eval(struct aura_context *ctx, union aura_var var, int t) {
	if (t == AURA_TLIST) {
		int progid = var.slist.prog;
		union list_node *prog = ctx->prog[progid];
		execute_slist(ctx, prog, var.slist.offset, var.slist.size, progid);
	} else {
		if (t != AURA_TDLIST)
			aura_error(ctx, "Eval need a list");
		execute_dlist(ctx, var);
	}
}

static void
cfunc_eval(struct aura_context *ctx, void *ud) {
	union aura_var var;
	int t = auraS_get(&ctx->stack, -1, &var);
	auraS_pop(&ctx->stack, 1);
	newframe(ctx);
	eval(ctx, var, t);
	endframe(ctx);
}

static void
cfunc_upeval(struct aura_context *ctx, void *ud) {
	union aura_var var;
	int t = auraS_get(&ctx->stack, -1, &var);
	auraS_pop(&ctx->stack, 1);
	eval(ctx, var, t);
}

static void
push_boolean(struct aura_context *ctx, void *ud) {
	if (!auraS_checkstack(&ctx->stack, 1))
		aura_error(ctx, "Stack overflow");
	auraS_pushboolean(&ctx->stack, ud != NULL);
}

struct slist_arg {
	uint16_t offset;
	uint16_t size;
	int prog;
};

static void
cfunc_evalslist(struct aura_context *ctx, void *ud) {
	union {
		void *ud;
		struct slist_arg arg;
	} u;
	u.ud = ud;
	int progid = u.arg.prog;
	assert(progid >=0 && progid < AURA_MAXPROG);
	const union list_node * node = ctx->prog[progid];
	int n = u.arg.size;
	int i;
	for (i=0;i<n;i++) {
		execute_listword(ctx, node, u.arg.offset+i, progid);
	}
}

static void
cfunc_evaldlist(struct aura_context *ctx, void *ud) {
	union {
		void *ud;
		int id[2];
	} u;
	u.ud = ud;
	union aura_var list;
	list.dlist.offset = u.id[0];
	list.dlist.size = u.id[1];
	execute_dlist(ctx, list);
}

static void
cfunc_def(struct aura_context *ctx, void *ud) {
	if (!auraS_checkstack(&ctx->stack, -2))
		aura_error(ctx, "Stack empty");
	union aura_var word, list;
	if (auraS_get(&ctx->stack, -1, &word) != AURA_TWORDREF)
		aura_error(ctx, "def need wordref");
	assert(word.word >=0 && word.word < AURA_MAXWORDS);
	struct aura_word * w = &ctx->words.w[word.word];
	if (w->func != NULL)
		aura_error(ctx, "Already defined");
	int t = auraS_get(&ctx->stack, -2, &list);
	if (t == AURA_TLIST) {
		union {
			void *ud;
			struct slist_arg arg;
		} u;
		u.arg.offset = list.slist.offset;
		u.arg.size = list.slist.size;
		u.arg.prog = list.slist.prog;
		w->func = cfunc_evalslist;
		w->u.ud = u.ud;
	} else {
		if (t != AURA_TDLIST) {
			aura_error(ctx, "def need list");
		}
		if (!auraS_persistence(&ctx->stack)) {
			aura_error(ctx, "def can't persistence list");
		}
		w->func = cfunc_evaldlist;
		w->u.id[0] = list.dlist.offset;
		w->u.id[1] = list.dlist.size;
	}
	auraS_pop(&ctx->stack, 2);
}

static inline float
tofloat(struct aura_context *ctx, int t, union aura_var v) {
	if (t == AURA_TFLOAT)
		return v.f;
	else if (t == AURA_TINT)
		return (float)v.d;
	else {
		aura_error(ctx, "Need a number");
		return 0;
	}
}

static void
cfunc_basicmath(struct aura_context *ctx, void *ud) {
	if (!auraS_checkstack(&ctx->stack, -2))
		aura_error(ctx, "Stack empty");
	union aura_var left, right;
	int lt = auraS_get(&ctx->stack, -2, &left);
	int rt = auraS_get(&ctx->stack, -1, &right);
	int op = (int)(intptr_t)ud;
	auraS_pop(&ctx->stack, 2);
	if (lt == AURA_TINT && rt == AURA_TINT) {
		int lv = left.d;
		int rv = right.d;
		switch (op) {
		case '+':
			auraS_pushint(&ctx->stack, lv + rv);
			break;
		case '-':
			auraS_pushint(&ctx->stack, lv - rv);
			break;
		case '*':
			auraS_pushint(&ctx->stack, lv * rv);
			break;
		case '/':
			if (rv == 0)
				aura_error(ctx, "Divide zero");
			auraS_pushint(&ctx->stack, lv / rv);
			break;
		case '>':
			auraS_pushboolean(&ctx->stack, lv > rv);
			break;
		case '<':
			auraS_pushboolean(&ctx->stack, lv < rv);
			break;
		case '}':	// >=
			auraS_pushboolean(&ctx->stack, lv >= rv);
			break;
		case '{':	// <=
			auraS_pushboolean(&ctx->stack, lv <= rv);
			break;
		}
	} else {
		float lv = tofloat(ctx, lt, left);
		float rv = tofloat(ctx, rt, right);
		switch (op) {
		case '+':
			auraS_pushfloat(&ctx->stack, lv + rv);
			break;
		case '-':
			auraS_pushfloat(&ctx->stack, lv - rv);
			break;
		case '*':
			auraS_pushfloat(&ctx->stack, lv * rv);
			break;
		case '/':
			if (rv == 0)
				aura_error(ctx, "Divide zero");
			auraS_pushfloat(&ctx->stack, lv / rv);
			break;
		case '>':
			auraS_pushboolean(&ctx->stack, lv > rv);
			break;
		case '<':
			auraS_pushboolean(&ctx->stack, lv < rv);
			break;
		case '}':	// >=
			auraS_pushboolean(&ctx->stack, lv >= rv);
			break;
		case '{':	// <=
			auraS_pushboolean(&ctx->stack, lv <= rv);
			break;
		}
	}
}

static int
compare(struct aura_context *ctx) {
	union aura_var left, right;
	int lt = auraS_get(&ctx->stack, -2, &left);
	int rt = auraS_get(&ctx->stack, -1, &right);
	auraS_pop(&ctx->stack, 2);
	if (lt == rt) {
		switch (lt) {
		case AURA_TLIST:
			return left.slist.prog == right.slist.prog &&
				left.slist.offset == right.slist.offset &&
				left.slist.size == right.slist.size;
		case AURA_TDLIST:
			return left.dlist.offset == right.dlist.offset &&
				left.dlist.size == right.dlist.size;
		case AURA_TWORD:
		case AURA_TWORDREF:
			return left.word == right.word;
		case AURA_TINT:
			return left.d == right.d;
		case AURA_TFLOAT:
			return left.f == right.f;
		default:
			return 1;
		}
	} else if (lt == AURA_TINT && rt == AURA_TFLOAT) {
		return (float)left.d == right.f;
	} else if (lt == AURA_TFLOAT && rt == AURA_TINT) {
		return left.f == (float)right.d;
	} else {
		return 0;
	}
}

static void
cfunc_compare(struct aura_context *ctx, void *ud) {
	if (!auraS_checkstack(&ctx->stack, -2))
		aura_error(ctx, "Stack empty");
	int r = (ud != NULL) ^ compare(ctx);
	auraS_pushboolean(&ctx->stack, r);
}

static void
cfunc_if(struct aura_context *ctx, void *ud) {
	if (!auraS_checkstack(&ctx->stack, -2))
		aura_error(ctx, "Stack empty");
	auraS_swap(&ctx->stack);
	cfunc_upeval(ctx, NULL);
	if (ctx->stack.type[ctx->stack.top-1] != AURA_TFALSE) {
		auraS_pop(&ctx->stack, 1);
		cfunc_upeval(ctx, NULL);
	} else {
		auraS_pop(&ctx->stack, 2);
	}
}

static void
cfunc_ifelse(struct aura_context *ctx, void *ud) {
	if (!auraS_checkstack(&ctx->stack, -3))
		aura_error(ctx, "Stack empty");
	auraS_rotate(&ctx->stack, -3, -1);
	cfunc_upeval(ctx, NULL);
	if (ctx->stack.type[ctx->stack.top-1] != AURA_TFALSE) {
		auraS_pop(&ctx->stack, 2);
	} else {
		auraS_copy(&ctx->stack, -2, -3);
		auraS_pop(&ctx->stack, 2);
	}
	cfunc_upeval(ctx, NULL);
}

static void
cfunc_while(struct aura_context *ctx, void *ud) {
	if (!auraS_checkstack(&ctx->stack, -2))
		aura_error(ctx, "Stack empty");
	if (!auraS_checkstack(&ctx->stack, 1))
		aura_error(ctx, "Stack overflow");
	for (;;) {
		auraS_pushvalue(&ctx->stack, -2);
		cfunc_upeval(ctx, NULL);
		if (ctx->stack.type[ctx->stack.top-1] != AURA_TFALSE) {
			auraS_pop(&ctx->stack, 1);	// pop true
			auraS_pushvalue(&ctx->stack, -1);	// push prog
			cfunc_upeval(ctx, NULL);
		} else {
			auraS_pop(&ctx->stack, 3);
			return;
		}
	}
}

struct aura_context *
aura_newstate(void *ud, aura_errfunction errfunc) {
	struct aura_context *ctx = (struct aura_context *)malloc(sizeof(*ctx));
	if (ctx == NULL)
		return NULL;
	memset(ctx, 0, sizeof(*ctx));
	ctx->ud = ud;
	ctx->errfunc = errfunc;

	aura_register(ctx, "true", push_boolean, (void *)1);
	aura_register(ctx, "false", push_boolean, (void *)0);
	aura_register(ctx, "eval", cfunc_eval, NULL);
	aura_register(ctx, "upeval", cfunc_upeval, NULL);
	aura_register(ctx, "def", cfunc_def, NULL);
	aura_register(ctx, "if", cfunc_if, NULL);
	aura_register(ctx, "ifelse", cfunc_ifelse, NULL);
	aura_register(ctx, "while", cfunc_while, NULL);
	aura_register(ctx, "+", cfunc_basicmath, (void *)'+');
	aura_register(ctx, "-", cfunc_basicmath, (void *)'-');
	aura_register(ctx, "*", cfunc_basicmath, (void *)'*');
	aura_register(ctx, "/", cfunc_basicmath, (void *)'/');
	aura_register(ctx, ">", cfunc_basicmath, (void *)'>');
	aura_register(ctx, "<", cfunc_basicmath, (void *)'<');
	aura_register(ctx, ">=", cfunc_basicmath, (void *)'}');
	aura_register(ctx, "<=", cfunc_basicmath, (void *)'{');
	aura_register(ctx, "==", cfunc_compare, NULL);
	aura_register(ctx, "!=", cfunc_compare, (void *)1);
	return ctx;
}

#ifdef AURA_TESTMAIN

#include <stdio.h>

static void
errorhook(void *ud, const char *msg) {
	printf("Error: %s\n", msg);
	assert(0);
}

static void
print(struct aura_context *ctx, void *ud) {
	union aura_var v;
	switch(auraS_get(&ctx->stack, -1, &v)) {
	case AURA_TINT:
		printf("[INT] %d\n", v.d);
		break;
	case AURA_TFLOAT:
		printf("[FLOAT] %g\n", v.f);
		break;
	case AURA_TTRUE:
		printf("[BOOL] true\n");
		break;
	case AURA_TFALSE:
		printf("[BOOL] false\n");
		break;
	case AURA_TDLIST:
	case AURA_TLIST:
		printf("[LIST]\n");
		break;
	default:
		printf("[UNKNOWN]\n");
		break;
	}
	auraS_pop(&ctx->stack, 1);
}

int
main() {
	struct aura_context *ctx = aura_newstate(NULL, errorhook);
	char source[] = 
		"[(x) $x $x] 'dup def "
		"[dup +] 'double def "
	;
	char output[AURA_MAXCHUNKSIZE];
	aura_register(ctx, "print", print, NULL);

	aura_load(ctx, source, sizeof(source), output);
	aura_run(ctx, 0, output);

	char source2[] =
//		"42 double print "
//		"[(x) 0 (s) [$x 0 >] [ $s $x + (s) $x 1 - (x) ] while $s ] 'sum def "
//		"10 sum print ";
		"[(n)"
		" 1 1 (a b)"
		" [$n 2 >]"
		"   [$b $a $b + (a b)"
		"    $n 1 - (n)] while"
		" $b ] 'fibonacci def"
		"  1000 fibonacci print";
	char output2[AURA_MAXCHUNKSIZE];

	aura_load(ctx, source2, sizeof(source2), output2);
	aura_run(ctx, 1, output2);

	aura_close(ctx);
	return 0;
}

#endif
