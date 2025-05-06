#include <assert.h>
#include <linux/limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common.h"
#include "stb/ds.h"

#include "ast.h"
#include "evaluator.h"
#include "parser.h"
#include "util.h"

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
			if ((value1->array_type.size.value == NULL && value2->array_type.size.value != NULL) || (value1->array_type.size.value != NULL && value2->array_type.size.value == NULL)) return false;
			if (value1->array_type.size.value != NULL) {
				if (!value_equal(value1->array_type.size.value, value2->array_type.size.value)) return false;
			}

			return value_equal(value1->array_type.inner.value, value2->array_type.inner.value);
		}
		case ARRAY_VIEW_TYPE_VALUE: {
			return value_equal(value1->array_view_type.inner.value, value2->array_view_type.inner.value);
		}
		case OPTIONAL_TYPE_VALUE: {
			return value_equal(value1->optional_type.inner.value, value2->optional_type.inner.value);
		}
		case RESULT_TYPE_VALUE: {
			return value_equal(value1->result_type.value.value, value2->result_type.value.value) && value_equal(value1->result_type.error.value, value2->result_type.error.value);
		}
		case INTEGER_TYPE_VALUE: {
			return value1->integer_type.signed_ == value2->integer_type.signed_ && value1->integer_type.size == value2->integer_type.size;
		}
		case STRUCT_TYPE_VALUE: {
			if (arrlen(value1->struct_type.items) != arrlen(value2->struct_type.items)) return false;

			for (long int i = 0; i < arrlen(value1->struct_type.items); i++) {
				if (strcmp(value1->struct_type.items[i].identifier, value2->struct_type.items[i].identifier) != 0) return false;
				if (!value_equal(value1->struct_type.items[i].type.value, value2->struct_type.items[i].type.value)) return false;
			}

			return true;
		}
		case ENUM_TYPE_VALUE: {
			if (arrlen(value1->enum_type.items) != arrlen(value2->enum_type.items)) return false;

			for (long int i = 0; i < arrlen(value1->enum_type.items); i++) {
				if (strcmp(value1->enum_type.items[i], value2->enum_type.items[i]) != 0) return false;
			}

			return true;
		}
		case FUNCTION_TYPE_VALUE: {
			if (arrlen(value1->function_type.arguments) != arrlen(value2->function_type.arguments)) return false;
			for (long int i = 0; i < arrlen(value1->function_type.arguments); i++) {
				if (strcmp(value1->function_type.arguments[i].identifier, value2->function_type.arguments[i].identifier) != 0) return false;
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
		default:
			assert(false);
	}
}

bool type_assignable(Value_Data *type1, Value_Data *type2) {
	return value_equal(type1, type2);
}

Value get_cached_file(Context *context, char *path) {
	for (long int i = 0; i < arrlen(context->cached_files); i++) {
		if (strcmp(context->cached_files[i].path, path) == 0) {
			return context->cached_files[i].value;
		}
	}

	return (Value) {};
}

void add_cached_file(Context *context, char *path, Value value) {
	Cached_File file = {
		.path = path,
		.value = value
	};

	arrpush(context->cached_files, file);
}

#define handle_evaluate_error(/* Source_Location */ location, /* char * */ fmt, ...) { \
	printf("%s:%zu:%zu: " fmt "\n", location.path, location.row, location.column __VA_OPT__(,) __VA_ARGS__); \
	exit(1); \
}

Value create_value_data(Value_Data *value, Node *node) {
	return (Value) { .value = value, .node = node };
}

static Value evaluate_state(State *state, Node *node);

static Value evaluate_function(State *state, Node *node) {
	Function_Node function = node->function;

	Node_Data *function_type_data = get_data(state->context, function.function_type);
	Value function_type_value = function_type_data->function_type.value;

	Value_Data *function_value = value_new(FUNCTION_VALUE);
	function_value->function.type = function_type_value.value;
	if (function.body != NULL) {
		function_value->function.body = function.body;
	}

	function_value->function.static_argument_id = state->context->static_argument_id;
	function_value->function.node = node;

	Scope *scopes = NULL;
	for (long int i = 0; i < arrlen(state->context->scopes); i++) {
		arrpush(scopes, state->context->scopes[i]);
	}

	function_value->function.scopes = scopes;

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

	struct_value_data->struct_type.items = NULL;
	for (long int i = 0; i < arrlen(struct_type.items); i++) {
		Struct_Item_Value item = {
			.identifier = struct_type.items[i].identifier,
			.type = evaluate_state(state, struct_type.items[i].type)
		};
		arrpush(struct_value_data->struct_type.items, item);
	}

	struct_value_data->struct_type.arguments = function_arguments;

	for (long int i = 0; i < arrlen(struct_type.operators); i++) {
		process_node(state->context, struct_type.operators[i].function);
		Operator_Value_Definition item = {
			.operator = struct_type.operators[i].operator,
			.function = evaluate_state(state, struct_type.operators[i].function)
		};
		arrpush(struct_value.value->struct_type.operators, item);
	}

	(void) arrpop(state->context->scopes);

	return struct_value;
}

static Value evaluate_union_type(State *state, Node *node) {
	Union_Type_Node union_type = node->union_type;

	Value_Data *union_value = value_new(UNION_TYPE_VALUE);
	union_value->union_type.items = NULL;
	for (long int i = 0; i < arrlen(union_type.items); i++) {
		Union_Item_Value item = {
			.identifier = union_type.items[i].identifier,
			.type = evaluate_state(state, union_type.items[i].type)
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
	for (long int i = 0; i < arrlen(tagged_union_type.items); i++) {
		Tagged_Union_Item_Value item = {
			.identifier = tagged_union_type.items[i].identifier,
			.type = evaluate_state(state, tagged_union_type.items[i].type)
		};
		arrpush(tagged_union_value->tagged_union_type.items, item);
		arrpush(enum_value->enum_type.items, tagged_union_type.items[i].identifier);
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
	Pointer_Node pointer = node->pointer;

	Value_Data *pointer_type_value = value_new(POINTER_TYPE_VALUE);
	if (pointer.inner) {
		pointer_type_value->pointer_type.inner = evaluate_state(state, pointer.inner);
	}
	return create_value_data(pointer_type_value, node);
}

static Value evaluate_optional(State *state, Node *node) {
	Optional_Node optional = node->optional;

	Value_Data *optional_type_value = value_new(OPTIONAL_TYPE_VALUE);
	optional_type_value->optional_type.inner = evaluate_state(state, optional.inner);
	return create_value_data(optional_type_value, node);
}

static Value evaluate_result(State *state, Node *node) {
	Result_Node result = node->result;

	Value_Data *result_type_value = value_new(RESULT_TYPE_VALUE);
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
	Identifier_Data identifier_data = get_data(state->context, node)->identifier;

	switch (identifier_data.kind) {
		case IDENTIFIER_VALUE:
			return identifier_data.value;
		case IDENTIFIER_ARGUMENT:
			return function_arguments[identifier_data.argument_index];
		case IDENTIFIER_SELF:
			for (long int i = arrlen(state->context->scopes) - 1; i >= 0; i--) {
				if (state->context->scopes[i].current_type.value != NULL) {
					return state->context->scopes[i].current_type;
				}
			}
			break;
		case IDENTIFIER_UNDERSCORE:
			return create_value_data(NULL, node);
		case IDENTIFIER_VARIABLE: {
			Node_Data *variable_data = get_data(state->context, identifier_data.variable_definition);
			return hmget(state->variables, variable_data);
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
			handle_evaluate_error(node->location, "Cannot evaluate identifier at compile time");
			break;
	}

	assert(false);
}

static Value evaluate_string(State *state, Node *node) {
	String_Data string_data = get_data(state->context, node)->string;
	char *string_value = string_data.value;
	size_t string_length = string_data.length;

	if (string_data.type.value->tag == ARRAY_VIEW_TYPE_VALUE && string_data.type.value->array_view_type.inner.value->tag == BYTE_TYPE_VALUE) {
		Value_Data *string = value_new(ARRAY_VIEW_VALUE);
		string->array_view.length = string_length;
		for (size_t i = 0; i < string_length; i++) {
			Value_Data *byte_value = value_new(BYTE_VALUE);
			byte_value->byte.value = string_value[i];
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
		arrpush(arguments, evaluate_state(state, call.arguments[i]));
	}

	if (function.value->tag != FUNCTION_VALUE) {
		assert(false);
	}

	Value_Data *result = NULL;

	size_t saved_static_argument_id = state->context->static_argument_id;
	state->context->static_argument_id = function.value->function.static_argument_id;

	Value *saved_function_arguments = function_arguments;
	function_arguments = arguments;

	Function_Value function_value = function.value->function;

	if (function_value.node->function.extern_name != NULL) {
		handle_evaluate_error(node->location, "Cannot run extern function at compile time");
	}

	if (function_value.node->function.internal_name != NULL) {
		char *internal_name = function_value.node->function.internal_name;

		if (streq(internal_name, "size_of")) {
			Value_Data *value = value_new(INTEGER_VALUE);
			value->integer.value = state->context->codegen.size_fn(arguments[0].value, state->context->codegen.data);
			result = value;
		} else if (streq(internal_name, "import")) {
			Value string = arguments[0];
			char *source = malloc(string.value->array_view.length + 1);
			source[string.value->array_view.length] = '\0';
			for (size_t i = 0; i < string.value->array_view.length; i++) {
				source[i] = string.value->array_view.values[i]->byte.value;
			}

			if (strcmp(source, "core") == 0) {
				char *cwd = getcwd(NULL, PATH_MAX);
				source = malloc(strlen(cwd) + 16);
				strcpy(source, cwd);
				strcat(source, "/core/core.lang");
			} else {
				size_t slash_index = 0;
				for (size_t i = 0; i < strlen(node->location.path); i++) {
					if (node->location.path[i] == '/') {
						slash_index = i;
					}
				}

				char *old_source = source;
				source = malloc(slash_index + strlen(source) + 2);
				strncpy(source, node->location.path, slash_index + 1);
				source[slash_index + 1] = '\0';
				strcat(source, old_source);
			}

			result = get_cached_file(state->context, source).value;
			if (result == NULL) {
				Node *file_node = parse_file(source);

				Scope *saved_scopes = state->context->scopes;
				state->context->scopes = NULL;
				process_node(state->context, file_node);
				Value value = evaluate_state(state, file_node);
				state->context->scopes = saved_scopes;

				add_cached_file(state->context, source, value);

				result = value.value;
			}
		} else if (streq(internal_name, "type_info_of")) {
			result = value_new(TAGGED_UNION_VALUE);

			Value type = arguments[0];

			Value_Data *enum_value = value_new(ENUM_VALUE);
			Value_Data *data = NULL;
			switch (type.value->tag) {
				case INTEGER_TYPE_VALUE: {
					enum_value->enum_.value = 0;

					data = value_new(STRUCT_VALUE);

					Value_Data *size_value = create_integer(type.value->integer_type.size).value;
					arrpush(data->struct_.values, size_value);

					Value_Data *signed_value = create_boolean(type.value->integer_type.signed_).value;
					arrpush(data->struct_.values, signed_value);
					break;
				}
				case STRUCT_TYPE_VALUE: {
					enum_value->enum_.value = 1;

					data = value_new(STRUCT_VALUE);

					Value_Data *items_value = value_new(ARRAY_VIEW_VALUE);
					items_value->array_view.length = arrlen(type.value->struct_type.items);
					for (long int i = 0; i < arrlen(type.value->struct_type.items); i++) {
						Value_Data *struct_item_value = value_new(STRUCT_VALUE);

						char *name_string = type.value->struct_type.items[i].identifier;
						size_t name_string_length = strlen(name_string);

						Value_Data *name_value = value_new(ARRAY_VIEW_VALUE);
						name_value->array_view.length = name_string_length;
						for (size_t i = 0; i < name_string_length; i++) {
							Value_Data *byte_value = value_new(BYTE_VALUE);
							byte_value->byte.value = name_string[i];
							arrpush(name_value->array_view.values, byte_value);
						}
						arrpush(struct_item_value->struct_.values, name_value);

						Value_Data *type_value = type.value->struct_type.items[i].type.value;
						arrpush(struct_item_value->struct_.values, type_value);

						arrpush(items_value->array_view.values, struct_item_value);
					}
					arrpush(data->struct_.values, items_value);
					break;
				}
				default:
					assert(false);
			}

			result->tagged_union.tag = enum_value;
			result->tagged_union.data = data;
		} else {
			assert(false);
		}
	} else {
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
	}

	function_arguments = saved_function_arguments;
	state->context->static_argument_id = saved_static_argument_id;

	return create_value_data(result, node);
}

static Value evaluate_binary_operator(State *state, Node *node) {
	Binary_Operator_Node binary_operator = node->binary_operator;

	Value left_value = evaluate_state(state, binary_operator.left);
	Value right_value = evaluate_state(state, binary_operator.right);

	switch (binary_operator.operator) {
		case OPERATOR_EQUALS: {
			if (value_equal(left_value.value, right_value.value)) {
				Value_Data *true_value = value_new(BOOLEAN_VALUE);
				true_value->boolean.value = true;
				return create_value_data(true_value, node);
			} else {
				Value_Data *false_value = value_new(BOOLEAN_VALUE);
				false_value->boolean.value = false;
				return create_value_data(false_value, node);
			}
		}
		case OPERATOR_LESS: {
			if (left_value.value->integer.value < right_value.value->integer.value) {
				Value_Data *true_value = value_new(BOOLEAN_VALUE);
				true_value->boolean.value = true;
				return create_value_data(true_value, node);
			} else {
				Value_Data *false_value = value_new(BOOLEAN_VALUE);
				false_value->boolean.value = false;
				return create_value_data(false_value, node);
			}
		}
		case OPERATOR_SUBTRACT: {
			return create_integer(left_value.value->integer.value - right_value.value->integer.value);
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
	assert(arrlen(structure.values) == 0);

	switch (structure_data.type.value->tag) {
		case STRUCT_TYPE_VALUE: {
			Value result = create_value_data(value_new(STRUCT_VALUE), node);
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
		Value value = evaluate_state(state, internal.inputs[0]);
		print_value(value.value);

		return (Value) {};
	} else {
		return internal_data.value;
	}
}

static Value evaluate_variable(State *state, Node *node) {
	Variable_Node variable = node->variable;
	Node_Data *node_data = get_data(state->context, node);

	Value value = {};
	if (variable.value != NULL) {
		value = evaluate_state(state, variable.value);
	}

	hmput(state->variables, node_data, value);

	return (Value) {};
}

static Value evaluate_structure_access(State *state, Node *node) {
	Structure_Access_Node structure_access = node->structure_access;
	Structure_Access_Data structure_access_data = get_data(state->context, node)->structure_access;

	assert(structure_access_data.structure_type.value->tag == STRUCT_TYPE_VALUE || structure_access_data.structure_type.value->tag == ARRAY_VIEW_TYPE_VALUE);
	assert(!structure_access_data.pointer_access);

	switch (structure_access_data.structure_type.value->tag) {
		case STRUCT_TYPE_VALUE: {
			Struct_Type_Value structure_type = structure_access_data.structure_type.value->struct_type;
			Struct_Value structure_value = evaluate_state(state, structure_access.structure).value->struct_;

			for (long int i = 0; i < arrlen(structure_type.items); i++) {
				if (streq(structure_type.items[i].identifier, structure_access.item)) {
					return (Value) { .value = structure_value.values[i] };
				}
			}
			break;
		}
		case ARRAY_VIEW_TYPE_VALUE: {
			Array_View_Value array_view_value = evaluate_state(state, structure_access.structure).value->array_view;

			if (streq(structure_access.item, "ptr")) {
				assert(false);
			} else if (streq(structure_access.item, "len")) {
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

static Value evaluate_switch(State *state, Node *node) {
	Switch_Node switch_ = node->switch_;
	Node_Data *data = get_data(state->context, node);

	Value value = evaluate_state(state, switch_.value);

	for (long int i = 0; i < arrlen(switch_.cases); i++) {
		Switch_Case switch_case = switch_.cases[i];

		Value case_value = switch_case.check == NULL ? (Value) {} : evaluate_state(state, switch_case.check);
		if (switch_case.check == NULL || value_equal(case_value.value, value.value->tagged_union.tag)) {
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

	Value value = evaluate_state(state, for_.item);
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

	if (evaluate_state(state, if_.condition).value->boolean.value) {
		return evaluate_state(state, if_.if_body);
	} else {
		if (if_.else_body != NULL) {
			return evaluate_state(state, if_.else_body);
		}
	}

	return (Value) {};
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
		case BOOLEAN_NODE:           return evaluate_boolean(state, node);
		case CALL_NODE:              return evaluate_call(state, node);
		case BINARY_OPERATOR_NODE:   return evaluate_binary_operator(state, node);
		case BLOCK_NODE:             return evaluate_block(state, node);
		case RETURN_NODE:            return evaluate_return(state, node);
		case STRUCTURE_NODE:         return evaluate_structure(state, node);
		case INTERNAL_NODE:          return evaluate_internal(state, node);
		case VARIABLE_NODE:          return evaluate_variable(state, node);
		case STRUCTURE_ACCESS_NODE:  return evaluate_structure_access(state, node);
		case SWITCH_NODE:            return evaluate_switch(state, node);
		case FOR_NODE:               return evaluate_for(state, node);
		case IF_NODE:                return evaluate_if(state, node);
		default:                     assert(false);
	}
}

Value evaluate(Context *context, Node *node) {
	State state = {
		.context = context
	};

	return evaluate_state(&state, node);
}
