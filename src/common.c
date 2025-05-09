#include <stdio.h>
#include <stdlib.h>

#include "stb/ds.h"

#include "common.h"

Value_Data *value_new(Value_Tag tag) {
	Value_Data *value = malloc(sizeof(Value_Data));
	memset(value, 0, sizeof(Value_Data));
	value->tag = tag;
	return value;
}

Node_Data *data_new(Node_Kind kind) {
	Node_Data *data = malloc(sizeof(Node_Data));
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

Value create_value(Value_Tag tag) {
	return (Value) { .value = value_new(tag) };
}

Value create_pointer_type(Value value) {
	Value pointer_type = create_value(POINTER_TYPE_VALUE);
	pointer_type.value->pointer_type.inner = value;
	return pointer_type;
}

Value create_optional_type(Value value) {
	Value optional_type = create_value(OPTIONAL_TYPE_VALUE);
	optional_type.value->optional_type.inner = value;
	return optional_type;
}

Value create_array_type(Value value) {
	Value array_type = create_value(ARRAY_TYPE_VALUE);
	array_type.value->array_type.inner = value;
	return array_type;
}

Value create_array_view_type(Value value) {
	Value array_view_type = create_value(ARRAY_VIEW_TYPE_VALUE);
	array_view_type.value->array_view_type.inner = value;
	return array_view_type;
}

Value create_integer_type(bool signed_, size_t size) {
	Value integer_type = create_value(INTEGER_TYPE_VALUE);
	integer_type.value->integer_type.signed_ = signed_;
	integer_type.value->integer_type.size = size;
	return integer_type;
}

Value create_float_type(size_t size) {
	Value float_type = create_value(FLOAT_TYPE_VALUE);
	float_type.value->float_type.size = size;
	return float_type;
}

Value create_integer(size_t value) {
	Value integer = create_value(INTEGER_VALUE);
	integer.value->integer.value = value;
	return integer;
}

Value create_byte(char value) {
	Value byte = create_value(BYTE_VALUE);
	byte.value->byte.value = value;
	return byte;
}

Value create_boolean(bool value) {
	Value boolean = create_value(BOOLEAN_VALUE);
	boolean.value->boolean.value = value;
	return boolean;
}
