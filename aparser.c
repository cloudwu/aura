#include <stdint.h>
#include <assert.h>
#include <string.h>
#include "aparser.h"
#include "aura.h"

#define MAXSIZE 0x10000
#define MAXATOM 8192
#define MAXNODE 2048

struct parser_atom {
	uint16_t offset;
	uint16_t size;
};

struct parser_node {
	uint16_t n;
};

struct parser_context {
	const char * buffer;
	struct parser_atom * a;
	struct parser_node * n;
	const char * beginptr;
	uint16_t sz;
	uint16_t atom_n;
	uint16_t node_n;
};

static inline void
skip_whitespace(struct parser_context *ctx) {
	int n = ctx->sz;
	const char * buffer = ctx->buffer;
	while (n > 0) {
		switch (buffer[0]) {
		case ' ':
		case '\t':
		case '\n':
		case '\r':
		case '\0':
			++buffer;
			--n;
			break;
		default:
			ctx->sz = n;
			ctx->buffer = buffer;
			return;
		}
	}
	ctx->sz = 0;
	ctx->buffer = buffer;
}

static inline int
parse_tuple(struct parser_context *ctx) {
	const char *ps = memchr(ctx->buffer+1, ')', ctx->sz-1);
	if (ps == NULL)
		return PARSER_ERR_TUPLE;
	return ps - ctx->buffer + 1;
}

static inline int
parse_atom(struct parser_context *ctx) {
	int n = ctx->sz - 1;
	const char * buffer = ctx->buffer + 1;
	for (;n>0;--n,++buffer) {
		switch (buffer[0]) {
		case ' ' :
		case '\t' :
		case '\n' :
		case '\r' :
		case '\0' :
		case '[' :
		case ']' :
			return ctx->sz - n;
		}
	}
	return ctx->sz;
}

static int
parse_token(struct parser_context *ctx) {
	skip_whitespace(ctx);
	if (ctx->sz == 0)
		return 0;
	switch (ctx->buffer[0]) {
	case '[' :
	case ']' :
		return 1;
	case '(' :
		return parse_tuple(ctx);
	default:
		return parse_atom(ctx);
	}
}

static int
parse_list(struct parser_context *ctx) {
	const char * beginptr = ctx->buffer;
	int n;
	struct parser_node * current_node = &ctx->n[ctx->node_n++];
	if (ctx->node_n > MAXNODE)
		return PARSER_ERR_MAXNODE;
	current_node->n = 0;
	int sublist_size;
	while ((n = parse_token(ctx)) > 0) {
		struct parser_atom * current_atom = &ctx->a[ctx->atom_n];
		switch (ctx->buffer[0]) {
		case '[':
			++ctx->atom_n;
			current_atom->offset = ~0;
			++ctx->buffer;
			--ctx->sz;
			sublist_size = parse_list(ctx);
			if (sublist_size <= 0) {
				return (sublist_size == 0) ? PARSER_ERR_LIST : sublist_size;
			}
			++current_node->n;
			current_atom->size = ~0;
			break;
		case ']':
			++ctx->buffer;
			--ctx->sz;
			return ctx->buffer - beginptr;
		default:
			++ctx->atom_n;
			++current_node->n;
			current_atom->offset = ctx->buffer - ctx->beginptr;
			current_atom->size = n;
			ctx->buffer += n;
			ctx->sz -= n;
			break;
		}
		if (ctx->atom_n > MAXATOM)
			return PARSER_ERR_MAXATOM;
	}
	return 0;
}

static inline int
is_list(struct parser_atom *atom) {
	return atom->offset == 0xffff && atom->size == 0xffff;
}

struct convert_context {
	struct parser_node *list;
	struct parser_atom *atom;
	union list_node *output;
	union list_node *base;
	const char * source;
};

static inline int
convert_number(union list_node *node, struct convert_context *ctx) {
	int n = 0;
	int i;
	const char * s = ctx->source + ctx->atom->offset;
	int neg = 0;
	int size = ctx->atom->size;
	if (*s == '-') {
		neg = 1;
		++s;
		--size;
	}
	else if (*s == '+') {
		++s;
		--size;
	}

	for (i=0; i<size; i++, s++) {
		if (*s >= '0' && *s <= '9') {
			n = n * 10 + (*s - '0');
		} else {
			break;
		}
	}
	if (i == ctx->atom->size) {
		node->index.type = AURA_TINT;
		ctx->output->d = neg ? -n : n;
		return 1;
	} else {
		if (*s != '.')
			return 0;
		++s;
		size -= i+1;
		int d = 0;
		for (i=0;i<8 || i<size;i++,s++) {
			if (i<size) {
				if (*s >= '0' && *s <= '9') {
					if (i<8) {
						d = d * 10 + (*s - '0');
					}
				} else {
					return 0;
				}
			} else {
				d = d * 10;
			}
			float f = n + d / 100000000.0f;
			node->index.type = AURA_TFLOAT;
			ctx->output->f = neg ? -f : f;
		}
		return 1;
	}
}

static void
convert_node(struct convert_context *ctx) {
	int i;
	union list_node *n = ctx->output;
	int count = ctx->list->n;
	n->list.n = count;
	n->list.offset = n - ctx->base + 1;
	ctx->list++;
	ctx->output += count + 1;
	for (i=0;i<count;i++) {
		union list_node *node = n + 1 + i;
		if (is_list(ctx->atom)) {
			++ctx->atom;
			node->index.type = AURA_TLIST;
			node->index.offset = ctx->output - ctx->base;
			convert_node(ctx);
		} else {
			node->index.offset = ctx->output - ctx->base;
			char first = ctx->source[ctx->atom->offset];
			switch (first) {
			case '-':
			case '+':
			case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
			case '.':
				if (convert_number(node, ctx))
					break;
				// FALLTHROUGH
			default:
				node->index.type = AURA_TWORD;
				ctx->output->atom.len = ctx->atom->size;
				ctx->output->atom.offset = ctx->atom->offset;
				break;
			}
			++ctx->output;
			++ctx->atom;
		}
	}
}

static void
convert(struct parser_context *ctx, union list_node *node) {
	struct convert_context conv;
	conv.list = ctx->n;
	conv.atom = ctx->a;
	conv.output = node+1;
	conv.base = node;
	conv.source = ctx->beginptr;
	node[0].index.type = AURA_TLIST;
	node[0].index.offset = 1;

	convert_node(&conv);
}

int
auraP_parse(const char * source, int sz, union list_node *node, int node_sz) {
	struct parser_atom a[MAXATOM];
	struct parser_node n[MAXNODE];
	struct parser_context ctx;
	if (sz > MAXSIZE)
		return PARSER_ERR_MAXSIZE;

	ctx.buffer = source;
	ctx.beginptr = source;
	ctx.a = a;
	ctx.n = n;
	ctx.sz = (uint16_t)sz;
	ctx.atom_n = 0;
	ctx.node_n = 0;
	int err = parse_list(&ctx);
	if (err)
		return err;
	int need_sz = ctx.atom_n * 2 + 1;
	if (need_sz > node_sz)
		return need_sz;
	convert(&ctx, node);

	return need_sz;
}

#include <stdio.h>

static void
dump_node(union list_node *node, int index, int indent, const char *source) {
	printf("%*s", indent * 2, "");
	union list_node * data = &node[node[index].index.offset];
	int i;
	switch (node[index].index.type) {
	case AURA_TLIST:
		printf("LIST (%d) :\n", data->list.n);
		for (i=0;i<data->list.n;i++) {
			dump_node(node, data->list.offset+i, indent + 1, source);
		}
		break;
	case AURA_TWORD:
		printf("WORD [%d]\n", data->word);
//		printf("WORD [%.*s]\n", data->atom.len, source + data->atom.offset);
//		break;
		break;
	case AURA_TWORDREF:
		printf("WORDREF [%d]\n", data->word);
		break;
	case AURA_TINT:
		printf("INT [%d]\n", data->d);
		break;
	case AURA_TFLOAT:
		printf("FLOAT [%g]\n", data->f);
		break;
	case AURA_TLOCAL:
		printf("LOCAL [%d]\n", data->word);
		break;
	case AURA_TLOCALSET:
		printf("LOCALSET [");
		for (i=0;i<4 && data->local[i] != 255;i++) {
			printf("%d ", data->local[i]);
		}
		printf("]\n");
		break;
	default:
		printf("[UNKNOWN] %d(%d)\n", node[index].index.type, index);
		break;
	}
}

void
auraP_dump(union list_node *node, const char *source) {
	dump_node(node, 0, 0, source);
}

#ifdef PARSER_TESTMAIN


#define test(s, node) auraP_parse(s, sizeof(s), node, sizeof(node)/sizeof(node[0]))

int
main() {
	union list_node node[100];
	const char source[] = " [hello world] \n(1 2) c[[42 [+1.2 -.345678912345678] -5.a]]  ";
	int n = test(source, node);
	printf("n = %d\n", n);
	auraP_dump(node, source);
	return 0;
}

#endif