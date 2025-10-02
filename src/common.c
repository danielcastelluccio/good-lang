#include <stdio.h>
#include <stdlib.h>

#include "stb/ds.h"

#include "common.h"

Node_Data *datas = NULL;
size_t datas_index = 0;

Node_Data *data_new() {
	if (datas == NULL || datas_index == 65536) {
		datas = malloc(sizeof(Node_Data) * 65536);
		memset(datas, 0, sizeof(Node_Data) * 65536);
		datas_index = 0;
	}

	Node_Data *data = &datas[datas_index++];
	return data;
}

Value get_type(Context *context, Node *node) {
	Node_Data *data = get_data(context, node);
	if (data == NULL) return (Value) {};
	return data->type;
}

void *data = NULL;
size_t data_index = 0;

void *custom_realloc(void *p, size_t old_size, size_t size) {
	if (data == NULL || data_index + size >= 65536) {
		if (size >= 65536) {
			data = malloc(size);
		} else {
			data = malloc(65536);
		}
		data_index = 0;
	}

	void *result = data + data_index;
	if (p != NULL) {
		memcpy(result, p, old_size);
	}

	data_index += size;
	return result;
}

uint64_t round_up_pow2_64(uint64_t v) {
    if (v <= 1) return 1;
    return 1ull << (64 - __builtin_clzll(v - 1));
}

void ensure_capacity(Context *context, Node *node) {
	size_t new_count = round_up_pow2_64(context->static_id + 1);
	if (new_count > node->data_count) {
		if (new_count > 1) {
			Node_Data *data = node->data;
			if (node->data_count == 1) {
				node->data = NULL;
			}

			node->data = custom_realloc(node->data, node->data_count * sizeof(Node_Data *), new_count * sizeof(Node_Data *));

			if (node->data_count == 1) {
				node->datas[node->data_count - 1] = data;
			}
			for (size_t i = node->data_count; i < new_count; i++) {
				node->datas[i] = NULL;
			}
		}

		node->data_count = new_count;
	}
}

void set_data(Context *context, Node *node, Node_Data *value) {
	ensure_capacity(context, node);

	if (node->data_count == 1) {
		node->data = value;
	} else {
		node->datas[context->static_id] = value;
	}
}

Node_Data *get_data(Context *context, Node *node) {
	ensure_capacity(context, node);

	if (node->data_count == 1) {
		return node->data;
	} else {
		return node->datas[context->static_id];
	}
}

Node_Data **get_data_ref(Context *context, Node *node) {
	ensure_capacity(context, node);

	if (node->data_count == 1) {
		return &node->data;
	} else {
		return &node->datas[context->static_id];
	}
}

void reset_node(Context *context, Node *node) {
	ensure_capacity(context, node);

	if (node->data_count == 1) {
		node->data = NULL;
	} else {
		node->datas[context->static_id] = NULL;
	}
}

Node_Data *data_create(Context *context, Node *node) {
	Node_Data *data = data_new();
	set_data(context, node, data);
	return data;
}
