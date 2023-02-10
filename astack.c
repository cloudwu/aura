#include "astack.h"
#include "aura.h"

#define HEAPSIZE(s) (AURA_LISTSIZE - (s)->list_heap)

int
auraS_createlist(struct aura_stack *s, int sz) {
	if (s->list_n + sz > HEAPSIZE(s))
		return 0;
	if (!auraS_checkstack(s, 1))
		return 0;
	uint8_t *list = &s->list_t[s->list_n];
	int i;
	for (i=0;i<sz;i++) {
		list[i] = AURA_TFALSE;
	}
	s->type[s->top] = AURA_TDLIST;
	union aura_var *v =&s->v[s->top];
	v->dlist.offset = s->list_n;
	v->dlist.size = (uint16_t)sz;
	s->list_n += sz;
	s->top++;

	return 1;
}

static int
deepcopy_list(struct aura_stack *s, union aura_var *var, int map[AURA_LISTSIZE]) {
	if (s->top + var->dlist.size > HEAPSIZE(s))
		return 0;
	s->list_heap += var->dlist.size;
	int i;
	int heap =  HEAPSIZE(s);
	map[var->dlist.offset] = heap;
	for (i = 0; i < var->dlist.size; i++) {
		int t = s->list_t[heap+i] = s->list_t[var->dlist.offset+i];
		union aura_var tmp = s->list[var->dlist.offset+i];
		if (t == AURA_TDLIST && map[tmp.dlist.offset] < 0) {
			if (!deepcopy_list(s, &tmp, map))
				return 0;
		}
		s->list[heap+i] = tmp;
	}
	var->dlist.offset = heap;
	return 1;
}

int
auraS_persistence(struct aura_stack *s) {
	assert(s->top > 0 && s->type[s->top-1] == AURA_TDLIST);
	union aura_var var = s->v[s->top-1];
	int heap = s->list_heap;
	int listmap[AURA_LISTSIZE];
	int i;
	if (var.dlist.offset >= HEAPSIZE(s))
		return 1;
	for (i=0;i< HEAPSIZE(s);i++) {
		listmap[i] = -1;	// not map
	}
	for (i= HEAPSIZE(s); i<AURA_LISTSIZE; i++) {
		listmap[i] = i;	// already persistence
	}
	if (deepcopy_list(s, &var, listmap)) {
		s->v[s->top-1] = var;
		return 1;
	} else {
		s->list_heap = heap;	// failed, restore heap
		return 0;
	}
}

void
auraS_setn(struct aura_stack *s, int index, int n) {
	index = auraS_absindex(s, index);
	assert(auraS_checkstackid(s, index));
	assert(s->type[index-1] == AURA_TDLIST);
	union aura_var *v = &s->v[index-1];
	assert(n >= 0 && n < v->dlist.size);
	union aura_var *list = &s->list[v->dlist.offset];
	uint8_t *list_type = &s->list_t[v->dlist.offset];
	int top = --s->top;
	list_type[n] = s->type[top];
	list[n] = s->v[top];
}

void
auraS_getn(struct aura_stack *s, int index, int n) {
	assert(auraS_checkstack(s, 1));
	index = auraS_absindex(s, index);
	assert(auraS_checkstackid(s, index));
	assert(s->type[index-1] == AURA_TDLIST);
	assert(s->type[index-1] == AURA_TDLIST);
	union aura_var *v = &s->v[index-1];
	assert(n >= 0 && n < v->dlist.size);
	union aura_var *list = &s->list[v->dlist.offset];
	uint8_t *list_type = &s->list_t[v->dlist.offset];

	int top = s->top++;
	s->type[top] = list_type[n];
	s->v[top] = list[n];
}


#ifdef STACK_TESTMAIN

#include <stdio.h>
#include <string.h>

static void
dumplist(struct aura_stack *s, int index) {
	union aura_var v;
	index = auraS_absindex(s, index);
	int t = auraS_get(s, index, &v);
	assert(t == AURA_TDLIST);
	int sz = v.dlist.size;
	int i;
	for (i=0;i<sz;i++) {
		auraS_getn(s, index, i);
		switch(auraS_get(s, -1, &v)) {
		case AURA_TINT:
			printf("[FLOAT] %d\n", v.d);
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
		auraS_pop(s, 1);
	}
}

int
main() {
	struct aura_stack s;
	memset(&s, 0, sizeof(s));
	int ok = auraS_createlist(&s, 8);
	assert(ok);
	int i;
	for (i=0;i<8;i++) {
		auraS_pushfloat(&s, (float)i);
		auraS_setn(&s, 1, i);
	}
	dumplist(&s, 1);
	return 0;
}

#endif

