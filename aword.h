#ifndef aura_word_h
#define aura_word_h

#include "aura.h"

#include <stdint.h>

#define AURA_WORDMAXLEN 16
#define AURA_MAXWORDS 4096
#define AURA_MAXLOCALS 255
#define AURA_INVALIDLOCAL 255

struct aura_word {
	aura_cfunction func;
	union {
		void * ud;
		int id[2];
	} u;
	char name[AURA_WORDMAXLEN];
};

struct aura_wordlist {
	int n;
	uint32_t hash[AURA_MAXWORDS];
	uint16_t index[AURA_MAXWORDS];
	struct aura_word w[AURA_MAXWORDS];
};

struct aura_local {
	char name[AURA_WORDMAXLEN];
};

struct aura_locallist {
	int n;
	uint32_t hash[AURA_MAXLOCALS];
	uint8_t index[AURA_MAXLOCALS];
	struct aura_local w[AURA_MAXLOCALS];
};

int auraW_index(struct aura_wordlist *words, const char *name, int sz);
const char * auraW_name(struct aura_wordlist *words, int id);
int auraW_register(struct aura_wordlist *words, const char *name, aura_cfunction func, void *ud);

int auraW_local(struct aura_locallist *locals, const char *name, int sz);
const char * auraW_localname(struct aura_locallist *locals, int id);
int auraW_localdef(struct aura_locallist *locals, const char *name, int sz, uint8_t tuple[4]);

#endif
