#include <stdio.h>
#include <stdlib.h>

#include "stb/ds.h"

#include "common.h"

Node_Data *datas = NULL;
size_t datas_index = 0;

Node_Data *data_new() {
	if (datas == NULL || datas_index == 65536) {
		datas = malloc(sizeof(Node_Data) * 65536);
		datas_index = 0;
	}

	Node_Data *data = &datas[datas_index++];
	memset(data, 0, sizeof(Node_Data));
	return data;
}

void set_data(Context *context, Node *node, Node_Data *value) {
	if ((long) context->static_id >= arrlen(context->datas)) {
		for (size_t i = arrlen(context->datas); i < context->static_id + 1; i++) {
			Node_Datas *datas = malloc(sizeof(Node_Data *));
			*datas = NULL;
			arrpush(context->datas, datas);
		}
	}

	hmput(*context->datas[context->static_id], node, value);
}

Node_Data *data_create(Context *context, Node *node) {
	Node_Data *data = data_new();
	set_data(context, node, data);
	return data;
}

Value get_type(Context *context, Node *node) {
	Node_Data *data = get_data(context, node);
	if (data == NULL) return (Value) {};
	return data->type;
}

Node_Data *get_data(Context *context, Node *node) {
	if ((long) context->static_id >= arrlen(context->datas)) {
		for (size_t i = arrlen(context->datas); i < context->static_id + 1; i++) {
			Node_Datas *datas = malloc(sizeof(Node_Data *));
			*datas = NULL;
			arrpush(context->datas, datas);
		}
	}
	return hmget(*context->datas[context->static_id], node);
}

Node_Data **get_data_ref(Context *context, Node *node) {
	if ((long) context->static_id >= arrlen(context->datas)) {
		for (size_t i = arrlen(context->datas); i < context->static_id + 1; i++) {
			Node_Datas *datas = malloc(sizeof(Node_Data *));
			*datas = NULL;
			arrpush(context->datas, datas);
		}
	}
	Node_Datas data = hmgetinsertp(*context->datas[context->static_id], node);
	return &data->value;
}

void reset_node(Context *context, Node *node) {
	if ((long) context->static_id >= arrlen(context->datas)) {
		for (size_t i = arrlen(context->datas); i < context->static_id + 1; i++) {
			Node_Datas *datas = malloc(sizeof(Node_Data *));
			*datas = NULL;
			arrpush(context->datas, datas);
		}
	}
	(void) hmdel(*context->datas[context->static_id], node);
}
