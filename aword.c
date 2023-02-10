#include "aword.h"
#include <string.h>

static inline uint32_t
hashword(const char *name, int l) {
	uint32_t h = l;
	for (; l > 0; l--) {
	    h ^= ((h<<5) + (h>>2) + (uint8_t)(name[l - 1]));
	}
	return h;
}

static inline int
isword(struct aura_wordlist *words, int id, const char *name, int sz) {
	if (sz >= AURA_WORDMAXLEN) {
		sz = AURA_WORDMAXLEN-1;
	}
	int index = words->index[id];
	struct aura_word *w = &words->w[index];
	return w->name[sz] == 0 && memcmp(name, w->name, sz) == 0;
}

static void
insert_word(struct aura_wordlist *words, int index, uint32_t h, const char *name, int sz) {
	int word_index = words->n++;
	struct aura_word *w = &words->w[word_index];
	if (sz >= AURA_WORDMAXLEN) {
		sz = AURA_WORDMAXLEN-1;
	}
	memcpy(w->name, name, sz);
	w->name[sz] = 0;
	memmove(&words->hash[index+1],&words->hash[index], (word_index - index) * sizeof(*words->hash));
	words->hash[index] = h;
	memmove(&words->index[index+1],&words->index[index], (word_index - index) * sizeof(*words->index));
	words->index[index] = word_index;
	w->func = NULL;
}

#include <stdio.h>

static int
bsearch(uint32_t *hashs, int n, uint32_t h) {
	int begin = 0;
	int end = n;
	while (begin < end) {
		int mid = (begin + end) / 2;
		if (hashs[mid] == h) {
			for (begin = mid; begin > 0 && hashs[begin-1] == h; begin --) {}
			break;
		} else if (hashs[mid] > h) {
			end = mid;
		} else {
			begin = mid + 1;
		}
	}
	return begin;
}

int
auraW_index(struct aura_wordlist *words, const char *name, int sz) {
	uint32_t h = hashword(name, sz);
	int begin = bsearch(words->hash, words->n, h);
	while (begin < words->n) {
		if (h != words->hash[begin])
			break;
		if (isword(words, begin, name, sz)) {
			return words->index[begin];
		}
		++begin;
	}
	if (words->n >= AURA_MAXWORDS)
		return -1;
	insert_word(words, begin, h, name, sz);
	return words->index[begin];
}

const char *
auraW_name(struct aura_wordlist *words, int id) {
	return words->w[id].name;
}

int
auraW_register(struct aura_wordlist *words, const char *name, aura_cfunction func, void *ud) {
	int index = auraW_index(words, name, strlen(name));
	if (index < 0)
		return index;
	struct aura_word *w = &words->w[index];
	w->func = func;
	w->u.ud = ud;
	return index;
}

static inline int
islocal(struct aura_locallist *words, int id, const char *name, int sz) {
	if (sz >= AURA_WORDMAXLEN) {
		sz = AURA_WORDMAXLEN-1;
	}
	int index = words->index[id];
	struct aura_local *w = &words->w[index];
	return w->name[sz] == 0 && memcmp(name, w->name, sz) == 0;
}

static void
insert_local(struct aura_locallist *words, int index, uint32_t h, const char *name, int sz) {
	int word_index = words->n++;
	struct aura_local *w = &words->w[word_index];
	if (sz >= AURA_WORDMAXLEN) {
		sz = AURA_WORDMAXLEN-1;
	}
	memcpy(w->name, name, sz);
	w->name[sz] = 0;
	memmove(&words->hash[index+1],&words->hash[index], (word_index - index) * sizeof(*words->hash));
	words->hash[index] = h;
	memmove(&words->index[index+1],&words->index[index], (word_index - index) * sizeof(*words->index));
	words->index[index] = word_index;
}

int
auraW_local(struct aura_locallist *words, const char *name, int sz) {
	uint32_t h = hashword(name, sz);
	int begin = bsearch(words->hash, words->n, h);
	while (begin < words->n) {
		if (h != words->hash[begin])
			break;
		if (islocal(words, begin, name, sz)) {
			return words->index[begin];
		}
		++begin;
	}
	if (words->n >= AURA_MAXLOCALS)
		return -1;
	insert_local(words, begin, h, name, sz);
	return words->index[begin];
}

const char *
auraW_localname(struct aura_locallist *words, int id) {
	return words->w[id].name;
}

static int
is_whitespace(char c) {
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

int
auraW_localdef(struct aura_locallist *locals, const char *name, int sz, uint8_t tuple[4]) {
	int index = 0;
	for (;;) {
		while (sz > 0 && is_whitespace(*name))
			++name, --sz;
		if (sz == 0)
			break;
		if (index >=4) {
			// too many locals
			return -1;
		}
		const char *local = name;
		while (sz > 0 && !is_whitespace(*name))
			++name, --sz;
		int id = auraW_local(locals, local, name-local);
		if (id < 0)
			return -1;
		tuple[index++] = (uint8_t)id;
	}
	if (index < 4)
		tuple[index] = 255;
	return index;
}

#ifdef WORD_TESTMAIN

#include <stdio.h>

#define test1(name) auraW_index(&words, name, sizeof(name "") - 1)
#define test(name) { int id = test1(name); printf("%d %s\n", id, auraW_name(&words, id)); }

int
main() {
	struct aura_wordlist words;
	memset(&words, 0, sizeof(words));
	test("hello");
	test("world");
	test("hello");
	test("longlonglonglonglonglonglonglonglonglonglonglonglonglonglonglong1");
	test("longlonglonglonglonglonglonglonglonglonglonglonglonglonglonglong2");
	test("world");
	return 0;
}

#endif

