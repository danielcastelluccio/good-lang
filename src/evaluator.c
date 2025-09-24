#include <assert.h>
#include <linux/limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "stb/ds.h"

#include "ast.h"
#include "evaluator.h"
#include "parser.h"
#include "util.h"
#include "value.h"

#include <setjmp.h>

#include <stdio.h>

typedef struct { Node_Data *key; Value value; } *Variable_Datas;
typedef struct { Node_Data *key; Value value; } *Switch_Datas;
typedef struct { Node_Data *key; Value *value; } *For_Datas;

typedef struct {
	Context *context;
	Variable_Datas variables; // stb_ds
	Switch_Datas switchs; // stb_ds
	For_Datas fors; // stb_ds
} State;

jmp_buf jmp;
Value jmp_result;

Node *function_node;
Value *function_arguments;

bool value_equal(Value_Data *value1, Value_Data *value2) {
	if (value1 == NULL || value2 == NULL) return false;
	if (value1->tag != value2->tag) return false;

	switch (value1->tag) {
		case POINTER_TYPE_VALUE: {
			if (value1->pointer_type.inner.value == NULL && value2->pointer_type.inner.value == NULL) return true;
			return value_equal(value1->pointer_type.inner.value, value2->pointer_type.inner.value);
		}
		case ARRAY_TYPE_VALUE: {
			if (value1->array_type.size.value != NULL) {
				if (value2->array_type.size.value == NULL) return false;
				if (!value_equal(value1->array_type.size.value, value2->array_type.size.value)) return false;
			}

			if (value1->array_type.sentinel.value != NULL) {
				if (value2->array_type.sentinel.value == NULL) return false;
				if (!value_equal(value1->array_type.sentinel.value, value2->array_type.sentinel.value)) return false;
			}

			return value_equal(value1->array_type.inner.value, value2->array_type.inner.value);
		}
		case ARRAY_VIEW_TYPE_VALUE: {
			return value_equal(value1->array_view_type.inner.value, value2->array_view_type.inner.value);
		}
		case ARRAY_VIEW_VALUE: {
			if (value1->array_view.length != value2->array_view.length) return false;

			for (size_t i = 0; i < value1->array_view.length; i++) {
				if (!value_equal(value1->array_view.values[i], value2->array_view.values[i])) return false;
			}
			return true;
		}
		case OPTIONAL_TYPE_VALUE: {
			return value_equal(value1->optional_type.inner.value, value2->optional_type.inner.value);
		}
		case RESULT_TYPE_VALUE: {
			if (value1->result_type.value.value != NULL) {
				if (!value_equal(value1->result_type.value.value, value2->result_type.value.value)) return false;
			}

			return value_equal(value1->result_type.error.value, value2->result_type.error.value);
		}
		case INTEGER_TYPE_VALUE: {
			return value1->integer_type.signed_ == value2->integer_type.signed_ && value1->integer_type.size == value2->integer_type.size;
		}
		case STRUCT_TYPE_VALUE: {
			if (value1->struct_type.node != value2->struct_type.node) return false;

			if (arrlen(value1->struct_type.members) != arrlen(value2->struct_type.members)) return false;

			for (long int i = 0; i < arrlen(value1->struct_type.members); i++) {
				if (!sv_eq(value1->struct_type.node->struct_type.members[i].name, value2->struct_type.node->struct_type.members[i].name)) return false;
				if (!value_equal(value1->struct_type.members[i].value, value2->struct_type.members[i].value)) return false;
			}

			return true;
		}
		case TUPLE_TYPE_VALUE: {
			if (arrlen(value1->tuple_type.members) != arrlen(value2->tuple_type.members)) return false;

			for (long int i = 0; i < arrlen(value1->tuple_type.members); i++) {
				if (!value_equal(value1->tuple_type.members[i].value, value2->tuple_type.members[i].value)) return false;
			}

			return true;
		}
		case UNION_TYPE_VALUE: {
			if (arrlen(value1->union_type.items) != arrlen(value2->union_type.items)) return false;

			for (long int i = 0; i < arrlen(value1->union_type.items); i++) {
				if (!sv_eq(value1->union_type.items[i].identifier, value2->union_type.items[i].identifier)) return false;
				if (!value_equal(value1->union_type.items[i].type.value, value2->union_type.items[i].type.value)) return false;
			}

			return true;
		}
		case TAGGED_UNION_TYPE_VALUE: {
			if (arrlen(value1->tagged_union_type.items) != arrlen(value2->tagged_union_type.items)) return false;

			for (long int i = 0; i < arrlen(value1->tagged_union_type.items); i++) {
				if (!sv_eq(value1->tagged_union_type.items[i].identifier, value2->tagged_union_type.items[i].identifier)) return false;
				if (!value_equal(value1->tagged_union_type.items[i].type.value, value2->tagged_union_type.items[i].type.value)) return false;
			}

			return true;
		}
		case ENUM_TYPE_VALUE: {
			if (arrlen(value1->enum_type.items) != arrlen(value2->enum_type.items)) return false;

			for (long int i = 0; i < arrlen(value1->enum_type.items); i++) {
				if (!sv_eq(value1->enum_type.items[i], value2->enum_type.items[i])) return false;
			}

			return true;
		}
		case FUNCTION_TYPE_VALUE: {
			if (arrlen(value1->function_type.arguments) != arrlen(value2->function_type.arguments)) return false;
			for (long int i = 0; i < arrlen(value1->function_type.arguments); i++) {
				if (!sv_eq(value1->function_type.arguments[i].identifier, value2->function_type.arguments[i].identifier)) return false;
				if (!value_equal(value1->function_type.arguments[i].type.value, value2->function_type.arguments[i].type.value)) return false;
			}

			if ((value1->function_type.return_type.value == NULL && value2->function_type.return_type.value != NULL) || (value1->function_type.return_type.value != NULL && value2->function_type.return_type.value == NULL)) return false;
			if (value1->function_type.return_type.value != NULL) {
				if (!value_equal(value1->function_type.return_type.value, value2->function_type.return_type.value)) {
					return false;
				}
			}

			if (value1->function_type.variadic != value2->function_type.variadic) return false;

			return true;
		}
		case BYTE_TYPE_VALUE:
		case BOOLEAN_TYPE_VALUE:
		case TYPE_TYPE_VALUE: {
			return true;
		}
		case INTEGER_VALUE: {
			return value1->integer.value == value2->integer.value;
		}
		case ENUM_VALUE: {
			return value1->enum_.value == value2->enum_.value;
		}
		case BYTE_VALUE: {
			return value1->byte.value == value2->byte.value;
		}
		default:
			assert(false);
	}
}

bool type_assignable(Value_Data *type1, Value_Data *type2) {
	return value_equal(type1, type2);
}

#define handle_evaluate_error(/* State * */ state, /* Source_Location */ location, /* char * */ fmt, ...) { \
	printf("%s:%u:%u: " fmt "\n", state->context->data->source_files[location.path_ref], location.row, location.column __VA_OPT__(,) __VA_ARGS__); \
	exit(1); \
}

Value create_value_data(Value_Data *value, Node *node) {
	return (Value) { .value = value, .node = node };
}

static Value initialize_value(Value type) {
	Value value = {};
	switch (type.value->tag) {
		case ARRAY_TYPE_VALUE: {
			value = create_value(ARRAY_VALUE);

			for (long int i = 0; i < type.value->array_type.size.value->integer.value; i++) {
				arrpush(value.value->array.values, initialize_value(type.value->array_type.inner).value);
			}
			break;
		}
		case INTEGER_TYPE_VALUE: {
			value = create_integer(0);
			break;
		}
		case STRUCT_TYPE_VALUE: {
			value = create_value(STRUCT_VALUE);

			for (long int i = 0; i < arrlen(type.value->struct_type.members); i++) {
				arrpush(value.value->struct_.values, initialize_value(type.value->struct_type.members[i]).value);
			}
			break;
		}
		default:
			assert(false);
	}

	return value;
}

static Value clone_value(Value input) {
	Value result = {};
	switch (input.value->tag) {
		case BOOLEAN_VALUE: {
			result = create_boolean(input.value->boolean.value);
			break;
		}
		case BOOLEAN_TYPE_VALUE: {
			result = create_value(BOOLEAN_TYPE_VALUE);
			break;
		}
		case INTEGER_VALUE: {
			result = create_integer(input.value->integer.value);
			break;
		}
		case INTEGER_TYPE_VALUE: {
			result = create_integer_type(input.value->integer_type.signed_, input.value->integer_type.size);
			break;
		}
		case STRUCT_VALUE: {
			result = create_value(STRUCT_VALUE);
			for (long int i = 0; i < arrlen(input.value->struct_.values); i++) {
				arrpush(result.value->struct_.values, clone_value((Value) { .value = input.value->struct_.values[i] }).value);
			}
			break;
		}
		default:
			assert(false);
	}

	return result;
}

static Value evaluate_state(State *state, Node *node);

static Value evaluate_function(State *state, Node *node) {
	Function_Node function = node->function;

	Node_Data *function_type_data = get_data(state->context, function.function_type);
	if (function_type_data == NULL) {
		Value_Data *value = value_new(FUNCTION_STUB_VALUE);
		value->function_stub.node = node;

		Scope *scopes = NULL;
		for (long int i = 0; i < arrlen(state->context->scopes); i++) {
			arrpush(scopes, state->context->scopes[i]);
		}

		value->function_stub.scopes = scopes;

		return create_value_data(value, node);
	}

	Value function_type_value = function_type_data->function_type.value;

	Value_Data *function_value = value_new(FUNCTION_VALUE);
	function_value->function.type = function_type_value.value;
	if (function.body != NULL) {
		function_value->function.body = function.body;
	}

	function_value->function.static_id = state->context->static_id;
	function_value->function.node = node;

	return create_value_data(function_value, node);
}

static Value evaluate_function_type(State *state, Node *node) {
	return create_value_data(get_data(state->context, node)->function_type.value.value, node);
}

static Value evaluate_struct_type(State *state, Node *node) {
	Struct_Type_Node struct_type = node->struct_type;

	Value_Data *struct_value_data = value_new(STRUCT_TYPE_VALUE);
	struct_value_data->struct_type.node = node;
	Value struct_value = create_value_data(struct_value_data, node);

	Scope scope = {
		.node = node,
		.current_type = struct_value
	};
	arrpush(state->context->scopes, scope);

	struct_value_data->struct_type.members = NULL;
	for (long int i = 0; i < arrlen(struct_type.members); i++) {
		arrpush(struct_value_data->struct_type.members, evaluate_state(state, struct_type.members[i].type));
	}

	if (struct_type.inherit_function) {
		struct_value_data->struct_type.inherited_node = function_node;
		struct_value_data->struct_type.inherited_arguments = function_arguments;
	}

	Scope *scopes = NULL;
	for (long int i = 0; i < arrlen(state->context->scopes); i++) {
		arrpush(scopes, state->context->scopes[i]);
	}
	struct_value.value->struct_type.scopes = scopes;

	(void) arrpop(state->context->scopes);

	return struct_value;
}

static Value evaluate_union_type(State *state, Node *node) {
	Union_Type_Node union_type = node->union_type;

	Value_Data *union_value = value_new(UNION_TYPE_VALUE);
	union_value->union_type.items = NULL;
	for (long int i = 0; i < arrlen(union_type.members); i++) {
		Union_Item_Value item = {
			.identifier = union_type.members[i].name,
			.type = evaluate_state(state, union_type.members[i].type)
		};
		arrpush(union_value->union_type.items, item);
	}

	return create_value_data(union_value, node);
}

static Value evaluate_tagged_union_type(State *state, Node *node) {
	Tagged_Union_Type_Node tagged_union_type = node->tagged_union_type;

	Value_Data *tagged_union_value = value_new(TAGGED_UNION_TYPE_VALUE);
	tagged_union_value->tagged_union_type.node = node;
	tagged_union_value->tagged_union_type.items = NULL;
	Value_Data *enum_value = value_new(ENUM_TYPE_VALUE);
	tagged_union_value->tagged_union_type.enum_ = enum_value;
	for (long int i = 0; i < arrlen(tagged_union_type.members); i++) {
		Tagged_Union_Item_Value item = {
			.identifier = tagged_union_type.members[i].name,
			.type = evaluate_state(state, tagged_union_type.members[i].type)
		};
		arrpush(tagged_union_value->tagged_union_type.items, item);
		arrpush(enum_value->enum_type.items, tagged_union_type.members[i].name);
	}

	return create_value_data(tagged_union_value, node);
}

static Value evaluate_enum_type(State *state, Node *node) {
	(void) state;
	Enum_Type_Node enum_type = node->enum_type;

	Value_Data *enum_value = value_new(ENUM_TYPE_VALUE);
	enum_value->enum_type.items = NULL;
	for (long int i = 0; i < arrlen(enum_type.items); i++) {
		arrpush(enum_value->enum_type.items, enum_type.items[i]);
	}

	return create_value_data(enum_value, node);
}

static Value evaluate_module(State *state, Node *node) {
	Module_Node module = node->module;
	Value_Data *module_value = value_new(MODULE_VALUE);

	Scope *scopes = NULL;
	for (long int i = 0; i < arrlen(state->context->scopes); i++) {
		arrpush(scopes, state->context->scopes[i]);
	}

	arrpush(scopes, (Scope) { .node = module.body });

	module_value->module.body = module.body;
	module_value->module.scopes = scopes;

	return create_value_data(module_value, node);
}

static Value evaluate_module_type(State *state, Node *node) {
	(void) state;
	return create_value_data(value_new(MODULE_TYPE_VALUE), node);
}

static Value evaluate_pointer(State *state, Node *node) {
	Pointer_Type_Node pointer = node->pointer_type;

	Value_Data *pointer_type_value = value_new(POINTER_TYPE_VALUE);
	pointer_type_value->pointer_type.inner = (Value) {};
	if (pointer.inner) {
		pointer_type_value->pointer_type.inner = evaluate_state(state, pointer.inner);
	}
	return create_value_data(pointer_type_value, node);
}

static Value evaluate_optional(State *state, Node *node) {
	Optional_Type_Node optional = node->optional_type;

	Value_Data *optional_type_value = value_new(OPTIONAL_TYPE_VALUE);
	optional_type_value->optional_type.inner = evaluate_state(state, optional.inner);
	return create_value_data(optional_type_value, node);
}

static Value evaluate_result(State *state, Node *node) {
	Result_Type_Node result = node->result_type;

	Value_Data *result_type_value = value_new(RESULT_TYPE_VALUE);
	result_type_value->result_type.value = (Value) {};
	if (result.value != NULL) {
		result_type_value->result_type.value = evaluate_state(state, result.value);
	}
	result_type_value->result_type.error = evaluate_state(state, result.error);
	return create_value_data(result_type_value, node);
}

static Value evaluate_array_type(State *state, Node *node) {
	Array_Type_Node array_type = node->array_type;

	Value_Data *array_type_value = value_new(ARRAY_TYPE_VALUE);
	array_type_value->array_type.size = evaluate_state(state, array_type.size);
	if (array_type.sentinel != NULL) {
		array_type_value->array_type.sentinel = evaluate_state(state, array_type.sentinel);
	}
	array_type_value->array_type.inner = evaluate_state(state, array_type.inner);
	return create_value_data(array_type_value, node);
}

static Value evaluate_array_view_type(State *state, Node *node) {
	Array_View_Type_Node array_view_type = node->array_view_type;

	Value_Data *array_type_value = value_new(ARRAY_VIEW_TYPE_VALUE);
	array_type_value->array_view_type.inner = evaluate_state(state, array_view_type.inner);
	return create_value_data(array_type_value, node);
}

static Value evaluate_identifier(State *state, Node *node) {
	Identifier_Node identifier = node->identifier;
	Identifier_Data identifier_data = get_data(state->context, node)->identifier;

	switch (identifier_data.kind) {
		case IDENTIFIER_VALUE:
			return identifier_data.value;
		case IDENTIFIER_STATIC_VARIABLE:
			return hmget(state->context->static_variables, identifier_data.static_variable.node_data);
		case IDENTIFIER_ARGUMENT:
			return function_arguments[identifier_data.argument_index];
		case IDENTIFIER_UNDERSCORE:
			return create_value_data(NULL, node);
		case IDENTIFIER_VARIABLE: {
			Node_Data *variable_data = get_data(state->context, identifier_data.variable);
			if (identifier.assign_value != NULL) {
				hmput(state->variables, variable_data, evaluate_state(state, identifier.assign_value));
				return (Value) {};
			} else {
				Value value = hmget(state->variables, variable_data);
				if (value.value == NULL) {
					handle_evaluate_error(state, node->location, "Cannot evaluate identifier at compile time");
				}

				return value;
			}
		}
		case IDENTIFIER_BINDING: {
			Node *node = identifier_data.binding.node;
			Node_Data *node_data = get_data(state->context, node);

			if (node->kind == SWITCH_NODE) {
				return hmget(state->switchs, node_data);
			} else if (node->kind == FOR_NODE) {
				return hmget(state->fors, node_data)[identifier_data.binding.index];
			}
			assert(false);
			break;
		}
		default:
			handle_evaluate_error(state, node->location, "Cannot evaluate identifier at compile time");
			break;
	}

	assert(false);
}

static Value evaluate_string(State *state, Node *node) {
	String_Data string_data = get_data(state->context, node)->string;
	String_View string_value = string_data.value;
	size_t string_length = string_value.len;

	if (string_data.type.value->tag == ARRAY_VIEW_TYPE_VALUE && string_data.type.value->array_view_type.inner.value->tag == BYTE_TYPE_VALUE) {
		Value_Data *string = value_new(ARRAY_VIEW_VALUE);
		string->array_view.length = string_length;
		for (size_t i = 0; i < string_length; i++) {
			Value_Data *byte_value = value_new(BYTE_VALUE);
			byte_value->byte.value = string_value.ptr[i];
			arrpush(string->array_view.values, byte_value);
		}

		return create_value_data(string, node);
	} else {
		assert(false);
		// Value_Data **values = malloc(sizeof(Value_Data *) * string_length);
		// for (size_t i = 0; i < string_length; i++) {
		// 	Value_Data *byte = value_new(BYTE_VALUE);
		// 	byte->byte.value = string_value[i];
		// 	values[i] = byte;
		// }

		// Value_Data *pointer = value_new(POINTER_VALUE);
		// Value_Data *array = value_new(ARRAY_VALUE);
		// array->array.length = string_length;
		// array->array.values = values;
		// pointer->pointer.value = array;

		// return create_value_data(pointer, node);
	}
}

static Value evaluate_number(State *state, Node *node) {
	(void) state;
	Number_Node number = node->number;
	switch (number.tag) {
		case INTEGER_NUMBER: {
			Value_Data *value = value_new(INTEGER_VALUE);
			value->integer.value = number.integer;
			return create_value_data(value, node);
		}
		default:
			assert(false);
	}
}

static Value evaluate_character(State *state, Node *node) {
	(void) state;
	Character_Node character = node->character;
	Value_Data *value = value_new(BYTE_VALUE);
	value->byte.value = character.value.ptr[0];
	return create_value_data(value, node);
}

static Value evaluate_boolean(State *state, Node *node) {
	(void) state;
	Boolean_Node boolean = node->boolean;

	Value_Data *value = value_new(BOOLEAN_VALUE);
	value->integer.value = boolean.value;
	return create_value_data(value, node);
}

static Value evaluate_call(State *state, Node *node) {
	Call_Node call = node->call;
	Call_Data call_data = get_data(state->context, node)->call;

	Value function;
	if (call_data.function_value.value != NULL) {
		function = call_data.function_value;
	} else {
		function = evaluate_state(state, call.function);
	}

	Value *arguments = NULL;
	for (long int i = 0; i < arrlen(call.arguments); i++) {
		arrpush(arguments, clone_value(evaluate_state(state, call.arguments[i])));
	}

	Value_Data *result = NULL;

	size_t saved_static_argument_id = state->context->static_id;
	state->context->static_id = function.value->function.static_id;

	Value *saved_function_arguments = function_arguments;
	Node *saved_function_node = function_node;
	function_arguments = arguments;
	function_node = function.value->function.node;

	assert(function.value->tag == FUNCTION_VALUE);

	Function_Value function_value = function.value->function;

	Variable_Datas saved_variables = state->variables;
	Switch_Datas saved_switchs = state->switchs;
	For_Datas saved_fors = state->fors;

	state->variables = NULL;
	state->switchs = NULL;
	state->fors = NULL;

	jmp_buf prev_jmp;
	memcpy(&prev_jmp, &jmp, sizeof(jmp_buf));
	if (!setjmp(jmp)) {
		result = evaluate_state(state, function_value.body).value;
	} else {
		result = jmp_result.value;
	}
	memcpy(&jmp, &prev_jmp, sizeof(jmp_buf));

	state->variables = saved_variables;
	state->switchs = saved_switchs;
	state->fors = saved_fors;

	function_arguments = saved_function_arguments;
	function_node = saved_function_node;
	state->context->static_id = saved_static_argument_id;

	return create_value_data(result, node);
}

static Value evaluate_binary_op(State *state, Node *node) {
	Binary_Op_Node binary_operator = node->binary_op;

	Value left_value = evaluate_state(state, binary_operator.left);
	Value right_value = evaluate_state(state, binary_operator.right);

	Value true_value = create_value(BOOLEAN_VALUE);
	true_value.value->boolean.value = true;

	Value false_value = create_value(BOOLEAN_VALUE);
	false_value.value->boolean.value = false;

	switch (binary_operator.operator) {
		case OP_EQUALS: {
			if (value_equal(left_value.value, right_value.value)) {
				return true_value;
			} else {
				return false_value;
			}
		}
		case OP_NOT_EQUALS: {
			if (value_equal(left_value.value, right_value.value)) {
				return false_value;
			} else {
				return true_value;
			}
		}
		case OP_LESS: {
			if (left_value.value->integer.value < right_value.value->integer.value) {
				return true_value;
			} else {
				return false_value;
			}
		}
		case OP_LESS_EQUALS: {
			if (left_value.value->integer.value <= right_value.value->integer.value) {
				return true_value;
			} else {
				return false_value;
			}
		}
		case OP_GREATER: {
			if (left_value.value->integer.value > right_value.value->integer.value) {
				return true_value;
			} else {
				return false_value;
			}
		}
		case OP_GREATER_EQUALS: {
			if (left_value.value->integer.value >= right_value.value->integer.value) {
				return true_value;
			} else {
				return false_value;
			}
		}
		case OP_ADD: {
			return create_integer(left_value.value->integer.value + right_value.value->integer.value);
		}
		case OP_SUBTRACT: {
			return create_integer(left_value.value->integer.value - right_value.value->integer.value);
		}
		case OP_AND: {
			return create_boolean(left_value.value->boolean.value && right_value.value->boolean.value);
		}
		default:
			assert(false);
	}
}

static Value evaluate_block(State *state, Node *node) {
	Block_Node block = node->block;

	Value result = create_value_data(value_new(NONE_VALUE), node);
	for (long int i = 0; i < arrlen(block.statements); i++) {
		Value value = evaluate_state(state, block.statements[i]);
		if (block.has_result && i == arrlen(block.statements) - 1) {
			result = value;
		}
	}

	return result;
}

static Value evaluate_return(State *state, Node *node) {
	Return_Node return_ = node->return_;

	Value result;
	if (return_.value != NULL) {
		result = evaluate_state(state, return_.value);
	} else {
		result = create_value_data(value_new(NONE_VALUE), node);
	}

	jmp_result = result;
	longjmp(jmp, 1);
}

static Value evaluate_structure(State *state, Node *node) {
	Structure_Node structure = node->structure;
	Structure_Data structure_data = get_data(state->context, node)->structure;

	switch (structure_data.type.value->tag) {
		case STRUCT_TYPE_VALUE: {
			Value result = create_value_data(value_new(STRUCT_VALUE), node);
			for (long int i = 0; i < arrlen(structure.values); i++) {
				arrpush(result.value->struct_.values, evaluate_state(state, structure.values[i].node).value);
			}
			return result;
		}
		case ARRAY_TYPE_VALUE: {
			Value result = create_value_data(value_new(ARRAY_VALUE), node);
			for (long int i = 0; i < arrlen(structure.values); i++) {
				arrpush(result.value->array.values, evaluate_state(state, structure.values[i].node).value);
			}
			return result;
		}
		default:
			assert(false);
	}
}

static void print_value(Value_Data *value) {
	switch (value->tag) {
		case INTEGER_VALUE: {
			printf("%li", value->integer.value);
			break;
		}
		case BOOLEAN_VALUE: {
			printf("%s", value->boolean.value ? "true" : "false");
			break;
		}
		case ARRAY_VIEW_VALUE: {
			for (size_t i = 0; i < value->array_view.length; i++) {
				print_value(value->array_view.values[i]);
			}
			break;
		}
		case BYTE_VALUE: {
			printf("%c", value->byte.value);
			break;
		}
		default:
			assert(false);
	}
}

static Value evaluate_internal(State *state, Node *node) {
	Internal_Node internal = node->internal;
	Internal_Data internal_data = get_data(state->context, node)->internal;

	if (internal.kind == INTERNAL_PRINT) {
		for (long int i = 0; i < arrlen(internal.inputs); i++) {
			Value value = evaluate_state(state, internal.inputs[i]);
			print_value(value.value);
		}

		return (Value) {};
	} else if (internal.kind == INTERNAL_EMBED) {
		return evaluate_state(state, internal_data.node);
	} else {
		return internal_data.value;
	}
}

static Value evaluate_variable(State *state, Node *node) {
	Variable_Node variable = node->variable;
	Node_Data *node_data = get_data(state->context, node);

	Value value = {};
	if (variable.value != NULL) {
		value = clone_value(evaluate_state(state, variable.value));
	} else {
		value = initialize_value(node_data->variable.type);
	}

	hmput(state->variables, node_data, value);

	return (Value) {};
}

static Value evaluate_structure_access(State *state, Node *node) {
	Structure_Access_Node structure_access = node->structure_access;
	Structure_Access_Data structure_access_data = get_data(state->context, node)->structure_access;

	switch (structure_access_data.structure_type.value->tag) {
		case STRUCT_TYPE_VALUE: {
			Struct_Type_Value structure_type = structure_access_data.structure_type.value->struct_type;
			Struct_Value structure_value = evaluate_state(state, structure_access.parent).value->struct_;

			for (long int i = 0; i < arrlen(structure_type.members); i++) {
				if (sv_eq(structure_type.node->struct_type.members[i].name, structure_access.name)) {
					if (structure_access.assign_value != NULL) {
						structure_value.values[i] = evaluate_state(state, structure_access.assign_value).value;
						return (Value) {};
					} else {
						return (Value) { .value = structure_value.values[i] };
					}
				}
			}
			break;
		}
		case ARRAY_VIEW_TYPE_VALUE: {
			Array_View_Value array_view_value = evaluate_state(state, structure_access.parent).value->array_view;

			if (sv_eq_cstr(structure_access.name, "ptr")) {
				assert(false);
			} else if (sv_eq_cstr(structure_access.name, "len")) {
				return (Value) { .value = create_integer(array_view_value.length).value };
			} else {
				assert(false);
			}
			break;
		}
		default:
			assert(false);
	}
	assert(false);
}

static Value evaluate_array_access(State *state, Node *node) {
	Array_Access_Node array_access = node->array_access;
	Array_Access_Data array_access_data = get_data(state->context, node)->array_access;

	switch (array_access_data.array_type.value->tag) {
		case ARRAY_TYPE_VALUE: {
			Array_Value array_value = evaluate_state(state, array_access.parent).value->array;

			long index = evaluate_state(state, array_access.index).value->integer.value;
			if (array_access.assign_value != NULL) {
				array_value.values[index] = evaluate_state(state, array_access.assign_value).value;
				return (Value) {};
			} else {
				return (Value) { .value = array_value.values[index] };
			}
			break;
		}
		case ARRAY_VIEW_TYPE_VALUE: {
			Array_View_Value array_view_value = evaluate_state(state, array_access.parent).value->array_view;

			long index = evaluate_state(state, array_access.index).value->integer.value;
			if (array_access.assign_value != NULL) {
				assert(false);
				return (Value) {};
			} else {
				return (Value) { .value = array_view_value.values[index] };
			}
			break;
		}
		default:
			assert(false);
	}
	assert(false);
}

static Value evaluate_slice(State *state, Node *node) {
	Slice_Node slice = node->slice;
	Slice_Data slice_data = get_data(state->context, node)->slice;

	switch (slice_data.array_type.value->tag) {
		case ARRAY_VIEW_TYPE_VALUE: {
			Array_View_Value array_view_value = evaluate_state(state, slice.parent).value->array_view;

			long start = evaluate_state(state, slice.start).value->integer.value;
			long end = evaluate_state(state, slice.end).value->integer.value;

			Value_Data **values = malloc(sizeof(Value_Data *) * (end - start));
			memcpy(values, &array_view_value.values[start], sizeof(Value_Data *) * (end - start));

			Value result = create_value(ARRAY_VIEW_VALUE);
			result.value->array_view.values = values;
			result.value->array_view.length = end - start;

			return result;
		}
		default:
			assert(false);
	}
	assert(false);
}

static Value evaluate_switch(State *state, Node *node) {
	Switch_Node switch_ = node->switch_;
	Node_Data *data = get_data(state->context, node);

	Value value = evaluate_state(state, switch_.condition);

	for (long int i = 0; i < arrlen(switch_.cases); i++) {
		Switch_Case switch_case = switch_.cases[i];

		Value case_value = switch_case.value == NULL ? (Value) {} : evaluate_state(state, switch_case.value);
		if (switch_case.value == NULL || value_equal(case_value.value, value.value->tagged_union.tag)) {
			if (case_value.value != NULL) {
				hmput(state->switchs, data, (Value) { .value = value.value->tagged_union.data });
			}

			return evaluate_state(state, switch_case.body);
		}
	}

	return (Value) {};
}

static Value evaluate_for(State *state, Node *node) {
	For_Node for_ = node->for_;
	Node_Data *data = get_data(state->context, node);

	assert(arrlen(for_.items) == 1);
	Value value = evaluate_state(state, for_.items[0]);
	for (size_t i = 0; i < value.value->array_view.length; i++) {
		Value_Data *item_value = value.value->array_view.values[i];

		Value *values = NULL;
		arrpush(values, (Value) { .value = item_value });
		arrpush(values, create_integer(i));

		hmput(state->fors, data, values);
		evaluate_state(state, for_.body);
	}

	return (Value) {};
}

static Value evaluate_if(State *state, Node *node) {
	If_Node if_ = node->if_;

	Value value = evaluate_state(state, if_.condition);
	bool truthy = false;
	switch (value.value->tag) {
		case BOOLEAN_VALUE:
			truthy = value.value->boolean.value;
			break;
		case OPTIONAL_VALUE:
			truthy = value.value->optional.present;
			break;
		default:
			assert(false);
	}

	if (truthy) {
		return evaluate_state(state, if_.if_body);
	} else {
		if (if_.else_body != NULL) {
			return evaluate_state(state, if_.else_body);
		}
	}

	return (Value) {};
}

static Value evaluate_is(State *state, Node *node) {
	Is_Node is = node->is;

	Value value = evaluate_state(state, is.node);
	Value type = get_type(state->context, is.node);
	assert(type.value->tag == TAGGED_UNION_TYPE_VALUE);

	Value check = evaluate_state(state, is.check);

	if (value_equal(value.value->tagged_union.tag, check.value)) {
		Value optional = create_value(OPTIONAL_VALUE);
		optional.value->optional.present = true;
		optional.value->optional.value = value.value->tagged_union.data;
		return optional;
	} else {
		Value optional = create_value(OPTIONAL_VALUE);
		optional.value->optional.present = false;
		return optional;
	}
}

static Value evaluate_cast(State *state, Node *node) {
	Cast_Node cast = node->cast;
	Cast_Data cast_data = get_data(state->context, node)->cast;

	Value value = evaluate_state(state, cast.node);
	if (cast_data.from_type.value->tag == INTEGER_TYPE_VALUE && cast_data.to_type.value->tag == BYTE_TYPE_VALUE) {
		return create_byte(value.value->integer.value);
	} else {
		assert(false);
	}
}

static Value evaluate_range(State *state, Node *node) {
	Range_Node range = node->range;

	Value start = {};
	if (range.start != NULL) {
		start = evaluate_state(state, range.start);
	}

	Value end = {};
	if (range.end != NULL) {
		end = evaluate_state(state, range.end);
	}

	Value value = create_value(RANGE_VALUE);
	value.value->range.start = start;
	value.value->range.end = end;

	return value;
}

static Value evaluate_global(State *state, Node *node) {
	Global_Node global = node->global;
	Value value = create_value(GLOBAL_VALUE);
	value.value->global.node = node;
	value.value->global.value = evaluate_state(state, global.value);
	return value;
}

static Value evaluate_const(State *state, Node *node) {
	Const_Node const_ = node->const_;
	Value value = create_value(CONST_VALUE);
	value.value->const_.value = evaluate_state(state, const_.value);
	return value;
}

static Value evaluate_null(State *state, Node *node) {
	Null_Data null_data = get_data(state->context, node)->null_;

	switch (null_data.type.value->tag) {
		case BYTE_TYPE_VALUE: {
			return create_byte(0);
		}
		default:
			assert(false);
	}
}

static Value evaluate_run(State *state, Node *node) {
	Run_Data run_data = get_data(state->context, node)->run;
	return run_data.value;
}

static Value evaluate_state(State *state, Node *node) {
	switch (node->kind) {
		case FUNCTION_NODE:          return evaluate_function(state, node);
		case FUNCTION_TYPE_NODE:     return evaluate_function_type(state, node);
		case STRUCT_TYPE_NODE:       return evaluate_struct_type(state, node);
		case UNION_TYPE_NODE:        return evaluate_union_type(state, node);
		case TAGGED_UNION_TYPE_NODE: return evaluate_tagged_union_type(state, node);
		case ENUM_TYPE_NODE:         return evaluate_enum_type(state, node);
		case MODULE_NODE:            return evaluate_module(state, node);
		case MODULE_TYPE_NODE:       return evaluate_module_type(state, node);
		case POINTER_NODE:           return evaluate_pointer(state, node);
		case OPTIONAL_NODE:          return evaluate_optional(state, node);
		case RESULT_NODE:            return evaluate_result(state, node);
		case ARRAY_TYPE_NODE:        return evaluate_array_type(state, node);
		case ARRAY_VIEW_TYPE_NODE:   return evaluate_array_view_type(state, node);
		case IDENTIFIER_NODE:        return evaluate_identifier(state, node);
		case STRING_NODE:            return evaluate_string(state, node);
		case NUMBER_NODE:            return evaluate_number(state, node);
		case CHARACTER_NODE:         return evaluate_character(state, node);
		case BOOLEAN_NODE:           return evaluate_boolean(state, node);
		case CALL_NODE:              return evaluate_call(state, node);
		case BINARY_OP_NODE:         return evaluate_binary_op(state, node);
		case BLOCK_NODE:             return evaluate_block(state, node);
		case RETURN_NODE:            return evaluate_return(state, node);
		case STRUCTURE_NODE:         return evaluate_structure(state, node);
		case INTERNAL_NODE:          return evaluate_internal(state, node);
		case VARIABLE_NODE:          return evaluate_variable(state, node);
		case STRUCTURE_ACCESS_NODE:  return evaluate_structure_access(state, node);
		case ARRAY_ACCESS_NODE:      return evaluate_array_access(state, node);
		case SLICE_NODE:             return evaluate_slice(state, node);
		case SWITCH_NODE:            return evaluate_switch(state, node);
		case FOR_NODE:               return evaluate_for(state, node);
		case IF_NODE:                return evaluate_if(state, node);
		case IS_NODE:                return evaluate_is(state, node);
		case CAST_NODE:              return evaluate_cast(state, node);
		case RANGE_NODE:             return evaluate_range(state, node);
		case GLOBAL_NODE:            return evaluate_global(state, node);
		case CONST_NODE:             return evaluate_const(state, node);
		case NULL_NODE:              return evaluate_null(state, node);
		case RUN_NODE:               return evaluate_run(state, node);
		case DEFINE_NODE:            return (Value) {};
		default:                     assert(false);
	}
}

Value evaluate(Context *context, Node *node) {
	State state = {
		.context = context
	};

	return evaluate_state(&state, node);
}
