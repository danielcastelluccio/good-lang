#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "ast.h"
#include "common.h"
#include "stb/ds.h"

#include "evaluator.h"

void process_node_context(Context *context, Temporary_Context temporary_context, Node *node);

void process_node(Context *context, Node *node) {
	process_node_context(context, (Temporary_Context) {}, node);
}

typedef struct {
	union {
		struct {
			Node *node;
			Scope *scope;
		} define;
		Node *variable;
		Value static_argument;
		size_t argument;
		size_t enum_variant;
	};
	Value type;
	enum {
		LOOKUP_RESULT_FAIL,
		LOOKUP_RESULT_DEFINE,
		LOOKUP_RESULT_VARIABLE,
		LOOKUP_RESULT_ARGUMENT,
		LOOKUP_RESULT_STATIC_ARGUMENT,
		LOOKUP_RESULT_ENUM_VARIANT,
		LOOKUP_RESULT_SELF
	} tag;
} Lookup_Result;

typedef struct {
	Node *node;
	Scope *scope;
} Lookup_Define_Result;

static Lookup_Result lookup(Context *context, char *identifier) {
	if (strcmp(identifier, "self") == 0) {
		return (Lookup_Result) { .tag = LOOKUP_RESULT_SELF, .type = create_internal_type("type") };
	}

	if (context->temporary_context.wanted_type.value != NULL) {
		Value wanted_type = context->temporary_context.wanted_type;
		if (wanted_type.value->tag == ENUM_TYPE_VALUE) {
			for (long int i = 0; i < arrlen(wanted_type.value->enum_type.items); i++) {
				if (strcmp(identifier, wanted_type.value->enum_type.items[i]) == 0) {
					return (Lookup_Result) { .tag = LOOKUP_RESULT_ENUM_VARIANT, .enum_variant = i, .type = context->temporary_context.wanted_type };
				}
			}
		}
	}

	bool found_function = false;
	for (long int i = 0; i < arrlen(context->scopes); i++) {
		Scope *scope = &context->scopes[arrlen(context->scopes) - i - 1];

		Node *variable = shget(scope->variables, identifier);
		if (variable != NULL) {
			return (Lookup_Result) { .tag = LOOKUP_RESULT_VARIABLE, .variable = variable, .type = get_data(context, variable)->variable.type };
		}

		if (!found_function && scope->node->kind == FUNCTION_NODE) {
			found_function = true;

			Node *current_function_type = scope->node->function.function_type;
			long int j = 0;
			for (long int i = 0; i < arrlen(current_function_type->function_type.arguments); i++) {
				if (!current_function_type->function_type.arguments[i].static_) {
					if (strcmp(current_function_type->function_type.arguments[i].identifier, identifier) == 0) {
						return (Lookup_Result) { .tag = LOOKUP_RESULT_ARGUMENT, .argument = j, .type = get_data(context, current_function_type)->function_type.value.value->function_type.arguments[i].type };
					}
					j++;
				}
			}
		}

		Typed_Value value = shget(scope->static_arguments, identifier);
		if (value.type.value != NULL) {
			return (Lookup_Result) { .tag = LOOKUP_RESULT_STATIC_ARGUMENT, .static_argument = value.value, .type = value.type };
		}

		Node *node = scope->node;
		switch (node->kind) {
			case BLOCK_NODE:
				for (long int i = 0; i < arrlen(node->block.statements); i++) {
					Node *statement = node->block.statements[arrlen(node->block.statements) - i - 1];
					if (statement->kind == DEFINE_NODE && strcmp(statement->define.identifier, identifier) == 0) {
						return (Lookup_Result) { .tag = LOOKUP_RESULT_DEFINE, .define = { .node = statement, .scope = scope } };
					}
				}
				break;
			default:
				break;
		}
	}

	return (Lookup_Result) { .tag = LOOKUP_RESULT_FAIL };
}

#define handle_semantic_error(/* Source_Location */ location, /* char * */ fmt, ...) { \
	printf("%s:%zu:%zu: " fmt "\n", location.path, location.row, location.column __VA_OPT__(,) __VA_ARGS__); \
	exit(1); \
}

static int print_type_node(Node *type_node, bool pointer, char *buffer) {
	char *buffer_start = buffer;
	if (pointer) {
		buffer += sprintf(buffer, "^");
	}

	switch (type_node->kind) {
		// case POINTER_NODE: {
		// 	buffer += sprintf(buffer, "^");
		// 	buffer += print_type_node(type_node->pointer.inner, false, buffer);
		// 	break;
		// }
		// case STRUCT_TYPE_NODE: {
		// 	buffer += sprintf(buffer, "struct{");
		// 	for (long int i = 0; i < arrlen(type_node->struct_type.items); i++) {
		// 		buffer += sprintf(buffer, "%s:", type_node->struct_type.items[i].identifier);
		// 		buffer += print_type_node(type_node->struct_type.items[i].type, false, buffer);
		// 		if (i < arrlen(type_node->struct_type.items) - 1) {
		// 			buffer += sprintf(buffer, ",");
		// 		}
		// 	}
		// 	buffer += sprintf(buffer, "}");
		// 	break;
		// }
		case IDENTIFIER_NODE: {
			buffer += sprintf(buffer, "%s", type_node->identifier.value);
			break;
		}
		case CALL_NODE: {
			buffer += print_type_node(type_node->call.function, false, buffer);
			buffer += sprintf(buffer, "(");
			for (long int i = 0; i < arrlen(type_node->call.arguments); i++) {
				buffer += print_type_node(type_node->call.arguments[i], false, buffer);
				if (i < arrlen(type_node->call.arguments) - 1) {
					buffer += sprintf(buffer, ",");
				}
			}
			buffer += sprintf(buffer, ")");
			break;
		}
		default:
			assert(false);
	}

	return buffer - buffer_start;
}

static int print_type(Value type, char *buffer) {
	if (type.node != NULL && (type.node->kind == CALL_NODE || type.node->kind == IDENTIFIER_NODE)) {
		return print_type_node(type.node, false, buffer);
	}

	char *buffer_start = buffer;
	switch (type.value->tag) {
		case POINTER_TYPE_VALUE: {
			buffer += sprintf(buffer, "^");
			buffer += print_type(type.value->pointer_type.inner, buffer);
			break;
		}
		case ARRAY_TYPE_VALUE: {
			buffer += sprintf(buffer, "[");
			if (type.value->array_type.size.value != NULL) {
				buffer += print_type(type.value->array_type.size, buffer);
			}
			buffer += sprintf(buffer, "]");
			buffer += print_type(type.value->array_type.inner, buffer);
			break;
		}
		case INTERNAL_VALUE: {
			buffer += sprintf(buffer, "%s", type.value->internal.identifier);
			break;
		}
		case STRUCT_TYPE_VALUE: {
			buffer += sprintf(buffer, "struct{");
			for (long int i = 0; i < arrlen(type.value->struct_type.items); i++) {
				buffer += sprintf(buffer, "%s:", type.value->struct_type.items[i].identifier);
				buffer += print_type(type.value->struct_type.items[i].type, buffer);
				if (i < arrlen(type.value->struct_type.items) - 1) {
					buffer += sprintf(buffer, ",");
				}
			}
			buffer += sprintf(buffer, "}");
			break;
		}
		case UNION_TYPE_VALUE: {
			buffer += sprintf(buffer, "union{");
			for (long int i = 0; i < arrlen(type.value->union_type.items); i++) {
				buffer += sprintf(buffer, "%s:", type.value->union_type.items[i].identifier);
				buffer += print_type(type.value->union_type.items[i].type, buffer);
				if (i < arrlen(type.value->union_type.items) - 1) {
					buffer += sprintf(buffer, ",");
				}
			}
			buffer += sprintf(buffer, "}");
			break;
		}
		case ENUM_TYPE_VALUE: {
			buffer += sprintf(buffer, "enum{");
			for (long int i = 0; i < arrlen(type.value->enum_type.items); i++) {
				buffer += sprintf(buffer, "%s", type.value->enum_type.items[i]);
				if (i < arrlen(type.value->struct_type.items) - 1) {
					buffer += sprintf(buffer, ",");
				}
			}
			buffer += sprintf(buffer, "}");
			break;
		}
		case STRING_TYPE_VALUE: {
			buffer += sprintf(buffer, "str");
			break;
		}
		case INTEGER_VALUE: {
			buffer += sprintf(buffer, "%li", type.value->integer.value);
			break;
		}
		case NONE_VALUE: {
			buffer += sprintf(buffer, "()");
			break;
		}
		default:
			assert(false);
	}

	return buffer - buffer_start;
}


static int print_type_outer(Value type, char *buffer) {
	char *buffer_start = buffer;
	if (type.value == NULL) {
		buffer += sprintf(buffer, "nothing");
	} else {
		buffer += sprintf(buffer, "'");
		buffer += print_type(type, buffer);
		buffer += sprintf(buffer, "'");
	}

	return buffer - buffer_start;
}

static void handle_type_error(Node *node, Value wanted, Value given) {
	char wanted_string[64] = {};
	print_type_outer(wanted, wanted_string);
	char given_string[64] = {};
	print_type_outer(given, given_string);
	handle_semantic_error(node->location, "Expected %s, but got %s", wanted_string, given_string);
}

static void process_block(Context *context, Node *node) {
	Block_Node block = node->block;

	Node_Data *data = node_data_new(BLOCK_NODE);
	data->block.wanted_type = context->temporary_context.wanted_type;
	data->block.type = (Value) {};
	set_data(context, node, data);

	arrpush(context->scopes, (Scope) { .node = node });
	for (long int i = 0; i < arrlen(block.statements); i++) {
		Node *statement = block.statements[i];
		process_node(context, statement);
	}
	(void) arrpop(context->scopes);

	if (data->block.has_type) {
		bool left = false;
		for (long int i = 0; i < arrlen(context->left_blocks); i++) {
			if (context->left_blocks[i] == node) {
				left = true;
				break;
			}
		}

		if (!left) {
			handle_semantic_error(node->location, "Expected value from block");
		}

		set_type(context, node, data->block.type);
	}
}

typedef struct {
	Value value;
	Value type;
} Process_Define_Result;

typedef struct { char *key; Typed_Value value; } *Pattern_Match_Result;

static bool pattern_match(Node *node, Value value, Context *context, char **inferred_arguments, Pattern_Match_Result *match_result) {
	switch (node->kind) {
		case POINTER_NODE: {
			if (value.value->tag == POINTER_TYPE_VALUE) {
				return pattern_match(node->pointer.inner, value.value->pointer_type.inner, context, inferred_arguments, match_result);
			}
			break;
		}
		case ARRAY_TYPE_NODE: {
			if (value.value->tag == ARRAY_TYPE_VALUE) {
				if (!pattern_match(node->array_type.inner, value.value->array_type.inner, context, inferred_arguments, match_result)) return false;
				return pattern_match(node->array_type.size, value.value->array_type.size, context, inferred_arguments, match_result);
			}
			break;
		}
		case CALL_NODE: {
			if (value.value->tag == STRUCT_TYPE_VALUE) {
				for (long int i = 0; i < arrlen(node->call.arguments); i++) {
					if (!pattern_match(node->call.arguments[i], value.value->struct_type.arguments[i], context, inferred_arguments, match_result)) {
			 			return false;
			 		}
				}
			}
			break;
		}
		case IDENTIFIER_NODE: {
			for (long int i = 0; i < arrlen(inferred_arguments); i++) {
				if (strcmp(inferred_arguments[i], node->identifier.value) == 0) {
					Typed_Value previous_value = shget(*match_result, inferred_arguments[i]);
					if (previous_value.value.value != NULL) {
						if (!value_equal(previous_value.value.value, value.value)) {
							return false;
						}
					}

					Typed_Value typed_value = {
						.value = value,
						.type = create_internal_type("type")
					};
					shput(*match_result, inferred_arguments[i], typed_value);
					break;
				}
			}
			break;
		}
		default:
			break;
	}

	return true;
}

static Process_Define_Result process_define(Context *context, Node *node, Scope *scopes) {
	Define_Node define = node->define;

	Scope *saved_scopes = context->scopes;
	if (scopes != NULL) context->scopes = scopes;
	arrpush(context->scopes, (Scope) { .node = node });

	Node_Data *cached_data = get_data(context, node);
	if (cached_data != NULL) {
		(void) arrpop(context->scopes);
		if (scopes != NULL) context->scopes = saved_scopes;
		return (Process_Define_Result) {
			.value = cached_data->define.typed_value.value,
			.type = cached_data->define.typed_value.type
		};
	}

	process_node(context, define.expression);
	Value type = get_type(context, define.expression);
	Value value = evaluate(context, define.expression);

	Scope *copied_scopes = NULL;
	for (long int i = 0; i < arrlen(context->scopes) - 1; i++) {
		arrpush(copied_scopes, context->scopes[i]);
	}

	(void) arrpop(context->scopes);
	if (scopes != NULL) context->scopes = saved_scopes;

	Typed_Value typed_value = {
		.value = value,
		.type = type
	};

	Node_Data *data;
	if (cached_data != NULL) {
		data = cached_data;
	} else {
		data = node_data_new(DEFINE_NODE);
		set_data(context, node, data);
	}

	data->define.typed_value = typed_value;

	return (Process_Define_Result) {
		.value = value,
		.type = type
	};
}

static Value *process_call_generic1(Context *context, Node **arguments) {
	Value *argument_types = NULL;
	for (long int i = 0; i < arrlen(arguments); i++) {
		Value type = get_type(context, arguments[i]);
		if (type.value == NULL) {
			Temporary_Context temporary_context = { .wanted_type = NULL };
			process_node_context(context, temporary_context, arguments[i]);

			type = get_type(context, arguments[i]);
			reset_node(context, arguments[i]);
		}

		arrpush(argument_types, type);
	}

	// return (Temporary_Context) { .call_argument_types = argument_types, .call_wanted_type = context->temporary_context.wanted_type };
	return argument_types;
}

static void process_function(Context *context, Node *node, bool given_static_arguments) {
	Function_Node function = node->function;

	bool compile_only_parent = context->compile_only;

	context->compile_only = false;

	bool static_argument = false;
	for (long int i = 0; i < arrlen(function.function_type->function_type.arguments); i++) {
		if (function.function_type->function_type.arguments[i].static_) {
			static_argument = true;
		}
	}

	Value function_type_value = {};
	function_type_value.value = value_new(FUNCTION_TYPE_VALUE);
	function_type_value.value->function_type.incomplete = true;
	function_type_value.value->function_type.node = function.function_type;
	set_type(context, node, function_type_value);
	Node_Data *function_type_data = node_data_new(FUNCTION_TYPE_NODE);
	function_type_data->function_type.value = function_type_value;
	set_data(context, function.function_type, function_type_data);

	if (!static_argument || given_static_arguments) {
		process_node(context, function.function_type);

		Node_Data *function_type_data = get_data(context, function.function_type);

		Value function_type_value = function_type_data->function_type.value;

		set_type(context, node, function_type_value);

		if (function.body != NULL) {
			arrpush(context->scopes, (Scope) { .node = node });

			Temporary_Context temporary_context = { .wanted_type = function_type_value.value->function_type.return_type };
			process_node_context(context, temporary_context, function.body);

			if (function_type_value.value->function_type.return_type.value != NULL) {
				Value returned_type = get_type(context, function.body);

				if (!value_equal(function_type_value.value->function_type.return_type.value, returned_type.value)) {
					handle_type_error(node, function_type_value.value->function_type.return_type, returned_type);
				}
			}
			(void) arrpop(context->scopes);
		}
	}

	Node_Data *data = node_data_new(FUNCTION_NODE);
	if (context->compile_only) {
		data->function.compile_only = true;
	}
	set_data(context, node, data);

	context->compile_only = compile_only_parent;
}

static Value process_call_generic2(Context *context, Node *node, Node *function, Node **arguments, Value *argument_types, Value *function_type) {
	typedef struct {
		char *identifier;
		Typed_Value value;
	} Static_Argument_Value;

	Value function_value = {};

	Node *function_type_node = function_type->value->function_type.node;

	bool compile_only_parent = context->compile_only;
	Static_Argument_Value *static_arguments = NULL;
	long int j = 0;
	for (long int i = 0; i < arrlen(arguments); i++) {
		if (j >= arrlen(function_type_node->function_type.arguments)) break;
		while (function_type_node->function_type.arguments[j].inferred) j++;

		if (function_type_node->function_type.arguments[j].static_) {
			Value wanted_type = {};
			if (!function_type_node->function_type.arguments[j].inferred) {
				if (i < arrlen(function_type_node->function_type.arguments) || !function_type_node->function_type.variadic) {
					process_node(context, function_type_node->function_type.arguments[j].type);
					wanted_type = evaluate(context, function_type_node->function_type.arguments[j].type);
				}
			}

			Temporary_Context temporary_context = { .wanted_type = wanted_type };
			process_node_context(context, temporary_context, arguments[i]);

			Static_Argument_Value static_argument = {
				.identifier = function_type_node->function_type.arguments[j].identifier,
				.value = { .value = evaluate(context, arguments[i]), .type = get_type(context, arguments[i]) }
			};
			char buffer[32];
			print_type(get_type(context, arguments[i]), buffer);
			arrpush(static_arguments, static_argument);
		} else {
			char **inferred_arguments = NULL;
			for (long int k = 0; k < arrlen(function_type_node->function_type.arguments); k++) {
				if (function_type_node->function_type.arguments[k].inferred) {
					arrpush(inferred_arguments, function_type_node->function_type.arguments[k].identifier);
				}
			}

			bool pattern_match_collision = false;

			Pattern_Match_Result result = NULL;
			for (long int k = 0; k < arrlen(function_type_node->function_type.arguments); k++) {
				if (function_type_node->function_type.arguments[k].inferred) continue;

				Node *argument = function_type_node->function_type.arguments[k].type;
				Value argument_value = argument_types[i];
				if (!pattern_match(argument, argument_value, context, inferred_arguments, &result)) {
					pattern_match_collision = true;
				};
			}

			for (long int k = 0; k < arrlen(function_type_node->function_type.arguments); k++) {
				if (!function_type_node->function_type.arguments[k].inferred) continue;

				Typed_Value typed_value = shget(result, function_type_node->function_type.arguments[k].identifier);
				if (typed_value.value.value != NULL) {
					Static_Argument_Value static_argument = {
						.identifier = function_type_node->function_type.arguments[k].identifier,
						.value = typed_value
					};

					arrpush(static_arguments, static_argument);
				}
			}

			(void) pattern_match_collision;
		}
		j++;
	}

	if (arrlen(static_arguments) > 0) {
		arrpush(context->scopes, (Scope) { .node = node });

		for (long int i = 0; i < arrlen(static_arguments); i++) {
			shput(arrlast(context->scopes).static_arguments, static_arguments[i].identifier, static_arguments[i].value);
		}

		Value *values = NULL;
		for (long int i = 0; i < arrlen(function_type_node->function_type.arguments); i++) {
			Function_Argument argument = function_type_node->function_type.arguments[i];
			if (argument.static_) {
				arrpush(values, shget(arrlast(context->scopes).static_arguments, argument.identifier).value);
			}
		}

		Node_Data *function_data = get_data(context, function_type_node);
		if (function_data == NULL) {
			function_data = node_data_new(FUNCTION_TYPE_NODE);
		}
		set_data(context, function_type_node, function_data);

		for (long int i = 0; i < arrlen(function_data->function_type.function_values); i++) {
			bool match = arrlen(function_data->function_type.function_values[i].static_arguments) == arrlen(values);
			for (long int j = 0; j < arrlen(function_data->function_type.function_values[i].static_arguments); j++) {
				if (!value_equal(function_data->function_type.function_values[i].static_arguments[j].value, values[j].value)) {
					match = false;
				}
			}

			if (match) {
				Typed_Value matched = function_data->function_type.function_values[i].value;
				function_value = matched.value;
				*function_type = matched.type;
			}
		}

		if (function_value.value == NULL) {
			if (function->kind == IDENTIFIER_NODE) {
				function_value = get_data(context, function)->identifier.value;
			}

			size_t saved_static_argument_id = context->static_argument_id;
			context->static_argument_id = ++context->static_argument_id_counter;

			reset_node(context, function_value.node);
			process_function(context, function_value.node, true);
			context->compile_only = compile_only_parent;

			*function_type = get_type(context, function_value.node);
			set_type(context, function, *function_type);

			function_value = evaluate(context, function_value.node);

			Static_Argument_Variation static_argument_variation = {
				.static_arguments = values,
				.value = { .value = function_value, .type = *function_type }
			};
			arrpush(function_data->function_type.function_values, static_argument_variation);

			context->static_argument_id = saved_static_argument_id;
		}

		(void) arrpop(context->scopes);
	}

	if (function_type->value->tag != FUNCTION_TYPE_VALUE) {
		char given_string[64] = {};
		print_type_outer(*function_type, given_string);
		handle_semantic_error(node->location, "Expected function pointer, but got %s", given_string);
	}

	long int real_argument_count = 0;
	for (long int i = 0; i < arrlen(function_type->value->function_type.arguments); i++) {
		if (!function_type->value->function_type.arguments[i].inferred) real_argument_count++;
	}
	if (arrlen(arguments) != real_argument_count && !function_type->value->function_type.variadic) {
		handle_semantic_error(node->location, "Expected %li arguments, but got %li arguments", real_argument_count, arrlen(arguments));
	}

	long int k = 0;
	for (long int i = 0; i < arrlen(arguments); i++) {
		while (function_type->value->function_type.arguments[k].inferred) k++;
		if (function_type->value->function_type.arguments[k].static_) continue;

		Value wanted_type = {};
		if (k < arrlen(function_type->value->function_type.arguments) || !function_type->value->function_type.variadic) {
			wanted_type = function_type->value->function_type.arguments[k].type;
		}

		Temporary_Context temporary_context = { .wanted_type = wanted_type };
		process_node_context(context, temporary_context, arguments[i]);
		Value type = get_type(context, arguments[i]);

		if (wanted_type.value != NULL && !type_assignable(wanted_type.value, type.value)) {
			handle_type_error(node, wanted_type, type);
		}
		k++;
	}

	return function_value;
}

static void process_struct_type(Context *context, Node *node) {
	Struct_Type_Node struct_type = node->struct_type;
	for (long int i = 0; i < arrlen(struct_type.items); i++) {
		process_node(context, struct_type.items[i].type);
	}

	// for (long int i = 0; i < arrlen(struct_type.operators); i++) {
	// 	process_node(context, struct_type.operators[i].function);
	// }

	Value_Data *value = value_new(INTERNAL_VALUE);
	value->internal.identifier = "type";
	set_type(context, node, (Value) { .value = value });
}

static void process_call(Context *context, Node *node) {
	Call_Node call = node->call;

	process_node(context, call.function);
	Value function_type = get_type(context, call.function);

	Value *argument_types = process_call_generic1(context, call.arguments);

	Node_Data *data = get_data(context, node);
	if (data == NULL) {
		data = node_data_new(CALL_NODE);
	}

	Value function_value = process_call_generic2(context, node, call.function, call.arguments, argument_types, &function_type);

	data->call.function_type = function_type;
	data->call.function_value = function_value;
	set_data(context, node, data);
	set_type(context, node, function_type.value->function_type.return_type);
}

// static Scope *clone_scopes(Scope *scopes) {
// 	Scope *cloned = NULL;
// 	for (long int i = 0; i < arrlen(scopes); i++) {
// 		arrpush(cloned, scopes[i]);
// 	}
// 	return cloned;
// }

static Process_Define_Result process_define_context(Context *context, Temporary_Context temporary_context, Node *node, Scope *scopes) {
	Temporary_Context saved_temporary_context = context->temporary_context;
	context->temporary_context = temporary_context;
	bool compile_only_parent = context->compile_only;

	Process_Define_Result process_define_result = process_define(context, node, scopes);
	context->compile_only = compile_only_parent;

	context->temporary_context = saved_temporary_context;
	return process_define_result;
}

// static Node *get_operator(Context *context, Node *define_node, char *operator) {
// 	Define_Operators *operators = hmget(context->operators, define_node);
// 	if (operators != NULL) {
// 		return shget(*operators, operator);
// 	}
//
// 	return NULL;
// }

static void process_call_method(Context *context, Node *node) {
	Call_Method_Node call_method = node->call_method;

	Node **arguments = NULL;
	arrpush(arguments, call_method.argument1);
	for (long int i = 0; i < arrlen(call_method.arguments); i++) {
		arrpush(arguments, call_method.arguments[i]);
	}

	process_node(context, call_method.argument1);
	Value argument1_type = get_type(context, call_method.argument1);
	if (argument1_type.value->tag != POINTER_TYPE_VALUE) {
		reset_node(context, call_method.argument1);

		Temporary_Context temporary_context = { .want_pointer = true };
		process_node_context(context, temporary_context, call_method.argument1);

		argument1_type = get_type(context, call_method.argument1);
	}

	Custom_Operator_Function custom_operator_function = {};
	Operator_Definition *operators = argument1_type.value->pointer_type.inner.value->struct_type.node->struct_type.operators;
	for (long int i = 0; i < arrlen(operators); i++) {
		if (strcmp(operators[i].operator, call_method.method) == 0) {
			custom_operator_function = (Custom_Operator_Function) {
				.function = argument1_type.value->pointer_type.inner.value->struct_type.operators[i].function.value,
				.function_type = (Value) { .value = argument1_type.value->pointer_type.inner.value->struct_type.operators[i].function.value->function.type },
			};
			break;
		}
	}

	Value *argument_types = process_call_generic1(context, arguments);
	process_call_generic2(context, node, NULL, arguments, argument_types, &custom_operator_function.function_type);

	Node_Data *data = node_data_new(CALL_METHOD_NODE);
	data->call_method.arguments = arguments;
	data->call_method.custom_operator_function = custom_operator_function;
	set_data(context, node, data);
	set_type(context, node, custom_operator_function.function_type.value->function_type.return_type);
}

static void process_identifier(Context *context, Node *node) {
	Identifier_Node identifier = node->identifier;

	Node_Data *data = node_data_new(IDENTIFIER_NODE);
	data->identifier.want_pointer = context->temporary_context.want_pointer;

	Value value = {};
	Value type = {};
	if (identifier.module == NULL &&
			(strcmp(identifier.value, "byte") == 0
			|| strcmp(identifier.value, "uint") == 0
			|| strcmp(identifier.value, "flt") == 0
			|| strcmp(identifier.value, "void") == 0
			|| strcmp(identifier.value, "type") == 0)) {
		value.value = value_new(INTERNAL_VALUE);
		value.value->internal.identifier = identifier.value;

		type.value = value_new(INTERNAL_VALUE);
		type.value->internal.identifier = "type";
	} else if (strcmp(identifier.value, "str") == 0) {
		value.value = value_new(INTERNAL_VALUE);
		value.value->internal.identifier = identifier.value;

		type.value = value_new(INTERNAL_VALUE);
		type.value->internal.identifier = "type";
	} else if (strcmp(identifier.value, "import") == 0) {
		value.value = value_new(INTERNAL_VALUE);
		value.value->internal.identifier = identifier.value;

		type.value = value_new(FUNCTION_TYPE_VALUE);
		Function_Argument_Value argument = {
			.identifier = "",
			.type = create_string_type()
		};
		arrpush(type.value->function_type.arguments, argument);
		type.value->function_type.return_type = (Value) { .value = value_new(MODULE_TYPE_VALUE) };
	// } else if (strcmp(identifier.value, "self") == 0) {
	// 	if (context->current_type.value == NULL) {
	// 		context->current_type_waiting = &data->identifier.value;
	// 	} else {
	// 		value = context->current_type;
	// 	}

	// 	type.value = value_new(INTERNAL_VALUE);
	// 	type.value->internal.identifier = "type";
	} else if (strcmp(identifier.value, "size_of") == 0) {
		value.value = value_new(INTERNAL_VALUE);
		value.value->internal.identifier = identifier.value;

		Value_Data *type_value = value_new(INTERNAL_VALUE);
		type_value->internal.identifier = "type";

		type.value = value_new(FUNCTION_TYPE_VALUE);
		Function_Argument_Value argument = {
			.identifier = "",
			.type = (Value) { .value = type_value }
		};
		arrpush(type.value->function_type.arguments, argument);
		type.value->function_type.return_type.value = value_new(INTERNAL_VALUE);
		type.value->function_type.return_type.value->internal.identifier = "uint";
	} else {
		Node *define_node = NULL;
		Scope *define_scopes = NULL;
		if (identifier.module != NULL) {
			process_node(context, identifier.module);
			Value module_value = evaluate(context, identifier.module);
			for (long int i = 0; i < arrlen(module_value.value->module.body->block.statements); i++) {
				Node *statement = module_value.value->module.body->block.statements[i];
				if (statement->kind == DEFINE_NODE && strcmp(statement->define.identifier, identifier.value) == 0) {
					define_node = statement;

					for (long i = 0; i < arrlen(module_value.value->module.scopes); i++) {
						arrpush(define_scopes, module_value.value->module.scopes[i]);
					}
					break;
				}
			}
		}

		Lookup_Result lookup_result = { .tag = LOOKUP_RESULT_FAIL };
		if (identifier.module == NULL) {
			lookup_result = lookup(context, identifier.value);
		}

		if (lookup_result.tag == LOOKUP_RESULT_SELF) {
			data->identifier.kind = IDENTIFIER_SELF;
			type = lookup_result.type;
		}

		if (lookup_result.tag == LOOKUP_RESULT_DEFINE) {
			for (long i = 0; i < arrlen(context->scopes); i++) {
				arrpush(define_scopes, context->scopes[i]);

				if (lookup_result.define.scope == &context->scopes[i]) break;
			}

			define_node = lookup_result.define.node;
		}

		if (define_node != NULL) {
			Process_Define_Result result = process_define_context(context, context->temporary_context, define_node, define_scopes);
			type = result.type;
			value = result.value;
		}

		if (lookup_result.tag == LOOKUP_RESULT_VARIABLE) {
			data->identifier.kind = IDENTIFIER_VARIABLE;
			data->identifier.variable_definition = lookup_result.variable;

			type = lookup_result.type;
			if (data->identifier.want_pointer) {
				Value_Data *pointer_type = value_new(POINTER_TYPE_VALUE);
				pointer_type->pointer_type.inner = type;
				type = (Value) { .value = pointer_type };
			}

			if (context->temporary_context.assign_value != NULL) {
				Temporary_Context temporary_context = { .wanted_type = type };
				process_node_context(context, temporary_context, context->temporary_context.assign_value);

				Value value_type = get_type(context, context->temporary_context.assign_value);
				if (!type_assignable(type.value, value_type.value)) {
					handle_type_error(context->temporary_context.assign_node, type, value_type);
				}

				data->identifier.assign_value = context->temporary_context.assign_value;
			}
		}

		if (lookup_result.tag == LOOKUP_RESULT_ARGUMENT) {
			data->identifier.kind = IDENTIFIER_ARGUMENT;
			data->identifier.argument_index = lookup_result.argument;

			type = lookup_result.type;
			if (data->identifier.want_pointer) {
				Value_Data *pointer_type = value_new(POINTER_TYPE_VALUE);
				pointer_type->pointer_type.inner = type;
				type = (Value) { .value = pointer_type };
			}
		}

		if (lookup_result.tag == LOOKUP_RESULT_STATIC_ARGUMENT) {
			value = lookup_result.static_argument;
			type = lookup_result.type;
		}

		if (lookup_result.tag == LOOKUP_RESULT_ENUM_VARIANT) {
			value.value = value_new(ENUM_VALUE);
			value.value->enum_.value = lookup_result.enum_variant;
			type = lookup_result.type;
		}
	}

	if (type.value == NULL) {
		handle_semantic_error(node->location, "Identifier '%s' not found", identifier.value);
	}

	if (value.value != NULL) {
		if (value.value->tag == INTERNAL_VALUE && strcmp(value.value->internal.identifier, "type") == 0) {
			context->compile_only = true;
		}

		data->identifier.kind = IDENTIFIER_VALUE;
		data->identifier.value = value;
	}

	data->identifier.type = type;
	set_data(context, node, data);
	set_type(context, node, type);
}

static void process_module(Context *context, Node *node) {
	Module_Node module = node->module;
	process_node(context, module.body);
	set_type(context, node, (Value) { .value = value_new(MODULE_TYPE_VALUE) });
}

static void process_union_type(Context *context, Node *node) {
	Union_Type_Node union_type = node->union_type;
	for (long int i = 0; i < arrlen(union_type.items); i++) {
		process_node(context, union_type.items[i].type);
	}

	Value_Data *value = value_new(INTERNAL_VALUE);
	value->internal.identifier = "type";
	set_type(context, node, (Value) { .value = value });
}

static void process_enum_type(Context *context, Node *node) {
	Value_Data *value = value_new(INTERNAL_VALUE);
	value->internal.identifier = "type";
	set_type(context, node, (Value) { .value = value });
}

static void process_string(Context *context, Node *node) {
	String_Node string = node->string;

	size_t original_length = strlen(string.value);
	char *new_string = malloc(original_length);
	memset(new_string, 0, original_length);

	size_t original_index = 0;
	size_t new_index = 0;
	while (original_index < original_length) {
		if (string.value[original_index] == '\\') {
			switch (string.value[original_index + 1]) {
				case 'n':
					new_string[new_index] = '\n';
					break;
				case '0':
					new_string[new_index] = '\0';
					break;
				default:
					assert(false);
			}
			new_index += 1;
			original_index += 2;
		} else {
			new_string[new_index] = string.value[original_index];
			original_index += 1;
			new_index += 1;
		}
	}

	Value type = context->temporary_context.wanted_type;
	bool invalid_type = false;
	if (type.value == NULL) invalid_type = true;
	else if (type.value->tag == POINTER_TYPE_VALUE && type.value->pointer_type.inner.value->tag == ARRAY_TYPE_VALUE && type.value->pointer_type.inner.value->array_type.inner.value->tag == INTERNAL_VALUE && strcmp(type.value->pointer_type.inner.value->array_type.inner.value->internal.identifier, "byte") == 0) {
		type.value = value_new(POINTER_TYPE_VALUE);
		type.value->pointer_type.inner.value = value_new(ARRAY_TYPE_VALUE);
		type.value->pointer_type.inner.value->array_type.inner = create_internal_type("byte");
		type.value->pointer_type.inner.value->array_type.size.value = value_new(INTEGER_VALUE);
		type.value->pointer_type.inner.value->array_type.size.value->integer.value = new_index;
	}
	else if (type.value->tag == STRING_TYPE_VALUE) {}
	else invalid_type = true;

	if (invalid_type) {
		type = create_string_type();
	}

	Node_Data *data = node_data_new(STRING_NODE);
	data->string.type = type;
	data->string.value = new_string;
	data->string.length = new_index;
	set_data(context, node, data);
	set_type(context, node, type);
}

static void process_number(Context *context, Node *node) {
	Number_Node number = node->number;

	Value wanted_type = context->temporary_context.wanted_type;
	bool invalid_wanted_type = false;
	if (wanted_type.value == NULL) invalid_wanted_type = true;
	else if (wanted_type.value->tag == INTERNAL_VALUE && strcmp(wanted_type.value->internal.identifier, "uint") == 0) {}
	else if (wanted_type.value->tag == INTERNAL_VALUE && strcmp(wanted_type.value->internal.identifier, "flt") == 0) {}
	else invalid_wanted_type = true;

	if (invalid_wanted_type) {
		wanted_type.value = value_new(INTERNAL_VALUE);

		if (number.tag == DECIMAL_NUMBER) {
			wanted_type.value->internal.identifier = "flt";
		} else {
			wanted_type.value->internal.identifier = "uint";
		}
	}

	Node_Data *data = node_data_new(NUMBER_NODE);
	data->number.type = wanted_type;
	set_data(context, node, data);
	set_type(context, node, wanted_type);
}

static void process_null(Context *context, Node *node) {
	Value wanted_type = context->temporary_context.wanted_type;
	if (wanted_type.value == NULL) {
		assert(false);
	}

	Node_Data *data = node_data_new(NULL_NODE);
	data->null_.type = wanted_type;
	set_data(context, node, data);
	set_type(context, node, wanted_type);
}

static void process_boolean(Context *context, Node *node) {
	set_type(context, node, create_boolean_type());
}

static void process_structure(Context *context, Node *node) {
	Structure_Node structure = node->structure;

	Value wanted_type = context->temporary_context.wanted_type;
	if (wanted_type.value == NULL) {
		assert(false);
	}

	assert(wanted_type.value->tag == STRUCT_TYPE_VALUE || wanted_type.value->tag == ARRAY_TYPE_VALUE);

	for (long int i = 0; i < arrlen(structure.values); i++) {
		Value item_wanted_type = {};
		switch (wanted_type.value->tag) {
			case STRUCT_TYPE_VALUE:
				item_wanted_type = wanted_type.value->struct_type.items[i].type;
				break;
			case ARRAY_TYPE_VALUE:
				item_wanted_type = wanted_type.value->array_type.inner;
				break;
			default:
				assert(false);
		}
		Temporary_Context temporary_context = { .wanted_type = item_wanted_type };
		process_node_context(context, temporary_context, structure.values[i]);
	}

	Node_Data *data = node_data_new(STRUCT_NODE);
	data->structure.type = wanted_type;
	set_data(context, node, data);
	set_type(context, node, wanted_type);
}

static void process_run(Context *context, Node *node) {
	Run_Node run = node->run;

	process_node(context, run.value);
	Value value = evaluate(context, run.value);

	Node_Data *data = node_data_new(RUN_NODE);
	data->run.value = value;
	set_data(context, node, data);
	set_type(context, node, get_type(context, run.value));
}

static void process_reference(Context *context, Node *node) {
	Reference_Node reference = node->reference;

	Temporary_Context temporary_context = { .want_pointer = true };
	process_node_context(context, temporary_context, reference.node);

	set_type(context, node, get_type(context, reference.node));
}

static void process_dereference(Context *context, Node *node) {
	Dereference_Node dereference = node->dereference;

	process_node(context, dereference.node);
	Value type = get_type(context, dereference.node);
	if (type.value->tag != POINTER_TYPE_VALUE) {
		char given_string[64] = {};
		print_type_outer(type, given_string);
		handle_semantic_error(node->location, "Expected pointer, but got %s", given_string);
	}

	Node_Data *data = node_data_new(DEREFERENCE_NODE);
	Value inner_type = type.value->pointer_type.inner;
	data->dereference.type = inner_type;
	if (context->temporary_context.assign_value != NULL) {
		Temporary_Context temporary_context = { .wanted_type = type.value->pointer_type.inner };
		process_node_context(context, temporary_context, context->temporary_context.assign_value);

		Value value_type = get_type(context, context->temporary_context.assign_value);
		if (!type_assignable(inner_type.value, value_type.value)) {
			handle_type_error(context->temporary_context.assign_node, inner_type, value_type);
		}
		data->dereference.assign_value = context->temporary_context.assign_value;
	} else {
		set_type(context, node, inner_type);
	}
	set_data(context, node, data);
}

static void process_structure_access(Context *context, Node *node) {
	Structure_Access_Node structure_access = node->structure_access;

	process_node(context, structure_access.structure);

	Value structure_pointer_type = get_type(context, structure_access.structure);
	if (structure_pointer_type.value->tag != POINTER_TYPE_VALUE) {
		reset_node(context, structure_access.structure);

		Temporary_Context temporary_context = { .want_pointer = true };
		process_node_context(context, temporary_context, structure_access.structure);

		structure_pointer_type = get_type(context, structure_access.structure);
	}

	if (structure_pointer_type.value->tag != POINTER_TYPE_VALUE || (structure_pointer_type.value->pointer_type.inner.value->tag != STRUCT_TYPE_VALUE && structure_pointer_type.value->pointer_type.inner.value->tag != UNION_TYPE_VALUE)) {
		char given_string[64] = {};
		print_type_outer(structure_pointer_type, given_string);
		handle_semantic_error(node->location, "Expected structure or union, but got %s", given_string);
	}

	Value structure_type = structure_pointer_type.value->pointer_type.inner;

	Value item_type = {};
	for (long int i = 0; i < arrlen(structure_type.value->struct_type.items); i++) {
		if (strcmp(structure_type.value->struct_type.items[i].identifier, structure_access.item) == 0) {
			item_type = structure_type.value->struct_type.items[i].type;
		}
	}

	Node_Data *data = node_data_new(STRUCTURE_ACCESS_NODE);
	data->structure_access.structure_value = structure_type;
	data->structure_access.want_pointer = context->temporary_context.want_pointer;

	if (context->temporary_context.assign_value != NULL) {
		Temporary_Context temporary_context = { .wanted_type = item_type };
		process_node_context(context, temporary_context, context->temporary_context.assign_value);

		Value value_type = get_type(context, context->temporary_context.assign_value);
		if (!type_assignable(item_type.value, value_type.value)) {
			handle_type_error(context->temporary_context.assign_node, item_type, value_type);
		}
		data->structure_access.assign_value = context->temporary_context.assign_value;
	} else {
		if (data->structure_access.want_pointer) {
			Value_Data *pointer_item_type = value_new(POINTER_TYPE_VALUE);
			pointer_item_type->pointer_type.inner = item_type;
			item_type = (Value) { .value = pointer_item_type };
		}

		set_type(context, node, item_type);
	}
	set_data(context, node, data);
}

static void process_array_access(Context *context, Node *node) {
	Array_Access_Node array_access = node->array_access;

	process_node(context, array_access.array);
	process_node(context, array_access.index);

	Value array_type = get_type(context, array_access.array);
	Value array_type_original = array_type;
	if (array_type.value->tag != POINTER_TYPE_VALUE) {
		reset_node(context, array_access.array);

		Temporary_Context temporary_context = { .want_pointer = true };
		process_node_context(context, temporary_context, array_access.array);

		array_type = get_type(context, array_access.array);
	}

	Custom_Operator_Function custom_operator_function = {};
	if (array_type.value->pointer_type.inner.value->tag == STRUCT_TYPE_VALUE) {
		Operator_Definition *operators = array_type.value->pointer_type.inner.value->struct_type.node->struct_type.operators;
		for (long int i = 0; i < arrlen(operators); i++) {
			if (strcmp(operators[i].operator, "[]") == 0) {
				custom_operator_function = (Custom_Operator_Function) {
					.function = array_type.value->pointer_type.inner.value->struct_type.operators[i].function.value,
					.function_type = (Value) { .value = array_type.value->pointer_type.inner.value->struct_type.operators[i].function.value->function.type },
				};
				break;
			}
		}
	}

	if (custom_operator_function.function == NULL && array_type.value->pointer_type.inner.value->tag != ARRAY_TYPE_VALUE) {
		char given_string[64] = {};
		print_type_outer(array_type_original, given_string);
		handle_semantic_error(node->location, "Expected array, but got %s", given_string);
	}

	Value item_type = {};
	if (custom_operator_function.function == NULL) {
		item_type = array_type.value->pointer_type.inner.value->array_type.inner;
	} else {
		item_type = custom_operator_function.function_type.value->function_type.return_type;
	}

	Node_Data *data = node_data_new(ARRAY_ACCESS_NODE);
	data->array_access.array_type = array_type;
	data->array_access.want_pointer = context->temporary_context.want_pointer;
	data->array_access.custom_operator_function = custom_operator_function;
	data->array_access.item_type = item_type;

	if (context->temporary_context.assign_value != NULL) {
		Temporary_Context temporary_context = { .wanted_type = item_type };
		process_node_context(context, temporary_context, context->temporary_context.assign_value);

		Value value_type = get_type(context, context->temporary_context.assign_value);
		if (!type_assignable(item_type.value, value_type.value)) {
			handle_type_error(context->temporary_context.assign_node, item_type, value_type);
		}
		data->array_access.assign_value = context->temporary_context.assign_value;
	} else {
		if (data->array_access.want_pointer) {
			Value_Data *pointer_item_type = value_new(POINTER_TYPE_VALUE);
			pointer_item_type->pointer_type.inner = item_type;
			item_type = (Value) { .value = pointer_item_type };
		}

		set_type(context, node, item_type);
	}
	set_data(context, node, data);
}

static void process_variable(Context *context, Node *node) {
	Variable_Node variable = node->variable;

	Scope *scope = &arrlast(context->scopes);
	shput(scope->variables, variable.identifier, node);

	Temporary_Context temporary_context = {};
	Value type = {};
	if (variable.type != NULL) {
		process_node(context, variable.type);
		type = evaluate(context, variable.type);
		temporary_context.wanted_type = type;
	}

	if (variable.value != NULL) {
		process_node_context(context, temporary_context, variable.value);

		Value value_type = get_type(context, variable.value);
		if (type.value == NULL) {
			type = value_type;
		} else if (!type_assignable(type.value, value_type.value)) {
			handle_type_error(node, type, value_type);
		}
	} else if (type.value == NULL) {
		handle_semantic_error(node->location, "Expected value");
	}

	Node_Data *data = node_data_new(VARIABLE_NODE);
	data->variable.type = type;
	set_data(context, node, data);
}

static void process_yield(Context *context, Node *node) {
	Yield_Node yield = node->yield;

	size_t levels = yield.levels;
	Node *block = NULL;
	for (long int i = 0; i < arrlen(context->scopes); i++) {
		Node *scope_node = context->scopes[arrlen(context->scopes) - i - 1].node;

		if (scope_node != NULL && scope_node->kind == BLOCK_NODE) {
			arrpush(context->left_blocks, scope_node);
			if (levels == 0) {
				block = scope_node;
				break;
			} else {
				levels--;
			}
		}
	}

	Node_Data *block_data = get_data(context, block);

	if (yield.value != NULL) {
		Temporary_Context temporary_context = { .wanted_type = block_data->block.wanted_type };
		process_node_context(context, temporary_context, yield.value);
	}

	Value type = get_type(context, yield.value);
	if (block_data->block.has_type) {
		if (type.value != NULL) {
			if (block_data->block.type.value == NULL) {
				handle_semantic_error(node->location, "Expected no value");
			} else if (!value_equal(type.value, block_data->block.type.value)) {
				char previous_string[64] = {};
				print_type_outer(type, previous_string);
				char current_string[64] = {};
				print_type_outer(block_data->block.type, current_string);
				handle_semantic_error(node->location, "Mismatched types %s and %s", previous_string, current_string);
			}
		}

		if (block_data->block.type.value != NULL) {
			if (type.value == NULL) {
				handle_semantic_error(node->location, "Expected value");
			}
		}
	}
	block_data->block.type = type;
	block_data->block.has_type = true;

	Node_Data *data = node_data_new(YIELD_NODE);
	data->yield.block = block;
	set_data(context, node, data);
}

static void process_break(Context *context, Node *node) {
	Break_Node break_ = node->break_;

	size_t levels = break_.levels;
	Node *while_ = NULL;
	for (long int i = 0; i < arrlen(context->scopes); i++) {
		Node *scope_node = context->scopes[arrlen(context->scopes) - i - 1].node;

		if (scope_node != NULL && scope_node->kind == WHILE_NODE) {
			if (levels == 0) {
				while_ = scope_node;
				break;
			} else {
				levels--;
			}
		}
	}

	Node_Data *while_data = get_data(context, while_);

	if (break_.value != NULL) {
		Temporary_Context temporary_context = { .wanted_type = while_data->while_.wanted_type };
		process_node_context(context, temporary_context, break_.value);
	}

	Value type = get_type(context, break_.value);
	if (while_data->while_.has_type) {
		if (type.value != NULL) {
			if (while_data->while_.type.value == NULL) {
				handle_semantic_error(node->location, "Expected no value");
			} else if (!value_equal(type.value, while_data->while_.type.value)) {
				char previous_string[64] = {};
				print_type_outer(type, previous_string);
				char current_string[64] = {};
				print_type_outer(while_data->while_.type, current_string);
				handle_semantic_error(node->location, "Mismatched types %s and %s", previous_string, current_string);
			}
		}

		if (while_data->while_.type.value != NULL) {
			if (type.value == NULL) {
				handle_semantic_error(node->location, "Expected value");
			}
		}
	}
	while_data->while_.type = type;
	while_data->while_.has_type = true;

	Node_Data *data = node_data_new(BREAK_NODE);
	data->break_.while_ = while_;
	set_data(context, node, data);
}

static void process_assign(Context *context, Node *node) {
	Assign_Node assign = node->assign;

	Temporary_Context temporary_context = { .assign_value = assign.value, .assign_node = node };
	process_node_context(context, temporary_context, assign.container);
}

// static void process_return(Context *context, Node *node) {
// 	Return_Node return_ = node->return_;
//
// 	for (long int i = 0; i < arrlen(context->scopes); i++) {
// 		Node *scope_node = context->scopes[arrlen(context->scopes) - i - 1].node;
//
// 		if (scope_node != NULL) {
// 			if (scope_node->kind == FUNCTION_NODE) {
// 				break;
// 			} else if (scope_node->kind == BLOCK_NODE) {
// 				arrpush(context->left_blocks, scope_node);
// 			}
// 		}
// 	}
//
// 	if (return_.value != NULL) {
// 		Node *current_function = NULL;
// 		for (long int i = 0; i < arrlen(context->scopes); i++) {
// 			Node *scope_node = context->scopes[arrlen(context->scopes) - i - 1].node;
// 			if (scope_node->kind == FUNCTION_NODE) {
// 				current_function = scope_node;
// 			}
// 		}
//
// 		Value *return_type = get_data(context, current_function->function.function_type)->function_type.value->function_type.return_type;
// 		Temporary_Context temporary_context = { .wanted_type = return_type };
// 		process_node_context(context, temporary_context, return_.value);
//
// 		Value *type = get_type(context, return_.value);
// 		if (!type_assignable(return_type, type)) {
// 			handle_type_error(node, return_type, type);
// 		}
// 	}
// }

static void process_if(Context *context, Node *node) {
	If_Node if_ = node->if_;

	process_node(context, if_.condition);

	Node_Data *data = node_data_new(IF_NODE);
	if (if_.static_) {
		Value evaluated = evaluate(context, if_.condition);

		data->if_.static_condition = evaluated.value->boolean.value;
	}

	Node **saved_left_blocks = context->left_blocks;
	context->left_blocks = NULL;
	process_node(context, if_.if_body);

	Value if_type = get_type(context, if_.if_body);
	if (if_.else_body != NULL) {
		Node **saved_if_left_blocks = context->left_blocks;
		context->left_blocks = NULL;
		process_node(context, if_.else_body);

		Node **saved_else_left_blocks = context->left_blocks;

		context->left_blocks = saved_left_blocks;

		for (long int i = 0; i < arrlen(saved_if_left_blocks); i++) {
			for (long int j = 0; j < arrlen(saved_else_left_blocks); j++) {
				if (saved_if_left_blocks[i] == saved_else_left_blocks[j]) {
					arrpush(context->left_blocks, saved_if_left_blocks[i]);
				}
			}
		}

		Value else_type = get_type(context, if_.else_body);
		if (else_type.value != NULL) {
			if (if_type.value == NULL) {
				handle_semantic_error(node->location, "Expected value from if");
			}
		}

		if (if_type.value != NULL) {
			if (else_type.value == NULL) {
				handle_semantic_error(node->location, "Expected value from else");
			}

			if (!value_equal(if_type.value, else_type.value)) {
				char if_string[64] = {};
				print_type_outer(if_type, if_string);
				char else_string[64] = {};
				print_type_outer(else_type, else_string);
				handle_semantic_error(node->location, "Mismatched types %s and %s", if_string, else_string);
			}

			data->if_.type = if_type;
			set_type(context, node, if_type);
		}
	} else {
		context->left_blocks = saved_left_blocks;
		if (if_type.value != NULL) {
			handle_semantic_error(node->location, "Expected else");
		}
	}

	set_data(context, node, data);
}

static void process_switch(Context *context, Node *node) {
	Switch_Node switch_ = node->switch_;

	process_node(context, switch_.value);

	Node_Data *data = node_data_new(SWITCH_NODE);

	Node **saved_left_blocks = context->left_blocks;

	Value type = get_type(context, switch_.value);
	if (type.value->tag != ENUM_TYPE_VALUE) {
		char string[64] = {};
		print_type_outer(type, string);
		handle_semantic_error(node->location, "Expected enum, but got %s", string);
	}

	Value switch_type = {};
	bool set_switch_type = false;
	long int case_count = 0;
	bool else_case = false;
	for (long int i = 0; i < arrlen(switch_.cases); i++) {
		Switch_Case switch_case = switch_.cases[i];

		if (switch_case.check != NULL) {
			Temporary_Context temporary_context = { .wanted_type = get_type(context, switch_.value) };
			process_node_context(context, temporary_context, switch_case.check);
			case_count++;
		} else {
			else_case = true;
		}

		Node **saved_previous_left_blocks = context->left_blocks;
		context->left_blocks = NULL;
		process_node(context, switch_case.body);

		Node **saved_case_left_blocks = context->left_blocks;

		context->left_blocks = saved_left_blocks;

		for (long int i = 0; i < arrlen(saved_previous_left_blocks); i++) {
			for (long int j = 0; j < arrlen(saved_case_left_blocks); j++) {
				if (saved_previous_left_blocks[i] == saved_case_left_blocks[j]) {
					arrpush(context->left_blocks, saved_previous_left_blocks[i]);
				}
			}
		}

		Value case_type = get_type(context, switch_case.body);
		if (case_type.value != NULL) {
			if (switch_type.value == NULL) {
				if (set_switch_type) {
					handle_semantic_error(node->location, "Expected value from case");
				} else {
					switch_type = case_type;
				}
			}

			set_switch_type = true;
		}

		if (switch_type.value != NULL) {
			if (case_type.value == NULL) {
				handle_semantic_error(node->location, "Expected value from case");
			}

			if (!value_equal(switch_type.value, case_type.value)) {
				char switch_string[64] = {};
				print_type_outer(switch_type, switch_string);
				char case_string[64] = {};
				print_type_outer(case_type, case_string);
				handle_semantic_error(node->location, "Mismatched types %s and %s", switch_string, case_string);
			}

			data->switch_.type = switch_type;
			set_type(context, node, switch_type);
		}
	}

	if (case_count < arrlen(type.value->enum_type.items) && !else_case) {
		context->left_blocks = saved_left_blocks;
		if (switch_type.value != NULL) {
			handle_semantic_error(node->location, "Expected else case");
		}
	}

	set_data(context, node, data);
}

static void process_while(Context *context, Node *node) {
	While_Node while_ = node->while_;

	Node_Data *data = node_data_new(WHILE_NODE);
	data->while_.wanted_type = context->temporary_context.wanted_type;
	data->while_.type = (Value) {};
	set_data(context, node, data);

	arrpush(context->scopes, (Scope) { .node = node });
	process_node(context, while_.condition);

	process_node(context, while_.body);

	if (while_.else_body != NULL) {
		Temporary_Context temporary_context = { .wanted_type = context->temporary_context.wanted_type };
		process_node_context(context, temporary_context, while_.else_body);

		Value type = get_type(context, while_.else_body);
		if (data->while_.has_type) {
			if (type.value != NULL) {
				if (data->while_.type.value == NULL) {
					handle_semantic_error(node->location, "Expected no value in else");
				} else if (!value_equal(type.value, data->while_.type.value)) {
					char previous_string[64] = {};
					print_type_outer(type, previous_string);
					char current_string[64] = {};
					print_type_outer(data->while_.type, current_string);
					handle_semantic_error(node->location, "Mismatched types %s and %s", previous_string, current_string);
				}
			}

			if (data->while_.type.value != NULL) {
				if (type.value == NULL) {
					handle_semantic_error(node->location, "Expected value in else");
				}
			}
		}
	}

	(void) arrpop(context->scopes);

	if (data->while_.has_type && while_.else_body != NULL) {
		set_type(context, node, data->while_.type);
	}
}

static bool can_compare(Value_Data *type) {
	if (type->tag == ENUM_TYPE_VALUE) return true;
	if (type->tag == INTERNAL_VALUE && strcmp(type->internal.identifier, "uint") == 0) return true;
	if (type->tag == INTERNAL_VALUE && strcmp(type->internal.identifier, "flt") == 0) return true;
	if (type->tag == INTERNAL_VALUE && strcmp(type->internal.identifier, "byte") == 0) return true;

	return false;
}

static void process_binary_operator(Context *context, Node *node) {
	Binary_Operator_Node binary_operator = node->binary_operator;

	process_node(context, binary_operator.left);
	Temporary_Context temporary_context = { .wanted_type = get_type(context, binary_operator.left) };
	process_node_context(context, temporary_context, binary_operator.right);

	Value left_type = get_type(context, binary_operator.left);
	Value right_type = get_type(context, binary_operator.right);
	if (!type_assignable(left_type.value, right_type.value)) {
		char left_string[64] = {};
		print_type_outer(left_type, left_string);
		char right_string[64] = {};
		print_type_outer(right_type, right_string);
		handle_semantic_error(node->location, "Mismatched types %s and %s", left_string, right_string);
	}

	Value type = left_type;
	if (type.value->tag == INTERNAL_VALUE && strcmp(type.value->internal.identifier, "uint") == 0) {}
	else if (type.value->tag == INTERNAL_VALUE && strcmp(type.value->internal.identifier, "flt") == 0) {}
	else if (type.value->tag == INTERNAL_VALUE && strcmp(type.value->internal.identifier, "type") == 0) {}
	else if (can_compare(type.value) && binary_operator.operator == OPERATOR_EQUALS) {}
	else {
		char left_string[64] = {};
		print_type_outer(left_type, left_string);
		handle_semantic_error(node->location, "Cannot operate on %s", left_string);
	}

	Value result_type = {};
	switch (binary_operator.operator) {
		case OPERATOR_EQUALS:
		case OPERATOR_LESS:
		case OPERATOR_LESS_EQUALS:
		case OPERATOR_GREATER:
		case OPERATOR_GREATER_EQUALS:
			result_type.value = value_new(INTERNAL_VALUE);
			result_type.value->internal.identifier = "bool";
			break;
		case OPERATOR_ADD:
		case OPERATOR_SUBTRACT:
		case OPERATOR_MULTIPLY:
		case OPERATOR_DIVIDE:
			result_type = left_type;
			break;
	}

	Node_Data *data = node_data_new(BINARY_OPERATOR_NODE);
	data->binary_operator.type = left_type;
	set_data(context, node, data);
	set_type(context, node, result_type);
}

static void process_function_type(Context *context, Node *node) {
	Function_Type_Node function_type = node->function_type;

	Function_Argument_Value *argument_type_values = NULL;
	for (long int i = 0; i < arrlen(function_type.arguments); i++) {
		Function_Argument_Value argument;
		if (function_type.arguments[i].inferred) {
			argument = (Function_Argument_Value) {
				.identifier = function_type.arguments[i].identifier,
				.static_ = function_type.arguments[i].static_,
				.inferred = true
			};
		} else {
			bool compile_only_saved = context->compile_only;
			process_node(context, function_type.arguments[i].type);
			context->compile_only = compile_only_saved;
			argument = (Function_Argument_Value) {
				.identifier = function_type.arguments[i].identifier,
				.type = evaluate(context, function_type.arguments[i].type),
				.static_ = function_type.arguments[i].static_
			};
		}

		arrpush(argument_type_values, argument);
	}

	Value return_type_value = {};
	if (function_type.return_ != NULL) {
		process_node(context, function_type.return_);
		return_type_value = evaluate(context, function_type.return_);
	}

	Value_Data *function_type_value = value_new(FUNCTION_TYPE_VALUE);
	function_type_value->function_type.arguments = argument_type_values;
	function_type_value->function_type.return_type = return_type_value;
	function_type_value->function_type.variadic = function_type.variadic;
	function_type_value->function_type.node = node;

	Node_Data *data = node_data_new(FUNCTION_TYPE_NODE);
	data->function_type.value.value = function_type_value;
	set_data(context, node, data);
}

static void process_pointer(Context *context, Node *node) {
	Pointer_Node pointer = node->pointer;
	process_node(context, pointer.inner);

	Value_Data *value = value_new(INTERNAL_VALUE);
	value->internal.identifier = "type";
	set_type(context, node, (Value) { .value = value });
}

static void process_array_type(Context *context, Node *node) {
	Array_Type_Node array_type = node->array_type;
	process_node(context, array_type.inner);
	if (array_type.size != NULL) {
		process_node(context, array_type.size);
	}

	Value_Data *value = value_new(INTERNAL_VALUE);
	value->internal.identifier = "type";
	set_type(context, node, (Value) { .value = value });
}

void process_node_context(Context *context, Temporary_Context temporary_context, Node *node) {
	Value type = get_type(context, node);
	if (type.value != NULL) {
		return;
	}

	Temporary_Context saved_temporary_context = context->temporary_context;
	context->temporary_context = temporary_context;

	switch (node->kind) {
		case DEFINE_NODE: {
			process_define(context, node, NULL);
			break;
		}
		case BLOCK_NODE: {
			process_block(context, node);
			break;
		}
		case CALL_NODE: {
			process_call(context, node);
			break;
		}
		case CALL_METHOD_NODE: {
			process_call_method(context, node);
			break;
		}
		case IDENTIFIER_NODE: {
			process_identifier(context, node);
			break;
		}
		case STRING_NODE: {
			process_string(context, node);
			break;
		}
		case NUMBER_NODE: {
			process_number(context, node);
			break;
		}
		case NULL_NODE: {
			process_null(context, node);
			break;
		}
		case BOOLEAN_NODE: {
			process_boolean(context, node);
			break;
		}
		case STRUCT_NODE: {
			process_structure(context, node);
			break;
		}
		case RUN_NODE: {
			process_run(context, node);
			break;
		}
		case REFERENCE_NODE: {
			process_reference(context, node);
			break;
		}
		case DEREFERENCE_NODE: {
			process_dereference(context, node);
			break;
		}
		case STRUCTURE_ACCESS_NODE: {
			process_structure_access(context, node);
			break;
		}
		case ARRAY_ACCESS_NODE: {
			process_array_access(context, node);
			break;
		}
		case VARIABLE_NODE: {
			process_variable(context, node);
			break;
		}
		case YIELD_NODE: {
			process_yield(context, node);
			break;
		}
		case BREAK_NODE: {
			process_break(context, node);
			break;
		}
		case ASSIGN_NODE: {
			process_assign(context, node);
			break;
		}
		// case RETURN_NODE: {
		// 	process_return(context, node);
		// 	break;
		// }
		case IF_NODE: {
			process_if(context, node);
			break;
		}
		case SWITCH_NODE: {
			process_switch(context, node);
			break;
		}
		case WHILE_NODE: {
			process_while(context, node);
			break;
		}
		case BINARY_OPERATOR_NODE: {
			process_binary_operator(context, node);
			break;
		}
		case FUNCTION_NODE: {
			process_function(context, node, false);
			break;
		}
		case FUNCTION_TYPE_NODE: {
			process_function_type(context, node);
			break;
		}
		case POINTER_NODE: {
			process_pointer(context, node);
			break;
		}
		case ARRAY_TYPE_NODE: {
			process_array_type(context, node);
			break;
		}
		case STRUCT_TYPE_NODE: {
			process_struct_type(context, node);
			break;
		}
		case UNION_TYPE_NODE: {
			process_union_type(context, node);
			break;
		}
		case ENUM_TYPE_NODE: {
			process_enum_type(context, node);
			break;
		}
		case MODULE_NODE: {
			process_module(context, node);
			break;
		}
		default:
			assert(false);
	}

	context->temporary_context = saved_temporary_context;
}

Context process(Node *root, Codegen codegen) {
	Context context = { .codegen = codegen };

	process_node(&context, root);

	return context;
}
