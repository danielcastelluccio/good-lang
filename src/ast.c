#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"

Node *nodes = NULL;
size_t node_index = 0;

Node *ast_new(Node_Kind kind, Source_Location location) {
	if (nodes == NULL || node_index == 1 << 20) {
		nodes = malloc(sizeof(Node) * 1 << 20);
		node_index = 0;
	}

	Node *node = &nodes[node_index++];
	node->kind = kind;
	node->location = location;
	node->data_count = 0;
	node->data = NULL;
	return node;
}

void set_assign_value(Node *node, Node *assign_value, bool static_) {
	switch (node->kind) {
		case STRUCTURE_ACCESS_NODE: {
			node->structure_access.assign_value = assign_value;
			break;
		}
		case IDENTIFIER_NODE: {
			node->identifier.assign_value = assign_value;
			node->identifier.assign_static = static_;
			break;
		}
		case ARRAY_ACCESS_NODE: {
			node->array_access.assign_value = assign_value;
			break;
		}
		case DEOPTIONAL_NODE: {
			node->deoptional.assign_value = assign_value;
			break;
		}
		case DEREFERENCE_NODE: {
			node->dereference.assign_value = assign_value;
			break;
		}
		case INTERNAL_NODE: {
			assert(node->internal.kind == INTERNAL_EMBED || node->internal.kind == INTERNAL_CONTEXT || node->internal.kind == INTERNAL_GLOBAL_VALUE);
			node->internal.assign_value = assign_value;

			node->internal.assign_static = static_;
			break;
		}
		default:
			assert(false);
	}
}
