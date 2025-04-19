#include <stdlib.h>

#include "stb/ds.h"

#include "common.h"

Value_Data *value_new(Value_Tag tag) {
	Value_Data *value = malloc(sizeof(Value_Data));
	memset(value, 0, sizeof(Value_Data));
	value->tag = tag;
	return value;
}

Node_Data *node_data_new(Node_Kind kind) {
	Node_Data *data = malloc(sizeof(Node_Data));
	memset(data, 0, sizeof(Node_Data));
	data->kind = kind;
	return data;
}

Value get_type(Context *context, Node *node) {
	Node_Types *node_types = hmget(context->node_types, context->static_argument_id);
	if (node_types == NULL) {
		node_types = malloc(sizeof(Node_Types *));
		*node_types = NULL;
		hmput(context->node_types, context->static_argument_id, node_types);
	}

	Value result = hmget(*node_types, node);
	if (result.value == NULL && context->static_argument_id != 0) {
		size_t saved_static_argument_id = context->static_argument_id;
		context->static_argument_id = 0;
		result = get_type(context, node);
		context->static_argument_id = saved_static_argument_id;
	}
	return result;
}

void set_type(Context *context, Node *node, Value type) {
	Node_Types *node_types = hmget(context->node_types, context->static_argument_id);
	if (node_types == NULL) {
		node_types = malloc(sizeof(Node_Types *));
		*node_types = NULL;
		hmput(context->node_types, context->static_argument_id, node_types);
	}
	hmput(*node_types, node, type);
}

Node_Data *get_data(Context *context, Node *node) {
	Node_Datas *node_datas = hmget(context->node_datas, context->static_argument_id);
	if (node_datas == NULL) {
		node_datas = malloc(sizeof(Node_Datas *));
		*node_datas = NULL;
		hmput(context->node_datas, context->static_argument_id, node_datas);
	}

	Node_Data *result = hmget(*node_datas, node);
	if (result == NULL && context->static_argument_id != 0) {
		size_t saved_static_argument_id = context->static_argument_id;
		context->static_argument_id = 0;
		result = get_data(context, node);
		context->static_argument_id = saved_static_argument_id;
	}
	return result;
}

void set_data(Context *context, Node *node, Node_Data *value) {
	Node_Datas *node_datas = hmget(context->node_datas, context->static_argument_id);
	if (node_datas == NULL) {
		node_datas = malloc(sizeof(Node_Datas *));
		*node_datas = NULL;
		hmput(context->node_datas, context->static_argument_id, node_datas);
	}
	hmput(*node_datas, node, value);
}

void reset_node(Context *context, Node *node) {
	Node_Types *node_types = hmget(context->node_types, context->static_argument_id);
	if (node_types == NULL) {
		node_types = malloc(sizeof(Node_Types *));
		*node_types = NULL;
		hmput(context->node_types, context->static_argument_id, node_types);
	}
	(void) hmdel(*node_types, node);

	Node_Datas *node_datas = hmget(context->node_datas, context->static_argument_id);
	if (node_datas == NULL) {
		node_datas = malloc(sizeof(Node_Datas *));
		*node_datas = NULL;
		hmput(context->node_datas, context->static_argument_id, node_datas);
	}
	(void) hmdel(*node_datas, node);
}

Value create_string_type() {
	// return (Value) { .value = value_new(STRING_TYPE_VALUE) };
	return create_internal_type("str");
}

Value create_boolean_type() {
	return create_internal_type("bool");
}

Value create_internal_type(char *identifier) {
	Value_Data *internal_type = value_new(INTERNAL_VALUE);
	internal_type->internal.identifier = identifier;
	return (Value) { .value = internal_type };
}
