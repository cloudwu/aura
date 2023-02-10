#ifndef aura_parser_h
#define aura_parser_h

#include <stdint.h>

#define PARSER_ERR_MAXSIZE -1
#define PARSER_ERR_TUPLE -2
#define PARSER_ERR_MAXNODE -3
#define PARSER_ERR_MAXATOM -4
#define PARSER_ERR_LIST -5

union list_node {
	struct {
		uint8_t type;
		uint16_t offset;
	} index;
	struct {
		uint16_t n;
		uint16_t offset;
	} list;
	struct {
		uint16_t len;
		uint16_t offset;
	} atom;
	uint8_t local[4];
	int word;
	float f;
	int d;
};

int auraP_parse(const char * source, int sz, union list_node *node, int node_sz);
void auraP_dump(union list_node *node, const char *source);

#endif
