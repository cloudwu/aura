#ifndef aura_h
#define aura_h

#define AURA_TLIST 0
#define AURA_TWORD 1
#define AURA_TINT 2
#define AURA_TFLOAT 3
#define AURA_TBOOLEAN 4
#define AURA_TWORDREF 5
#define AURA_TLOCAL 6
#define AURA_TLOCALSET 7

#define AURA_MAXCHUNKSIZE 0x10000

struct aura_context;

typedef void (*aura_cfunction)(struct aura_context *ctx, void* ud);
typedef void (*aura_errfunction)(void *ud, const char *msg);

struct aura_context * aura_newstate(void *ud, aura_errfunction errorhook);
void aura_close(struct aura_context *ctx);
void aura_error(struct aura_context *ctx, const char *msg);
int aura_load(struct aura_context *ctx, const char *source, int sz, char output[AURA_MAXCHUNKSIZE]);
void aura_register(struct aura_context *ctx, const char *name, aura_cfunction func, void *ud);

#endif
