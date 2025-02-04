#include <stdlib.h>
#include <string.h>

#include "ast.h"

Node *ast_new(Node_Kind kind, Source_Location location) {
	Node *node = malloc(sizeof(Node));
	memset(node, 0, sizeof(Node));
	node->kind = kind;
	node->location = location;
	return node;
}
