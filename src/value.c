#include <stdlib.h>
#include <string.h>

#include "value.h"

Value_Data *values = NULL;
size_t value_index = 0;

Value_Data *value_new(Value_Tag tag) {
	if (values == NULL || value_index == 65536) {
		values = malloc(sizeof(Value_Data) * 65536);
		value_index = 0;
	}

	Value_Data *value = &values[value_index++];
	value->tag = tag;
	return value;
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
	array_type.value->array_type.size = (Value) {};
	array_type.value->array_type.sentinel = (Value) {};
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

Value create_range_type(Value value) {
	Value range_type = create_value(RANGE_TYPE_VALUE);
	range_type.value->range_type.type = value;
	return range_type;
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

Value create_enum(size_t value) {
	Value enum_ = create_value(ENUM_VALUE);
	enum_.value->enum_.value = value;
	return enum_;
}
