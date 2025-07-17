#include <limits.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ast.h"
#include "evaluator.h"
#include "parser.h"
#include "stb/ds.h"
#include "util.h"
#include "value.h"

void process_node_context(Context *context, Temporary_Context temporary_context, Node *node);

void process_node(Context *context, Node *node) {
	process_node_context(context, (Temporary_Context) {}, node);
}

static void process_node_with_scopes(Context *context, Node *node, Scope *scopes) {
	Scope *saved_scopes = context->scopes;
	if (scopes != NULL) context->scopes = scopes;
	arrpush(context->scopes, (Scope) { .node = node });

	process_node(context, node);

	(void) arrpop(context->scopes);
	if (scopes != NULL) context->scopes = saved_scopes;
}

static Node *find_define(Block_Node block, char *identifier) {
	for (long int i = 0; i < arrlen(block.statements); i++) {
		Node *statement = block.statements[i];
		if (statement->kind == DEFINE_NODE && strcmp(statement->define.identifier, identifier) == 0) {
			return statement;
		}
	}

	return NULL;
}

typedef struct {
	union {
		struct {
			Node *node;
			Scope *scope;
		} define;
		Node *variable;
		Variable_Definition static_variable;
		struct {
			Node *node;
			size_t index;
		} binding;
		Value static_binding;
		size_t argument;
		size_t enum_variant;
	};
	Value type;
	enum {
		LOOKUP_RESULT_FAIL,
		LOOKUP_RESULT_DEFINE,
		LOOKUP_RESULT_DEFINE_INTERNAL,
		LOOKUP_RESULT_VARIABLE,
		LOOKUP_RESULT_BINDING,
		LOOKUP_RESULT_ARGUMENT,
		LOOKUP_RESULT_STATIC_BINDING,
		LOOKUP_RESULT_STATIC_VARIABLE
	} tag;
} Lookup_Result;

static Lookup_Result lookup(Context *context, char *identifier) {
	if (context->internal_root != NULL) {
		Node *internal_block = context->internal_root->module.body;

		Node *define = find_define(internal_block->block, identifier);
		if (define != NULL) {
			Scope *scope = malloc(sizeof(Scope));
			*scope = (Scope) {
				.node = internal_block
			};

			return (Lookup_Result) { .tag = LOOKUP_RESULT_DEFINE_INTERNAL, .define = { .node = define, .scope = scope } };
		}
	}

	bool found_function = false;
	for (long int i = 0; i < arrlen(context->scopes); i++) {
		Scope *scope = &context->scopes[arrlen(context->scopes) - i - 1];

		Variable_Definition variable = shget(scope->variables, identifier);
		if (variable.node != NULL) {
			return (Lookup_Result) { .tag = LOOKUP_RESULT_VARIABLE, .variable = variable.node, .type = variable.node_data->variable.type };
		}

		Binding binding = shget(scope->bindings, identifier);
		if (binding.type.value != NULL) {
			return (Lookup_Result) { .tag = LOOKUP_RESULT_BINDING, .binding = { .node = scope->node, .index = binding.index }, .type = binding.type };
		}

		if (!found_function && scope->node->kind == FUNCTION_NODE) {
			found_function = true;

			Node *current_function_type = scope->node->function.function_type;
			long int j = 0;
			for (long int i = 0; i < arrlen(current_function_type->function_type.arguments); i++) {
				if (!current_function_type->function_type.arguments[i].static_) {
					if (strcmp(current_function_type->function_type.arguments[i].identifier, identifier) == 0) {
						return (Lookup_Result) { .tag = LOOKUP_RESULT_ARGUMENT, .argument = j, .type = scope->node_type.value->function_type.arguments[i].type };
					}
					j++;
				}
			}
		}

		Typed_Value value = shget(scope->static_bindings, identifier);
		if (value.type.value != NULL) {
			return (Lookup_Result) { .tag = LOOKUP_RESULT_STATIC_BINDING, .static_binding = value.value, .type = value.type };
		}

		variable = shget(scope->static_variables, identifier);
		if (variable.node != NULL) {
			return (Lookup_Result) { .tag = LOOKUP_RESULT_STATIC_VARIABLE, .static_variable = (Variable_Definition) { .node = variable.node, .node_data = variable.node_data }, .type = variable.node_data->variable.type };
		}

		Node *node = scope->node;
		switch (node->kind) {
			case BLOCK_NODE: {
				Node *define = find_define(node->block, identifier);
				if (define != NULL) {
					return (Lookup_Result) { .tag = LOOKUP_RESULT_DEFINE, .define = { .node = define, .scope = scope } };
				}
				break;
			}
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
		case NUMBER_NODE: {
			buffer += sprintf(buffer, "%li", type_node->number.integer);
			break;
		}
		case BOOLEAN_NODE: {
			buffer += sprintf(buffer, "%s", type_node->boolean.value ? "true" : "false");
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
			if (type.value->pointer_type.inner.value != NULL) {
				buffer += print_type(type.value->pointer_type.inner, buffer);
			}
			break;
		}
		case OPTIONAL_TYPE_VALUE: {
			buffer += sprintf(buffer, "?");
			buffer += print_type(type.value->optional_type.inner, buffer);
			break;
		}
		case RESULT_TYPE_VALUE: {
			if (type.value->result_type.value.value != NULL) {
				buffer += print_type(type.value->result_type.value, buffer);
			}
			buffer += sprintf(buffer, "!");
			buffer += print_type(type.value->result_type.error, buffer);
			break;
		}
		case ARRAY_TYPE_VALUE: {
			buffer += sprintf(buffer, "[");
			if (type.value->array_type.size.value != NULL) {
				buffer += print_type(type.value->array_type.size, buffer);
			} else {
				buffer += sprintf(buffer, "_");
			}
			buffer += sprintf(buffer, "]");
			buffer += print_type(type.value->array_type.inner, buffer);
			break;
		}
		case ARRAY_VIEW_TYPE_VALUE: {
			buffer += sprintf(buffer, "[]");
			buffer += print_type(type.value->array_type.inner, buffer);
			break;
		}
		case STRUCT_TYPE_VALUE: {
			buffer += sprintf(buffer, "struct{");
			for (long int i = 0; i < arrlen(type.value->struct_type.members); i++) {
				buffer += sprintf(buffer, "%s:", type.value->struct_type.node->struct_type.members[i].name);
				buffer += print_type(type.value->struct_type.members[i], buffer);
				if (i < arrlen(type.value->struct_type.members) - 1) {
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
		case TAGGED_UNION_TYPE_VALUE: {
			buffer += sprintf(buffer, "tagged_union{");
			for (long int i = 0; i < arrlen(type.value->tagged_union_type.items); i++) {
				buffer += sprintf(buffer, "%s:", type.value->tagged_union_type.items[i].identifier);
				buffer += print_type(type.value->tagged_union_type.items[i].type, buffer);
				if (i < arrlen(type.value->tagged_union_type.items) - 1) {
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
				if (i < arrlen(type.value->struct_type.members) - 1) {
					buffer += sprintf(buffer, ",");
				}
			}
			buffer += sprintf(buffer, "}");
			break;
		}
		case INTEGER_TYPE_VALUE: {
			buffer += sprintf(buffer, "int(%s,%li)", type.value->integer_type.signed_ ? "true" : "false", type.value->integer_type.size);
			break;
		}
		case BYTE_TYPE_VALUE: {
			buffer += sprintf(buffer, "byte");
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

static void handle_expected_type_error(Node *node, Value wanted, Value given) {
	char wanted_string[64] = {};
	print_type_outer(wanted, wanted_string);
	char given_string[64] = {};
	print_type_outer(given, given_string);
	handle_semantic_error(node->location, "Expected %s, but got %s", wanted_string, given_string);
}

static void handle_mismatched_type_error(Node *node, Value type1, Value type2) {
	char type1_string[64] = {};
	print_type_outer(type1, type1_string);
	char type2_string[64] = {};
	print_type_outer(type2, type2_string);
	handle_semantic_error(node->location, "Mismatched types %s and %s", type1_string, type2_string);
}

#define handle_type_error(/* Node * */ node, /* char * */ message, /* Value */ type) { \
	char type_string[64] = {}; \
	print_type_outer(type, type_string); \
	handle_semantic_error(node->location, message, type_string); \
}

#define handle_2type_error(/* Node * */ node, /* char * */ message, /* Value */ type1, /* Value */ type2) { \
	char type1_string[64] = {}; \
	print_type_outer(type1, type1_string); \
	char type2_string[64] = {}; \
	print_type_outer(type2, type2_string); \
	handle_semantic_error(node->location, message, type1_string, type2_string); \
}

typedef struct { char *string; size_t length; } Expanded_String;
static Expanded_String expand_escapes(char *string) {
	size_t original_length = strlen(string);
	char *new_string = malloc(original_length);
	memset(new_string, 0, original_length);

	size_t original_index = 0;
	size_t new_index = 0;
	while (original_index < original_length) {
		if (string[original_index] == '\\') {
			switch (string[original_index + 1]) {
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
			new_string[new_index] = string[original_index];
			original_index += 1;
			new_index += 1;
		}
	}

	return (Expanded_String) { .string = new_string, .length = new_index };
}

typedef struct { char *key; Typed_Value value; } *Pattern_Match_Result;
static bool pattern_match(Node *node, Value value, Context *context, char **inferred_arguments, Pattern_Match_Result *match_result) {
	switch (node->kind) {
		case POINTER_NODE: {
			if (value.value->tag == POINTER_TYPE_VALUE) {
				return pattern_match(node->pointer_type.inner, value.value->pointer_type.inner, context, inferred_arguments, match_result);
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
						.type = create_value(TYPE_TYPE_VALUE)
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

static bool uses_inferred_arguments(Node *node, char **inferred_arguments) {
	switch (node->kind) {
		case POINTER_NODE: {
			if (node->pointer_type.inner == NULL) return false;
			return uses_inferred_arguments(node->pointer_type.inner, inferred_arguments);
		}
		case ARRAY_TYPE_NODE: {
			return uses_inferred_arguments(node->array_type.inner, inferred_arguments) || uses_inferred_arguments(node->array_type.size, inferred_arguments);
		}
		case CALL_NODE: {
			for (long int i = 0; i < arrlen(node->call.arguments); i++) {
				if (uses_inferred_arguments(node->call.arguments[i], inferred_arguments)) {
					return true;
				}
			}
			return false;
		}
		case IDENTIFIER_NODE: {
			for (long int i = 0; i < arrlen(inferred_arguments); i++) {
				if (strcmp(inferred_arguments[i], node->identifier.value) == 0) {
					return true;
				}
			}
			return false;
		}
		default:
			return false;
	}
}

static void process_initial_argument_types(Context *context, Value function_type, Node **arguments) {
	char **inferred_arguments = NULL;
	Node *function_type_node = function_type.value->function_type.node;
	if (function_type_node != NULL) {
		Function_Argument *function_node_arguments = function_type_node->function_type.arguments;

		for (long int k = 0; k < arrlen(function_node_arguments); k++) {
			if (function_node_arguments[k].inferred) {
				arrpush(inferred_arguments, function_node_arguments[k].identifier);
			}
		}
	}

	for (long int i = 0; i < arrlen(arguments); i++) {
		Value type = get_type(context, arguments[i]);
		if (type.value == NULL) {
			if (function_type_node != NULL) {
				Function_Argument *function_node_arguments = function_type_node->function_type.arguments;
				long int function_argument_index = i + arrlen(inferred_arguments);
				if (function_argument_index < arrlen(function_node_arguments) && uses_inferred_arguments(function_node_arguments[function_argument_index].type, inferred_arguments)) {
					process_node(context, arguments[i]);
				}
			}
		}

	}
}

static bool has_static_arguments(Node *function_type) {
	for (long int i = 0; i < arrlen(function_type->function_type.arguments); i++) {
		if (function_type->function_type.arguments[i].static_) return true;
	}

	return false;
}

static Custom_Operator_Function find_custom_operator(Value type, char *operator) {
	if (type.value->tag == STRUCT_TYPE_VALUE) {
		Operator_Overload *operators = type.value->struct_type.node->struct_type.operator_overloads;
		for (long int i = 0; i < arrlen(operators); i++) {
			if (strcmp(operators[i].name, operator) == 0) {
				return (Custom_Operator_Function) {
					.function = type.value->struct_type.operators[i].function.value,
					.function_type = (Value) { .value = type.value->struct_type.operators[i].function.value->function.type },
				};
			}
		}
	}

	return (Custom_Operator_Function) {};
}

static void process_function(Context *context, Node *node, bool given_static_arguments);

static Value process_call_generic(Context *context, Node *node, Value function_value, Value *function_type, Node **arguments) {
	typedef struct {
		char *identifier;
		Typed_Value value;
	} Static_Argument_Value;

	Node *function_type_node = function_type->value->function_type.node;

	char **inferred_arguments = NULL;

	if (has_static_arguments(function_type_node)) {
		assert(function_type->value->function_type.incomplete);

		size_t saved_static_id = context->static_id;
		size_t new_static_id = ++context->static_id_counter;

		bool pattern_match_fail = false;
		bool compile_only_parent = context->compile_only;
		Static_Argument_Value *static_arguments = NULL;

		Function_Argument *function_node_arguments = function_type_node->function_type.arguments;

		for (long int k = 0; k < arrlen(function_node_arguments); k++) {
			if (function_node_arguments[k].inferred) {
				arrpush(inferred_arguments, function_node_arguments[k].identifier);

				if (function_node_arguments[k].default_value != NULL) {
					process_node(context, function_node_arguments[k].default_value);
				}
			}
		}

		Pattern_Match_Result result = NULL;
		for (long int argument_index = 0; argument_index < arrlen(arguments); argument_index++) {
			long int function_argument_index = argument_index + arrlen(inferred_arguments);
			if (function_argument_index >= arrlen(function_node_arguments)) break;

			if (function_node_arguments[function_argument_index].static_) {
				Value wanted_type = {};
				if (!function_node_arguments[function_argument_index].inferred) {
					if (argument_index < arrlen(function_node_arguments) || !function_type_node->function_type.variadic) {
						context->static_id = new_static_id;
						process_node(context, function_node_arguments[function_argument_index].type);
						wanted_type = evaluate(context, function_node_arguments[function_argument_index].type);
						context->static_id = saved_static_id;
					}
				}

				reset_node(context, arguments[argument_index]);
				Temporary_Context temporary_context = { .wanted_type = wanted_type };
				process_node_context(context, temporary_context, arguments[argument_index]);

				Static_Argument_Value static_argument = {
					.identifier = function_node_arguments[function_argument_index].identifier,
					.value = { .value = evaluate(context, arguments[argument_index]), .type = get_type(context, arguments[argument_index]) }
				};
				arrpush(static_arguments, static_argument);
			} else {
				for (long int k = 0; k < arrlen(function_node_arguments); k++) {
					if (function_node_arguments[k].inferred || function_node_arguments[k].static_) continue;

					Node *argument = function_node_arguments[k].type;
					Value argument_value = get_type(context, arguments[k - arrlen(inferred_arguments)]);
					if (argument_value.value != NULL) {
						if (!pattern_match(argument, argument_value, context, inferred_arguments, &result)) {
							pattern_match_fail = true;
						}
					}
				}
			}
		}

		if (function_type_node->function_type.return_ != NULL && context->temporary_context.wanted_type.value != NULL) {
			if (!pattern_match(function_type_node->function_type.return_, context->temporary_context.wanted_type, context, inferred_arguments, &result)) {
				pattern_match_fail = true;
			}
		}

		for (long int k = 0; k < arrlen(function_node_arguments); k++) {
			if (!function_node_arguments[k].inferred) continue;

			Typed_Value typed_value = shget(result, function_node_arguments[k].identifier);
			if (typed_value.value.value == NULL && function_node_arguments[k].default_value != NULL) {
				typed_value.type = get_type(context, function_node_arguments[k].default_value);
				typed_value.value = evaluate(context, function_node_arguments[k].default_value);
			}

			if (typed_value.value.value != NULL) {
				Static_Argument_Value static_argument = {
					.identifier = function_node_arguments[k].identifier,
					.value = typed_value
				};

				arrpush(static_arguments, static_argument);
			} else {
				pattern_match_fail = true;
			}
		}

		if (pattern_match_fail) {
			handle_semantic_error(node->location, "Pattern matching failed");
		}

		struct { char *key; Typed_Value value; } *static_arguments_map = NULL;;
		for (long int i = 0; i < arrlen(static_arguments); i++) {
			shput(static_arguments_map, static_arguments[i].identifier, static_arguments[i].value);
		}

		Value *static_argument_values = NULL;
		for (long int i = 0; i < arrlen(function_type_node->function_type.arguments); i++) {
			Function_Argument argument = function_type_node->function_type.arguments[i];
			if (argument.static_) {
				arrpush(static_argument_values, shget(static_arguments_map, argument.identifier).value);
			}
		}

		Node_Data *function_data = get_data(context, function_type_node);
		if (function_data == NULL) {
			function_data = data_new(FUNCTION_TYPE_NODE);
		}
		set_data(context, function_type_node, function_data);

		bool found_match = false;

		Static_Argument_Variation *function_values = function_data->function_type.function_values;
		for (long int i = 0; i < arrlen(function_values); i++) {
			bool match = arrlen(function_values[i].static_arguments) == arrlen(static_argument_values);
			for (long int j = 0; j < arrlen(function_values[i].static_arguments); j++) {
				if (!value_equal(function_values[i].static_arguments[j].value, static_argument_values[j].value)) {
					match = false;
				}
			}

			if (match) {
				Typed_Value matched = function_values[i].value;
				function_value = matched.value;
				*function_type = matched.type;
				found_match = true;
			}
		}

		if (!found_match) {
			context->static_id = new_static_id;

			reset_node(context, function_value.value->function.node);

			Scope *saved_scopes = context->scopes;
			context->scopes = NULL;
			for (long int i = 0; i < arrlen(function_value.value->function.scopes); i++) {
				arrpush(context->scopes, function_value.value->function.scopes[i]);
			}

			arrpush(context->scopes, (Scope) { .node = node });

			for (long int i = 0; i < arrlen(static_arguments); i++) {
				shput(arrlast(context->scopes).static_bindings, static_arguments[i].identifier, static_arguments[i].value);
			}

			if (function_value.value->tag == FUNCTION_VALUE) {
				process_function(context, function_value.value->function.node, true);

				context->compile_only = compile_only_parent;

				*function_type = get_type(context, function_value.value->function.node);
				function_value = evaluate(context, function_value.value->function.node);
			} else if (function_value.value->tag == INTERNAL_VALUE) {
				if (streq(function_value.value->internal.identifier, "size_of")) {
					*function_type = create_value(FUNCTION_TYPE_VALUE);
					function_type->value->function_type.variadic = false;
					function_type->value->function_type.arguments = NULL;

					Function_Argument_Value argument1 = {
						.static_ = true,
						.inferred = true
					};
					arrpush(function_type->value->function_type.arguments, argument1);
					Function_Argument_Value argument2 = {
						.type = create_value(TYPE_TYPE_VALUE)
					};
					arrpush(function_type->value->function_type.arguments, argument2);

					function_type->value->function_type.return_type = static_arguments[0].value.value;
				} else {
					assert(false);
				}
			} else {
				assert(false);
			}

			context->static_id = saved_static_id;

			Static_Argument_Variation static_argument_variation = {
				.static_arguments = static_argument_values,
				.value = { .value = function_value, .type = *function_type }
			};
			arrpush(function_data->function_type.function_values, static_argument_variation);

			(void) arrpop(context->scopes);
			context->scopes = saved_scopes;
		}
	}

	if (function_type->value->tag != FUNCTION_TYPE_VALUE) {
		handle_type_error(node, "Expected function pointer, but got %s", *function_type);
	}

	assert(!function_type->value->function_type.incomplete);
	long int real_argument_count = 0;
	for (long int i = 0; i < arrlen(function_type->value->function_type.arguments); i++) {
		if (!function_type->value->function_type.arguments[i].inferred) real_argument_count++;
	}
	if (arrlen(arguments) != real_argument_count && !function_type->value->function_type.variadic) {
		handle_semantic_error(node->location, "Expected %li arguments, but got %li arguments", real_argument_count, arrlen(arguments));
	}

	Function_Argument_Value *function_type_arguments = function_type->value->function_type.arguments;
	for (long int argument_index = 0; argument_index < arrlen(arguments); argument_index++) {
		long int function_argument_index = argument_index + arrlen(inferred_arguments);
		if (function_argument_index < arrlen(function_type_arguments) && function_type_arguments[function_argument_index].static_) continue;

		Value wanted_type = {};
		if (function_argument_index < arrlen(function_type_arguments) || !function_type->value->function_type.variadic) {
			wanted_type = function_type_arguments[function_argument_index].type;
		}

		Temporary_Context temporary_context = { .wanted_type = wanted_type };
		process_node_context(context, temporary_context, arguments[argument_index]);
		Value type = get_type(context, arguments[argument_index]);

		if (wanted_type.value != NULL && !type_assignable(wanted_type.value, type.value)) {
			handle_expected_type_error(node, wanted_type, type);
		}
		function_argument_index++;
	}

	return function_value;
}

static void process_enforce_pointer(Context *context, Node *node) {
	process_node(context, node);
	Value structure_pointer_type = get_type(context, node);
	if (structure_pointer_type.value->tag != POINTER_TYPE_VALUE) {
		reset_node(context, node);

		Temporary_Context temporary_context = { .want_pointer = true };
		process_node_context(context, temporary_context, node);

		structure_pointer_type = get_type(context, node);
	}
}

static void process_enforce_pointer_sometimes(Context *context, Node *node, bool is_assign) {
	process_node(context, node);

	Value real_structure_type = get_type(context, node);
	if ((is_assign || context->temporary_context.want_pointer) && real_structure_type.value->tag != POINTER_TYPE_VALUE) {
		reset_node(context, node);

		Temporary_Context temporary_context = { .want_pointer = true };
		process_node_context(context, temporary_context, node);

		real_structure_type = get_type(context, node);
	}
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

static void process_array_access(Context *context, Node *node) {
	Array_Access_Node array_access = node->array_access;

	process_node(context, array_access.parent);

	Temporary_Context temporary_context = { .wanted_type = create_integer_type(false, context->codegen.default_integer_size) };
	process_node_context(context, temporary_context, array_access.index);

	Value array_type = get_type(context, array_access.parent);
	Value array_type_original = array_type;
	if (array_type.value->tag != POINTER_TYPE_VALUE) {
		reset_node(context, array_access.parent);

		Temporary_Context temporary_context = { .want_pointer = true };
		process_node_context(context, temporary_context, array_access.parent);

		array_type = get_type(context, array_access.parent);
	}

	assert(array_type.value->tag == POINTER_TYPE_VALUE);

	Custom_Operator_Function custom_operator_function = find_custom_operator(array_type.value->pointer_type.inner, "[]");

	if (custom_operator_function.function == NULL
			&& array_type.value->pointer_type.inner.value->tag != ARRAY_TYPE_VALUE
			&& array_type.value->pointer_type.inner.value->tag != ARRAY_VIEW_TYPE_VALUE) {
		handle_type_error(node, "Cannot perform array access operation on %s", array_type_original);
	}

	Value item_type = {};
	if (custom_operator_function.function == NULL) {
		switch (array_type.value->pointer_type.inner.value->tag) {
			case ARRAY_TYPE_VALUE: {
				item_type = array_type.value->pointer_type.inner.value->array_type.inner;
				break;
			}
			case ARRAY_VIEW_TYPE_VALUE: {
				item_type = array_type.value->pointer_type.inner.value->array_view_type.inner;
				break;
			}
			default:
				assert(false);
		}
	} else {
		Node **arguments = NULL;
		arrpush(arguments, array_access.parent);
		arrpush(arguments, array_access.index);

		process_initial_argument_types(context, custom_operator_function.function_type, arguments);
		custom_operator_function.function = process_call_generic(context, node, (Value) { .value = custom_operator_function.function }, &custom_operator_function.function_type, arguments).value;

		item_type = custom_operator_function.function_type.value->function_type.return_type.value->pointer_type.inner;
	}

	Node_Data *data = data_new(ARRAY_ACCESS_NODE);
	data->array_access.array_type = array_type;
	data->array_access.want_pointer = context->temporary_context.want_pointer;
	data->array_access.custom_operator_function = custom_operator_function;
	data->array_access.item_type = item_type;

	if (array_access.assign_value != NULL) {
		Temporary_Context temporary_context = { .wanted_type = item_type };
		process_node_context(context, temporary_context, array_access.assign_value);

		Value value_type = get_type(context, array_access.assign_value);
		if (!type_assignable(item_type.value, value_type.value)) {
			handle_expected_type_error(node, item_type, value_type);
		}
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

static void process_array_type(Context *context, Node *node) {
	Array_Type_Node array_type = node->array_type;
	process_node(context, array_type.inner);
	process_node(context, array_type.size);
	set_type(context, node, create_value(TYPE_TYPE_VALUE));
}

static void process_array_view_type(Context *context, Node *node) {
	Array_View_Type_Node array_view_type = node->array_view_type;
	process_node(context, array_view_type.inner);
	set_type(context, node, create_value(TYPE_TYPE_VALUE));
}

static bool can_compare(Value_Data *type) {
	if (type->tag == ENUM_TYPE_VALUE) return true;
	if (type->tag == INTEGER_TYPE_VALUE) return true;
	if (type->tag == BYTE_TYPE_VALUE) return true;
	if (type->tag == FLOAT_TYPE_VALUE) return true;
	if (type->tag == BYTE_TYPE_VALUE) return true;

	return false;
}

static void process_binary_op(Context *context, Node *node) {
	Binary_Op_Node binary_operator = node->binary_op;

	process_node(context, binary_operator.left);
	Temporary_Context temporary_context = { .wanted_type = get_type(context, binary_operator.left) };
	process_node_context(context, temporary_context, binary_operator.right);

	Value left_type = get_type(context, binary_operator.left);
	Value right_type = get_type(context, binary_operator.right);
	if (!type_assignable(left_type.value, right_type.value)) {
		handle_mismatched_type_error(node, left_type, right_type);
	}

	Value type = left_type;
	if (type.value->tag == INTEGER_TYPE_VALUE) {}
	else if (type.value->tag == FLOAT_TYPE_VALUE) {}
	else if (type.value->tag == TYPE_TYPE_VALUE) {}
	else if (can_compare(type.value) && (binary_operator.operator == OP_EQUALS || binary_operator.operator == OP_NOT_EQUALS)) {}
	else {
		handle_type_error(node, "Cannot operate on %s", left_type);
	}

	Value result_type = {};
	switch (binary_operator.operator) {
		case OP_EQUALS:
		case OP_NOT_EQUALS:
		case OP_LESS:
		case OP_LESS_EQUALS:
		case OP_GREATER:
		case OP_GREATER_EQUALS:
			result_type = create_value(BOOLEAN_TYPE_VALUE);
			break;
		case OP_ADD:
		case OP_SUBTRACT:
		case OP_MULTIPLY:
		case OP_DIVIDE:
			result_type = left_type;
			break;
	}

	Node_Data *data = data_new(BINARY_OP_NODE);
	data->binary_operator.type = left_type;
	set_data(context, node, data);
	set_type(context, node, result_type);
}

static void process_block(Context *context, Node *node) {
	Block_Node block = node->block;

	Node_Data *data = data_new(BLOCK_NODE);

	arrpush(context->scopes, (Scope) { .node = node });
	for (long int i = 0; i < arrlen(block.statements); i++) {
		Node *statement = block.statements[i];

		if (statement->kind == DEFER_NODE) {
			arrpush(data->block.defers, statement->defer.node);
		} else {
			if (block.has_result && i == arrlen(block.statements) - 1) {
				Temporary_Context temporary_context = { .wanted_type = context->temporary_context.wanted_type };
				process_node_context(context, temporary_context, statement);
				set_type(context, node, get_type(context, statement));
			} else {
				process_node(context, statement);

				Value type = get_type(context, statement);
				if (type.value != NULL) {
					handle_type_error(statement, "Unused value %s", type);
				}
			}
		}
	}

	for (long int i = 0; i < arrlen(data->block.defers); i++) {
		Node *statement = data->block.defers[i];
		process_node(context, statement);
	}
	(void) arrpop(context->scopes);

	set_data(context, node, data);
}

static void process_boolean(Context *context, Node *node) {
	set_type(context, node, create_value(BOOLEAN_TYPE_VALUE));
}

static void process_break(Context *context, Node *node) {
	Break_Node break_ = node->break_;

	Node *while_ = NULL;
	for (long int i = 0; i < arrlen(context->scopes); i++) {
		Node *scope_node = context->scopes[arrlen(context->scopes) - i - 1].node;

		if (scope_node != NULL && scope_node->kind == WHILE_NODE) {
			while_ = scope_node;
			break;
		}
	}

	if (while_ == NULL) {
		handle_semantic_error(node->location, "No surrounding while");
	}

	While_Data while_data = get_data(context, while_)->while_;

	if (break_.value != NULL) {
		Temporary_Context temporary_context = { .wanted_type = while_data.wanted_type };
		process_node_context(context, temporary_context, break_.value);
	}

	Value type = get_type(context, break_.value);
	if (while_data.has_type) {
		if (type.value != NULL) {
			if (while_data.type.value == NULL) {
				handle_semantic_error(node->location, "Expected no value");
			} else if (!value_equal(type.value, while_data.type.value)) {
				handle_mismatched_type_error(node, type, while_data.type);
			}
		}

		if (while_data.type.value != NULL) {
			if (type.value == NULL) {
				handle_semantic_error(node->location, "Expected value");
			}
		}
	}
	while_data.type = type;
	while_data.has_type = true;

	Node_Data *data = data_new(BREAK_NODE);
	data->break_.while_ = while_;
	set_data(context, node, data);
}

static void process_cast(Context *context, Node *node) {
	Cast_Node cast = node->cast;

	process_node(context, cast.node);
	Value from_type = get_type(context, cast.node);

	Value to_type = context->temporary_context.wanted_type;
	if (cast.type != NULL) {
		process_node(context, cast.type);
		to_type = evaluate(context, cast.type);
	}

	bool cast_ok = false;
	if (from_type.value->tag == POINTER_TYPE_VALUE && to_type.value->tag == POINTER_TYPE_VALUE) cast_ok = true;
	else if (from_type.value->tag == INTEGER_TYPE_VALUE && to_type.value->tag == BYTE_TYPE_VALUE) cast_ok = true;

	if (!cast_ok) {
		handle_2type_error(node, "Cannot cast from %s to %s", from_type, to_type);
	}

	Node_Data *data = data_new(CAST_NODE);
	data->cast.from_type = from_type;
	data->cast.to_type = to_type;

	set_data(context, node, data);
	set_type(context, node, context->temporary_context.wanted_type);
}

static void process_call(Context *context, Node *node) {
	Call_Node call = node->call;

	process_node(context, call.function);
	Value function_type = get_type(context, call.function);

	process_initial_argument_types(context, function_type, call.arguments);

	Node_Data *data = get_data(context, node);
	if (data == NULL) {
		data = data_new(CALL_NODE);
	}

	Value function_value = {};
	if (call.function->kind == IDENTIFIER_NODE && get_data(context, call.function)->identifier.kind == IDENTIFIER_VALUE) {
		function_value = get_data(context, call.function)->identifier.value;
	}
	function_value = process_call_generic(context, node, function_value, &function_type, call.arguments);

	data->call.function_type = function_type;
	data->call.function_value = function_value;
	set_data(context, node, data);
	set_type(context, node, function_type.value->function_type.return_type);
}

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

	Custom_Operator_Function custom_operator_function = find_custom_operator(argument1_type.value->pointer_type.inner, call_method.method);
	if (custom_operator_function.function == NULL) {
		handle_semantic_error(node->location, "Method '%s' not found", call_method.method);
	}

	process_initial_argument_types(context, custom_operator_function.function_type, arguments);
	custom_operator_function.function = process_call_generic(context, node, (Value) { .value = custom_operator_function.function }, &custom_operator_function.function_type, arguments).value;

	Node_Data *data = data_new(CALL_METHOD_NODE);
	data->call_method.arguments = arguments;
	data->call_method.custom_operator_function = custom_operator_function;
	set_data(context, node, data);
	set_type(context, node, custom_operator_function.function_type.value->function_type.return_type);
}

static void process_character(Context *context, Node *node) {
	Character_Node character = node->character;

	Expanded_String new_string = expand_escapes(character.value);
	if (new_string.length != 1) {
		handle_semantic_error(node->location, "Expected only one character");
	}

	Node_Data *data = data_new(CHARACTER_NODE);
	data->character.value = new_string.string[0];
	set_data(context, node, data);
	set_type(context, node, create_value(BYTE_TYPE_VALUE));
}

static void process_catch(Context *context, Node *node) {
	Catch_Node catch = node->catch;

	process_node(context, catch.value);

	Value result_type = get_type(context, catch.value);

	bool saved_returned = context->returned;
	arrpush(context->scopes, (Scope) { .node = node });
	if (catch.binding != NULL) {
		Binding binding = {
			.type = result_type.value->result_type.error,
			.index = 0
		};

		shput(arrlast(context->scopes).bindings, catch.binding, binding);
	}
	process_node(context, catch.error);
	(void) arrpop(context->scopes);

	Node_Data *data = data_new(CATCH_NODE);
	data->switch_.returned = context->returned;

	context->returned = saved_returned;

	set_type(context, node, result_type.value->result_type.value);
	data->catch.type = result_type;
	set_data(context, node, data);
}

static void process_define(Context *context, Node *node) {
	Define_Node define = node->define;

	arrpush(context->scopes, (Scope) { .node = node });

	Node_Data *data = get_data(context, node);
	if (data != NULL) {
		return;
	}

	Value wanted_type = {};
	if (define.type != NULL) {
		process_node(context, define.type);
		wanted_type = evaluate(context, define.type);
	}

	Temporary_Context temporary_context = { .wanted_type = wanted_type };
	process_node_context(context, temporary_context, define.expression);

	Value type = get_type(context, define.expression);
	Value value = evaluate(context, define.expression);

	data = data_new(DEFINE_NODE);
	set_data(context, node, data);

	data->define.typed_value = (Typed_Value) {
		.value = value,
		.type = type
	};

	return;
}

static void process_deoptional(Context *context, Node *node) {
	Deoptional_Node deoptional = node->deoptional;

	process_enforce_pointer(context, deoptional.node);

	Value type = get_type(context, deoptional.node);
	if (type.value->tag != POINTER_TYPE_VALUE || type.value->pointer_type.inner.value->tag != OPTIONAL_TYPE_VALUE) {
		handle_type_error(node, "Expected optional, but got %s", type);
	}

	Node_Data *data = data_new(DEOPTIONAL_NODE);
	Value inner_type = type.value->pointer_type.inner.value->optional_type.inner;
	data->deoptional.type = inner_type;
	if (deoptional.assign_value != NULL) {
		Temporary_Context temporary_context = { .wanted_type = inner_type };
		process_node_context(context, temporary_context, deoptional.assign_value);

		Value value_type = get_type(context, deoptional.assign_value);
		if (!type_assignable(inner_type.value, value_type.value)) {
			handle_expected_type_error(node, inner_type, value_type);
		}
	} else {
		set_type(context, node, inner_type);
	}
	set_data(context, node, data);
}

static void process_dereference(Context *context, Node *node) {
	Dereference_Node dereference = node->dereference;

	process_node(context, dereference.node);
	Value type = get_type(context, dereference.node);
	if (type.value->tag != POINTER_TYPE_VALUE) {
		handle_type_error(node, "Expected pointer, but got %s", type);
	}

	Node_Data *data = data_new(DEREFERENCE_NODE);
	Value inner_type = type.value->pointer_type.inner;
	data->dereference.type = inner_type;
	if (dereference.assign_value != NULL) {
		Temporary_Context temporary_context = { .wanted_type = type.value->pointer_type.inner };
		process_node_context(context, temporary_context, dereference.assign_value);

		Value value_type = get_type(context, dereference.assign_value);
		if (!type_assignable(inner_type.value, value_type.value)) {
			handle_expected_type_error(node, inner_type, value_type);
		}
	} else {
		set_type(context, node, inner_type);
	}
	set_data(context, node, data);
}

static void process_enum_type(Context *context, Node *node) {
	set_type(context, node, create_value(TYPE_TYPE_VALUE));
}

static void process_extern(Context *context, Node *node) {
	set_type(context, node, context->temporary_context.wanted_type);
}

static void process_for(Context *context, Node *node) {
	For_Node for_ = node->for_;

	arrpush(context->scopes, (Scope) { .node = node });

	Value *item_types = NULL;
	Value *element_types = NULL;
	for (long int i = 0; i < arrlen(for_.items); i++) {
		process_node(context, for_.items[i]);
		arrpush(item_types, get_type(context, for_.items[i]));

		switch (item_types[i].value->tag) {
			case ARRAY_VIEW_TYPE_VALUE: {
				arrpush(element_types, item_types[i].value->array_view_type.inner);
				break;
			}
			case RANGE_TYPE_VALUE: {
				arrpush(element_types, item_types[i].value->range_type.type);
				break;
			}
			default:
				assert(false);
		}
	}

	Node_Data *data = data_new(FOR_NODE);
	if (for_.static_) {
		assert(arrlen(for_.items) == 1);
		assert(item_types[0].value->tag == ARRAY_VIEW_TYPE_VALUE);

		Value looped_value = evaluate(context, for_.items[0]);

		size_t saved_static_id = context->static_id;
		for (size_t i = 0; i < looped_value.value->array_view.length; i++) {
			size_t static_id = ++context->static_id_counter;
			context->static_id = static_id;
			arrpush(data->for_.static_ids, static_id);

			Typed_Value typed_value = {
				.value = (Value) { .value = looped_value.value->array_view.values[i] },
				.type = element_types[0]
			};
			shput(arrlast(context->scopes).static_bindings, for_.bindings[0], typed_value);

			if (arrlen(for_.bindings) > 1) {
				Typed_Value typed_value = {
					.value = create_integer(i),
					.type = create_integer_type(false, context->codegen.default_integer_size)
				};
				shput(arrlast(context->scopes).static_bindings, for_.bindings[1], typed_value);
			}

			process_node(context, for_.body);
		}
		context->static_id = saved_static_id;
	} else {
		assert(arrlen(for_.items) == arrlen(for_.bindings));

		for (long int i = 0; i < arrlen(for_.items); i++) {
			Binding binding = {
				.type = element_types[i],
				.index = i
			};
			shput(arrlast(context->scopes).bindings, for_.bindings[i], binding);
		}

		process_node(context, for_.body);

		(void) arrpop(context->scopes);
	}

	data->for_.types = item_types;
	set_data(context, node, data);
}

static bool process_function_type(Context *context, Node *node, bool given_static_arguments);
static void process_function(Context *context, Node *node, bool given_static_arguments) {
	Function_Node function = node->function;

	bool compile_only_parent = context->compile_only;
	bool returned_parent = context->returned;

	context->compile_only = false;
	context->returned = false;

	bool static_argument = process_function_type(context, function.function_type, given_static_arguments);

	set_type(context, node, get_data(context, function.function_type)->function_type.value);

	if (!static_argument || given_static_arguments) {
		Node_Data *function_type_data = get_data(context, function.function_type);

		Value function_type_value = function_type_data->function_type.value;

		set_type(context, node, function_type_value);

		if (function.body != NULL) {
			Scope scope = {
				.node = node,
				.node_type = function_type_value
			};
			arrpush(context->scopes, scope);

			Temporary_Context temporary_context = { .wanted_type = function_type_value.value->function_type.return_type };
			process_node_context(context, temporary_context, function.body);

			if (function_type_value.value->function_type.return_type.value != NULL) {
				Value returned_type = get_type(context, function.body);

				Value wanted_return_type = function_type_value.value->function_type.return_type;
				if (!type_assignable(wanted_return_type.value, returned_type.value) && !context->returned) {
					handle_expected_type_error(node, wanted_return_type, returned_type);
				}
			}
			(void) arrpop(context->scopes);
		}
	}

	Node_Data *data = data_new(FUNCTION_NODE);
	if (context->compile_only) {
		data->function.compile_only = true;
	}
	if (context->returned) {
		data->function.returned = true;
	}
	set_data(context, node, data);

	context->compile_only = compile_only_parent;
	context->returned = returned_parent;
}

static bool process_function_type(Context *context, Node *node, bool given_static_arguments) {
	Function_Type_Node function_type = node->function_type;

	bool static_argument = false;
	for (long int i = 0; i < arrlen(function_type.arguments); i++) {
		if (function_type.arguments[i].static_) {
			static_argument = true;
		}
	}

	if (static_argument && !given_static_arguments) {
		Value function_type_value = {};
		function_type_value.value = value_new(FUNCTION_TYPE_VALUE);
		function_type_value.value->function_type.incomplete = true;
		function_type_value.value->function_type.node = node;
		Node_Data *function_type_data = data_new(FUNCTION_TYPE_NODE);
		function_type_data->function_type.value = function_type_value;
		set_data(context, node, function_type_data);

		return static_argument;
	}

	Function_Argument_Value *argument_type_values = NULL;
	for (long int i = 0; i < arrlen(function_type.arguments); i++) {
		Function_Argument_Value argument = {};
		if (function_type.arguments[i].inferred) {
			argument = (Function_Argument_Value) {
				.identifier = function_type.arguments[i].identifier,
				.static_ = function_type.arguments[i].static_,
				.inferred = true
			};
		} else {
			process_node(context, function_type.arguments[i].type);
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

	Node_Data *data = data_new(FUNCTION_TYPE_NODE);
	data->function_type.value.value = function_type_value;
	set_data(context, node, data);

	set_type(context, node, create_value(TYPE_TYPE_VALUE));

	return static_argument;
}

static void process_identifier(Context *context, Node *node) {
	Identifier_Node identifier = node->identifier;

	Node_Data *data = data_new(IDENTIFIER_NODE);
	data->identifier.want_pointer = context->temporary_context.want_pointer;

	Lookup_Result lookup_result = { .tag = LOOKUP_RESULT_FAIL };

	Value value = {};
	Value type = {};
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
	} else {
		Value wanted_type = context->temporary_context.wanted_type;
		if (wanted_type.value != NULL && wanted_type.value->tag == ENUM_TYPE_VALUE) {
			for (long int i = 0; i < arrlen(wanted_type.value->enum_type.items); i++) {
				if (strcmp(identifier.value, wanted_type.value->enum_type.items[i]) == 0) {
					value.value = value_new(ENUM_VALUE);
					value.value->enum_.value = i;
					type = context->temporary_context.wanted_type;
				}
			}
		}

		if (type.value == NULL) {
			if (strcmp(identifier.value, "_") == 0) {
				data->identifier.kind = IDENTIFIER_UNDERSCORE;
				type = create_value(NONE_VALUE);

				if (identifier.assign_value != NULL) {
					process_node(context, identifier.assign_value);
				}
			} else {
				lookup_result = lookup(context, identifier.value);
			}
		}
	}

	if (lookup_result.tag == LOOKUP_RESULT_DEFINE_INTERNAL) {
		arrpush(define_scopes, *lookup_result.define.scope);
		define_node = lookup_result.define.node;
	}

	if (lookup_result.tag == LOOKUP_RESULT_DEFINE) {
		for (long i = 0; i < arrlen(context->scopes); i++) {
			arrpush(define_scopes, context->scopes[i]);

			if (lookup_result.define.scope == &context->scopes[i]) break;
		}

		define_node = lookup_result.define.node;
	}

	if (define_node != NULL) {
		size_t saved_static_id = context->static_id;
		context->static_id = 0;
		process_node_with_scopes(context, define_node, define_scopes);
		Typed_Value typed_value = get_data(context, define_node)->define.typed_value;
		context->static_id = saved_static_id;
		type = typed_value.type;
		value = typed_value.value;
	}

	if (lookup_result.tag == LOOKUP_RESULT_VARIABLE) {
		data->identifier.kind = IDENTIFIER_VARIABLE;
		data->identifier.variable = lookup_result.variable;

		type = lookup_result.type;
		if (data->identifier.want_pointer) {
			Value_Data *pointer_type = value_new(POINTER_TYPE_VALUE);
			pointer_type->pointer_type.inner = type;
			type = (Value) { .value = pointer_type };
		}

		if (identifier.assign_value != NULL) {
			Temporary_Context temporary_context = { .wanted_type = type };
			process_node_context(context, temporary_context, identifier.assign_value);

			Value value_type = get_type(context, identifier.assign_value);
			if (!type_assignable(type.value, value_type.value)) {
				handle_expected_type_error(node, type, value_type);
			}
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

	if (lookup_result.tag == LOOKUP_RESULT_BINDING) {
		data->identifier.kind = IDENTIFIER_BINDING;
		data->identifier.binding.node = lookup_result.binding.node;
		data->identifier.binding.index = lookup_result.binding.index;

		type = lookup_result.type;
		if (data->identifier.want_pointer) {
			Value_Data *pointer_type = value_new(POINTER_TYPE_VALUE);
			pointer_type->pointer_type.inner = type;
			type = (Value) { .value = pointer_type };
		}
	}

	if (lookup_result.tag == LOOKUP_RESULT_STATIC_BINDING) {
		value = lookup_result.static_binding;
		type = lookup_result.type;
	}

	if (lookup_result.tag == LOOKUP_RESULT_STATIC_VARIABLE) {
		data->identifier.kind = IDENTIFIER_STATIC_VARIABLE;
		data->identifier.static_variable = lookup_result.static_variable;
		type = lookup_result.type;

		if (identifier.assign_value != NULL) {
			Temporary_Context temporary_context = { .wanted_type = type };
			process_node_context(context, temporary_context, identifier.assign_value);

			Value value_type = get_type(context, identifier.assign_value);
			if (!type_assignable(type.value, value_type.value)) {
				handle_expected_type_error(node, type, value_type);
			}
		}
	}

	if (type.value == NULL) {
		handle_semantic_error(node->location, "Identifier '%s' not found", identifier.value);
	}

	if (value.value != NULL) {
		value.node = node;

		data->identifier.kind = IDENTIFIER_VALUE;
		data->identifier.value = value;
	}

	data->identifier.type = type;
	set_data(context, node, data);

	if (identifier.assign_value == NULL) {
		set_type(context, node, type);
	}
}

static void process_if(Context *context, Node *node) {
	If_Node if_ = node->if_;

	process_node(context, if_.condition);
	Value condition_type = get_type(context, if_.condition);

	Node_Data *data = data_new(IF_NODE);
	data->if_.type = condition_type;

	if (if_.static_) {
		Value evaluated = evaluate(context, if_.condition);

		bool truthy = false;
		switch (evaluated.value->tag) {
			case BOOLEAN_VALUE:
				truthy = evaluated.value->boolean.value;
				break;
			case OPTIONAL_VALUE:
				truthy = evaluated.value->optional.present;
				break;
			default:
				assert(false);
		}
		data->if_.static_condition = truthy;

		Temporary_Context temporary_context = { .wanted_type = context->temporary_context.wanted_type };
		if (truthy) {
			if (evaluated.value->tag == OPTIONAL_VALUE) {
				Typed_Value typed_value = {
					.value = (Value) { .value = evaluated.value->optional.value },
					.type = condition_type.value->optional_type.inner
				};
				shput(arrlast(context->scopes).static_bindings, if_.bindings[0], typed_value);
			}

			process_node_context(context, temporary_context, if_.if_body);
		} else {
			if (if_.else_body != NULL) {
				process_node_context(context, temporary_context, if_.else_body);
			}
		}
	} else {
		bool saved_returned = context->returned;
		context->returned = false;

		arrpush(context->scopes, (Scope) { .node = node });
		if (arrlen(if_.bindings) > 0) {
			Binding binding = {
				.type = condition_type.value->tag == RESULT_TYPE_VALUE ? condition_type.value->result_type.value : condition_type.value->optional_type.inner,
				.index = 0
			};

			shput(arrlast(context->scopes).bindings, if_.bindings[0], binding);
		}
		process_node(context, if_.if_body);
		(void) arrpop(context->scopes);

		Value if_type = get_type(context, if_.if_body);
		if (if_.else_body != NULL) {
			bool saved_if_returned = context->returned;
			context->returned = false;
			data->if_.then_returned = saved_if_returned;

			Temporary_Context temporary_context = { .wanted_type = if_type };
			process_node_context(context, temporary_context, if_.else_body);

			bool saved_else_returned = context->returned;
			data->if_.else_returned = saved_else_returned;

			context->returned = saved_returned;

			if (saved_if_returned && saved_else_returned) {
				context->returned = true;
			}

			Value else_type = get_type(context, if_.else_body);
			if (else_type.value != NULL) {
				if (if_type.value == NULL) {
					handle_semantic_error(node->location, "Expected value from if");
				}
			}

			if (if_type.value != NULL) {
				if (!saved_else_returned) {
					if (else_type.value == NULL) {
						handle_semantic_error(node->location, "Expected value from else");
					}

					if (!value_equal(if_type.value, else_type.value)) {
						handle_mismatched_type_error(node, if_type, else_type);
					}
				}

				data->if_.result_type = if_type;
				set_type(context, node, if_type);
			}
		} else {
			context->returned = saved_returned;
			if (if_type.value != NULL) {
				handle_semantic_error(node->location, "Expected else");
			}
		}

		data->if_.returned = context->returned;
	}
	set_data(context, node, data);
}

static void process_internal(Context *context, Node *node) {
	Internal_Node internal = node->internal;

	Value value = {};
	Node *inner_node = NULL;
	switch (internal.kind) {
		case INTERNAL_UINT: {
			value.value = value_new(INTEGER_TYPE_VALUE);
			value.value->integer_type.size = context->codegen.default_integer_size;
			value.value->integer_type.signed_ = false;

			set_type(context, node, (Value) { .value = value_new(TYPE_TYPE_VALUE) });
			break;
		}
		case INTERNAL_UINT8: {
			value.value = value_new(INTEGER_TYPE_VALUE);
			value.value->integer_type.size = 8;
			value.value->integer_type.signed_ = false;

			set_type(context, node, (Value) { .value = value_new(TYPE_TYPE_VALUE) });
			break;
		}
		case INTERNAL_TYPE: {
			value.value = value_new(TYPE_TYPE_VALUE);
			set_type(context, node, (Value) { .value = value_new(TYPE_TYPE_VALUE) });

			context->compile_only = true;
			break;
		}
		case INTERNAL_BYTE: {
			value.value = value_new(BYTE_TYPE_VALUE);
			set_type(context, node, (Value) { .value = value_new(TYPE_TYPE_VALUE) });
			break;
		}
		case INTERNAL_FLT64: {
			value.value = value_new(FLOAT_TYPE_VALUE);
			value.value->float_type.size = 64;
			set_type(context, node, (Value) { .value = value_new(TYPE_TYPE_VALUE) });
			break;
		}
		case INTERNAL_BOOL: {
			value.value = value_new(BOOLEAN_TYPE_VALUE);
			set_type(context, node, (Value) { .value = value_new(TYPE_TYPE_VALUE) });
			break;
		}
		case INTERNAL_TYPE_OF: {
			process_node(context, internal.inputs[0]);
			value.value = get_type(context, internal.inputs[0]).value;
			set_type(context, node, (Value) { .value = value_new(TYPE_TYPE_VALUE) });
			break;
		}
		case INTERNAL_INT: {
			process_node(context, internal.inputs[0]);
			process_node(context, internal.inputs[1]);

			value.value = get_type(context, internal.inputs[0]).value;
			value.value = value_new(INTEGER_TYPE_VALUE);
			value.value->integer_type.signed_ = evaluate(context, internal.inputs[0]).value->boolean.value;
			value.value->integer_type.size = evaluate(context, internal.inputs[1]).value->integer.value;

			set_type(context, node, (Value) { .value = value_new(TYPE_TYPE_VALUE) });
			break;
		}
		case INTERNAL_C_CHAR_SIZE: {
			value = create_integer(context->codegen.c_size_fn(C_CHAR_SIZE));
			set_type(context, node, (Value) { .value = create_integer_type(false, 8).value });
			break;
		}
		case INTERNAL_C_SHORT_SIZE: {
			value = create_integer(context->codegen.c_size_fn(C_SHORT_SIZE));
			set_type(context, node, (Value) { .value = create_integer_type(false, 8).value });
			break;
		}
		case INTERNAL_C_INT_SIZE: {
			value = create_integer(context->codegen.c_size_fn(C_INT_SIZE));
			set_type(context, node, (Value) { .value = create_integer_type(false, 8).value });
			break;
		}
		case INTERNAL_C_LONG_SIZE: {
			value = create_integer(context->codegen.c_size_fn(C_LONG_SIZE));
			set_type(context, node, (Value) { .value = create_integer_type(false, 8).value });
			break;
		}
		case INTERNAL_EMBED: {
			Value *values = NULL;
			for (long int i = 0; i < arrlen(internal.inputs); i++) {
				process_node(context, internal.inputs[i]);
				arrpush(values, evaluate(context, internal.inputs[i]));
			}

			size_t total_length = 0;
			for (long int i = 0; i < arrlen(values); i++) {
				switch (values[i].value->tag) {
					case ARRAY_VIEW_VALUE:
						total_length += values[i].value->array_view.length;
						break;
					case BYTE_VALUE:
						total_length += 1;
						break;
					default:
						assert(false);
				}
			}

			char *source_string = malloc(total_length + 1);
			source_string[total_length] = '\0';

			size_t index = 0;
			for (long int i = 0; i < arrlen(values); i++) {
				switch (values[i].value->tag) {
					case ARRAY_VIEW_VALUE:
						for (size_t j = 0; j < values[i].value->array_view.length; j++) {
							source_string[index++] = values[i].value->array_view.values[j]->byte.value;
						}
						break;
					case BYTE_VALUE:
						source_string[index++] = values[i].value->byte.value;
						break;
					default:
						assert(false);
				}
			}

			inner_node = parse_source_expr(source_string, index, "");
			Temporary_Context temporary_context = { .wanted_type = context->temporary_context.wanted_type };
			process_node_context(context, temporary_context, inner_node);

			set_type(context, node, get_type(context, inner_node));
			break;
		}
		case INTERNAL_PRINT: {
			process_node(context, internal.inputs[0]);
			break;
		}
		case INTERNAL_EXPRESSION: {
			process_node(context, internal.inputs[0]);

			set_type(context, node, context->temporary_context.wanted_type);
			break;
		}
		case INTERNAL_SELF: {
			for (long int i = arrlen(context->scopes) - 1; i >= 0; i--) {
				if (context->scopes[i].current_type.value != NULL) {
					value = context->scopes[i].current_type;
				}
			}

			set_type(context, node, create_value(TYPE_TYPE_VALUE));
			break;
		}
		case INTERNAL_SIZE_OF: {
			process_node(context, internal.inputs[0]);

			Value type = evaluate(context, internal.inputs[0]);

			value.value = value_new(INTEGER_VALUE);
			value.value->integer.value = context->codegen.size_fn(type.value, context->codegen.data);

			set_type(context, node, create_integer_type(false, context->codegen.default_integer_size));
			break;
		}
		case INTERNAL_IMPORT: {
			process_node(context, internal.inputs[0]);

			Value string = evaluate(context, internal.inputs[0]);

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

			value = get_cached_file(context, source);
			if (value.value == NULL) {
				Node *file_node = parse_file(source);

				Scope *saved_scopes = context->scopes;
				context->scopes = NULL;
				process_node(context, file_node);
				value = evaluate(context, file_node);
				context->scopes = saved_scopes;

				add_cached_file(context, source, value);
			}

			set_type(context, node, create_value(MODULE_TYPE_VALUE));
			break;
		}
		case INTERNAL_TYPE_INFO_OF: {
			process_node(context, internal.inputs[0]);

			Value type = evaluate(context, internal.inputs[0]);

			value = create_value(TAGGED_UNION_VALUE);

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
					items_value->array_view.length = arrlen(type.value->struct_type.members);
					for (long int i = 0; i < arrlen(type.value->struct_type.members); i++) {
						Value_Data *struct_item_value = value_new(STRUCT_VALUE);

						char *name_string;
						if (type.value->struct_type.node != NULL) {
							name_string = type.value->struct_type.node->struct_type.members[i].name;
						} else {
							char *buffer = malloc(8);
							memset(buffer, 0, 8);
							buffer[0] = '_';
							sprintf(buffer + 1, "%i", (int) i);
							name_string = buffer;
						}

						size_t name_string_length = strlen(name_string);

						Value_Data *name_value = value_new(ARRAY_VIEW_VALUE);
						name_value->array_view.length = name_string_length;
						for (size_t i = 0; i < name_string_length; i++) {
							Value_Data *byte_value = value_new(BYTE_VALUE);
							byte_value->byte.value = name_string[i];
							arrpush(name_value->array_view.values, byte_value);
						}
						arrpush(struct_item_value->struct_.values, name_value);

						Value_Data *type_value = type.value->struct_type.members[i].value;
						arrpush(struct_item_value->struct_.values, type_value);

						arrpush(items_value->array_view.values, struct_item_value);
					}
					arrpush(data->struct_.values, items_value);
					break;
				}
				case UNION_TYPE_VALUE: {
					enum_value->enum_.value = 2;

					data = value_new(STRUCT_VALUE);

					Value_Data *items_value = value_new(ARRAY_VIEW_VALUE);
					items_value->array_view.length = arrlen(type.value->union_type.items);
					for (long int i = 0; i < arrlen(type.value->union_type.items); i++) {
						Value_Data *struct_item_value = value_new(STRUCT_VALUE);

						char *name_string = type.value->union_type.items[i].identifier;
						size_t name_string_length = strlen(name_string);

						Value_Data *name_value = value_new(ARRAY_VIEW_VALUE);
						name_value->array_view.length = name_string_length;
						for (size_t i = 0; i < name_string_length; i++) {
							Value_Data *byte_value = value_new(BYTE_VALUE);
							byte_value->byte.value = name_string[i];
							arrpush(name_value->array_view.values, byte_value);
						}
						arrpush(struct_item_value->struct_.values, name_value);

						Value_Data *type_value = type.value->union_type.items[i].type.value;
						arrpush(struct_item_value->struct_.values, type_value);

						arrpush(items_value->array_view.values, struct_item_value);
					}
					arrpush(data->struct_.values, items_value);
					break;
				}
				case TAGGED_UNION_TYPE_VALUE: {
					enum_value->enum_.value = 3;

					data = value_new(STRUCT_VALUE);

					Value_Data *items_value = value_new(ARRAY_VIEW_VALUE);
					items_value->array_view.length = arrlen(type.value->tagged_union_type.items);
					for (long int i = 0; i < arrlen(type.value->tagged_union_type.items); i++) {
						Value_Data *struct_item_value = value_new(STRUCT_VALUE);

						char *name_string = type.value->tagged_union_type.items[i].identifier;
						size_t name_string_length = strlen(name_string);

						Value_Data *name_value = value_new(ARRAY_VIEW_VALUE);
						name_value->array_view.length = name_string_length;
						for (size_t i = 0; i < name_string_length; i++) {
							Value_Data *byte_value = value_new(BYTE_VALUE);
							byte_value->byte.value = name_string[i];
							arrpush(name_value->array_view.values, byte_value);
						}
						arrpush(struct_item_value->struct_.values, name_value);

						Value_Data *type_value = type.value->tagged_union_type.items[i].type.value;
						arrpush(struct_item_value->struct_.values, type_value);

						arrpush(items_value->array_view.values, struct_item_value);
					}
					arrpush(data->struct_.values, items_value);
					break;
				}
				case ENUM_TYPE_VALUE: {
					enum_value->enum_.value = 4;

					data = value_new(STRUCT_VALUE);

					Value_Data *items_value = value_new(ARRAY_VIEW_VALUE);
					items_value->array_view.length = arrlen(type.value->enum_type.items);
					for (long int i = 0; i < arrlen(type.value->enum_type.items); i++) {
						char *name_string = type.value->enum_type.items[i];
						size_t name_string_length = strlen(name_string);

						Value_Data *name_value = value_new(ARRAY_VIEW_VALUE);
						name_value->array_view.length = name_string_length;
						for (size_t i = 0; i < name_string_length; i++) {
							Value_Data *byte_value = value_new(BYTE_VALUE);
							byte_value->byte.value = name_string[i];
							arrpush(name_value->array_view.values, byte_value);
						}
						arrpush(items_value->array_view.values, name_value);
					}
					arrpush(data->struct_.values, items_value);
					break;
				}
				case OPTIONAL_TYPE_VALUE: {
					enum_value->enum_.value = 5;

					data = value_new(STRUCT_VALUE);

					Value_Data *type_value = type.value->optional_type.inner.value;
					arrpush(data->struct_.values, type_value);
					break;
				}
				case ARRAY_TYPE_VALUE: {
					enum_value->enum_.value = 6;

					data = value_new(STRUCT_VALUE);

					Value_Data *size_value = type.value->array_type.size.value;
					arrpush(data->struct_.values, size_value);
					Value_Data *type_value = type.value->array_type.inner.value;
					arrpush(data->struct_.values, type_value);
					break;
				}
				case ARRAY_VIEW_TYPE_VALUE: {
					enum_value->enum_.value = 7;

					data = value_new(STRUCT_VALUE);

					Value_Data *type_value = type.value->array_type.inner.value;
					arrpush(data->struct_.values, type_value);
					break;
				}
				case TUPLE_TYPE_VALUE: {
					enum_value->enum_.value = 8;

					data = value_new(STRUCT_VALUE);

					Value_Data *items_value = value_new(ARRAY_VIEW_VALUE);
					items_value->array_view.length = arrlen(type.value->tuple_type.members);
					for (long int i = 0; i < arrlen(type.value->tuple_type.members); i++) {
						Value_Data *type_value = type.value->tuple_type.members[i].value;
						arrpush(items_value->array_view.values, type_value);
					}
					arrpush(data->struct_.values, items_value);
					break;
				}
				case BYTE_TYPE_VALUE: {
					enum_value->enum_.value = 9;

					data = value_new(STRUCT_VALUE);
					break;
				}
				default:
					assert(false);
			}

			value.value->tagged_union.tag = enum_value;
			value.value->tagged_union.data = data;

			size_t saved_static_id = context->static_id;
			context->static_id = 0;
			Value type_info_type = get_data(context, find_define(context->internal_root->module.body->block, "Type_Info"))->define.typed_value.value;
			context->static_id = saved_static_id;
			set_type(context, node, type_info_type);
		}
	}

	Node_Data *data = data_new(INTERNAL_NODE);
	data->internal.value = value;
	data->internal.node = inner_node;
	set_data(context, node, data);
}

static void process_is(Context *context, Node *node) {
	Is_Node is = node->is;

	process_node(context, is.node);

	Value type = get_type(context, is.node);
	if (type.value->tag != TAGGED_UNION_TYPE_VALUE) {
		handle_type_error(node, "Expected tagged union, but got %s", type);
	}

	Temporary_Context temporary_context = { .wanted_type = (Value) { .value = type.value->tagged_union_type.enum_ } };
	process_node_context(context, temporary_context, is.check);
	Value check_value = evaluate(context, is.check);

	Value tagged_type = type.value->tagged_union_type.items[check_value.value->enum_.value].type;
	set_type(context, node, create_optional_type(tagged_type));

	Node_Data *data = data_new(IS_NODE);
	data->is.value = check_value;
	data->is.type = create_optional_type(tagged_type);
	set_data(context, node, data);
}

static void process_module(Context *context, Node *node) {
	Module_Node module = node->module;
	process_node(context, module.body);
	set_type(context, node, (Value) { .value = value_new(MODULE_TYPE_VALUE) });
}

static void process_module_type(Context *context, Node *node) {
	set_type(context, node, (Value) { .value = value_new(TYPE_TYPE_VALUE) });
}

static void process_null(Context *context, Node *node) {
	Value wanted_type = context->temporary_context.wanted_type;
	if (wanted_type.value == NULL) {
		assert(false);
	}

	Node_Data *data = data_new(NULL_NODE);
	data->null_.type = wanted_type;
	set_data(context, node, data);
	set_type(context, node, wanted_type);
}

static void process_number(Context *context, Node *node) {
	Number_Node number = node->number;

	Value wanted_type = context->temporary_context.wanted_type;
	bool invalid_wanted_type = false;
	if (wanted_type.value == NULL) invalid_wanted_type = true;
	else if (wanted_type.value->tag == INTEGER_TYPE_VALUE) {}
	else if (wanted_type.value->tag == FLOAT_TYPE_VALUE) {}
	else invalid_wanted_type = true;

	if (invalid_wanted_type) {
		if (number.tag == DECIMAL_NUMBER) {
			wanted_type = create_float_type(context->codegen.default_integer_size);
		} else {
			wanted_type = create_integer_type(true, context->codegen.default_integer_size);
		}
	}

	Node_Data *data = data_new(NUMBER_NODE);
	data->number.type = wanted_type;
	set_data(context, node, data);
	set_type(context, node, wanted_type);
}

static void process_optional(Context *context, Node *node) {
	Optional_Type_Node optional = node->optional_type;
	process_node(context, optional.inner);

	set_type(context, node, create_value(TYPE_TYPE_VALUE));
}

static void process_pointer(Context *context, Node *node) {
	Pointer_Type_Node pointer = node->pointer_type;
	if (pointer.inner != NULL) {
		process_node(context, pointer.inner);
	}

	set_type(context, node, create_value(TYPE_TYPE_VALUE));
}

static void process_range(Context *context, Node *node) {
	Range_Node range = node->range;

	Value wanted_type = {};
	if (context->temporary_context.wanted_type.value != NULL && context->temporary_context.wanted_type.value->tag == RANGE_TYPE_VALUE) {
		wanted_type = context->temporary_context.wanted_type.value->range_type.type;
	}

	Temporary_Context temporary_context = { .wanted_type = wanted_type };
	process_node_context(context, temporary_context, range.start);

	if (wanted_type.value == NULL) {
		wanted_type = get_type(context, range.start);
	}

	if (range.end != NULL) {
		temporary_context = (Temporary_Context) { .wanted_type = wanted_type };
		process_node_context(context, temporary_context, range.end);
	}

	set_type(context, node, create_range_type(wanted_type));
}

static void process_reference(Context *context, Node *node) {
	Reference_Node reference = node->reference;

	Temporary_Context temporary_context = { .want_pointer = true };
	process_node_context(context, temporary_context, reference.node);

	set_type(context, node, get_type(context, reference.node));
}

static void process_result(Context *context, Node *node) {
	Result_Type_Node result = node->result_type;
	if (result.value != NULL) {
		process_node(context, result.value);
	}
	process_node(context, result.error);

	set_type(context, node, create_value(TYPE_TYPE_VALUE));
}

static void process_return(Context *context, Node *node) {
	Return_Node return_ = node->return_;
	
	context->returned = true;

	Node *current_function = NULL;
	for (long int i = 0; i < arrlen(context->scopes); i++) {
		Node *scope_node = context->scopes[arrlen(context->scopes) - i - 1].node;
		if (scope_node->kind == FUNCTION_NODE) {
			current_function = scope_node;
			break;
		}
	}

	Value return_type = get_data(context, current_function->function.function_type)->function_type.value.value->function_type.return_type;

	Node_Data *data = data_new(RETURN_NODE);
	if (return_.value != NULL) {
		Value wanted_type = return_type;
		if (wanted_type.value->tag == RESULT_TYPE_VALUE) {
			switch (return_.type) {
				case RETURN_SUCCESS:
					wanted_type = wanted_type.value->result_type.value;
					break;
				case RETURN_ERROR:
					wanted_type = wanted_type.value->result_type.error;
					break;
				default:
					break;
			}
		}

		Temporary_Context temporary_context = { .wanted_type = wanted_type };
		process_node_context(context, temporary_context, return_.value);

		Value type = get_type(context, return_.value);
		if (!type_assignable(wanted_type.value, type.value)) {
			handle_expected_type_error(node, wanted_type, type);
		}
	}

	data->return_.type = return_type;
	set_data(context, node, data);
}

static void process_run(Context *context, Node *node) {
	Run_Node run = node->run;

	Temporary_Context temporary_context = { .wanted_type = context->temporary_context.wanted_type };
	process_node_context(context, temporary_context, run.node);
	Value value = evaluate(context, run.node);

	Node_Data *data = data_new(RUN_NODE);
	data->run.value = value;
	set_data(context, node, data);
	set_type(context, node, get_type(context, run.node));
}

static void process_string(Context *context, Node *node) {
	String_Node string = node->string;

	Expanded_String new_string = expand_escapes(string.value);

	Value type = context->temporary_context.wanted_type;
	bool invalid_type = false;
	if (type.value == NULL) invalid_type = true;
	else if (type.value->tag == POINTER_TYPE_VALUE && type.value->pointer_type.inner.value->tag == ARRAY_TYPE_VALUE && type.value->pointer_type.inner.value->array_type.inner.value->tag == BYTE_TYPE_VALUE) {}
	else if (type.value->tag == ARRAY_VIEW_TYPE_VALUE && type.value->array_view_type.inner.value->tag == BYTE_TYPE_VALUE) {}
	else invalid_type = true;

	if (invalid_type) {
		type = create_array_view_type(create_value(BYTE_TYPE_VALUE));
	}

	Node_Data *data = data_new(STRING_NODE);
	data->string.type = type;
	data->string.value = new_string.string;
	data->string.length = new_string.length;
	set_data(context, node, data);
	set_type(context, node, type);
}

static void process_struct_type(Context *context, Node *node) {
	Struct_Type_Node struct_type = node->struct_type;
	for (long int i = 0; i < arrlen(struct_type.members); i++) {
		process_node(context, struct_type.members[i].type);
	}

	Value value = create_value(TYPE_TYPE_VALUE);
	set_type(context, node, value);
}

static void process_structure(Context *context, Node *node) {
	Structure_Node structure = node->structure;

	Value wanted_type = context->temporary_context.wanted_type;

	Value result_type = wanted_type.value == NULL ? create_value(TUPLE_TYPE_VALUE) : wanted_type;
	for (long int i = 0; i < arrlen(structure.values); i++) {
		Value item_wanted_type = {};
		if (wanted_type.value != NULL) {
			switch (wanted_type.value->tag) {
				case STRUCT_TYPE_VALUE:
					// TODO: support specific item initialization
					item_wanted_type = wanted_type.value->struct_type.members[i];
					break;
				case TUPLE_TYPE_VALUE:
					item_wanted_type = wanted_type.value->tuple_type.members[i];
					break;
				case ARRAY_TYPE_VALUE:
					item_wanted_type = wanted_type.value->array_type.inner;
					break;
				case TAGGED_UNION_TYPE_VALUE:
					for (long int j = 0; j < arrlen(wanted_type.value->tagged_union_type.items); j++) {
						if (streq(wanted_type.value->tagged_union_type.items[j].identifier, structure.values[i].identifier)) {
							item_wanted_type = wanted_type.value->tagged_union_type.items[j].type;
							break;
						}
					}
					assert(item_wanted_type.value != NULL);
					break;
				case UNION_TYPE_VALUE:
					for (long int j = 0; j < arrlen(wanted_type.value->union_type.items); j++) {
						if (streq(wanted_type.value->union_type.items[j].identifier, structure.values[i].identifier)) {
							item_wanted_type = wanted_type.value->union_type.items[j].type;
							break;
						}
					}
					assert(item_wanted_type.value != NULL);
					break;
				default:
					assert(false);
			}
		}

		Temporary_Context temporary_context = { .wanted_type = item_wanted_type };
		process_node_context(context, temporary_context, structure.values[i].node);

		if (wanted_type.value == NULL) {
			arrpush(result_type.value->tuple_type.members, get_type(context, structure.values[i].node));
		}
	}

	Node_Data *data = data_new(STRUCTURE_NODE);
	data->structure.type = result_type;
	set_data(context, node, data);
	set_type(context, node, result_type);
}

static void process_structure_access(Context *context, Node *node) {
	Structure_Access_Node structure_access = node->structure_access;

	process_enforce_pointer_sometimes(context, structure_access.parent, structure_access.assign_value != NULL);

	Value raw_structure_type = get_type(context, structure_access.parent);

	Value structure_type = raw_structure_type;
	if (structure_type.value->tag == POINTER_TYPE_VALUE) {
		structure_type = structure_type.value->pointer_type.inner;
	}

	if (structure_type.value->tag != STRUCT_TYPE_VALUE
			&& structure_type.value->tag != TUPLE_TYPE_VALUE
			&& structure_type.value->tag != UNION_TYPE_VALUE
			&& structure_type.value->tag != ARRAY_VIEW_TYPE_VALUE) {
		handle_type_error(node, "Cannot perform structure access operation on %s", structure_type);
	}

	Value item_type = {};
	switch (structure_type.value->tag) {
		case STRUCT_TYPE_VALUE: {
			for (long int i = 0; i < arrlen(structure_type.value->struct_type.members); i++) {
				if (strcmp(structure_type.value->struct_type.node->struct_type.members[i].name, structure_access.name) == 0) {
					item_type = structure_type.value->struct_type.members[i];
					break;
				}
			}
			break;
		}
		case TUPLE_TYPE_VALUE: {
			for (long int i = 0; i < arrlen(structure_type.value->tuple_type.members); i++) {
				char name[21] = {};
				sprintf(name, "_%li", i);
				if (strcmp(name, structure_access.name) == 0) {
					item_type = structure_type.value->tuple_type.members[i];
					break;
				}
			}
			break;
		}
		case UNION_TYPE_VALUE: {
			for (long int i = 0; i < arrlen(structure_type.value->union_type.items); i++) {
				if (strcmp(structure_type.value->union_type.items[i].identifier, structure_access.name) == 0) {
					item_type = structure_type.value->union_type.items[i].type;
					break;
				}
			}
			break;
		}
		case ARRAY_VIEW_TYPE_VALUE: {
			if (strcmp("len", structure_access.name) == 0) {
				item_type = create_integer_type(false, context->codegen.default_integer_size);
			} else if (strcmp("ptr", structure_access.name) == 0) {
				item_type = create_pointer_type(create_array_type(structure_type.value->array_view_type.inner));
			}
			break;
		}
		default:
			assert(false);
	}

	if (item_type.value == NULL) {
		handle_semantic_error(node->location, "Item '%s' not found", structure_access.name);
	}

	Node_Data *data = data_new(STRUCTURE_ACCESS_NODE);
	data->structure_access.structure_type = structure_type;
	data->structure_access.want_pointer = context->temporary_context.want_pointer;
	data->structure_access.item_type = item_type;
	data->structure_access.pointer_access = raw_structure_type.value->tag == POINTER_TYPE_VALUE;

	if (structure_access.assign_value != NULL) {
		Temporary_Context temporary_context = { .wanted_type = item_type };
		process_node_context(context, temporary_context, structure_access.assign_value);

		Value value_type = get_type(context, structure_access.assign_value);
		if (!type_assignable(item_type.value, value_type.value)) {
			handle_expected_type_error(node, item_type, value_type);
		}
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

static void process_switch(Context *context, Node *node) {
	Switch_Node switch_ = node->switch_;

	process_node(context, switch_.condition);

	Node_Data *data = data_new(SWITCH_NODE);

	bool saved_returned = context->returned;

	Value type = get_type(context, switch_.condition);
	if (type.value->tag != ENUM_TYPE_VALUE && type.value->tag != TAGGED_UNION_TYPE_VALUE) {
		handle_type_error(node, "Expected enum, but got %s", type);
	}

	Value_Data *enum_type = type.value;
	if (enum_type->tag == TAGGED_UNION_TYPE_VALUE) {
		enum_type = enum_type->tagged_union_type.enum_;
	}

	for (long int i = 0; i < arrlen(switch_.cases); i++) {
		Switch_Case switch_case = switch_.cases[i];

		if (switch_case.value != NULL) {
			Temporary_Context temporary_context = { .wanted_type = (Value) { .value = enum_type } };
			process_node_context(context, temporary_context, switch_case.value);
		}
	}

	if (switch_.static_) {
		Value_Data *switched_value = evaluate(context, switch_.condition).value;
		Value_Data *switched_enum_value = evaluate(context, switch_.condition).value;
		if (switched_enum_value->tag == TAGGED_UNION_VALUE) {
			switched_enum_value = switched_enum_value->tagged_union.tag;
		}

		data->switch_.static_case = -1;
		for (long int i = 0; i < arrlen(switch_.cases); i++) {
			Switch_Case switch_case = switch_.cases[i];
			if (switch_case.value != NULL) {
				Value value = evaluate(context, switch_case.value);

				if (value_equal(value.value, switched_enum_value)) {
					data->switch_.static_case = i;
					break;
				}
			} else {
				data->switch_.static_case = i;
			}
		}

		if (data->switch_.static_case >= 0) {
			Switch_Case switch_case = switch_.cases[data->switch_.static_case];
			if (switched_value->tag == TAGGED_UNION_VALUE && switch_case.binding != NULL) {
				Typed_Value typed_value = {
					.value = (Value) { .value = switched_value->tagged_union.data },
					.type = type.value->tagged_union_type.items[switched_enum_value->enum_.value].type
				};
				shput(arrlast(context->scopes).static_bindings, switch_case.binding, typed_value);
			}

			Temporary_Context temporary_context = { .wanted_type = context->temporary_context.wanted_type };
			process_node_context(context, temporary_context, switch_case.body);
		}
	} else {
		Value switch_type = {};
		bool set_switch_type = false;
		long int case_count = 0;
		bool else_case = false;
		for (long int i = 0; i < arrlen(switch_.cases); i++) {
			Switch_Case switch_case = switch_.cases[i];

			Value binding_type = (Value) {};
			if (switch_case.value != NULL) {
				Temporary_Context temporary_context = { .wanted_type = (Value) { .value = enum_type } };
				process_node_context(context, temporary_context, switch_case.value);
				case_count++;

				if (type.value->tag == TAGGED_UNION_TYPE_VALUE) {
					Value check_value = evaluate(context, switch_case.value);
					binding_type = type.value->tagged_union_type.items[check_value.value->enum_.value].type;
				}
			} else {
				else_case = true;
			}

			arrpush(context->scopes, (Scope) { .node = node });

			if (switch_case.binding != NULL) {
				Binding binding = {
					.type = binding_type,
					.index = 0
				};
				shput(arrlast(context->scopes).bindings, switch_case.binding, binding);
			}

			bool saved_previous_returned = context->returned;
			context->returned = NULL;
			Temporary_Context temporary_context = { .wanted_type = context->temporary_context.wanted_type };
			process_node_context(context, temporary_context, switch_case.body);

			bool saved_case_returned = context->returned;

			context->returned = saved_returned;

			(void) arrpop(context->scopes);

			arrpush(data->switch_.cases_returned, saved_case_returned);
			if ((saved_previous_returned || i == 0) && saved_case_returned) {
				context->returned = true;
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
					handle_mismatched_type_error(node, switch_type, case_type);
				}

				data->switch_.type = switch_type;
				set_type(context, node, switch_type);
			}
		}

		data->switch_.returned = context->returned;

		if (case_count < arrlen(enum_type->enum_type.items) && !else_case) {
			context->returned = saved_returned;
			if (switch_type.value != NULL) {
				handle_semantic_error(node->location, "Expected else case");
			}
		}
	}

	set_data(context, node, data);
}

static void process_tagged_union_type(Context *context, Node *node) {
	Tagged_Union_Type_Node tagged_union_type = node->tagged_union_type;
	for (long int i = 0; i < arrlen(tagged_union_type.members); i++) {
		process_node(context, tagged_union_type.members[i].type);
	}

	set_type(context, node, create_value(TYPE_TYPE_VALUE));
}

static void process_union_type(Context *context, Node *node) {
	Union_Type_Node union_type = node->union_type;
	for (long int i = 0; i < arrlen(union_type.members); i++) {
		process_node(context, union_type.members[i].type);
	}

	set_type(context, node, create_value(TYPE_TYPE_VALUE));
}

static void process_variable(Context *context, Node *node) {
	Variable_Node variable = node->variable;

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

			if (value_type.value == NULL) {
				handle_semantic_error(node->location, "Expected value");
			}
		} else if (!type_assignable(type.value, value_type.value)) {
			handle_expected_type_error(node, type, value_type);
		}
	} else if (type.value == NULL) {
		handle_semantic_error(node->location, "Expected value");
	}

	Node_Data *data = data_new(VARIABLE_NODE);

	Variable_Definition variable_definition = {
		.node = node,
		.node_data = data
	};
	if (variable.static_) {
		shput(arrlast(context->scopes).static_variables, variable.name, variable_definition);
		Value value = evaluate(context, variable.value);
		hmput(context->static_variables, data, value);
	} else {
		shput(arrlast(context->scopes).variables, variable.name, variable_definition);
	}

	data->variable.type = type;
	set_data(context, node, data);
}

static void process_while(Context *context, Node *node) {
	While_Node while_ = node->while_;

	Node_Data *data = data_new(WHILE_NODE);
	data->while_.wanted_type = context->temporary_context.wanted_type;
	data->while_.type = (Value) {};
	set_data(context, node, data);

	arrpush(context->scopes, (Scope) { .node = node });
	process_node(context, while_.condition);

	process_node(context, while_.body);

	Value while_type = data->while_.type;

	if (while_.else_body != NULL) {
		Temporary_Context temporary_context = { .wanted_type = context->temporary_context.wanted_type };
		process_node_context(context, temporary_context, while_.else_body);

		Value type = get_type(context, while_.else_body);
		if (data->while_.has_type) {
			if (type.value != NULL) {
				if (while_type.value == NULL) {
					handle_semantic_error(node->location, "Expected no value in else");
				} else if (!value_equal(type.value, while_type.value)) {
					handle_mismatched_type_error(node, type, while_type);
				}
			}

			if (while_type.value != NULL) {
				if (type.value == NULL) {
					handle_semantic_error(node->location, "Expected value in else");
				}
			}
		}
	}

	(void) arrpop(context->scopes);

	if (data->while_.has_type && while_.else_body != NULL) {
		set_type(context, node, while_type);
	}
}

void process_node_context(Context *context, Temporary_Context temporary_context, Node *node) {
	Value type = get_type(context, node);
	if (type.value != NULL) {
		return;
	}

	Temporary_Context saved_temporary_context = context->temporary_context;
	context->temporary_context = temporary_context;

	switch (node->kind) {
		case ARRAY_ACCESS_NODE:
			process_array_access(context, node);
			break;
		case ARRAY_TYPE_NODE:
			process_array_type(context, node);
			break;
		case ARRAY_VIEW_TYPE_NODE:
			process_array_view_type(context, node);
			break;
		case BINARY_OP_NODE:
			process_binary_op(context, node);
			break;
		case BLOCK_NODE:
			process_block(context, node);
			break;
		case BOOLEAN_NODE:
			process_boolean(context, node);
			break;
		case BREAK_NODE:
			process_break(context, node);
			break;
		case CAST_NODE:
			process_cast(context, node);
			break;
		case CALL_NODE:
			process_call(context, node);
			break;
		case CALL_METHOD_NODE:
			process_call_method(context, node);
			break;
		case CHARACTER_NODE:
			process_character(context, node);
			break;
		case CATCH_NODE:
			process_catch(context, node);
			break;
		case DEFINE_NODE:
			process_define(context, node);
			break;
		case DEOPTIONAL_NODE:
			process_deoptional(context, node);
			break;
		case DEREFERENCE_NODE:
			process_dereference(context, node);
			break;
		case ENUM_TYPE_NODE:
			process_enum_type(context, node);
			break;
		case EXTERN_NODE:
			process_extern(context, node);
			break;
		case FOR_NODE:
			process_for(context, node);
			break;
		case FUNCTION_NODE:
			process_function(context, node, false);
			break;
		case FUNCTION_TYPE_NODE:
			process_function_type(context, node, false);
			break;
		case IDENTIFIER_NODE:
			process_identifier(context, node);
			break;
		case IF_NODE:
			process_if(context, node);
			break;
		case INTERNAL_NODE:
			process_internal(context, node);
			break;
		case IS_NODE:
			process_is(context, node);
			break;
		case MODULE_NODE:
			process_module(context, node);
			break;
		case MODULE_TYPE_NODE:
			process_module_type(context, node);
			break;
		case NULL_NODE:
			process_null(context, node);
			break;
		case NUMBER_NODE:
			process_number(context, node);
			break;
		case OPTIONAL_NODE:
			process_optional(context, node);
			break;
		case POINTER_NODE:
			process_pointer(context, node);
			break;
		case RANGE_NODE:
			process_range(context, node);
			break;
		case REFERENCE_NODE:
			process_reference(context, node);
			break;
		case RESULT_NODE:
			process_result(context, node);
			break;
		case RETURN_NODE:
			process_return(context, node);
			break;
		case RUN_NODE:
			process_run(context, node);
			break;
		case STRING_NODE:
			process_string(context, node);
			break;
		case STRUCT_TYPE_NODE:
			process_struct_type(context, node);
			break;
		case STRUCTURE_NODE:
			process_structure(context, node);
			break;
		case STRUCTURE_ACCESS_NODE:
			process_structure_access(context, node);
			break;
		case SWITCH_NODE:
			process_switch(context, node);
			break;
		case TAGGED_UNION_TYPE_NODE:
			process_tagged_union_type(context, node);
			break;
		case UNION_TYPE_NODE:
			process_union_type(context, node);
			break;
		case VARIABLE_NODE:
			process_variable(context, node);
			break;
		case WHILE_NODE:
			process_while(context, node);
			break;
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
