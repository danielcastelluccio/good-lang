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

Node_Data *process_node_context_internal(Context *context, Temporary_Context temporary_context, Node *node);
Node_Data *process_node_context(Context *context, Temporary_Context temporary_context, Node *node) {
	// arrpush(context->datas_new, data_create(context, node));
	Node_Data *result = process_node_context_internal(context, temporary_context, node);
	context->node_index++;
	return result;
}

Node_Data *process_node(Context *context, Node *node) {
	return process_node_context(context, (Temporary_Context) {}, node);
}

static Node_Data *process_node_with_scopes(Context *context, Node *node, Scope *scopes) {
	Scope *saved_scopes = context->scopes;
	size_t saved_static_id = context->static_id;
	if (scopes != NULL) context->scopes = scopes;
	arrpush(context->scopes, (Scope) { .node = node });
	context->static_id = 0;

	Node_Data *data = process_node(context, node);

	context->static_id = saved_static_id;
	(void) arrpop(context->scopes);
	if (scopes != NULL) context->scopes = saved_scopes;

	return data;
}

static Node *find_define(Block_Node block, String_View identifier) {
	for (long int i = 0; i < arrlen(block.statements); i++) {
		Node *statement = block.statements[i];
		if (statement->kind == DEFINE_NODE && sv_eq(statement->define.identifier, identifier)) {
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
			bool var;
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

static Lookup_Result lookup(Context *context, String_View identifier) {
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

		Scope_Identifier scope_identifier = hmget(scope->identifiers, sv_hash(identifier));
		switch (scope_identifier.tag) {
			case SCOPE_VARIABLE:
				return (Lookup_Result) { .tag = LOOKUP_RESULT_VARIABLE, .variable = scope_identifier.variable.node, .type = scope_identifier.variable.node_data->variable.type };
			case SCOPE_BINDING:
				return (Lookup_Result) { .tag = LOOKUP_RESULT_BINDING, .binding = { .node = scope->node, .index = scope_identifier.binding.index }, .type = scope_identifier.binding.type };
			case SCOPE_STATIC_BINDING:
				return (Lookup_Result) { .tag = LOOKUP_RESULT_STATIC_BINDING, .static_binding = scope_identifier.static_binding.value, .type = scope_identifier.static_binding.type };
			case SCOPE_STATIC_VARIABLE:
				return (Lookup_Result) { .tag = LOOKUP_RESULT_STATIC_VARIABLE, .static_variable = { .node = scope_identifier.static_variable.node, .node_data = scope_identifier.static_variable.node_data }, .type = scope_identifier.static_variable.node_data->variable.type };
			case SCOPE_DEFINE:
				return (Lookup_Result) { .tag = LOOKUP_RESULT_DEFINE, .define = { .node = scope_identifier.define, .scope = scope } };
			case SCOPE_INVALID:
				break;
		}

		if (!found_function && scope->node->kind == FUNCTION_NODE) {
			found_function = true;

			Node *current_function_type = scope->node->function.function_type;
			long int j = 0;
			for (long int i = 0; i < arrlen(current_function_type->function_type.arguments); i++) {
				if (!current_function_type->function_type.arguments[i].static_) {
					if (sv_eq(current_function_type->function_type.arguments[i].identifier, identifier)) {
						return (Lookup_Result) { .tag = LOOKUP_RESULT_ARGUMENT, .argument = j, .type = scope->node_type.value->function_type.arguments[i].type };
					}
					j++;
				}
			}
		}
	}

	return (Lookup_Result) { .tag = LOOKUP_RESULT_FAIL };
}

#define handle_semantic_error(/* Context * */ context, /* Source_Location */ location, /* char * */ fmt, ...) { \
	printf("%s:%u:%u: " fmt "\n", context->data->source_files[location.path_ref], location.row, location.column __VA_OPT__(,) __VA_ARGS__); \
	exit(1); \
}

static int print_type_node(Node *type_node, bool pointer, char *buffer) {
	char *buffer_start = buffer;
	if (pointer) {
		buffer += sprintf(buffer, "^");
	}

	switch (type_node->kind) {
		case IDENTIFIER_NODE: {
			buffer += sprintf(buffer, "%.*s", (int) type_node->identifier.value.len, type_node->identifier.value.ptr);
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
		case INTERNAL_NODE: {
			switch (type_node->internal.kind) {
				case INTERNAL_UINT: {
					buffer += sprintf(buffer, "uint");
					break;
				}
				case INTERNAL_UINT8: {
					buffer += sprintf(buffer, "uint8");
					break;
				}
				case INTERNAL_SINT: {
					buffer += sprintf(buffer, "sint");
					break;
				}
				case INTERNAL_BOOL: {
					buffer += sprintf(buffer, "bool");
					break;
				}
				case INTERNAL_BYTE: {
					buffer += sprintf(buffer, "byte");
					break;
				}
				default:
					assert(false);
			}
			break;
		}
		default:
			assert(false);
	}

	return buffer - buffer_start;
}

static int print_type(Value type, char *buffer) {
	if (type.node != NULL && (type.node->kind == CALL_NODE || type.node->kind == IDENTIFIER_NODE || type.node->kind == INTERNAL_NODE)) {
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

			if (type.value->array_type.sentinel.value != NULL) {
				buffer += sprintf(buffer, ";");
				buffer += print_type(type.value->array_type.sentinel, buffer);
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
				String_View name = type.value->struct_type.node->struct_type.members[i].name;
				buffer += sprintf(buffer, "%.*s:", (int) name.len, name.ptr);
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
				String_View name = type.value->union_type.items[i].identifier;
				buffer += sprintf(buffer, "%.*s:", (int) name.len, name.ptr);
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
				String_View name = type.value->tagged_union_type.items[i].identifier;
				buffer += sprintf(buffer, "%.*s:", (int) name.len, name.ptr);
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
				String_View item = type.value->enum_type.items[i];
				buffer += sprintf(buffer, "%.*s", (int) item.len, item.ptr);
				if (i < arrlen(type.value->struct_type.members) - 1) {
					buffer += sprintf(buffer, ",");
				}
			}
			buffer += sprintf(buffer, "}");
			break;
		}
		case FUNCTION_TYPE_VALUE: {
			bool found_inferred = false;
			bool found_noninferred = false;
			buffer += sprintf(buffer, "fn");
			for (long int i = 0; i < arrlen(type.value->function_type.arguments); i++) {
				Function_Argument_Value arg = type.value->function_type.arguments[i];

				if (arg.inferred) {
					if (!found_inferred) {
						buffer += sprintf(buffer, "[");
					}
					found_inferred = true;

					buffer += sprintf(buffer, "%.*s", (int) arg.identifier.len, arg.identifier.ptr);
				} else {
					if (found_inferred) {
						buffer += sprintf(buffer, "]");
					}
					if (!found_noninferred) {
						buffer += sprintf(buffer, "(");
					}

					found_noninferred = true;

					if (arg.static_) {
						buffer += sprintf(buffer, "static ");
					}

					buffer += sprintf(buffer, "%.*s:", (int) arg.identifier.len, arg.identifier.ptr);
					buffer += print_type(arg.type, buffer);

					if (i < arrlen(type.value->function_type.arguments) - 1) {
						buffer += sprintf(buffer, ",");
					}
				}
			}

			if (found_inferred && !found_noninferred) {
				buffer += sprintf(buffer, "](");
			} else if (!found_noninferred) {
				buffer += sprintf(buffer, "(");
			}

			buffer += sprintf(buffer, ")");
			break;
		}
		case FUNCTION_TYPE_STUB_VALUE: {
			buffer += sprintf(buffer, "fn[]()");
			break;
		}
		case GLOBAL_TYPE_VALUE: {
			buffer += sprintf(buffer, "global:");
			buffer += print_type(type.value->global_type.type, buffer);
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
		case BOOLEAN_TYPE_VALUE: {
			buffer += sprintf(buffer, "bool");
			break;
		}
		case TYPE_TYPE_VALUE: {
			buffer += sprintf(buffer, "type");
			break;
		}
		case MODULE_TYPE_VALUE: {
			buffer += sprintf(buffer, "mod");
			break;
		}
		case INTEGER_VALUE: {
			buffer += sprintf(buffer, "%li", type.value->integer.value);
			break;
		}
		case BYTE_VALUE: {
			buffer += sprintf(buffer, "%i", type.value->byte.value);
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

static void handle_expected_type_error(Context *context, Node *node, Value wanted, Value given) {
	char wanted_string[64] = {};
	print_type_outer(wanted, wanted_string);
	char given_string[64] = {};
	print_type_outer(given, given_string);
	handle_semantic_error(context, node->location, "Expected %s, but got %s", wanted_string, given_string);
}

static void handle_mismatched_type_error(Context *context, Node *node, Value type1, Value type2) {
	char type1_string[64] = {};
	print_type_outer(type1, type1_string);
	char type2_string[64] = {};
	print_type_outer(type2, type2_string);
	handle_semantic_error(context, node->location, "Mismatched types %s and %s", type1_string, type2_string);
}

#define handle_type_error(/* Node * */ node, /* char * */ message, /* Value */ type) { \
	char type_string[64] = {}; \
	print_type_outer(type, type_string); \
	handle_semantic_error(context, node->location, message, type_string); \
}

#define handle_2type_error(/* Node * */ node, /* char * */ message, /* Value */ type1, /* Value */ type2) { \
	char type1_string[64] = {}; \
	print_type_outer(type1, type1_string); \
	char type2_string[64] = {}; \
	print_type_outer(type2, type2_string); \
	handle_semantic_error(context, node->location, message, type1_string, type2_string); \
}

static String_View expand_escapes(String_View sv, Value type) {
	size_t original_length = sv.len;
	char *new_string = malloc(original_length + 1);
	memset(new_string, 0, original_length + 1);

	size_t original_index = 0;
	size_t new_index = 0;
	while (original_index < original_length) {
		if (sv.ptr[original_index] == '\\') {
			switch (sv.ptr[original_index + 1]) {
				case 'n':
					new_string[new_index] = '\n';
					break;
				case '0':
					new_string[new_index] = '\0';
					break;
				case '"':
					new_string[new_index] = '"';
					break;
				default:
					assert(false);
			}
			new_index += 1;
			original_index += 2;
		} else {
			new_string[new_index] = sv.ptr[original_index];
			original_index += 1;
			new_index += 1;
		}
	}

	if (type.value != NULL && type.value->tag == POINTER_TYPE_VALUE && type.value->pointer_type.inner.value->tag == ARRAY_TYPE_VALUE && type.value->pointer_type.inner.value->array_type.sentinel.value != NULL) {
		new_string[new_index] = type.value->pointer_type.inner.value->array_type.sentinel.value->byte.value;
		new_index++;
	}

	return (String_View) { .ptr = new_string, .len = new_index };
}

typedef struct { size_t key; Typed_Value value; } *Pattern_Match_Result;
static bool pattern_match(Node *node, Value value, Context *context, String_View *inferred_arguments, Pattern_Match_Result *match_result) {
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
				process_node(context, node->call.function);
				Node *function_node = evaluate(context, node->call.function).value->function.node;

				if (function_node == value.value->struct_type.inherited_node) {
					for (long int i = 0; i < arrlen(node->call.arguments); i++) {
						if (!pattern_match(node->call.arguments[i], value.value->struct_type.inherited_arguments[i], context, inferred_arguments, match_result)) {
							return false;
						}
					}
				}
			}
			break;
		}
		case IDENTIFIER_NODE: {
			for (long int i = 0; i < arrlen(inferred_arguments); i++) {
				if (sv_eq(inferred_arguments[i], node->identifier.value)) {
					Typed_Value previous_value = hmget(*match_result, sv_hash(inferred_arguments[i]));
					if (previous_value.value.value != NULL) {
						if (!value_equal(previous_value.value.value, value.value)) {
							return false;
						}
					}

					Typed_Value typed_value = {
						.value = value,
						.type = create_value(TYPE_TYPE_VALUE)
					};
					hmput(*match_result, sv_hash(inferred_arguments[i]), typed_value);
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

static bool uses_inferred_arguments(Node *node, String_View *inferred_arguments) {
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
				if (sv_eq(inferred_arguments[i], node->identifier.value)) {
					return true;
				}
			}
			return false;
		}
		default:
			return false;
	}
}

static void process_initial_argument_types(Context *context, Value_Data *function_value, Node **arguments) {
	Node *function_type_node = NULL;
	switch (function_value->tag) {
		case FUNCTION_STUB_VALUE:
			function_type_node = function_value->function_stub.node->function.function_type;
			break;
		case FUNCTION_VALUE:
			return;
		default:
			assert(false);
	}

	String_View *inferred_arguments = NULL;
	if (function_type_node != NULL) {
		Function_Argument *function_node_arguments = function_type_node->function_type.arguments;

		for (long int k = 0; k < arrlen(function_node_arguments); k++) {
			if (function_node_arguments[k].inferred) {
				arrpush(inferred_arguments, function_node_arguments[k].identifier);
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
}

static Custom_Operator_Function find_custom_operator(Context *context, Value type, String_View operator_id) {
	if (type.value->tag == STRUCT_TYPE_VALUE) {
		Node **operators = type.value->struct_type.node->struct_type.operators;
		for (long int i = 0; i < arrlen(operators); i++) {
			Node *operator = operators[i];
			if (sv_eq(operator->operator.identifier, operator_id)) {
				Scope *scopes = NULL;
				for (long i = 0; i < arrlen(type.value->struct_type.scopes); i++) {
					arrpush(scopes, type.value->struct_type.scopes[i]);
				}

				Operator_Data data = process_node_with_scopes(context, operator, scopes)->operator;
	
				return (Custom_Operator_Function) {
					.function = data.typed_value.value.value,
					.function_type = data.typed_value.type,
				};
			}
		}
	}

	return (Custom_Operator_Function) {};
}

static Node_Data *process_function(Context *context, Node *node, bool given_static_arguments);

static Value process_call_generic(Context *context, Node *node, Value function_value, Value *function_type, Node **call_arguments) {
	typedef struct {
		String_View identifier;
		Typed_Value value;
	} Static_Argument_Value;

	String_View *inferred_arguments = NULL;

	if (function_value.value != NULL && function_value.value->tag == FUNCTION_STUB_VALUE) {
		Node *function_type_node = function_value.value->function_stub.node->function.function_type;

		size_t saved_static_id = context->static_id;
		size_t new_static_id = ++context->static_id_counter;

		bool pattern_match_fail = false;
		bool compile_only_parent = context->compile_only;
		Static_Argument_Value *static_arguments = NULL;

		Function_Argument *function_arguments = function_type_node->function_type.arguments;

		for (long int k = 0; k < arrlen(function_arguments); k++) {
			if (function_arguments[k].inferred) {
				arrpush(inferred_arguments, function_arguments[k].identifier);

				if (function_arguments[k].default_value != NULL) {
					process_node(context, function_arguments[k].default_value);
				}
			}
		}

		assert(arrlen(call_arguments) + arrlen(inferred_arguments) == arrlen(function_arguments));

		Pattern_Match_Result result = NULL;
		for (long int call_arg_index = 0; call_arg_index < arrlen(call_arguments); call_arg_index++) {
			long int function_arg_index = call_arg_index + arrlen(inferred_arguments);
			if (function_arg_index >= arrlen(function_arguments)) break;
			if (!function_arguments[function_arg_index].static_) continue;

			Value wanted_type = {};
			if (!function_arguments[function_arg_index].inferred) {
				context->static_id = new_static_id;
				process_node(context, function_arguments[function_arg_index].type);
				wanted_type = evaluate(context, function_arguments[function_arg_index].type);
				context->static_id = saved_static_id;
			}

			Temporary_Context temporary_context = { .wanted_type = wanted_type };
			Value type = process_node_context(context, temporary_context, call_arguments[call_arg_index])->type;

			Static_Argument_Value static_argument = {
				.identifier = function_arguments[function_arg_index].identifier,
				.value = { .value = evaluate(context, call_arguments[call_arg_index]), .type = type }
			};
			arrpush(static_arguments, static_argument);
		}

		for (long int k = 0; k < arrlen(function_arguments); k++) {
			if (function_arguments[k].inferred || function_arguments[k].static_) continue;

			Node *argument = function_arguments[k].type;
			Value argument_type = get_type(context, call_arguments[k - arrlen(inferred_arguments)]);
			if (argument_type.value != NULL) {
				if (!pattern_match(argument, argument_type, context, inferred_arguments, &result)) {
					pattern_match_fail = true;
				}
			}
		}

		if (function_type_node->function_type.return_ != NULL && context->temporary_context.wanted_type.value != NULL) {
			if (!pattern_match(function_type_node->function_type.return_, context->temporary_context.wanted_type, context, inferred_arguments, &result)) {
				pattern_match_fail = true;
			}
		}

		for (long int k = 0; k < arrlen(function_arguments); k++) {
			if (!function_arguments[k].inferred) break;

			Typed_Value typed_value = hmget(result, sv_hash(function_arguments[k].identifier));
			if (typed_value.value.value == NULL && function_arguments[k].default_value != NULL) {
				typed_value.type = get_type(context, function_arguments[k].default_value);
				typed_value.value = evaluate(context, function_arguments[k].default_value);
			}

			if (typed_value.value.value != NULL) {
				Static_Argument_Value static_argument = {
					.identifier = function_arguments[k].identifier,
					.value = typed_value
				};

				arrpush(static_arguments, static_argument);
			} else {
				pattern_match_fail = true;
			}
		}

		if (pattern_match_fail) {
			handle_semantic_error(context, node->location, "Pattern matching failed");
		}

		struct { size_t key; Typed_Value value; } *static_arguments_map = NULL;;
		for (long int i = 0; i < arrlen(static_arguments); i++) {
			hmput(static_arguments_map, sv_hash(static_arguments[i].identifier), static_arguments[i].value);
		}

		Value *static_argument_values = NULL;
		for (long int i = 0; i < arrlen(function_type_node->function_type.arguments); i++) {
			Function_Argument argument = function_type_node->function_type.arguments[i];
			if (argument.static_) {
				arrpush(static_argument_values, hmget(static_arguments_map, sv_hash(argument.identifier)).value);
			}
		}

		size_t saved_saved_static_id = context->static_id;
		context->static_id = 0;
		Node_Data *function_data = get_data(context, function_value.value->function_stub.node);
		if (function_data == NULL) {
			function_data = data_create(context, function_value.value->function_stub.node);
		}
		context->static_id = saved_saved_static_id;

		bool found_match = false;

		Static_Argument_Variation *function_values = function_data->function.function_values;
		for (long int i = 0; i < arrlen(function_values); i++) {
			bool match = arrlen(function_values[i].static_arguments) == arrlen(static_argument_values);
			if (match) {
				for (long int j = 0; j < arrlen(function_values[i].static_arguments); j++) {
					if (!value_equal(function_values[i].static_arguments[j].value, static_argument_values[j].value)) {
						match = false;
						break;
					}
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

			reset_node(context, function_value.value->function_stub.node);

			Scope *saved_scopes = context->scopes;
			context->scopes = NULL;
			for (long int i = 0; i < arrlen(function_value.value->function_stub.scopes); i++) {
				arrpush(context->scopes, function_value.value->function_stub.scopes[i]);
			}

			arrpush(context->scopes, (Scope) { .node = node });

			for (long int i = 0; i < arrlen(static_arguments); i++) {
				Scope_Identifier scope_identifier = {
					.tag = SCOPE_STATIC_BINDING,
					.static_binding = static_arguments[i].value
				};
				hmput(arrlast(context->scopes).identifiers, sv_hash(static_arguments[i].identifier), scope_identifier);
			}

			assert(function_value.value->tag == FUNCTION_STUB_VALUE);

			*function_type = process_function(context, function_value.value->function_stub.node, true)->type;
			function_value = evaluate(context, function_value.value->function_stub.node);

			context->static_id = saved_static_id;

			Static_Argument_Variation static_argument_variation = {
				.static_arguments = static_argument_values,
				.value = { .value = function_value, .type = *function_type }
			};
			arrpush(function_data->function.function_values, static_argument_variation);

			(void) arrpop(context->scopes);
			context->scopes = saved_scopes;
		}

		context->compile_only = compile_only_parent;
	}

	if (function_type->value->tag != FUNCTION_TYPE_VALUE) {
		handle_type_error(node, "Expected function, but got %s", *function_type);
	}

	assert(function_type->value->tag == FUNCTION_TYPE_VALUE);
	long int real_argument_count = 0;
	for (long int i = 0; i < arrlen(function_type->value->function_type.arguments); i++) {
		if (!function_type->value->function_type.arguments[i].inferred) real_argument_count++;
	}
	if (arrlen(call_arguments) != real_argument_count && !function_type->value->function_type.variadic) {
		handle_semantic_error(context, node->location, "Expected %li arguments, but got %li arguments", real_argument_count, arrlen(call_arguments));
	}

	Function_Argument_Value *function_type_arguments = function_type->value->function_type.arguments;
	for (long int argument_index = 0; argument_index < arrlen(call_arguments); argument_index++) {
		long int function_argument_index = argument_index + arrlen(inferred_arguments);
		if (function_argument_index < arrlen(function_type_arguments) && function_type_arguments[function_argument_index].static_) continue;

		Value wanted_type = {};
		if (function_argument_index < arrlen(function_type_arguments) || !function_type->value->function_type.variadic) {
			wanted_type = function_type_arguments[function_argument_index].type;
		}

		Temporary_Context temporary_context = { .wanted_type = wanted_type };
		Value type = process_node_context(context, temporary_context, call_arguments[argument_index])->type;

		if (wanted_type.value != NULL && !type_assignable(wanted_type.value, type.value)) {
			handle_expected_type_error(context, node, wanted_type, type);
		}
		function_argument_index++;
	}

	return function_value;
}

static Value process_enforce_pointer(Context *context, Node *node, Value structure_type) {
	if (structure_type.value->tag != POINTER_TYPE_VALUE) {
		reset_node(context, node);

		Temporary_Context temporary_context = { .want_pointer = true };
		structure_type = process_node_context(context, temporary_context, node)->type;
	}

	return structure_type;
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

static bool is_trivially_evaluatable(Node *node, Node_Data *data) {
	switch (node->kind) {
		case FUNCTION_NODE: {
			return true;
		}
		case IDENTIFIER_NODE: {
			return data->identifier.kind == IDENTIFIER_VALUE;
		}
		default:
			return false;
	}
}

static Node_Data *process_array_access(Context *context, Node *node) {
	Array_Access_Node array_access = node->array_access;

	Value raw_array_type = process_node(context, array_access.parent)->type;
	if (array_access.assign_value != NULL || (context->temporary_context.want_pointer && raw_array_type.value->tag == ARRAY_TYPE_VALUE)) {
		raw_array_type = process_enforce_pointer(context, array_access.parent, raw_array_type);
	}

	Temporary_Context temporary_context = { .wanted_type = create_integer_type(false, context->codegen.default_integer_size) };
	process_node_context(context, temporary_context, array_access.index);

	Value array_type = raw_array_type;
	if (array_type.value->tag == POINTER_TYPE_VALUE) {
		array_type = array_type.value->pointer_type.inner;
	}

	Custom_Operator_Function custom_operator_function = find_custom_operator(context, array_type.value->pointer_type.inner, cstr_to_sv("[]"));

	if (custom_operator_function.function == NULL
			&& array_type.value->tag != ARRAY_TYPE_VALUE
			&& array_type.value->tag != ARRAY_VIEW_TYPE_VALUE) {
		handle_type_error(node, "Cannot perform array access operation on %s", array_type);
	}

	Value item_type = {};
	if (custom_operator_function.function == NULL) {
		switch (array_type.value->tag) {
			case ARRAY_TYPE_VALUE: {
				item_type = array_type.value->array_type.inner;
				break;
			}
			case ARRAY_VIEW_TYPE_VALUE: {
				item_type = array_type.value->array_view_type.inner;
				break;
			}
			default:
				assert(false);
		}
	} else {
		Node **arguments = NULL;
		arrpush(arguments, array_access.parent);
		arrpush(arguments, array_access.index);

		process_initial_argument_types(context, custom_operator_function.function, arguments);
		custom_operator_function.function = process_call_generic(context, node, (Value) { .value = custom_operator_function.function }, &custom_operator_function.function_type, arguments).value;

		item_type = custom_operator_function.function_type.value->function_type.return_type.value->pointer_type.inner;
	}

	Node_Data *data = context->temporary_context.data;
	data->array_access.array_type = array_type;
	data->array_access.want_pointer = context->temporary_context.want_pointer;
	data->array_access.custom_operator_function = custom_operator_function;
	data->array_access.item_type = item_type;
	data->array_access.pointer_access = raw_array_type.value->tag == POINTER_TYPE_VALUE;

	if (array_access.assign_value != NULL) {
		Temporary_Context temporary_context = { .wanted_type = item_type };
		Value value_type = process_node_context(context, temporary_context, array_access.assign_value)->type;

		if (!type_assignable(item_type.value, value_type.value)) {
			handle_expected_type_error(context, node, item_type, value_type);
		}
	} else {
		if (data->array_access.want_pointer) {
			Value_Data *pointer_item_type = value_new(POINTER_TYPE_VALUE);
			pointer_item_type->pointer_type.inner = item_type;
			item_type = (Value) { .value = pointer_item_type };
		}

		data->type = item_type;
	}

	return data;
}

static Node_Data *process_array_type(Context *context, Node *node) {
	Array_Type_Node array_type = node->array_type;
	process_node(context, array_type.inner);
	process_node(context, array_type.size);
	if (array_type.sentinel != NULL) {
		Value byte_type_value = create_value(BYTE_TYPE_VALUE);
		Temporary_Context temporary_context = { .wanted_type = byte_type_value };
		Value sentinel_type = process_node_context(context, temporary_context, array_type.sentinel)->type;

		if (!value_equal(byte_type_value.value, sentinel_type.value)) {
			handle_mismatched_type_error(context, node, byte_type_value, sentinel_type);
		}

	}

	Node_Data *data = context->temporary_context.data;
	data->type = create_value(TYPE_TYPE_VALUE);
	return data;
}

static Node_Data *process_array_view_type(Context *context, Node *node) {
	Array_View_Type_Node array_view_type = node->array_view_type;
	process_node(context, array_view_type.inner);

	Node_Data *data = context->temporary_context.data;
	data->type = create_value(TYPE_TYPE_VALUE);
	return data;
}

static bool can_compare(Value_Data *type) {
	if (type->tag == ENUM_TYPE_VALUE) return true;
	if (type->tag == INTEGER_TYPE_VALUE) return true;
	if (type->tag == BYTE_TYPE_VALUE) return true;
	if (type->tag == FLOAT_TYPE_VALUE) return true;
	if (type->tag == BYTE_TYPE_VALUE) return true;
	if (type->tag == ARRAY_VIEW_TYPE_VALUE) return true;
	if (type->tag == POINTER_TYPE_VALUE) return true;

	return false;
}

static Node_Data *process_binary_op(Context *context, Node *node) {
	Binary_Op_Node binary_operator = node->binary_op;

	Value left_type;
	Value right_type;
	if (binary_operator.left->kind == NUMBER_NODE) {
		right_type = process_node(context, binary_operator.right)->type;
		Temporary_Context temporary_context = { .wanted_type = right_type };
		left_type = process_node_context(context, temporary_context, binary_operator.left)->type;
	} else {
		left_type = process_node(context, binary_operator.left)->type;
		Temporary_Context temporary_context = { .wanted_type = left_type };
		right_type = process_node_context(context, temporary_context, binary_operator.right)->type;
	}

	if (!value_equal(left_type.value, right_type.value)) {
		handle_mismatched_type_error(context, node, left_type, right_type);
	}

	Value type = left_type;
	if (type.value->tag == INTEGER_TYPE_VALUE) {}
	else if (type.value->tag == FLOAT_TYPE_VALUE) {}
	else if (type.value->tag == TYPE_TYPE_VALUE) {}
	else if (can_compare(type.value) && (binary_operator.operator == OP_EQUALS || binary_operator.operator == OP_NOT_EQUALS)) {}
	else if (type.value->tag == BOOLEAN_TYPE_VALUE && (binary_operator.operator == OP_AND || binary_operator.operator == OP_OR)) {}
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
		case OP_MODULUS:
			result_type = left_type;
			break;
		case OP_AND:
		case OP_OR:
			result_type = create_value(BOOLEAN_TYPE_VALUE);
			break;
	}

	Node_Data *data = context->temporary_context.data;
	data->binary_operator.type = left_type;
	data->type = result_type;
	return data;
}

static Node_Data *process_block(Context *context, Node *node) {
	Block_Node block = node->block;

	Node_Data *data = context->temporary_context.data;
	data->block.defers = NULL;

	arrpush(context->scopes, (Scope) { .node = node });
	for (long int i = 0; i < arrlen(block.statements); i++) {
		if (block.statements[i]->kind == DEFINE_NODE) {
			Scope_Identifier scope_identifier = {
				.tag = SCOPE_DEFINE,
				.define = block.statements[i]
			};
			hmput(arrlast(context->scopes).identifiers, sv_hash(block.statements[i]->define.identifier), scope_identifier);
		}
	}

	for (long int i = 0; i < arrlen(block.statements); i++) {
		Node *statement = block.statements[i];

		if (statement->kind == DEFER_NODE) {
			arrpush(data->block.defers, statement->defer.node);
		} else {
			if (block.has_result && i == arrlen(block.statements) - 1) {
				Temporary_Context temporary_context = { .wanted_type = context->temporary_context.wanted_type };
				Value statement_type = process_node_context(context, temporary_context, statement)->type;
				data->type = statement_type;
			} else {
				Value type = process_node(context, statement)->type;
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

	return data;
}

static Node_Data *process_boolean(Context *context, Node *node) {
	(void) node;
	Node_Data *data = context->temporary_context.data;
	data->type = create_value(BOOLEAN_TYPE_VALUE);
	return data;
}

static Node_Data *process_break(Context *context, Node *node) {
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
		handle_semantic_error(context, node->location, "No surrounding while");
	}

	While_Data while_data = get_data(context, while_)->while_;

	Value type = (Value) {};
	if (break_.value != NULL) {
		Temporary_Context temporary_context = { .wanted_type = while_data.wanted_type };
		type = process_node_context(context, temporary_context, break_.value)->type;
	}

	if (while_data.has_type) {
		if (type.value != NULL) {
			if (while_data.type.value == NULL) {
				handle_semantic_error(context, node->location, "Expected no value");
			} else if (!value_equal(type.value, while_data.type.value)) {
				handle_mismatched_type_error(context, node, type, while_data.type);
			}
		}

		if (while_data.type.value != NULL) {
			if (type.value == NULL) {
				handle_semantic_error(context, node->location, "Expected value");
			}
		}
	}
	while_data.type = type;
	while_data.has_type = true;

	Node_Data *data = context->temporary_context.data;
	data->break_.while_ = while_;
	return data;
}

static bool is_simple_cast(Value from_type, Value to_type) {
	if (from_type.value->tag != to_type.value->tag) return false;
	if (value_equal(from_type.value, to_type.value)) return true;

	switch (from_type.value->tag) {
		case POINTER_TYPE_VALUE:
			return true;
		case RESULT_TYPE_VALUE:
			if (is_simple_cast(from_type.value->result_type.value, to_type.value->result_type.value) && is_simple_cast(from_type.value->result_type.error, to_type.value->result_type.error)) return true;
			return false;
		case ENUM_TYPE_VALUE:
			return false;
		case INTEGER_TYPE_VALUE:
			return false;
		default:
			assert(false);
	}
}

static Node_Data *process_cast(Context *context, Node *node) {
	Cast_Node cast = node->cast;

	Value from_type = process_node(context, cast.node)->type;

	process_node(context, cast.type);
	Value to_type = evaluate(context, cast.type);

	bool cast_ok = false;
	if (is_simple_cast(from_type, to_type)) cast_ok = true;
	else if (from_type.value->tag == INTEGER_TYPE_VALUE && to_type.value->tag == BYTE_TYPE_VALUE) cast_ok = true;
	else if (from_type.value->tag == BYTE_TYPE_VALUE && to_type.value->tag == INTEGER_TYPE_VALUE) cast_ok = true;
	else if (from_type.value->tag == POINTER_TYPE_VALUE && to_type.value->tag == INTEGER_TYPE_VALUE && to_type.value->integer_type.signed_ == false && to_type.value->integer_type.size == context->codegen.default_integer_size) cast_ok = true;

	if (!cast_ok) {
		handle_2type_error(node, "Cannot cast from %s to %s", from_type, to_type);
	}

	Node_Data *data = context->temporary_context.data;
	data->cast.from_type = from_type;
	data->cast.to_type = to_type;

	data->type = to_type;
	return data;
}

static Node_Data *process_call(Context *context, Node *node) {
	Call_Node call = node->call;

	Node_Data *function_data = process_node(context, call.function);
	Value function_type = function_data->type;

	Node_Data *data = context->temporary_context.data;

	Value function_value = {};
	if (is_trivially_evaluatable(call.function, function_data)) {
		function_value = evaluate(context, call.function);

		process_initial_argument_types(context, function_value.value, call.arguments);
	}
	function_value = process_call_generic(context, node, function_value, &function_type, call.arguments);

	data->call.function_type = function_type;
	data->call.function_value = function_value;
	data->type = function_type.value->function_type.return_type;
	return data;
}

static Node_Data *process_call_method(Context *context, Node *node) {
	Call_Method_Node call_method = node->call_method;

	Node **arguments = NULL;
	arrpush(arguments, call_method.argument1);
	for (long int i = 0; i < arrlen(call_method.arguments); i++) {
		arrpush(arguments, call_method.arguments[i]);
	}

	Value argument1_type = process_node(context, call_method.argument1)->type;
	if (argument1_type.value->tag != POINTER_TYPE_VALUE) {
		reset_node(context, call_method.argument1);

		Temporary_Context temporary_context = { .want_pointer = true };
		argument1_type = process_node_context(context, temporary_context, call_method.argument1)->type;
	}

	Custom_Operator_Function custom_operator_function = find_custom_operator(context, argument1_type.value->pointer_type.inner, call_method.method);
	if (custom_operator_function.function == NULL) {
		char type_string[64] = {};
		print_type_outer(argument1_type.value->pointer_type.inner, type_string);
		handle_semantic_error(context, node->location, "Method '%.*s' not found for %s", (int) call_method.method.len, call_method.method.ptr, type_string);
	}

	process_initial_argument_types(context, custom_operator_function.function, arguments);
	custom_operator_function.function = process_call_generic(context, node, (Value) { .value = custom_operator_function.function }, &custom_operator_function.function_type, arguments).value;

	Node_Data *data = context->temporary_context.data;
	data->call_method.arguments = arguments;
	data->call_method.custom_operator_function = custom_operator_function;
	data->type = custom_operator_function.function_type.value->function_type.return_type;
	return data;
}

static Node_Data *process_character(Context *context, Node *node) {
	Character_Node character = node->character;

	String_View new_string = expand_escapes(character.value, (Value) {});
	if (new_string.len != 1) {
		handle_semantic_error(context, node->location, "Expected only one character");
	}

	Node_Data *data = context->temporary_context.data;
	data->character.value = new_string.ptr[0];
	data->type = create_value(BYTE_TYPE_VALUE);
	return data;
}

static Node_Data *process_catch(Context *context, Node *node) {
	Catch_Node catch = node->catch;

	Value result_type = process_node(context, catch.value)->type;

	bool saved_returned = context->returned;
	arrpush(context->scopes, (Scope) { .node = node });
	if (catch.binding.ptr != NULL) {
		Binding binding = {
			.type = result_type.value->result_type.error,
			.index = 0
		};

		Scope_Identifier scope_identifier = {
			.tag = SCOPE_BINDING,
			.binding = binding
		};
		hmput(arrlast(context->scopes).identifiers, sv_hash(catch.binding), scope_identifier);
	}
	process_node(context, catch.error);
	(void) arrpop(context->scopes);

	Node_Data *data = context->temporary_context.data;
	data->switch_.returned = context->returned;

	context->returned = saved_returned;

	data->catch.type = result_type;

	data->type = result_type.value->result_type.value;
	return data;
}

static Node_Data *process_const(Context *context, Node *node) {
	Const_Node const_ = node->const_;

	process_node(context, const_.value);

	Value const_type = create_value(CONST_TYPE_VALUE);
	const_type.value->const_type.type = NUMBER;

	Node_Data *data = context->temporary_context.data;
	data->type = create_value(CONST_TYPE_VALUE);
	return data;
}

static Node_Data *process_define(Context *context, Node *node) {
	Define_Node define = node->define;

	Value wanted_type = {};
	if (define.type != NULL) {
		process_node(context, define.type);
		wanted_type = evaluate(context, define.type);
	}

	Temporary_Context temporary_context = { .wanted_type = wanted_type };
	Value type = process_node_context(context, temporary_context, define.expression)->type;
	Value value = evaluate(context, define.expression);

	Node_Data *data = context->temporary_context.data;

	data->define.typed_value = (Typed_Value) {
		.value = value,
		.type = type
	};

	return data;
}

static Node_Data *process_deoptional(Context *context, Node *node) {
	Deoptional_Node deoptional = node->deoptional;

	Value type = process_node(context, deoptional.node)->type;
	type = process_enforce_pointer(context, deoptional.node, type);

	if (type.value->tag != POINTER_TYPE_VALUE || type.value->pointer_type.inner.value->tag != OPTIONAL_TYPE_VALUE) {
		handle_type_error(node, "Expected optional, but got %s", type);
	}

	Node_Data *data = context->temporary_context.data;
	Value inner_type = type.value->pointer_type.inner.value->optional_type.inner;
	data->deoptional.type = inner_type;

	if (deoptional.assign_value != NULL) {
		Temporary_Context temporary_context = { .wanted_type = inner_type };
		Value value_type = process_node_context(context, temporary_context, deoptional.assign_value)->type;

		if (!type_assignable(inner_type.value, value_type.value)) {
			handle_expected_type_error(context, node, inner_type, value_type);
		}
	} else {
		data->type = inner_type;
	}

	return data;
}

static Node_Data *process_dereference(Context *context, Node *node) {
	Dereference_Node dereference = node->dereference;

	Value type = process_node(context, dereference.node)->type;
	if (type.value->tag != POINTER_TYPE_VALUE) {
		handle_type_error(node, "Expected pointer, but got %s", type);
	}

	Node_Data *data = context->temporary_context.data;
	Value inner_type = type.value->pointer_type.inner;
	data->dereference.type = inner_type;

	if (dereference.assign_value != NULL) {
		Temporary_Context temporary_context = { .wanted_type = type.value->pointer_type.inner };
		Value value_type = process_node_context(context, temporary_context, dereference.assign_value)->type;

		if (!type_assignable(inner_type.value, value_type.value)) {
			handle_expected_type_error(context, node, inner_type, value_type);
		}
	} else {
		data->type = inner_type;
	}

	return data;
}

static Node_Data *process_enum_type(Context *context, Node *node) {
	(void) node;
	Node_Data *data = context->temporary_context.data;
	data->type = create_value(TYPE_TYPE_VALUE);
	return data;
}

static Node_Data *process_for(Context *context, Node *node) {
	For_Node for_ = node->for_;

	arrpush(context->scopes, (Scope) { .node = node });

	Value *item_types = NULL;
	Value *element_types = NULL;
	for (long int i = 0; i < arrlen(for_.items); i++) {
		Value item_type = process_node(context, for_.items[i])->type;
		arrpush(item_types, item_type);

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

	Node_Data *data = context->temporary_context.data;
	if (for_.static_) {
		assert(arrlen(for_.items) == 1);

		Value looped_value_type = item_types[0];
		Value looped_value = evaluate(context, for_.items[0]);

		size_t length;
		switch (looped_value_type.value->tag) {
			case ARRAY_VIEW_TYPE_VALUE: {
				length = looped_value.value->array_view.length;
				break;
			}
			case RANGE_TYPE_VALUE: {
				length = looped_value.value->range.end.value->integer.value - looped_value.value->range.start.value->integer.value;
				break;
			}
			default:
				assert(false);
		}

		size_t saved_static_id = context->static_id;
		for (size_t i = 0; i < length; i++) {
			size_t static_id = ++context->static_id_counter;
			context->static_id = static_id;
			arrpush(data->for_.static_ids, static_id);

			Typed_Value typed_value;
			switch (looped_value_type.value->tag) {
				case ARRAY_VIEW_TYPE_VALUE: {
					typed_value = (Typed_Value) {
						.value = (Value) { .value = looped_value.value->array_view.values[i] },
						.type = element_types[0]
					};
					break;
				}
				case RANGE_TYPE_VALUE: {
					typed_value = (Typed_Value) {
						.value = create_integer(i + looped_value.value->range.start.value->integer.value),
						.type = element_types[0]
					};
					break;
				}
				default:
					assert(false);
			}
			Scope_Identifier scope_identifier = {
				.tag = SCOPE_STATIC_BINDING,
				.static_binding = typed_value
			};
			hmput(arrlast(context->scopes).identifiers, sv_hash(for_.bindings[0]), scope_identifier);

			if (arrlen(for_.bindings) > 1) {
				Typed_Value typed_value = {
					.value = create_integer(i),
					.type = create_integer_type(false, context->codegen.default_integer_size)
				};

				scope_identifier = (Scope_Identifier) {
					.tag = SCOPE_STATIC_BINDING,
					.static_binding = typed_value
				};
				hmput(arrlast(context->scopes).identifiers, sv_hash(for_.bindings[1]), scope_identifier);
			}

			process_node(context, for_.body);
		}
		context->static_id = saved_static_id;
	} else {
		assert(arrlen(for_.items) >= arrlen(for_.bindings));

		for (long int i = 0; i < arrlen(for_.bindings); i++) {
			Binding binding = {
				.type = element_types[i],
				.index = i
			};

			Scope_Identifier scope_identifier = {
				.tag = SCOPE_BINDING,
				.binding = binding
			};
			hmput(arrlast(context->scopes).identifiers, sv_hash(for_.bindings[i]), scope_identifier);
		}

		process_node(context, for_.body);

		(void) arrpop(context->scopes);
	}

	data->for_.types = item_types;
	return data;
}

static Node_Data *process_function_type(Context *context, Node *node);
static Node_Data *process_function(Context *context, Node *node, bool given_static_arguments) {
	Function_Node function = node->function;

	bool compile_only_parent = context->compile_only;
	bool returned_parent = context->returned;

	context->compile_only = false;
	context->returned = false;

	bool static_argument = false;
	for (long int i = 0; i < arrlen(function.function_type->function_type.arguments); i++) {
		if (function.function_type->function_type.arguments[i].static_) {
			static_argument = true;
		}
	}

	Node_Data *data = data_create(context, node);

	if (static_argument && !given_static_arguments) {
		data->type = create_value(FUNCTION_TYPE_STUB_VALUE);
		return data;
	}

	Node_Data *function_type_data = process_function_type(context, function.function_type);

	Value function_type_value = function_type_data->function_type.value;

	data->type = function_type_value;

	if (function.body != NULL) {
		Scope scope = {
			.node = node,
			.node_type = function_type_value
		};
		arrpush(context->scopes, scope);

		Temporary_Context temporary_context = { .wanted_type = function_type_value.value->function_type.return_type };
		Value returned_type = process_node_context(context, temporary_context, function.body)->type;

		if (function_type_value.value->function_type.return_type.value != NULL) {
			Value wanted_return_type = function_type_value.value->function_type.return_type;
			if (!type_assignable(wanted_return_type.value, returned_type.value) && !context->returned) {
				handle_expected_type_error(context, node, wanted_return_type, returned_type);
			}
		}
		(void) arrpop(context->scopes);
	}

	if (context->compile_only) {
		data->function.compile_only = true;
	}
	if (context->returned) {
		data->function.returned = true;
	}

	context->compile_only = compile_only_parent;
	context->returned = returned_parent;
	
	return data;
}

static Node_Data *process_function_type(Context *context, Node *node) {
	Function_Type_Node function_type = node->function_type;

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

	Node_Data *data = data_create(context, node);
	data->function_type.value.value = function_type_value;

	data->type = create_value(TYPE_TYPE_VALUE);
	return data;
}

static Node_Data *process_global(Context *context, Node *node) {
	Global_Node global = node->global;

	Value type = {};
	if (global.type != NULL) {
		process_node(context, global.type);
		type = evaluate(context, global.type);
	}

	if (global.value != NULL) {
		Temporary_Context temporary_context = { .wanted_type = type };
		type = process_node_context(context, temporary_context, global.value)->type;
	}

	Value global_type = create_value(GLOBAL_TYPE_VALUE);
	global_type.value->global_type.type = type;

	Node_Data *data = context->temporary_context.data;
	data->type = global_type;
	return data;
}

static Node_Data *process_identifier(Context *context, Node *node) {
	Identifier_Node identifier = node->identifier;

	Node_Data *data = context->temporary_context.data;
	data->identifier.want_pointer = context->temporary_context.want_pointer;

	Lookup_Result lookup_result = { .tag = LOOKUP_RESULT_FAIL };

	Value value = {};
	Value type = {};
	Node *define_node = NULL;
	Scope *define_scopes = NULL;
	if (identifier.module != NULL) {
		Value module_type = process_node(context, identifier.module)->type;
		if (module_type.value->tag != MODULE_TYPE_VALUE) {
			handle_type_error(node, "Expected module, but got %s", module_type);
		}

		Value module_value = evaluate(context, identifier.module);

		for (long int i = 0; i < arrlen(module_value.value->module.body->block.statements); i++) {
			Node *statement = module_value.value->module.body->block.statements[i];
			if (statement->kind == DEFINE_NODE && sv_eq(statement->define.identifier, identifier.value)) {
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
				if (sv_eq(identifier.value, wanted_type.value->enum_type.items[i])) {
					value.value = value_new(ENUM_VALUE);
					value.value->enum_.value = i;
					type = context->temporary_context.wanted_type;
				}
			}
		}

		if (type.value == NULL) {
			if (sv_eq_cstr(identifier.value, "_")) {
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
		Typed_Value typed_value = process_node_with_scopes(context, define_node, define_scopes)->define.typed_value;

		type = typed_value.type;
		value = typed_value.value;

		if (define_node->define.special) {
			switch (type.value->tag) {
				case GLOBAL_TYPE_VALUE: {
					type = type.value->global_type.type;

					if (data->identifier.want_pointer) {
						Value_Data *pointer_type = value_new(POINTER_TYPE_VALUE);
						pointer_type->pointer_type.inner = type;
						type = (Value) { .value = pointer_type };
					}

					if (identifier.assign_value != NULL) {
						Temporary_Context temporary_context = { .wanted_type = type };
						Value value_type = process_node_context(context, temporary_context, identifier.assign_value)->type;
						if (!type_assignable(type.value, value_type.value)) {
							handle_expected_type_error(context, node, type, value_type);
						}
					}
					break;
				}
				case CONST_TYPE_VALUE: {
					Value const_type = type;

					type = context->temporary_context.wanted_type;

					if (type.value == NULL) {
						switch (const_type.value->const_type.type) {
							case NUMBER:
								type = create_integer_type(true, context->codegen.default_integer_size);
								break;
							default:
								assert(false);
						}
					}
					break;
				}
				default:
					assert(false);
			}
		}
	}

	switch (lookup_result.tag) {
		case LOOKUP_RESULT_VARIABLE: {
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
				Value value_type = process_node_context(context, temporary_context, identifier.assign_value)->type;
				if (!type_assignable(type.value, value_type.value)) {
					handle_expected_type_error(context, node, type, value_type);
				}
			}
			break;
		}
		case LOOKUP_RESULT_ARGUMENT: {
			data->identifier.kind = IDENTIFIER_ARGUMENT;
			data->identifier.argument_index = lookup_result.argument;

			type = lookup_result.type;
			if (data->identifier.want_pointer) {
				Value_Data *pointer_type = value_new(POINTER_TYPE_VALUE);
				pointer_type->pointer_type.inner = type;
				type = (Value) { .value = pointer_type };
			}
			break;
		}
		case LOOKUP_RESULT_BINDING: {
			data->identifier.kind = IDENTIFIER_BINDING;
			data->identifier.binding.node = lookup_result.binding.node;
			data->identifier.binding.index = lookup_result.binding.index;

			type = lookup_result.type;
			if (data->identifier.want_pointer) {
				Value_Data *pointer_type = value_new(POINTER_TYPE_VALUE);
				pointer_type->pointer_type.inner = type;
				type = (Value) { .value = pointer_type };
			}
			break;
		}
		case LOOKUP_RESULT_STATIC_BINDING: {
			value = lookup_result.static_binding;
			type = lookup_result.type;
			break;
		}
		case LOOKUP_RESULT_STATIC_VARIABLE: {
			type = lookup_result.type;

			if (identifier.assign_value != NULL) {
				Temporary_Context temporary_context = { .wanted_type = type };
				Value value_type = process_node_context(context, temporary_context, identifier.assign_value)->type;
				if (!type_assignable(type.value, value_type.value)) {
					handle_expected_type_error(context, node, type, value_type);
				}

				if (identifier.assign_static) {
					hmput(context->static_variables, lookup_result.static_variable.node_data, evaluate(context, identifier.assign_value));
				}
			} else {
				value = hmget(context->static_variables, lookup_result.static_variable.node_data);
			}
			break;
		}
		case LOOKUP_RESULT_DEFINE:
		case LOOKUP_RESULT_DEFINE_INTERNAL:
		case LOOKUP_RESULT_FAIL:
			break;
	}

	if (type.value == NULL) {
		handle_semantic_error(context, node->location, "Identifier '%.*s' not found", (int) identifier.value.len, identifier.value.ptr);
	}

	if (value.value != NULL) {
		value.node = node;

		if (define_node != NULL && define_node->define.special) {
			data->identifier.kind = IDENTIFIER_SPECIAL_VALUE;
		} else {
			data->identifier.kind = IDENTIFIER_VALUE;
		}

		data->identifier.value = value;
	}

	data->identifier.type = type;

	if (identifier.assign_value == NULL) {
		data->type = type;
	}

	return data;
}

static Node_Data *process_if(Context *context, Node *node) {
	If_Node if_ = node->if_;

	Value condition_type = process_node(context, if_.condition)->type;

	Node_Data *data = context->temporary_context.data;
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

				Scope_Identifier scope_identifier = {
					.tag = SCOPE_STATIC_BINDING,
					.static_binding = typed_value
				};
				hmput(arrlast(context->scopes).identifiers, sv_hash(if_.bindings[0]), scope_identifier);
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

			Scope_Identifier scope_identifier = {
				.tag = SCOPE_BINDING,
				.binding = binding
			};
			hmput(arrlast(context->scopes).identifiers, sv_hash(if_.bindings[0]), scope_identifier);
		}
		Value if_type = process_node(context, if_.if_body)->type;
		(void) arrpop(context->scopes);

		if (if_.else_body != NULL) {
			bool saved_if_returned = context->returned;
			context->returned = false;
			data->if_.then_returned = saved_if_returned;

			Temporary_Context temporary_context = { .wanted_type = if_type };
			Value else_type = process_node_context(context, temporary_context, if_.else_body)->type;

			bool saved_else_returned = context->returned;
			data->if_.else_returned = saved_else_returned;

			context->returned = saved_returned;

			if (saved_if_returned && saved_else_returned) {
				context->returned = true;
			}

			if (else_type.value != NULL) {
				if (if_type.value == NULL) {
					handle_semantic_error(context, node->location, "Expected value from if");
				}
			}

			if (if_type.value != NULL) {
				if (!saved_else_returned) {
					if (else_type.value == NULL) {
						handle_semantic_error(context, node->location, "Expected value from else");
					}

					if (!value_equal(if_type.value, else_type.value)) {
						handle_mismatched_type_error(context, node, if_type, else_type);
					}
				}

				data->if_.result_type = if_type;
				data->type = if_type;
			}
		} else {
			context->returned = saved_returned;
			if (if_type.value != NULL) {
				handle_semantic_error(context, node->location, "Expected else");
			}
		}

		data->if_.returned = context->returned;
	}

	return data;
}

static Node_Data *process_internal(Context *context, Node *node) {
	Internal_Node internal = node->internal;

	Node_Data *data = context->temporary_context.data;

	Value *value = &data->internal.value;
	value->node = node;
	switch (internal.kind) {
		case INTERNAL_UINT: {
			value->value = value_new(INTEGER_TYPE_VALUE);
			value->value->integer_type.size = context->codegen.default_integer_size;
			value->value->integer_type.signed_ = false;

			data->type = (Value) { .value = value_new(TYPE_TYPE_VALUE) };
		return data;
		}
		case INTERNAL_UINT8: {
			value->value = value_new(INTEGER_TYPE_VALUE);
			value->value->integer_type.size = 8;
			value->value->integer_type.signed_ = false;

			data->type = (Value) { .value = value_new(TYPE_TYPE_VALUE) };
		return data;
		}
		case INTERNAL_SINT: {
			value->value = value_new(INTEGER_TYPE_VALUE);
			value->value->integer_type.size = context->codegen.default_integer_size;
			value->value->integer_type.signed_ = true;

			data->type = (Value) { .value = value_new(TYPE_TYPE_VALUE) };
		return data;
		}
		case INTERNAL_TYPE: {
			value->value = value_new(TYPE_TYPE_VALUE);
			context->compile_only = true;
			data->type = (Value) { .value = value_new(TYPE_TYPE_VALUE) };
		return data;
		}
		case INTERNAL_BYTE: {
			value->value = value_new(BYTE_TYPE_VALUE);
			data->type = (Value) { .value = value_new(TYPE_TYPE_VALUE) };
		return data;
		}
		case INTERNAL_FLT64: {
			value->value = value_new(FLOAT_TYPE_VALUE);
			value->value->float_type.size = 64;
			data->type = (Value) { .value = value_new(TYPE_TYPE_VALUE) };
		return data;
		}
		case INTERNAL_BOOL: {
			value->value = value_new(BOOLEAN_TYPE_VALUE);
			data->type = (Value) { .value = value_new(TYPE_TYPE_VALUE) };
		return data;
		}
		case INTERNAL_TYPE_OF: {
			value->value = process_node(context, internal.inputs[0])->type.value;
			data->type = (Value) { .value = value_new(TYPE_TYPE_VALUE) };
		return data;
		}
		case INTERNAL_INT: {
			process_node(context, internal.inputs[0]);
			process_node(context, internal.inputs[1]);

			value->value = value_new(INTEGER_TYPE_VALUE);
			value->value->integer_type.signed_ = evaluate(context, internal.inputs[0]).value->boolean.value;
			value->value->integer_type.size = evaluate(context, internal.inputs[1]).value->integer.value;

			data->type = (Value) { .value = value_new(TYPE_TYPE_VALUE) };
		return data;
		}
		case INTERNAL_C_CHAR_SIZE: {
			*value = create_integer(context->codegen.c_size_fn(C_CHAR_SIZE));
			data->type = (Value) { .value = create_integer_type(false, 8).value };
		return data;
		}
		case INTERNAL_C_SHORT_SIZE: {
			*value = create_integer(context->codegen.c_size_fn(C_SHORT_SIZE));
			data->type = (Value) { .value = create_integer_type(false, 8).value };
		return data;
		}
		case INTERNAL_C_INT_SIZE: {
			*value = create_integer(context->codegen.c_size_fn(C_INT_SIZE));
			data->type = (Value) { .value = create_integer_type(false, 8).value };
		return data;
		}
		case INTERNAL_C_LONG_SIZE: {
			*value = create_integer(context->codegen.c_size_fn(C_LONG_SIZE));
			data->type = (Value) { .value = create_integer_type(false, 8).value };
		return data;
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

			data->internal.node = parse_source_statement(context->data, source_string, index, node->location.path_ref, node->location.row, node->location.column);

			if (internal.assign_value != NULL) {
				set_assign_value(data->internal.node, internal.assign_value, internal.assign_static);
			}

			Temporary_Context temporary_context = context->temporary_context;
			Value internal_type = process_node_context(context, temporary_context, data->internal.node)->type;

			data->type = internal_type;
		return data;
		}
		case INTERNAL_PRINT: {
			for (long int i = 0; i < arrlen(internal.inputs); i++) {
				process_node(context, internal.inputs[i]);
			}
			return data;
		}
		case INTERNAL_SELF: {
			for (long int i = arrlen(context->scopes) - 1; i >= 0; i--) {
				if (context->scopes[i].current_type.value != NULL) {
					*value = context->scopes[i].current_type;
				}
			}

			data->type = create_value(TYPE_TYPE_VALUE);
		return data;
		}
		case INTERNAL_SIZE_OF: {
			process_node(context, internal.inputs[0]);

			Value type = evaluate(context, internal.inputs[0]);

			value->value = value_new(INTEGER_VALUE);
			value->value->integer.value = context->codegen.size_fn(type.value, context->codegen.data);

			data->type = create_integer_type(false, context->codegen.default_integer_size);
		return data;
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
				char *node_path = context->data->source_files[node->location.path_ref];
				for (size_t i = 0; i < strlen(node_path); i++) {
					if (node_path[i] == '/') {
						slash_index = i;
					}
				}

				char *old_source = source;
				source = malloc(slash_index + strlen(source) + 2);
				strncpy(source, node_path, slash_index + 1);
				source[slash_index + 1] = '\0';
				strcat(source, old_source);
			}

			*value = get_cached_file(context, source);
			if (value->value == NULL) {
				Node *file_node = parse_file(context->data, source);

				Scope *saved_scopes = context->scopes;
				context->scopes = NULL;
				process_node(context, file_node);
				*value = evaluate(context, file_node);
				context->scopes = saved_scopes;

				add_cached_file(context, source, *value);
			}

			data->type = create_value(MODULE_TYPE_VALUE);
		return data;
		}
		case INTERNAL_TYPE_INFO_OF: {
			process_node(context, internal.inputs[0]);

			Value type = evaluate(context, internal.inputs[0]);

			*value = create_value(TAGGED_UNION_VALUE);

			Value_Data *enum_value = value_new(ENUM_VALUE);
			Value_Data *value_data = NULL;
			switch (type.value->tag) {
				case INTEGER_TYPE_VALUE: {
					enum_value->enum_.value = 0;

					value_data = value_new(STRUCT_VALUE);
					value_data->struct_.values = NULL;

					Value_Data *size_value = create_integer(type.value->integer_type.size).value;
					arrpush(value_data->struct_.values, size_value);

					Value_Data *signed_value = create_boolean(type.value->integer_type.signed_).value;
					arrpush(value_data->struct_.values, signed_value);
					break;
				}
				case STRUCT_TYPE_VALUE: {
					enum_value->enum_.value = 1;

					value_data = value_new(STRUCT_VALUE);
					value_data->struct_.values = NULL;

					Value_Data *items_value = value_new(ARRAY_VIEW_VALUE);
					items_value->array_view.values = NULL;
					items_value->array_view.length = arrlen(type.value->struct_type.members);
					for (long int i = 0; i < arrlen(type.value->struct_type.members); i++) {
						Value_Data *struct_item_value = value_new(STRUCT_VALUE);
						struct_item_value->struct_.values = NULL;

						String_View name_string;
						if (type.value->struct_type.node != NULL) {
							name_string = type.value->struct_type.node->struct_type.members[i].name;
						} else {
							char *buffer = malloc(8);
							memset(buffer, 0, 8);
							buffer[0] = '_';
							sprintf(buffer + 1, "%i", (int) i);
							name_string = cstr_to_sv(buffer);
						}

						size_t name_string_length = name_string.len;

						Value_Data *name_value = value_new(ARRAY_VIEW_VALUE);
						name_value->array_view.values = NULL;
						name_value->array_view.length = name_string_length;
						for (size_t i = 0; i < name_string_length; i++) {
							Value_Data *byte_value = value_new(BYTE_VALUE);
							byte_value->byte.value = name_string.ptr[i];
							arrpush(name_value->array_view.values, byte_value);
						}
						arrpush(struct_item_value->struct_.values, name_value);

						Value_Data *type_value = type.value->struct_type.members[i].value;
						arrpush(struct_item_value->struct_.values, type_value);

						arrpush(items_value->array_view.values, struct_item_value);
					}
					arrpush(value_data->struct_.values, items_value);
					break;
				}
				case UNION_TYPE_VALUE: {
					enum_value->enum_.value = 2;

					value_data = value_new(STRUCT_VALUE);
					value_data->struct_.values = NULL;

					Value_Data *items_value = value_new(ARRAY_VIEW_VALUE);
					items_value->array_view.length = arrlen(type.value->union_type.items);
					for (long int i = 0; i < arrlen(type.value->union_type.items); i++) {
						Value_Data *struct_item_value = value_new(STRUCT_VALUE);

						String_View name_string = type.value->union_type.items[i].identifier;
						size_t name_string_length = name_string.len;

						Value_Data *name_value = value_new(ARRAY_VIEW_VALUE);
						name_value->array_view.length = name_string_length;
						for (size_t i = 0; i < name_string_length; i++) {
							Value_Data *byte_value = value_new(BYTE_VALUE);
							byte_value->byte.value = name_string.ptr[i];
							arrpush(name_value->array_view.values, byte_value);
						}
						arrpush(struct_item_value->struct_.values, name_value);

						Value_Data *type_value = type.value->union_type.items[i].type.value;
						arrpush(struct_item_value->struct_.values, type_value);

						arrpush(items_value->array_view.values, struct_item_value);
					}
					arrpush(value_data->struct_.values, items_value);
					break;
				}
				case TAGGED_UNION_TYPE_VALUE: {
					enum_value->enum_.value = 3;

					value_data = value_new(STRUCT_VALUE);
					value_data->struct_.values = NULL;

					Value_Data *items_value = value_new(ARRAY_VIEW_VALUE);
					items_value->array_view.length = arrlen(type.value->tagged_union_type.items);
					for (long int i = 0; i < arrlen(type.value->tagged_union_type.items); i++) {
						Value_Data *struct_item_value = value_new(STRUCT_VALUE);

						String_View name_string = type.value->tagged_union_type.items[i].identifier;
						size_t name_string_length = name_string.len;

						Value_Data *name_value = value_new(ARRAY_VIEW_VALUE);
						name_value->array_view.length = name_string_length;
						for (size_t i = 0; i < name_string_length; i++) {
							Value_Data *byte_value = value_new(BYTE_VALUE);
							byte_value->byte.value = name_string.ptr[i];
							arrpush(name_value->array_view.values, byte_value);
						}
						arrpush(struct_item_value->struct_.values, name_value);

						Value_Data *type_value = type.value->tagged_union_type.items[i].type.value;
						arrpush(struct_item_value->struct_.values, type_value);

						arrpush(items_value->array_view.values, struct_item_value);
					}
					arrpush(value_data->struct_.values, items_value);
					break;
				}
				case ENUM_TYPE_VALUE: {
					enum_value->enum_.value = 4;

					value_data = value_new(STRUCT_VALUE);
					value_data->struct_.values = NULL;

					Value_Data *items_value = value_new(ARRAY_VIEW_VALUE);
					items_value->array_view.length = arrlen(type.value->enum_type.items);
					for (long int i = 0; i < arrlen(type.value->enum_type.items); i++) {
						String_View name_string = type.value->enum_type.items[i];
						size_t name_string_length = name_string.len;

						Value_Data *name_value = value_new(ARRAY_VIEW_VALUE);
						name_value->array_view.length = name_string_length;
						for (size_t i = 0; i < name_string_length; i++) {
							Value_Data *byte_value = value_new(BYTE_VALUE);
							byte_value->byte.value = name_string.ptr[i];
							arrpush(name_value->array_view.values, byte_value);
						}
						arrpush(items_value->array_view.values, name_value);
					}
					arrpush(value_data->struct_.values, items_value);
					break;
				}
				case OPTIONAL_TYPE_VALUE: {
					enum_value->enum_.value = 5;

					value_data = value_new(STRUCT_VALUE);
					value_data->struct_.values = NULL;

					Value_Data *type_value = type.value->optional_type.inner.value;
					arrpush(value_data->struct_.values, type_value);
					break;
				}
				case ARRAY_TYPE_VALUE: {
					enum_value->enum_.value = 6;

					value_data = value_new(STRUCT_VALUE);
					value_data->struct_.values = NULL;

					Value_Data *size_value = type.value->array_type.size.value;
					arrpush(value_data->struct_.values, size_value);
					Value_Data *type_value = type.value->array_type.inner.value;
					arrpush(value_data->struct_.values, type_value);
					break;
				}
				case ARRAY_VIEW_TYPE_VALUE: {
					enum_value->enum_.value = 7;

					value_data = value_new(STRUCT_VALUE);
					value_data->struct_.values = NULL;

					Value_Data *type_value = type.value->array_type.inner.value;
					arrpush(value_data->struct_.values, type_value);
					break;
				}
				case TUPLE_TYPE_VALUE: {
					enum_value->enum_.value = 8;

					value_data = value_new(STRUCT_VALUE);
					value_data->struct_.values = NULL;

					Value_Data *items_value = value_new(ARRAY_VIEW_VALUE);
					items_value->array_view.values = NULL;
					items_value->array_view.length = arrlen(type.value->tuple_type.members);
					for (long int i = 0; i < arrlen(type.value->tuple_type.members); i++) {
						Value_Data *type_value = type.value->tuple_type.members[i].value;
						arrpush(items_value->array_view.values, type_value);
					}
					arrpush(value_data->struct_.values, items_value);
					break;
				}
				case BYTE_TYPE_VALUE: {
					enum_value->enum_.value = 9;

					value_data = value_new(STRUCT_VALUE);
					break;
				}
				case POINTER_TYPE_VALUE: {
					enum_value->enum_.value = 10;

					value_data = value_new(STRUCT_VALUE);
					break;
				}
				case BOOLEAN_TYPE_VALUE: {
					enum_value->enum_.value = 11;

					value_data = value_new(STRUCT_VALUE);
					break;
				}
				default:
					assert(false);
			}

			value->value->tagged_union.tag = enum_value;
			value->value->tagged_union.data = value_data;

			size_t saved_static_id = context->static_id;
			context->static_id = 0;
			Value type_info_type = get_data(context, find_define(context->internal_root->module.body->block, cstr_to_sv("Type_Info")))->define.typed_value.value;
			context->static_id = saved_static_id;
			data->type = type_info_type;
			return data;
		}
		case INTERNAL_OS: {
			Value operating_system_type = get_data(context, find_define(context->internal_root->module.body->block, cstr_to_sv("Operating_System")))->define.typed_value.value;

			#if defined(__linux__)
				size_t os_value = 0;
			#elif defined(__APPLE__)
				size_t os_value = 1;
			#elif defined(_WIN32)
				size_t os_value = 2;
			#endif

			value->value = value_new(ENUM_VALUE);
			value->value->enum_.value = os_value;
			data->type = operating_system_type;
			return data;
		}
		case INTERNAL_OK: {
			Value wanted_type = context->temporary_context.wanted_type;
			assert(wanted_type.value->tag == RESULT_TYPE_VALUE);

			if (arrlen(internal.inputs) > 0) {
				Temporary_Context temporary_context = { .wanted_type = wanted_type.value->result_type.value };
				process_node_context(context, temporary_context, internal.inputs[0]);
			}

			data->type = wanted_type;
			return data;
		}
		case INTERNAL_ERR: {
			Value wanted_type = context->temporary_context.wanted_type;
			assert(wanted_type.value->tag == RESULT_TYPE_VALUE);

			if (arrlen(internal.inputs) > 0) {
				Temporary_Context temporary_context = { .wanted_type = wanted_type.value->result_type.error };
				process_node_context(context, temporary_context, internal.inputs[0]);
			}

			data->type = wanted_type;
			return data;
		}
		case INTERNAL_COMPILE_ERROR: {
			process_node(context, internal.inputs[0]);

			Value string = evaluate(context, internal.inputs[0]);

			char *message = malloc(string.value->array_view.length + 1);
			message[string.value->array_view.length] = '\0';
			for (size_t i = 0; i < string.value->array_view.length; i++) {
				message[i] = string.value->array_view.values[i]->byte.value;
			}

			handle_semantic_error(context, node->location, "%.*s", (int) string.value->array_view.length, message);
			return data;
		}
		default:
			assert(false);
	}
}

static Node_Data *process_is(Context *context, Node *node) {
	Is_Node is = node->is;

	Value type = process_node(context, is.node)->type;
	if (type.value->tag != TAGGED_UNION_TYPE_VALUE) {
		handle_type_error(node, "Expected tagged union, but got %s", type);
	}

	Temporary_Context temporary_context = { .wanted_type = (Value) { .value = type.value->tagged_union_type.enum_ } };
	process_node_context(context, temporary_context, is.check);
	Value check_value = evaluate(context, is.check);

	Value tagged_type = type.value->tagged_union_type.items[check_value.value->enum_.value].type;

	Node_Data *data = context->temporary_context.data;
	data->is.value = check_value;
	data->is.type = create_optional_type(tagged_type);

	data->type = create_optional_type(tagged_type);
	return data;
}

static Node_Data *process_module(Context *context, Node *node) {
	Module_Node module = node->module;
	process_node(context, module.body);

	Node_Data *data = context->temporary_context.data;
	data->type = (Value) { .value = value_new(MODULE_TYPE_VALUE) };
	return data;
}

static Node_Data *process_module_type(Context *context, Node *node) {
	(void) node;
	Node_Data *data = context->temporary_context.data;
	data->type = (Value) { .value = value_new(TYPE_TYPE_VALUE) };
	return data;
}

static Node_Data *process_not(Context *context, Node *node) {
	Not_Node not = node->not;
	process_node(context, not.node);

	Node_Data *data = context->temporary_context.data;
	data->type = create_value(BOOLEAN_TYPE_VALUE);
	return data;
}

static Node_Data *process_null(Context *context, Node *node) {
	(void) node;
	Value wanted_type = context->temporary_context.wanted_type;
	if (wanted_type.value == NULL) {
		assert(false);
	}

	Node_Data *data = context->temporary_context.data;
	data->null_.type = wanted_type;
	data->type = wanted_type;
	return data;
}

Node *sint_node;

static Node_Data *process_number(Context *context, Node *node) {
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
			if (sint_node == NULL) {
				sint_node = ast_new(INTERNAL_NODE, (Source_Location) {});
				sint_node->internal.kind = INTERNAL_SINT;
			}
			wanted_type.node = sint_node;
		}
	}

	Node_Data *data = context->temporary_context.data;
	data->number.type = wanted_type;
	data->type = wanted_type;
	return data;
}

static Node_Data *process_operator(Context *context, Node *node) {
	Operator_Node operator = node->operator;
	Value expression_type = process_node(context, operator.expression)->type;

	Value value = evaluate(context, operator.expression);

	Node_Data *data = context->temporary_context.data;
	data->operator.typed_value = (Typed_Value) {
		.type = expression_type,
		.value = value
	};
	return data;
}

static Node_Data *process_optional(Context *context, Node *node) {
	Optional_Type_Node optional = node->optional_type;
	process_node(context, optional.inner);

	Node_Data *data = context->temporary_context.data;
	data->type = create_value(TYPE_TYPE_VALUE);
	return data;
}

static Node_Data *process_pointer(Context *context, Node *node) {
	Pointer_Type_Node pointer = node->pointer_type;
	if (pointer.inner != NULL) {
		process_node(context, pointer.inner);
	}

	Node_Data *data = context->temporary_context.data;
	data->type = create_value(TYPE_TYPE_VALUE);
	return data;
}

static Node_Data *process_range(Context *context, Node *node) {
	Range_Node range = node->range;

	Value wanted_type = {};
	if (context->temporary_context.wanted_type.value != NULL && context->temporary_context.wanted_type.value->tag == RANGE_TYPE_VALUE) {
		wanted_type = context->temporary_context.wanted_type.value->range_type.type;
	}

	Value start_type;
	Value end_type;
	if (range.end != NULL && range.start->kind == NUMBER_NODE) {
		Temporary_Context temporary_context = (Temporary_Context) { .wanted_type = wanted_type };
		end_type = process_node_context(context, temporary_context, range.end)->type;

		if (wanted_type.value == NULL) {
			wanted_type = end_type;
		}

		temporary_context = (Temporary_Context) { .wanted_type = wanted_type };
		start_type = process_node_context(context, temporary_context, range.start)->type;
	} else {
		Temporary_Context temporary_context = (Temporary_Context) { .wanted_type = wanted_type };
		start_type = process_node_context(context, temporary_context, range.start)->type;

		if (wanted_type.value == NULL) {
			wanted_type = start_type;
		}

		if (range.end != NULL) {
			temporary_context = (Temporary_Context) { .wanted_type = wanted_type };
			end_type = process_node_context(context, temporary_context, range.end)->type;
		}
	}

	if (range.end != NULL) {
		if (!value_equal(start_type.value, end_type.value)) {
			handle_mismatched_type_error(context, node, start_type, end_type);
		}
	}

	Node_Data *data = context->temporary_context.data;
	data->type = create_range_type(wanted_type);
	return data;
}

static Node_Data *process_reference(Context *context, Node *node) {
	Reference_Node reference = node->reference;

	Temporary_Context temporary_context = { .want_pointer = true };
	Value type = process_node_context(context, temporary_context, reference.node)->type;

	Node_Data *data = context->temporary_context.data;
	data->type = type;
	return data;
}

static Node_Data *process_result(Context *context, Node *node) {
	Result_Type_Node result = node->result_type;
	if (result.value != NULL) {
		process_node(context, result.value);
	}
	process_node(context, result.error);

	Node_Data *data = context->temporary_context.data;
	data->type = create_value(TYPE_TYPE_VALUE);
	return data;
}

static Node_Data *process_return(Context *context, Node *node) {
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

	Node_Data *data = context->temporary_context.data;
	if (return_.value != NULL) {
		Value wanted_type = return_type;

		Temporary_Context temporary_context = { .wanted_type = wanted_type };
		Value type = process_node_context(context, temporary_context, return_.value)->type;
		if (!type_assignable(wanted_type.value, type.value)) {
			handle_expected_type_error(context, node, wanted_type, type);
		}
	}

	data->return_.type = return_type;
	return data;
}

static Node_Data *process_run(Context *context, Node *node) {
	Run_Node run = node->run;

	Temporary_Context temporary_context = { .wanted_type = context->temporary_context.wanted_type };
	Value type = process_node_context(context, temporary_context, run.node)->type;
	Value value = evaluate(context, run.node);

	Node_Data *data = context->temporary_context.data;
	data->run.value = value;
	data->type = type;
	return data;
}

static Node_Data *process_slice(Context *context, Node *node) {
	Slice_Node slice = node->slice;

	Temporary_Context temporary_context = { .wanted_type = create_integer_type(false, context->codegen.default_integer_size) };
	process_node_context(context, temporary_context, slice.start);

	process_node_context(context, temporary_context, slice.end);

	Value raw_array_type = process_node(context, slice.parent)->type;
	if (raw_array_type.value->tag == ARRAY_TYPE_VALUE) {
		raw_array_type = process_enforce_pointer(context, slice.parent, raw_array_type);
	}

	Value array_type = raw_array_type;
	if (array_type.value->tag == POINTER_TYPE_VALUE) {
		array_type = array_type.value->pointer_type.inner;
	}

	if (array_type.value->tag != ARRAY_TYPE_VALUE
			&& array_type.value->tag != ARRAY_VIEW_TYPE_VALUE) {
		handle_type_error(node, "Cannot perform array access operation on %s", array_type);
	}

	Value item_type = {};
	switch (array_type.value->tag) {
		case ARRAY_TYPE_VALUE: {
			item_type = array_type.value->array_type.inner;
			break;
		}
		case ARRAY_VIEW_TYPE_VALUE: {
			item_type = array_type.value->array_view_type.inner;
			break;
		}
		default:
			assert(false);
	}

	Node_Data *data = context->temporary_context.data;
	data->slice.array_type = array_type;
	data->slice.pointer_access = raw_array_type.value->tag == POINTER_TYPE_VALUE;
	data->slice.item_type = item_type;

	Value array_view_type = create_value(ARRAY_VIEW_TYPE_VALUE);
	array_view_type.value->array_view_type.inner = item_type;

	data->type = array_view_type;
	return data;
}

static Node_Data *process_string(Context *context, Node *node) {
	String_Node string = node->string;

	Value type = context->temporary_context.wanted_type;
	bool invalid_type = false;
	if (type.value == NULL) invalid_type = true;
	else if (type.value->tag == POINTER_TYPE_VALUE && type.value->pointer_type.inner.value->tag == ARRAY_TYPE_VALUE && type.value->pointer_type.inner.value->array_type.inner.value->tag == BYTE_TYPE_VALUE) {}
	else if (type.value->tag == ARRAY_VIEW_TYPE_VALUE && type.value->array_view_type.inner.value->tag == BYTE_TYPE_VALUE) {}
	else invalid_type = true;

	if (invalid_type) {
		type = create_array_view_type(create_value(BYTE_TYPE_VALUE));
	}

	String_View new_string = expand_escapes(string.value, type);

	Node_Data *data = context->temporary_context.data;
	data->string.type = type;
	data->string.value = new_string;
	data->type = type;
	return data;
}

static Node_Data *process_struct_type(Context *context, Node *node) {
	Struct_Type_Node struct_type = node->struct_type;
	for (long int i = 0; i < arrlen(struct_type.members); i++) {
		process_node(context, struct_type.members[i].type);
	}

	Node_Data *data = context->temporary_context.data;
	data->type = create_value(TYPE_TYPE_VALUE);
	return data;
}

static Node_Data *process_structure(Context *context, Node *node) {
	Structure_Node structure = node->structure;

	Value wanted_type = context->temporary_context.wanted_type;

	Value result_type = wanted_type;
	if (wanted_type.value == NULL) {
		result_type = create_value(TUPLE_TYPE_VALUE);
		result_type.value->tuple_type.members = NULL;
	}

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
						if (sv_eq(wanted_type.value->tagged_union_type.items[j].identifier, structure.values[i].identifier)) {
							item_wanted_type = wanted_type.value->tagged_union_type.items[j].type;
							break;
						}
					}
					assert(item_wanted_type.value != NULL);
					break;
				case UNION_TYPE_VALUE:
					for (long int j = 0; j < arrlen(wanted_type.value->union_type.items); j++) {
						if (sv_eq(wanted_type.value->union_type.items[j].identifier, structure.values[i].identifier)) {
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
		Value value_type = process_node_context(context, temporary_context, structure.values[i].node)->type;

		if (wanted_type.value == NULL) {
			arrpush(result_type.value->tuple_type.members, value_type);
		}
	}

	Node_Data *data = context->temporary_context.data;
	data->structure.type = result_type;
	data->type = result_type;
	return data;
}

static Node_Data *process_structure_access(Context *context, Node *node) {
	Structure_Access_Node structure_access = node->structure_access;

	Value raw_structure_type = process_node(context, structure_access.parent)->type;
	if (context->temporary_context.want_pointer || structure_access.assign_value != NULL) {
		raw_structure_type = process_enforce_pointer(context, structure_access.parent, raw_structure_type);
	}

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
				if (sv_eq(structure_type.value->struct_type.node->struct_type.members[i].name, structure_access.name)) {
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
				if (sv_eq_cstr(structure_access.name, name)) {
					item_type = structure_type.value->tuple_type.members[i];
					break;
				}
			}
			break;
		}
		case UNION_TYPE_VALUE: {
			for (long int i = 0; i < arrlen(structure_type.value->union_type.items); i++) {
				if (sv_eq(structure_type.value->union_type.items[i].identifier, structure_access.name)) {
					item_type = structure_type.value->union_type.items[i].type;
					break;
				}
			}
			break;
		}
		case ARRAY_VIEW_TYPE_VALUE: {
			if (sv_eq_cstr(structure_access.name, "len")) {
				item_type = create_integer_type(false, context->codegen.default_integer_size);
			} else if (sv_eq_cstr(structure_access.name, "ptr")) {
				item_type = create_pointer_type(create_array_type(structure_type.value->array_view_type.inner));
			}
			break;
		}
		default:
			assert(false);
	}

	if (item_type.value == NULL) {
		handle_semantic_error(context, node->location, "Item '%.*s' not found", (int) structure_access.name.len, structure_access.name.ptr);
	}

	Node_Data *data = context->temporary_context.data;
	data->structure_access.structure_type = structure_type;
	data->structure_access.want_pointer = context->temporary_context.want_pointer;
	data->structure_access.item_type = item_type;
	data->structure_access.pointer_access = raw_structure_type.value->tag == POINTER_TYPE_VALUE;

	if (structure_access.assign_value != NULL) {
		Temporary_Context temporary_context = { .wanted_type = item_type };
		Value value_type = process_node_context(context, temporary_context, structure_access.assign_value)->type;
		if (!type_assignable(item_type.value, value_type.value)) {
			handle_expected_type_error(context, node, item_type, value_type);
		}
	} else {
		if (data->structure_access.want_pointer) {
			Value_Data *pointer_item_type = value_new(POINTER_TYPE_VALUE);
			pointer_item_type->pointer_type.inner = item_type;
			item_type = (Value) { .value = pointer_item_type };
		}

		data->type = item_type;
	}

	return data;
}

static Node_Data *process_switch(Context *context, Node *node) {
	Switch_Node switch_ = node->switch_;

	Value type = process_node(context, switch_.condition)->type;

	Node_Data *data = context->temporary_context.data;

	bool saved_returned = context->returned;

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
			if (switched_value->tag == TAGGED_UNION_VALUE && switch_case.binding.ptr != NULL) {
				Typed_Value typed_value = {
					.value = (Value) { .value = switched_value->tagged_union.data },
					.type = type.value->tagged_union_type.items[switched_enum_value->enum_.value].type
				};

				Scope_Identifier scope_identifier = {
					.tag = SCOPE_STATIC_BINDING,
					.static_binding = typed_value
				};
				hmput(arrlast(context->scopes).identifiers, sv_hash(switch_case.binding), scope_identifier);
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

			if (switch_case.binding.ptr != NULL) {
				Binding binding = {
					.type = binding_type,
					.index = 0
				};

				Scope_Identifier scope_identifier = {
					.tag = SCOPE_BINDING,
					.binding = binding
				};
				hmput(arrlast(context->scopes).identifiers, sv_hash(switch_case.binding), scope_identifier);
			}

			bool saved_previous_returned = context->returned;
			context->returned = NULL;
			Temporary_Context temporary_context = { .wanted_type = context->temporary_context.wanted_type };
			Value case_type = process_node_context(context, temporary_context, switch_case.body)->type;

			bool saved_case_returned = context->returned;

			context->returned = saved_returned;

			(void) arrpop(context->scopes);

			arrpush(data->switch_.cases_returned, saved_case_returned);
			if ((saved_previous_returned || i == 0) && saved_case_returned) {
				context->returned = true;
			}

			if (case_type.value != NULL) {
				if (switch_type.value == NULL) {
					if (set_switch_type) {
						handle_semantic_error(context, node->location, "Expected value from case");
					} else {
						switch_type = case_type;
					}
				}

				set_switch_type = true;
			}

			if (switch_type.value != NULL) {
				if (case_type.value == NULL) {
					handle_semantic_error(context, node->location, "Expected value from case");
				}

				if (!value_equal(switch_type.value, case_type.value)) {
					handle_mismatched_type_error(context, node, switch_type, case_type);
				}

				data->switch_.type = switch_type;
				data->type = switch_type;
			}
		}

		data->switch_.returned = context->returned;

		if (case_count < arrlen(enum_type->enum_type.items) && !else_case) {
			context->returned = saved_returned;
			if (switch_type.value != NULL) {
				handle_semantic_error(context, node->location, "Expected else case");
			}
		}
	}

	return data;
}

static Node_Data *process_tagged_union_type(Context *context, Node *node) {
	Tagged_Union_Type_Node tagged_union_type = node->tagged_union_type;
	for (long int i = 0; i < arrlen(tagged_union_type.members); i++) {
		process_node(context, tagged_union_type.members[i].type);
	}

	Node_Data *data = context->temporary_context.data;
	data->type = create_value(TYPE_TYPE_VALUE);
	return data;
}

static Node_Data *process_union_type(Context *context, Node *node) {
	Union_Type_Node union_type = node->union_type;
	for (long int i = 0; i < arrlen(union_type.members); i++) {
		process_node(context, union_type.members[i].type);
	}

	Node_Data *data = context->temporary_context.data;
	data->type = create_value(TYPE_TYPE_VALUE);
	return data;
}

static Node_Data *process_variable(Context *context, Node *node) {
	Variable_Node variable = node->variable;

	Temporary_Context temporary_context = {};
	Value type = {};
	if (variable.type != NULL) {
		process_node(context, variable.type);
		type = evaluate(context, variable.type);
		temporary_context.wanted_type = type;
	}

	if (variable.value != NULL) {
		Value value_type = process_node_context(context, temporary_context, variable.value)->type;
		if (type.value == NULL) {
			type = value_type;

			if (value_type.value == NULL) {
				handle_semantic_error(context, node->location, "Expected value");
			}
		} else if (!type_assignable(type.value, value_type.value)) {
			handle_expected_type_error(context, node, type, value_type);
		}
	} else if (type.value == NULL) {
		handle_semantic_error(context, node->location, "Expected value");
	}

	Node_Data *data = context->temporary_context.data;

	Variable_Definition variable_definition = {
		.node = node,
		.node_data = data
	};
	if (variable.static_) {
		Scope_Identifier scope_identifier = {
			.tag = SCOPE_STATIC_VARIABLE,
			.static_variable = variable_definition
		};
		hmput(arrlast(context->scopes).identifiers, sv_hash(variable.name), scope_identifier);
		Value value = evaluate(context, variable.value);
		hmput(context->static_variables, data, value);
	} else {
		Scope_Identifier scope_identifier = {
			.tag = SCOPE_VARIABLE,
			.variable = variable_definition
		};
		hmput(arrlast(context->scopes).identifiers, sv_hash(variable.name), scope_identifier);
	}

	data->variable.type = type;
	return data;
}

static Node_Data *process_while(Context *context, Node *node) {
	While_Node while_ = node->while_;

	Node_Data *data = context->temporary_context.data;
	data->while_.wanted_type = context->temporary_context.wanted_type;
	data->while_.type = (Value) {};

	arrpush(context->scopes, (Scope) { .node = node });

	process_node(context, while_.condition);

	if (while_.static_) {
		assert(while_.else_body == NULL);

		size_t saved_static_id = context->static_id;
		while (evaluate(context, while_.condition).value->boolean.value) {
			size_t static_id = ++context->static_id_counter;
			context->static_id = static_id;
			arrpush(data->while_.static_ids, static_id);

			process_node(context, while_.body);

			process_node(context, while_.condition);
		}
		context->static_id = saved_static_id;
	} else {
		process_node(context, while_.body);

		Value while_type = data->while_.type;

		if (while_.else_body != NULL) {
			Temporary_Context temporary_context = { .wanted_type = context->temporary_context.wanted_type };
			Value type = process_node_context(context, temporary_context, while_.else_body)->type;
			if (data->while_.has_type) {
				if (type.value != NULL) {
					if (while_type.value == NULL) {
						handle_semantic_error(context, node->location, "Expected no value in else");
					} else if (!value_equal(type.value, while_type.value)) {
						handle_mismatched_type_error(context, node, type, while_type);
					}
				}

				if (while_type.value != NULL) {
					if (type.value == NULL) {
						handle_semantic_error(context, node->location, "Expected value in else");
					}
				}
			}
		}

		if (data->while_.has_type && while_.else_body != NULL) {
			data->type = while_type;
		}
	}

	(void) arrpop(context->scopes);
	return data;
}

Node_Data *process_node_context_internal(Context *context, Temporary_Context temporary_context, Node *node) {
	Node_Data **data = get_data_ref(context, node);
	if (*data != NULL) {
		return *data;
	}

	*data = data_new();

	temporary_context.data = *data;

	Temporary_Context saved_temporary_context = context->temporary_context;
	context->temporary_context = temporary_context;

	Node_Data *value = NULL;
	switch (node->kind) {
		case ARRAY_ACCESS_NODE:
			value = process_array_access(context, node);
			break;
		case ARRAY_TYPE_NODE:
			value = process_array_type(context, node);
			break;
		case ARRAY_VIEW_TYPE_NODE:
			value = process_array_view_type(context, node);
			break;
		case BINARY_OP_NODE:
			value = process_binary_op(context, node);
			break;
		case BLOCK_NODE:
			value = process_block(context, node);
			break;
		case BOOLEAN_NODE:
			value = process_boolean(context, node);
			break;
		case BREAK_NODE:
			value = process_break(context, node);
			break;
		case CAST_NODE:
			value = process_cast(context, node);
			break;
		case CALL_NODE:
			value = process_call(context, node);
			break;
		case CALL_METHOD_NODE:
			value = process_call_method(context, node);
			break;
		case CHARACTER_NODE:
			value = process_character(context, node);
			break;
		case CATCH_NODE:
			value = process_catch(context, node);
			break;
		case CONST_NODE:
			value = process_const(context, node);
			break;
		case DEFINE_NODE:
			value = process_define(context, node);
			break;
		case DEOPTIONAL_NODE:
			value = process_deoptional(context, node);
			break;
		case DEREFERENCE_NODE:
			value = process_dereference(context, node);
			break;
		case ENUM_TYPE_NODE:
			value = process_enum_type(context, node);
			break;
		case FOR_NODE:
			value = process_for(context, node);
			break;
		case FUNCTION_NODE:
			value = process_function(context, node, false);
			break;
		case FUNCTION_TYPE_NODE:
			value = process_function_type(context, node);
			break;
		case GLOBAL_NODE:
			value = process_global(context, node);
			break;
		case IDENTIFIER_NODE:
			value = process_identifier(context, node);
			break;
		case IF_NODE:
			value = process_if(context, node);
			break;
		case INTERNAL_NODE:
			value = process_internal(context, node);
			break;
		case IS_NODE:
			value = process_is(context, node);
			break;
		case MODULE_NODE:
			value = process_module(context, node);
			break;
		case MODULE_TYPE_NODE:
			value = process_module_type(context, node);
			break;
		case NOT_NODE:
			value = process_not(context, node);
			break;
		case NULL_NODE:
			value = process_null(context, node);
			break;
		case NUMBER_NODE:
			value = process_number(context, node);
			break;
		case OPERATOR_NODE:
			value = process_operator(context, node);
			break;
		case OPTIONAL_NODE:
			value = process_optional(context, node);
			break;
		case POINTER_NODE:
			value = process_pointer(context, node);
			break;
		case RANGE_NODE:
			value = process_range(context, node);
			break;
		case REFERENCE_NODE:
			value = process_reference(context, node);
			break;
		case RESULT_NODE:
			value = process_result(context, node);
			break;
		case RETURN_NODE:
			value = process_return(context, node);
			break;
		case RUN_NODE:
			value = process_run(context, node);
			break;
		case SLICE_NODE:
			value = process_slice(context, node);
			break;
		case STRING_NODE:
			value = process_string(context, node);
			break;
		case STRUCT_TYPE_NODE:
			value = process_struct_type(context, node);
			break;
		case STRUCTURE_NODE:
			value = process_structure(context, node);
			break;
		case STRUCTURE_ACCESS_NODE:
			value = process_structure_access(context, node);
			break;
		case SWITCH_NODE:
			value = process_switch(context, node);
			break;
		case TAGGED_UNION_TYPE_NODE:
			value = process_tagged_union_type(context, node);
			break;
		case UNION_TYPE_NODE:
			value = process_union_type(context, node);
			break;
		case VARIABLE_NODE:
			value = process_variable(context, node);
			break;
		case WHILE_NODE:
			value = process_while(context, node);
			break;
		default:
			assert(false);
	}

	context->temporary_context = saved_temporary_context;
	
	return value;
}

Context process(Data *data, Node *root, Codegen codegen) {
	Context context = { .codegen = codegen, .data = data };
	arrsetcap(context.scopes, 32);

	process_node(&context, root);

	return context;
}
