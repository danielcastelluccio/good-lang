#include <stdlib.h>
#include <string.h>

#include "ast.h"

Node *nodes = NULL;
size_t node_index = 0;

Node *ast_new(Node_Kind kind, Source_Location location) {
	if (nodes == NULL || node_index == 1024) {
		nodes = malloc(sizeof(Node) * 1024);
		node_index = 0;
	}

	Node *node = &nodes[node_index++];
	memset(node, 0, sizeof(Node));
	node->kind = kind;
	node->location = location;
	return node;
}
