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
	Node_Types *types = hmget(context->types, context->static_id);
	if (types == NULL) {
		types = malloc(sizeof(Node_Types *));
		*types = NULL;
		hmput(context->types, context->static_id, types);
	}
	return hmget(*types, node);
}

void set_type(Context *context, Node *node, Value type) {
	Node_Types *types = hmget(context->types, context->static_id);
	if (types == NULL) {
		types = malloc(sizeof(Node_Types *));
		*types = NULL;
		hmput(context->types, context->static_id, types);
	}
	hmput(*types, node, type);
}

Node_Data *get_data(Context *context, Node *node) {
	Node_Datas *datas = hmget(context->datas, context->static_id);
	if (datas == NULL) {
		datas = malloc(sizeof(Node_Datas *));
		*datas = NULL;
		hmput(context->datas, context->static_id, datas);
	}
	return hmget(*datas, node);
}

void set_data(Context *context, Node *node, Node_Data *value) {
	Node_Datas *datas = hmget(context->datas, context->static_id);
	if (datas == NULL) {
		datas = malloc(sizeof(Node_Datas *));
		*datas = NULL;
		hmput(context->datas, context->static_id, datas);
	}
	hmput(*datas, node, value);
}

void reset_node(Context *context, Node *node) {
	Node_Types *types = hmget(context->types, context->static_id);
	if (types == NULL) {
		types = malloc(sizeof(Node_Types *));
		*types = NULL;
		hmput(context->types, context->static_id, types);
	}
	(void) hmdel(*types, node);

	Node_Datas *datas = hmget(context->datas, context->static_id);
	if (datas == NULL) {
		datas = malloc(sizeof(Node_Datas *));
		*datas = NULL;
		hmput(context->datas, context->static_id, datas);
	}
	(void) hmdel(*datas, node);
}
