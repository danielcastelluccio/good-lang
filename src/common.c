#include <stdlib.h>

#include "stb/ds.h"

#include "common.h"

Value *value_new(Value_Tag tag) {
	Value *value = malloc(sizeof(Value));
	memset(value, 0, sizeof(Value));
	value->tag = tag;
	return value;
}

Node_Data *node_data_new(Node_Kind kind) {
	Node_Data *data = malloc(sizeof(Node_Data));
	memset(data, 0, sizeof(Node_Data));
	data->kind = kind;
	return data;
}

Value *get_type(Context *context, Node *node) {
	Node_Types *node_types = hmget(context->node_types, context->generic_id);
	if (node_types == NULL) {
		node_types = malloc(sizeof(Node_Types *));
		*node_types = NULL;
		hmput(context->node_types, context->generic_id, node_types);
	}

	Value *result = hmget(*node_types, node);
	if (result == NULL && context->generic_id != 0) {
		size_t saved_generic_id = context->generic_id;
		context->generic_id = 0;
		result = get_type(context, node);
		context->generic_id = saved_generic_id;
	}
	return result;
}

void set_type(Context *context, Node *node, Value *value) {
	Node_Types *node_types = hmget(context->node_types, context->generic_id);
	if (node_types == NULL) {
		node_types = malloc(sizeof(Node_Types *));
		*node_types = NULL;
		hmput(context->node_types, context->generic_id, node_types);
	}
	hmput(*node_types, node, value);
}

Node_Data *get_data(Context *context, Node *node) {
	Node_Datas *node_datas = hmget(context->node_datas, context->generic_id);
	if (node_datas == NULL) {
		node_datas = malloc(sizeof(Node_Datas *));
		*node_datas = NULL;
		hmput(context->node_datas, context->generic_id, node_datas);
	}

	Node_Data *result = hmget(*node_datas, node);
	if (result == NULL && context->generic_id != 0) {
		size_t saved_generic_id = context->generic_id;
		context->generic_id = 0;
		result = get_data(context, node);
		context->generic_id = saved_generic_id;
	}
	return result;
}

void set_data(Context *context, Node *node, Node_Data *value) {
	Node_Datas *node_datas = hmget(context->node_datas, context->generic_id);
	if (node_datas == NULL) {
		node_datas = malloc(sizeof(Node_Datas *));
		*node_datas = NULL;
		hmput(context->node_datas, context->generic_id, node_datas);
	}
	hmput(*node_datas, node, value);
}

void reset_node(Context *context, Node *node) {
	Node_Types *node_types = hmget(context->node_types, context->generic_id);
	if (node_types == NULL) {
		node_types = malloc(sizeof(Node_Types *));
		hmput(context->node_types, context->generic_id, node_types);
	}
	(void) hmdel(*node_types, node);

	Node_Datas *node_datas = hmget(context->node_datas, context->generic_id);
	if (node_datas == NULL) {
		node_datas = malloc(sizeof(Node_Datas *));
		hmput(context->node_datas, context->generic_id, node_datas);
	}
	(void) hmdel(*node_datas, node);
}

Value *strip_define_data(Value *value) {
	while (value->tag == DEFINE_DATA_VALUE) {
		value = value->define_data.value;
	}
	return value;
}

Value *create_string_type() {
	Value *slice = value_new(SLICE_TYPE_VALUE);
	Value *byte = value_new(INTERNAL_VALUE);
	byte->internal.identifier = "byte";
	slice->slice_type.inner = byte;

	return slice;
}

Value *create_slice_type(Value *inner) {
	Value *slice_type = value_new(SLICE_TYPE_VALUE);
	slice_type->slice_type.inner = inner;
	return slice_type;
}
