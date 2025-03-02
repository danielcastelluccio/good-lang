#include <assert.h>
#include <stdio.h>

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
		Value *generic;
		size_t argument;
	};
	Value *type;
	enum {
		LOOKUP_RESULT_FAIL,
		LOOKUP_RESULT_DEFINE,
		LOOKUP_RESULT_VARIABLE,
		LOOKUP_RESULT_ARGUMENT,
		LOOKUP_RESULT_GENERIC
	} tag;
} Lookup_Result;

typedef struct {
	Node *node;
	Scope *scope;
} Lookup_Define_Result;

static Lookup_Result lookup(Context *context, char *identifier) {
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
			for (long int i = 0; i < arrlen(current_function_type->function_type.arguments); i++) {
				if (strcmp(current_function_type->function_type.arguments[i].identifier, identifier) == 0) {
					return (Lookup_Result) { .tag = LOOKUP_RESULT_ARGUMENT, .argument = i, .type = get_data(context, current_function_type)->function_type.value->function_type.arguments[i].type };
				}
			}
		}

		Generic_Binding binding = shget(scope->generic_bindings, identifier);
		if (binding.type != NULL) {
			return (Lookup_Result) { .tag = LOOKUP_RESULT_GENERIC, .generic = binding.binding, .type = binding.type };
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
	printf("%s:%zu:%zu: " fmt "\n", location.path, location.row, location.column, __VA_ARGS__); \
	exit(1); \
}

static int print_type(Value *value, char *buffer) {
	char *buffer_start = buffer;
	switch (value->tag) {
		case POINTER_TYPE_VALUE: {
			buffer += sprintf(buffer, "*");
			buffer += print_type(value->pointer_type.inner, buffer);
			break;
		}
		case ARRAY_TYPE_VALUE: {
			buffer += sprintf(buffer, "[]");
			buffer += print_type(value->array_type.inner, buffer);
			break;
		}
		case INTERNAL_VALUE: {
			buffer += sprintf(buffer, "%s", value->internal.identifier);
			break;
		}
		case STRUCTURE_TYPE_VALUE: {
			buffer += sprintf(buffer, "struct{");
			for (long int i = 0; i < arrlen(value->structure_type.items); i++) {
				buffer += sprintf(buffer, "%s:", value->structure_type.items[i].identifier);
				buffer += print_type(value->structure_type.items[i].type, buffer);
				if (i < arrlen(value->structure_type.items) - 1) {
					buffer += sprintf(buffer, ",");
				}
			}
			buffer += sprintf(buffer, "}");
			break;
		}
		case NONE_VALUE: {
			buffer += sprintf(buffer, "()");
			break;
		}
		case DEFINE_DATA_VALUE: {
			buffer += print_type(value->define_data.value, buffer);
			break;
		}
		default:
			assert(false);
	}

	return buffer - buffer_start;
}

static void handle_type_error(Node *node, Value *wanted, Value *given) {
	char wanted_string[64] = {};
	print_type(wanted, wanted_string);
	char given_string[64] = {};
	print_type(given, given_string);
	handle_semantic_error(node->location, "Expected '%s', but got '%s'", wanted_string, given_string);
}

static void process_block(Context *context, Node *node) {
	Block_Node block = node->block;
	arrpush(context->scopes, (Scope) { .node = node });
	for (long int i = 0; i < arrlen(block.statements); i++) {
		Node *statement = block.statements[i];
		process_node(context, statement);
	}
	(void) arrpop(context->scopes);
}

typedef struct {
	Value *value;
	Value *type;
} Process_Define_Result;

typedef struct { char *key; Value *value; } *Pattern_Match_Result;

static bool pattern_match(Node *node, Value *value, Context *context, Generic_Argument *generics, Pattern_Match_Result *match_result) {
	switch (node->kind) {
		case POINTER_NODE: {
			if (value->tag == POINTER_TYPE_VALUE) {
				return pattern_match(node->pointer.inner, value->pointer_type.inner, context, generics, match_result);
			}
			break;
		}
		case IDENTIFIER_NODE: {
			for (long int i = 0; i < arrlen(generics); i++) {
				if (strcmp(generics[i].identifier, node->identifier.value) == 0) {
					Value *previous_value = shget(*match_result, generics[i].identifier);
					if (previous_value != NULL) {
						if (!value_equal(previous_value, value)) {
							return false;
						}
					}

					shput(*match_result, generics[i].identifier, value);
					break;
				}
			}

			while (value->tag == DEFINE_DATA_VALUE) {
				for (long int i = 0; i < arrlen(node->identifier.generics); i++) {
					Lookup_Result result = lookup(context, node->identifier.value);
					assert(result.tag == LOOKUP_RESULT_DEFINE);
					if (value->define_data.define_node == result.define.node) {
						if (!pattern_match(node->identifier.generics[i], value->define_data.bindings[i].binding, context, generics, match_result)) {
							return false;
						};
					}
				}
				value = value->define_data.value;
			}
			break;
		}
		default:
			break;
	}

	return true;
}

static Process_Define_Result process_define(Context *context, Node *node, Scope *scopes, Generic_Binding *generics, size_t generic_id) {
	Define_Node define = node->define;

	Scope *saved_scopes = context->scopes;
	if (scopes != NULL) context->scopes = scopes;
	arrpush(context->scopes, (Scope) { .node = node });

	size_t saved_saved_generic_id = context->generic_id;
	context->generic_id = generic_id;

	if (arrlen(generics) == 0 && arrlen(define.generics) > 0) {
		if (context->temporary_context.call_argument_types != NULL || context->temporary_context.call_wanted_type != NULL) {
			bool pattern_match_collision = false;
			if (define.expression->kind == FUNCTION_NODE) {
				Function_Type_Node function_type = define.expression->function.function_type->function_type;
				Pattern_Match_Result result = NULL;
				if (context->temporary_context.call_argument_types != NULL) {
					for (long int i = 0; i < arrlen(function_type.arguments) && i < arrlen(context->temporary_context.call_argument_types); i++) {
						Node *argument = function_type.arguments[i].type;
						Value *argument_value = context->temporary_context.call_argument_types[i];
						if (!pattern_match(argument, argument_value, context, define.generics, &result)) {
							pattern_match_collision = true;
						};
					}
				}

				Node *return_ = function_type.return_;
				if (return_ != NULL && context->temporary_context.call_wanted_type != NULL) {
					if (!pattern_match(return_, context->temporary_context.call_wanted_type, context, define.generics, &result)) {
						pattern_match_collision = true;
					};
				}

				generics = NULL;
				for (long int i = 0; i < arrlen(define.generics); i++) {
					process_node(context, define.generics[i].type);

					Value *binding = shget(result, define.generics[i].identifier);
					if (binding != NULL) {
						Generic_Binding generic_binding = {
							.type = evaluate(context, define.generics[i].type),
							.binding = binding
						};

						arrpush(generics, generic_binding);
					}
				}
			}

			if (arrlen(generics) < arrlen(define.generics) || pattern_match_collision) {
				return (Process_Define_Result) {};
			}
		} else {
			return (Process_Define_Result) {};
		}
	}

	Node_Data *cached_data = get_data(context, node);
	if (cached_data != NULL) {
		if (cached_data->define.kind == DEFINE_SINGLE) {
			context->generic_id = saved_saved_generic_id;
			(void) arrpop(context->scopes);
			if (scopes != NULL) context->scopes = saved_scopes;
			return (Process_Define_Result) {
				.value = cached_data->define.value.binding,
				.type = cached_data->define.value.type
			};
		} else if (cached_data->define.kind == DEFINE_GENERIC) {
			for (long int i = 0; i < arrlen(cached_data->define.generic_values); i++) {
				if (arrlen(generics) == arrlen(cached_data->define.generic_values[i].generics)) {
					for (long int j = 0; j < arrlen(cached_data->define.generic_values[i].generics); j++) {
						if (type_assignable(generics[j].binding, cached_data->define.generic_values[i].generics[j].binding)) {
							Generic_Binding binding = cached_data->define.generic_values[i].value;
							context->generic_id = saved_saved_generic_id;
							(void) arrpop(context->scopes);
							if (scopes != NULL) context->scopes = saved_scopes;
							return (Process_Define_Result) {
								.value = binding.binding,
								.type = binding.type
							};
						}
					}
				}
			}
		} else {
			assert(false);
		}
	}
	size_t saved_generic_id = context->generic_id;

	if (arrlen(generics) > 0) {
		context->generic_id = ++context->generic_id_counter;
	}
	
	for (long int i = 0; i < arrlen(generics); i++) {
		shput(arrlast(context->scopes).generic_bindings, define.generics[i].identifier, generics[i]);
	}

	// if (define.generic_constraint != NULL) {
	// 	process_node(context, define.generic_constraint);
	// 	Value *result = strip_define_data(evaluate(context, define.generic_constraint));
	// 	if (!result->boolean.value) {
	// 		return (Process_Define_Result) {};
	// 	}
	// }

	process_node(context, define.expression);
	Value *type = get_type(context, define.expression);
	Value *value = evaluate(context, define.expression);

	Value *wrapped_value = value_new(DEFINE_DATA_VALUE);
	wrapped_value->define_data.value = value;
	wrapped_value->define_data.define_node = node;
	wrapped_value->define_data.bindings = generics;

	context->generic_id = saved_generic_id;
	(void) arrpop(context->scopes);
	if (scopes != NULL) context->scopes = saved_scopes;

	Generic_Binding binding = {
		.binding = wrapped_value,
		.type = type
	};

	Node_Data *data;
	if (cached_data != NULL) {
		data = cached_data;
	} else {
		data = node_data_new(DEFINE_NODE);
		set_data(context, node, data);
	}

	if (arrlen(generics) == 0) {
		data->define.kind = DEFINE_SINGLE;
		data->define.value = binding;
	} else {
		data->define.kind = DEFINE_GENERIC;
		Generic_Value generic_value = {
			.value = binding,
			.generics = generics
		};

		arrpush(data->define.generic_values, generic_value);
	}

	context->generic_id = saved_saved_generic_id;
	return (Process_Define_Result) {
		.value = wrapped_value,
		.type = type
	};
}

static void process_call(Context *context, Node *node) {
	Call_Node call = node->call;

	Value **argument_types = NULL;
	for (long int i = 0; i < arrlen(call.arguments); i++) {
		Temporary_Context temporary_context = { .wanted_type = NULL };
		process_node_context(context, temporary_context, call.arguments[i]);
		Value *type = get_type(context, call.arguments[i]);;
		arrpush(argument_types, type);

		reset_node(context, node);
	}

	Temporary_Context temporary_context = { .call_argument_types = argument_types, .call_wanted_type = context->temporary_context.wanted_type };
	process_node_context(context, temporary_context, call.function);
	Value *function_type = get_type(context, call.function);
	if (function_type->tag != FUNCTION_TYPE_VALUE) {
		char given_string[64] = {};
		print_type(function_type, given_string);
		handle_semantic_error(node->location, "Expected function pointer, but got '%s'", given_string);
	}

	if (arrlen(call.arguments) != arrlen(function_type->function_type.arguments) && !function_type->function_type.variadic) {
		handle_semantic_error(node->location, "Expected %li arguments, but got %li arguments", arrlen(function_type->function_type.arguments), arrlen(call.arguments));
	}

	for (long int i = 0; i < arrlen(call.arguments); i++) {
		Value *wanted_type = NULL;
		if (i < arrlen(function_type->function_type.arguments) - 1 || !function_type->function_type.variadic) {
			wanted_type = function_type->function_type.arguments[i].type;
		}

		Temporary_Context temporary_context = { .wanted_type = wanted_type };
		process_node_context(context, temporary_context, call.arguments[i]);
		Value *type = get_type(context, call.arguments[i]);;

		if (wanted_type != NULL && !type_assignable(wanted_type, type)) {
			handle_type_error(node, wanted_type, type);
		}
	}

	Node_Data *data = node_data_new(CALL_NODE);
	data->call.function_type = function_type;
	set_data(context, node, data);
	set_type(context, node, function_type->function_type.return_type);
}

static Value *create_string_type() {
	Value *pointer = value_new(POINTER_TYPE_VALUE);
	Value *array = value_new(ARRAY_TYPE_VALUE);
	Value *byte = value_new(INTERNAL_VALUE);
	byte->internal.identifier = "byte";
	array->array_type.inner = byte;
	pointer->pointer_type.inner = array;

	return pointer;
}

static void process_identifier(Context *context, Node *node) {
	Identifier_Node identifier = node->identifier;

	Node_Data *data = node_data_new(IDENTIFIER_NODE);
	data->identifier.want_pointer = context->temporary_context.want_pointer;

	Generic_Binding *generics = NULL;
	for (long int i = 0; i < arrlen(identifier.generics); i++) {
		Node *generic = identifier.generics[i];
		process_node(context, generic);
		Value *type = get_type(context, generic);
		Value *value = evaluate(context, generic);

		Generic_Binding binding = {
			.binding = value,
			.type = type
		};

		arrpush(generics, binding);
	}

	Value *value = NULL;
	Value *type = NULL;
	if (identifier.module == NULL &&
			(strcmp(identifier.value, "byte") == 0
			|| strcmp(identifier.value, "uint") == 0
			|| strcmp(identifier.value, "void") == 0
			|| strcmp(identifier.value, "type") == 0)) {
		value = value_new(INTERNAL_VALUE);
		value->internal.identifier = identifier.value;

		type = value_new(INTERNAL_VALUE);
		type->internal.identifier = "type";
	} else if (strcmp(identifier.value, "import") == 0) {
		value = value_new(INTERNAL_VALUE);
		value->internal.identifier = identifier.value;

		type = value_new(FUNCTION_TYPE_VALUE);
		Function_Argument_Value argument = {
			.identifier = "",
			.type = create_string_type()
		};
		arrpush(type->function_type.arguments, argument);
		type->function_type.return_type = value_new(MODULE_TYPE_VALUE);
	} else if (strcmp(identifier.value, "size_of") == 0) {
		value = value_new(INTERNAL_VALUE);
		value->internal.identifier = identifier.value;

		Value *type_type = value_new(INTERNAL_VALUE);
		type_type->internal.identifier = "type";

		type = value_new(FUNCTION_TYPE_VALUE);
		Function_Argument_Value argument = {
			.identifier = "",
			.type = type_type
		};
		arrpush(type->function_type.arguments, argument);
		type->function_type.return_type = value_new(INTERNAL_VALUE);
		type->function_type.return_type->internal.identifier = "uint";
	} else {
		Node *define_node = NULL;
		Scope *define_scopes = NULL;
		size_t generic_id = context->generic_id;
		if (identifier.module != NULL) {
			process_node(context, identifier.module);
			Value *module_value = strip_define_data(evaluate(context, identifier.module));
			for (long int i = 0; i < arrlen(module_value->module.body->block.statements); i++) {
				Node *statement = module_value->module.body->block.statements[i];
				if (statement->kind == DEFINE_NODE && strcmp(statement->define.identifier, identifier.value) == 0) {
					define_node = statement;
					generic_id = module_value->module.generic_id;

					for (long i = 0; i < arrlen(module_value->module.scopes); i++) {
						arrpush(define_scopes, module_value->module.scopes[i]);
					}
					break;
				}
			}
		}

		Lookup_Result lookup_result = lookup(context, identifier.value);
		if (lookup_result.tag == LOOKUP_RESULT_DEFINE) {
			for (long i = 0; i < arrlen(context->scopes); i++) {
				arrpush(define_scopes, context->scopes[i]);

				if (lookup_result.define.scope == &context->scopes[i]) break;
			}

			define_node = lookup_result.define.node;
		}

		if (define_node != NULL) {
			bool compile_only_parent = context->compile_only;
			Process_Define_Result result = process_define(context, define_node, define_scopes, generics, generic_id);
			type = result.type;
			value = result.value;

			if (type == NULL) {
				handle_semantic_error(node->location, "Unable to resolve generics for identifier '%s'", identifier.value);
			}
			context->compile_only = compile_only_parent;
		}

		if (lookup_result.tag == LOOKUP_RESULT_VARIABLE) {
			data->identifier.kind = IDENTIFIER_VARIABLE;
			data->identifier.variable_definition = lookup_result.variable;

			type = lookup_result.type;
			if (data->identifier.want_pointer) {
				Value *pointer_type = value_new(POINTER_TYPE_VALUE);
				pointer_type->pointer_type.inner = type;
				type = pointer_type;
			}

			if (context->temporary_context.assign_value != NULL) {
				Temporary_Context temporary_context = { .wanted_type = type };
				process_node_context(context, temporary_context, context->temporary_context.assign_value);

				Value *value_type = get_type(context, context->temporary_context.assign_value);
				if (!type_assignable(type, value_type)) {
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
				Value *pointer_type = value_new(POINTER_TYPE_VALUE);
				pointer_type->pointer_type.inner = type;
				type = pointer_type;
			}
		}

		if (lookup_result.tag == LOOKUP_RESULT_GENERIC) {
			value = lookup_result.generic;
			type = lookup_result.type;
		}
	}

	if (type == NULL) {
		handle_semantic_error(node->location, "Identifier '%s' not found", identifier.value);
	}

	if (value != NULL) {
		if (value->tag == INTERNAL_VALUE && strcmp(value->internal.identifier, "type") == 0) {
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
	set_type(context, node, value_new(MODULE_TYPE_VALUE));
}

static void process_structure_type(Context *context, Node *node) {
	Structure_Type_Node structure_type = node->structure_type;
	for (long int i = 0; i < arrlen(structure_type.items); i++) {
		process_node(context, structure_type.items[i].type);
	}

	Value *value = value_new(INTERNAL_VALUE);
	value->internal.identifier = "type";
	set_type(context, node, value);
}

static void process_string(Context *context, Node *node) {
	String_Node string = node->string;

	size_t original_length = strlen(string.value);
	char *new_string = malloc(original_length + 1);
	memset(new_string, 0, original_length + 1);

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

	new_string[new_index] = '\0';

	Value *type = create_string_type();

	Node_Data *data = node_data_new(STRING_NODE);
	data->string.type = type;
	data->string.value = new_string;
	set_data(context, node, data);
	set_type(context, node, type);
}

static void process_number(Context *context, Node *node) {
	Value *wanted_type = context->temporary_context.wanted_type;
	bool invalid_wanted_type = false;
	if (wanted_type == NULL) invalid_wanted_type = true;
	else if (wanted_type->tag != INTERNAL_VALUE || strcmp(wanted_type->internal.identifier, "uint") != 0) invalid_wanted_type = true;

	if (invalid_wanted_type) {
		wanted_type = value_new(INTERNAL_VALUE);
		wanted_type->internal.identifier = "uint";
	}

	Node_Data *data = node_data_new(NUMBER_NODE);
	data->number.type = wanted_type;
	set_data(context, node, data);
	set_type(context, node, wanted_type);
}

static void process_null(Context *context, Node *node) {
	Value *wanted_type = context->temporary_context.wanted_type;
	if (wanted_type == NULL) {
		assert(false);
	}

	Node_Data *data = node_data_new(NULL_NODE);
	data->null_.type = wanted_type;
	set_data(context, node, data);
	set_type(context, node, wanted_type);
}

static void process_structure(Context *context, Node *node) {
	Structure_Node structure = node->structure;

	Value *wanted_type = context->temporary_context.wanted_type;
	if (wanted_type == NULL) {
		assert(false);
	}

	Value *stripped_wanted_type = strip_define_data(wanted_type);

	assert(stripped_wanted_type->tag == STRUCTURE_TYPE_VALUE);

	for (long int i = 0; i < arrlen(structure.values); i++) {
		Temporary_Context temporary_context = { .wanted_type = stripped_wanted_type->structure_type.items[i].type };
		process_node_context(context, temporary_context, structure.values[i]);
	}

	Node_Data *data = node_data_new(STRUCTURE_NODE);
	data->structure.type = wanted_type;
	set_data(context, node, data);
	set_type(context, node, wanted_type);
}

static void process_run(Context *context, Node *node) {
	Run_Node run = node->run;

	process_node(context, run.value);
	Value *value = evaluate(context, run.value);

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

static void process_structure_access(Context *context, Node *node) {
	Structure_Access_Node structure_access = node->structure_access;

	process_node(context, structure_access.structure);

	Value *structure_pointer_type = strip_define_data(get_type(context, structure_access.structure));
	if (structure_pointer_type->tag != POINTER_TYPE_VALUE) {
		reset_node(context, structure_access.structure);

		Temporary_Context temporary_context = { .want_pointer = true };
		process_node_context(context, temporary_context, structure_access.structure);

		structure_pointer_type = strip_define_data(get_type(context, structure_access.structure));
	}

	if (structure_pointer_type->tag != POINTER_TYPE_VALUE || strip_define_data(structure_pointer_type->pointer_type.inner)->tag != STRUCTURE_TYPE_VALUE) {
		char given_string[64] = {};
		print_type(structure_pointer_type, given_string);
		handle_semantic_error(node->location, "Expected structure pointer, but got '%s'", given_string);
	}

	Value *structure_type = strip_define_data(structure_pointer_type->pointer_type.inner);

	Value *item_type = NULL;
	for (long int i = 0; i < arrlen(structure_type->structure_type.items); i++) {
		if (strcmp(structure_type->structure_type.items[i].identifier, structure_access.item) == 0) {
			item_type = structure_type->structure_type.items[i].type;
		}
	}

	Node_Data *data = node_data_new(STRUCTURE_ACCESS_NODE);
	data->structure_access.structure_value = structure_type;
	data->structure_access.want_pointer = context->temporary_context.want_pointer;

	if (context->temporary_context.assign_value != NULL) {
		Temporary_Context temporary_context = { .wanted_type = item_type };
		process_node_context(context, temporary_context, context->temporary_context.assign_value);

		Value *value_type = get_type(context, context->temporary_context.assign_value);
		if (!type_assignable(item_type, value_type)) {
			handle_type_error(context->temporary_context.assign_node, item_type, value_type);
		}
		data->structure_access.assign_value = context->temporary_context.assign_value;
	} else {
		if (data->structure_access.want_pointer) {
			Value *pointer_item_type = value_new(POINTER_TYPE_VALUE);
			pointer_item_type->pointer_type.inner = item_type;
			item_type = pointer_item_type;
		}

		set_type(context, node, item_type);
	}
	set_data(context, node, data);
}

static void process_array_access(Context *context, Node *node) {
	Array_Access_Node array_access = node->array_access;

	process_node(context, array_access.array);
	process_node(context, array_access.index);

	Value *array_pointer_type = strip_define_data(get_type(context, array_access.array));
	if (array_pointer_type->tag != POINTER_TYPE_VALUE) {
		reset_node(context, array_access.array);

		Temporary_Context temporary_context = { .want_pointer = true };
		process_node_context(context, temporary_context, array_access.array);

		array_pointer_type = strip_define_data(get_type(context, array_access.array));
	}

	if (array_pointer_type->tag != POINTER_TYPE_VALUE || strip_define_data(array_pointer_type->pointer_type.inner)->tag != ARRAY_TYPE_VALUE) {
		char given_string[64] = {};
		print_type(array_pointer_type, given_string);
		handle_semantic_error(node->location, "Expected array pointer, but got '%s'", given_string);
	}

	Value *array_type = strip_define_data(array_pointer_type->pointer_type.inner);
	Value *item_type = array_type->array_type.inner;

	Node_Data *data = node_data_new(ARRAY_ACCESS_NODE);
	data->array_access.array_value = array_type;
	data->array_access.want_pointer = context->temporary_context.want_pointer;

	if (context->temporary_context.assign_value != NULL) {
		Temporary_Context temporary_context = { .wanted_type = item_type };
		process_node_context(context, temporary_context, context->temporary_context.assign_value);

		Value *value_type = get_type(context, context->temporary_context.assign_value);
		if (!type_assignable(item_type, value_type)) {
			handle_type_error(context->temporary_context.assign_node, item_type, value_type);
		}
		data->structure_access.assign_value = context->temporary_context.assign_value;
	} else {
		if (data->structure_access.want_pointer) {
			Value *pointer_item_type = value_new(POINTER_TYPE_VALUE);
			pointer_item_type->pointer_type.inner = item_type;
			item_type = pointer_item_type;
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
	Value *type = NULL;
	if (variable.type != NULL) {
		process_node(context, variable.type);
		type = evaluate(context, variable.type);
		temporary_context.wanted_type = type;
	}

	if (variable.value != NULL) {
		process_node_context(context, temporary_context, variable.value);
	}

	Value *value_type = get_type(context, variable.value);
	assert(value_type != NULL || type != NULL);
	if (type == NULL) {
		type = value_type;
	} else if (value_type != NULL) {
		if (!type_assignable(type, value_type)) {
			handle_type_error(node, type, value_type);
		}
	}

	Node_Data *data = node_data_new(VARIABLE_NODE);
	data->variable.type = type;
	set_data(context, node, data);
}

static void process_assign(Context *context, Node *node) {
	Assign_Node assign = node->assign;

	Temporary_Context temporary_context = { .assign_value = assign.value, .assign_node = node };
	process_node_context(context, temporary_context, assign.container);
}

static void process_return(Context *context, Node *node) {
	Return_Node return_ = node->return_;

	if (return_.value != NULL) {
		Node *current_function = NULL;
		for (long int i = 0; i < arrlen(context->scopes); i++) {
			Node *scope_node = context->scopes[arrlen(context->scopes) - i - 1].node;
			if (scope_node->kind == FUNCTION_NODE) {
				current_function = scope_node;
			}
		}

		Value *return_type = get_data(context, current_function->function.function_type)->function_type.value->function_type.return_type;
		Temporary_Context temporary_context = { .wanted_type = return_type };
		process_node_context(context, temporary_context, return_.value);

		Value *type = get_type(context, return_.value);
		if (!type_assignable(return_type, type)) {
			handle_type_error(node, return_type, type);
		}
	}
}

static void process_if(Context *context, Node *node) {
	If_Node if_ = node->if_;

	process_node(context, if_.condition);
	if (if_.static_) {
		Value *evaluated = strip_define_data(evaluate(context, if_.condition));

		Node_Data *data = node_data_new(IF_NODE);
		data->if_.static_condition = evaluated->boolean.value;
		set_data(context, node, data);
	}

	process_node(context, if_.if_body);
	if (if_.else_body != NULL) {
		process_node(context, if_.else_body);
	}
}

static void process_binary_operator(Context *context, Node *node) {
	Binary_Operator_Node binary_operator = node->binary_operator;

	process_node(context, binary_operator.left);
	process_node(context, binary_operator.right);

	Value *left_type = get_type(context, binary_operator.left);
	assert(left_type != NULL);
	Value *right_type = get_type(context, binary_operator.right);
	assert(right_type != NULL);
	if (!type_assignable(left_type, right_type)) {
		char left_string[64] = {};
		print_type(left_type, left_string);
		char right_string[64] = {};
		print_type(right_type, right_string);
		handle_semantic_error(node->location, "Mismatched types '%s' and '%s'", left_string, right_string);
	}

	Value *stripped_type = strip_define_data(left_type);
	if (stripped_type->tag != INTERNAL_VALUE || strcmp(stripped_type->internal.identifier, "uint") != 0) {
		char left_string[64] = {};
		print_type(left_type, left_string);
		handle_semantic_error(node->location, "Cannot operate on '%s'", left_string);
	}

	Value *result_type = NULL;
	switch (binary_operator.operator) {
		case OPERATOR_EQUALS:
			result_type = value_new(INTERNAL_VALUE);
			result_type->internal.identifier = "bool";
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

static void process_function(Context *context, Node *node) {
	Function_Node function = node->function;

	bool compile_only_parent = context->compile_only;

	context->compile_only = false;
	process_node(context, function.function_type);

	Node_Data *function_type_data = get_data(context, function.function_type);

	Value *function_type_value = function_type_data->function_type.value;

	set_type(context, node, function_type_value);

	if (function.body != NULL) {
		arrpush(context->scopes, (Scope) { .node = node });
		process_node(context, function.body);
		(void) arrpop(context->scopes);
	}

	Node_Data *data = node_data_new(FUNCTION_NODE);
	if (context->compile_only) {
		data->function.compile_only = true;
	}
	set_data(context, node, data);

	context->compile_only = compile_only_parent;
}

static void process_function_type(Context *context, Node *node) {
	Function_Type_Node function_type = node->function_type;

	Function_Argument_Value *argument_type_values = NULL;
	for (long int i = 0; i < arrlen(function_type.arguments); i++) {
		process_node(context, function_type.arguments[i].type);
		Function_Argument_Value argument = {
			.identifier = function_type.arguments[i].identifier,
			.type = evaluate(context, function_type.arguments[i].type)
		};
		arrpush(argument_type_values, argument);
	}

	Value *return_type_value = value_new(INTERNAL_VALUE);
	return_type_value->internal.identifier = "void";
	if (function_type.return_ != NULL) {
		process_node(context, function_type.return_);
		return_type_value = evaluate(context, function_type.return_);
	}

	Value *function_type_value = value_new(FUNCTION_TYPE_VALUE);
	function_type_value->function_type.arguments = argument_type_values;
	function_type_value->function_type.return_type = return_type_value;
	function_type_value->function_type.variadic = function_type.variadic;

	Node_Data *data = node_data_new(FUNCTION_TYPE_NODE);
	data->function_type.value = function_type_value;
	set_data(context, node, data);
}

static void process_pointer(Context *context, Node *node) {
	Pointer_Node pointer = node->pointer;
	process_node(context, pointer.inner);

	Value *value = value_new(INTERNAL_VALUE);
	value->internal.identifier = "type";
	set_type(context, node, value);
}

static void process_array(Context *context, Node *node) {
	Array_Node array = node->array;
	process_node(context, array.inner);

	Value *value = value_new(INTERNAL_VALUE);
	value->internal.identifier = "type";
	set_type(context, node, value);
}

void process_node_context(Context *context, Temporary_Context temporary_context, Node *node) {
	Value* type = get_type(context, node);
	if (type != NULL) {
		return;
	}

	Temporary_Context saved_temporary_context = context->temporary_context;
	context->temporary_context = temporary_context;

	switch (node->kind) {
		case DEFINE_NODE: {
			process_define(context, node, NULL, NULL, context->generic_id);
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
		case STRUCTURE_NODE: {
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
		case ASSIGN_NODE: {
			process_assign(context, node);
			break;
		}
		case RETURN_NODE: {
			process_return(context, node);
			break;
		}
		case IF_NODE: {
			process_if(context, node);
			break;
		}
		case BINARY_OPERATOR_NODE: {
			process_binary_operator(context, node);
			break;
		}
		case FUNCTION_NODE: {
			process_function(context, node);
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
		case ARRAY_NODE: {
			process_array(context, node);
			break;
		}
		case MODULE_NODE: {
			process_module(context, node);
			break;
		}
		case STRUCTURE_TYPE_NODE: {
			process_structure_type(context, node);
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
