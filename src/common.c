#include <stdio.h>
#include <stdlib.h>

#include "stb/ds.h"

#include "common.h"

Node_Data *datas = NULL;
size_t datas_index = 0;

Node_Data *data_new(Node_Kind kind) {
	if (datas == NULL || datas_index == 65536) {
		datas = malloc(sizeof(Node_Data) * 65536);
		datas_index = 0;
	}

	Node_Data *data = &datas[datas_index++];
	memset(data, 0, sizeof(Node_Data));
	data->kind = kind;
	return data;
}

Value get_type(Context *context, Node *node) {
	if ((long) context->static_id >= arrlen(context->types)) {
		for (size_t i = arrlen(context->types); i < context->static_id + 1; i++) {
			Node_Types *types = malloc(sizeof(Node_Types *));
			*types = NULL;
			arrpush(context->types, types);
		}
	}

	return hmget(*context->types[context->static_id], node);
}

Value set_type(Context *context, Node *node, Value type) {
	if ((long) context->static_id >= arrlen(context->types)) {
		for (size_t i = arrlen(context->types); i < context->static_id + 1; i++) {
			Node_Types *types = malloc(sizeof(Node_Types *));
			*types = NULL;
			arrpush(context->types, types);
		}
	}

	hmput(*context->types[context->static_id], node, type);

	return type;
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

void reset_node(Context *context, Node *node) {
	if ((long) context->static_id >= arrlen(context->types)) {
		for (size_t i = arrlen(context->types); i < context->static_id + 1; i++) {
			Node_Types *types = malloc(sizeof(Node_Types *));
			*types = NULL;
			arrpush(context->types, types);
		}
	}

	(void) hmdel(*context->types[context->static_id], node);

	if ((long) context->static_id >= arrlen(context->datas)) {
		for (size_t i = arrlen(context->datas); i < context->static_id + 1; i++) {
			Node_Datas *datas = malloc(sizeof(Node_Data *));
			*datas = NULL;
			arrpush(context->datas, datas);
		}
	}
	(void) hmdel(*context->datas[context->static_id], node);
}
