#ifndef aura_stack_h
#define aura_stack_h

#include <stdint.h>
#include <assert.h>

#include "aura.h"
#include "atype.h"

#define AURA_STACKSIZE 4096
#define AURA_LISTSIZE (16*1024)

union aura_var {
	int d;
	float f;
	struct {
		uint32_t offset;
		uint32_t size;
	} dlist;
	struct {
		uint16_t offset;
		uint16_t size;
		int prog;
	} slist;
	int word;
	void * ud;
};

struct aura_stack {
	int top;
	int list_n;
	int list_heap;
	uint8_t type[AURA_STACKSIZE];
	uint8_t list_t[AURA_LISTSIZE];
	union aura_var v[AURA_STACKSIZE];
	union aura_var list[AURA_LISTSIZE];
};

static inline int
auraS_absindex(struct aura_stack *s, int idx) {
	return (idx > 0) ? idx : (s->top + idx + 1);
}

static inline void
auraS_settop(struct aura_stack *s, int top) {
	s->top = top;
}

static inline void
auraS_pop(struct aura_stack *s, int n) {
	s->top -= n;
}

static inline void
copy_stack_(struct aura_stack *s, int to, int from) {
	s->type[to-1] = s->type[from-1];
	s->v[to-1] = s->v[from-1];
}

static inline void
auraS_pushvalue(struct aura_stack *s, int idx) {
	idx = auraS_absindex(s, idx);
	int top = ++s->top;
	copy_stack_(s, top, idx);
}

static inline void
auraS_copy(struct aura_stack *s, int fromidx, int toidx) {
	fromidx = auraS_absindex(s, fromidx);
	toidx = auraS_absindex(s, toidx);
	copy_stack_(s, toidx, fromidx);
}


// See lua_rotate
static void
reverse_(struct aura_stack *s, int from, int to) {
	for (; from < to; from++, to--) {
		uint8_t tmp_type = s->type[from];
		union aura_var tmp_v = s->v[from];

		s->type[from] = s->type[to];
		s->v[from] = s->v[to];

		s->type[to] = tmp_type;
		s->v[to] = tmp_v;
	}
}

static inline void
auraS_rotate(struct aura_stack *s, int idx, int n) {
	int t = s->top - 1;
	int p = auraS_absindex(s, idx) - 1;
	assert ((n >= 0 ? n : -n) <= (t - p + 1));
	int m = (n >= 0 ? t - n : p - n - 1);
	reverse_(s, p, m);
	reverse_(s, m + 1, t);
	reverse_(s, p, t);
}

static inline void
auraS_swap(struct aura_stack *s) {
	int top = s->top;
	uint8_t tmp_type = s->type[top-1];
	union aura_var tmp_v = s->v[top-1];

	s->type[top-1] = s->type[top-2];
	s->v[top-1] = s->v[top-2];

	s->type[top-2] = tmp_type;
	s->v[top-2] = tmp_v;
}

static inline int
auraS_checkstack(struct aura_stack *s, int inc) {
	int n = s->top + inc;
	return (n >= 0 && n < AURA_STACKSIZE);
}

static inline int
auraS_checkstackid(struct aura_stack *s, int stkid) {
	return (stkid > 0 && stkid <= s->top);
}

static inline void
auraS_pushint(struct aura_stack *s, int v) {
	int top = s->top++;
	s->type[top] = AURA_TINT;
	s->v[top].d = v;
}

static inline void
auraS_pushfloat(struct aura_stack *s, float v) {
	int top = s->top++;
	s->type[top] = AURA_TFLOAT;
	s->v[top].f = v;
}

static inline void
auraS_pushboolean(struct aura_stack *s, int b) {
	int top = s->top++;
	s->type[top] = b ? AURA_TTRUE : AURA_TFALSE;
}

static inline void
auraS_pushlist(struct aura_stack *s, int list_offset, int list_size, int progid) {
	int top = s->top++;
	s->type[top] = AURA_TLIST;
	s->v[top].slist.prog = progid;
	s->v[top].slist.offset = (uint16_t)list_offset;
	s->v[top].slist.size = (uint16_t)list_size;
}

static inline void
auraS_pushdlist(struct aura_stack *s, int list_offset, int list_size) {
	int top = s->top++;
	s->type[top] = AURA_TDLIST;
	s->v[top].dlist.offset = (uint32_t)list_offset;
	s->v[top].dlist.size = (uint32_t)list_size;
}

static inline void
auraS_pushword(struct aura_stack *s, int word) {
	int top = s->top++;
	s->type[top] = AURA_TWORDREF;
	s->v[top].word = word;
}

static inline int
auraS_get(struct aura_stack *s, int stkid, union aura_var *v) {
	stkid = auraS_absindex(s, stkid);
	*v = s->v[stkid-1];
	return s->type[stkid-1];
}

int auraS_createlist(struct aura_stack *s, int sz);
int auraS_persistence(struct aura_stack *s);
void auraS_setn(struct aura_stack *s, int index, int n);
void auraS_getn(struct aura_stack *s, int index, int n);

#endif
