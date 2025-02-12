#include <assert.h>
#include <stdio.h>

#include "stb/ds.h"

#include "evaluator.h"

typedef struct {
	Node *node;
	Scope *scope;
} Lookup_Define_Result;

static Lookup_Define_Result lookup_define(Context *context, char *identifier) {
	for (long int i = 0; i < arrlen(context->scopes); i++) {
		Scope *scope = &context->scopes[i];

		Node *node = scope->node;
		switch (node->kind) {
			case BLOCK_NODE:
				for (long int i = 0; i < arrlen(node->block.statements); i++) {
					Node *statement = node->block.statements[i];
					if (statement->kind == DEFINE_NODE && strcmp(statement->define.identifier, identifier) == 0) {
						return (Lookup_Define_Result) { .node = statement, .scope = scope };
					}
				}
				break;
			default:
				break;
		}
	}
	return (Lookup_Define_Result) {};
}

static Node *lookup_variable(Context *context, char *identifier) {
	for (long int i = 0; i < arrlen(context->scopes); i++) {
		Scope *scope = &context->scopes[i];
		Node *variable = shget(scope->variables, identifier);
		if (variable != NULL) {
			return variable;
		}
	}
	return NULL;
}

static Generic_Binding lookup_generic_binding(Context *context, char *identifier) {
	for (long int i = 0; i < arrlen(context->scopes); i++) {
		Scope *scope = &context->scopes[i];
		Generic_Binding binding = shget(scope->generic_bindings, identifier);
		if (binding.type != NULL) {
			return binding;
		}
	}
	return (Generic_Binding) {};
}

#define handle_semantic_error(/* Source_Location */ location, /* char * */ fmt, ...) { \
	printf("%s:%zu:%zu: " fmt "\n", location.path, location.row, location.column, __VA_ARGS__); \
	exit(1); \
}

static void print_type(Value *value, char *buffer) {
	switch (value->tag) {
		case POINTER_TYPE_VALUE: {
			sprintf(buffer, "*");
			print_type(value->pointer_type.inner, buffer + 1);
			break;
		}
		case ARRAY_TYPE_VALUE: {
			sprintf(buffer, "[]");
			print_type(value->array_type.inner, buffer + 2);
			break;
		}
		case INTERNAL_VALUE: {
			sprintf(buffer, "%s", value->internal.identifier);
			break;
		}
		case NONE_VALUE: {
			sprintf(buffer, "()");
			break;
		}
		default:
			assert(false);
	}
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

static void pattern_match(Node *node, Value *value, Generic_Argument *generics, Pattern_Match_Result *match_result) {
	switch (node->kind) {
		case POINTER_NODE: {
			if (value->tag == POINTER_TYPE_VALUE) {
				pattern_match(node->pointer.inner, value->pointer_type.inner, generics, match_result);
			}
			break;
		}
		case IDENTIFIER_NODE: {
			for (long int i = 0; i < arrlen(generics); i++) {
				if (strcmp(generics[i].identifier, node->identifier.value) == 0) {
					shput(*match_result, generics[i].identifier, value);
					break;
				}
			}
			break;
		}
		default:
			break;
	}
}

static Process_Define_Result process_define(Context *context, Node *node, Scope *scopes, Generic_Binding *generics) {
	Define_Node define = node->define;

	if (arrlen(generics) == 0 && arrlen(define.generics) > 0) {
		if (define.expression->kind == FUNCTION_NODE) {
			Function_Type_Node function_type = define.expression->function.function_type->function_type;
			if (context->call_argument_types != NULL) {
				Pattern_Match_Result result = NULL;
				assert(arrlen(context->call_argument_types) <= arrlen(function_type.arguments));
				for (long int i = 0; i < arrlen(function_type.arguments); i++) {
					Node *argument = function_type.arguments[i].type;
					Value *argument_value = context->call_argument_types[i];
					pattern_match(argument, argument_value, define.generics, &result);
				}

				generics = NULL;
				for (long int i = 0; i < arrlen(define.generics); i++) {
					Value *type_type = value_new(INTERNAL_VALUE);
					type_type->internal.identifier = "type";
					Generic_Binding binding = {
						.type = type_type, // not always true if we are matching for array len?
						.binding = shget(result, define.generics[i].identifier)
					};
					arrpush(generics, binding);
				}
			}
		}

		if (arrlen(generics) == 0) {
			return (Process_Define_Result) {};
		}
	}

	Value* cached_type = get_type(context, node);
	if (cached_type != NULL) {
		return (Process_Define_Result) {
			.value = get_data(context, node)->define.value,
			.type = cached_type
		};
	}

	Value **saved_call_argument_types = context->call_argument_types;
	context->call_argument_types = NULL;

	Scope *saved_scopes = context->scopes;
	if (scopes != NULL) context->scopes = scopes;

	arrpush(context->scopes, (Scope) { .node = node });
	size_t saved_generic_id = context->generic_id;

	if (arrlen(generics) > 0) {
		context->generic_id = ++context->generic_id_counter;
	}
	
	for (long int i = 0; i < arrlen(generics); i++) {
		shput(arrlast(context->scopes).generic_bindings, define.generics[i].identifier, generics[i]);
	}

	if (define.generic_constraint != NULL) {
		process_node(context, define.generic_constraint);
		Value *result = evaluate(context, define.generic_constraint);
		if (!result->boolean.value) {
			return (Process_Define_Result) {};
		}
	}

	process_node(context, define.expression);
	Value *type = get_type(context, define.expression);
	Value *value = evaluate(context, define.expression);

	context->generic_id = saved_generic_id;
	(void) arrpop(context->scopes);
	if (scopes != NULL) context->scopes = saved_scopes;

	context->call_argument_types = saved_call_argument_types;

	Node_Data *data = node_data_new(DEFINE_NODE);
	if (arrlen(generics) == 0) {
		data->define.value = value;
		set_type(context, node, type);
	}
	set_data(context, node, data);

	return (Process_Define_Result) {
		.value = value,
		.type = type
	};
}

static void process_call(Context *context, Node *node) {
	Call_Node call = node->call;

	Value **argument_types = NULL;
	for (long int i = 0; i < arrlen(call.arguments); i++) {
		context->wanted_type = NULL;
		process_node(context, call.arguments[i]);
		Value *type = get_type(context, call.arguments[i]);;
		arrpush(argument_types, type);

		reset_node(context, node);
	}
	Value **saved_call_argument_types = context->call_argument_types;
	context->call_argument_types = argument_types;

	process_node(context, call.function);
	Value *function_type = get_type(context, call.function);
	if (function_type->tag != FUNCTION_TYPE_VALUE) {
		char given_string[64] = {};
		print_type(function_type, given_string);
		handle_semantic_error(node->location, "Expected function pointer, but got '%s'", given_string);
	}

	context->call_argument_types = saved_call_argument_types;

	for (long int i = 0; i < arrlen(call.arguments); i++) {
		Value *wanted_type = NULL;
		if (i < arrlen(function_type->function_type.arguments) - 1 || !function_type->function_type.variadic) {
			wanted_type = function_type->function_type.arguments[i].type;
		}

		context->wanted_type = wanted_type;
		process_node(context, call.arguments[i]);
		Value *type = get_type(context, call.arguments[i]);;

		if (wanted_type != NULL && !value_equal(wanted_type, type)) {
			handle_type_error(node, wanted_type, type);
		}
	}

	Node_Data *data = node_data_new(CALL_NODE);
	data->call.function_type = function_type;
	set_data(context, node, data);
	set_type(context, node, function_type->function_type.return_type);
}

static void process_identifier(Context *context, Node *node) {
	Identifier_Node identifier = node->identifier;

	Node_Data *data = node_data_new(IDENTIFIER_NODE);

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
	if (identifier.module != NULL) {
		process_node(context, identifier.module);
		Value *module_value = evaluate(context, identifier.module);
		for (long int i = 0; i < arrlen(module_value->module.body->block.statements); i++) {
			Node *statement = module_value->module.body->block.statements[i];
			if (statement->kind == DEFINE_NODE && strcmp(statement->define.identifier, identifier.value) == 0) {
				Process_Define_Result result = process_define(context, statement, module_value->module.scopes, generics);
				type = result.type;
				value = result.value;

				if (type == NULL) {
					handle_semantic_error(node->location, "Unable to resolve generics for identifier '%s'", identifier.value);
				}
				break;
			}
		}
	} else if (strcmp(identifier.value, "byte") == 0
			|| strcmp(identifier.value, "uint") == 0
			|| strcmp(identifier.value, "type") == 0) {
		value = value_new(INTERNAL_VALUE);
		value->internal.identifier = identifier.value;

		type = value_new(INTERNAL_VALUE);
		type->internal.identifier = "type";
	}
	else {
		Lookup_Define_Result lookup_define_result = lookup_define(context, identifier.value);
		if (lookup_define_result.node != NULL) {
			Scope *new_scopes = NULL;
			for (long i = 0; i < arrlen(context->scopes); i++) {
				arrpush(new_scopes, context->scopes[i]);

				if (lookup_define_result.scope == &context->scopes[i]) break;
			}

			Process_Define_Result result = process_define(context, lookup_define_result.node, new_scopes, generics);
			type = result.type;
			value = result.value;

			if (type == NULL) {
				handle_semantic_error(node->location, "Unable to resolve generics for identifier '%s'", identifier.value);
			}
		}

		if (context->current_function != NULL) {
			Node *variable_node = lookup_variable(context, identifier.value);
			if (variable_node != NULL) {
				data->identifier.kind = IDENTIFIER_VARIABLE;
				data->identifier.variable_definition = variable_node;

				type = get_data(context, variable_node)->variable.type;
			}

			Node *current_function_type = context->current_function->function.function_type;
			for (long int i = 0; i < arrlen(current_function_type->function_type.arguments); i++) {
				if (strcmp(current_function_type->function_type.arguments[i].identifier, identifier.value) == 0) {
					data->identifier.kind = IDENTIFIER_ARGUMENT;
					data->identifier.argument_index = i;

					type = get_data(context, current_function_type)->function_type.value->function_type.arguments[i].type;
					break;
				}
			}
		}

		Generic_Binding generic_binding = lookup_generic_binding(context, identifier.value);
		if (generic_binding.binding != NULL) {
			value = generic_binding.binding;
			type = generic_binding.type;
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

	set_data(context, node, data);
	set_type(context, node, type);
}

static void process_module(Context *context, Node *node) {
	Module_Node module = node->module;
	process_node(context, module.body);
	set_type(context, node, value_new(MODULE_TYPE_VALUE));
}

static void process_string(Context *context, Node *node) {
	String_Node string = node->string;

	Value *pointer = value_new(POINTER_TYPE_VALUE);
	Value *array = value_new(ARRAY_TYPE_VALUE);
	Value *byte = value_new(INTERNAL_VALUE);
	byte->internal.identifier = "byte";
	array->array_type.inner = byte;
	pointer->pointer_type.inner = array;

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

	Node_Data *data = node_data_new(STRING_NODE);
	data->string.type = pointer;
	data->string.value = new_string;
	set_data(context, node, data);
	set_type(context, node, pointer);
}

static void process_number(Context *context, Node *node) {
	Value *wanted_type = context->wanted_type;
	if (wanted_type == NULL) {
		wanted_type = value_new(INTERNAL_VALUE);
		wanted_type->internal.identifier = "uint";
	}

	Node_Data *data = node_data_new(NUMBER_NODE);
	data->number.type = wanted_type;
	set_data(context, node, data);
	set_type(context, node, wanted_type);
}

static void process_variable(Context *context, Node *node) {
	Variable_Node variable = node->variable;

	Scope *scope = &arrlast(context->scopes);
	shput(scope->variables, variable.identifier, node);

	Value *type = NULL;
	if (variable.type != NULL) {
		type = evaluate(context, variable.type);
	}

	process_node(context, variable.value);
	Value *value_type = get_type(context, variable.value);
	if (type == NULL) {
		type = value_type;
	} else {
		if (!value_equal(type, value_type)) {
			handle_type_error(node, type, value_type);
		}
	}

	Node_Data *data = node_data_new(VARIABLE_NODE);
	data->variable.type = type;
	set_data(context, node, data);
}

static void process_if(Context *context, Node *node) {
	If_Node if_ = node->if_;

	process_node(context, if_.condition);
	if (if_.static_) {
		Value *evaluated = evaluate(context, if_.condition);

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
	Binary_Operatory_Node binary_operator = node->binary_operator;

	process_node(context, binary_operator.left);
	process_node(context, binary_operator.right);
}

static void process_function(Context *context, Node *node) {
	Function_Node function = node->function;

	bool compile_only_parent = context->compile_only;

	context->compile_only = false;
	process_node(context, function.function_type);

	Node_Data *function_type_data = get_data(context, function.function_type);

	Value *function_type_value = function_type_data->function_type.value;

	set_type(context, node, function_type_value);

	Node *current_function_saved = context->current_function;
	context->current_function = node;
	if (function.body != NULL) {
		arrpush(context->scopes, (Scope) { .node = function.body });
		process_node(context, function.body);
		(void) arrpop(context->scopes);
	}
	context->current_function = current_function_saved;

	if (context->compile_only) {
		hmput(context->compile_only_function_nodes, node, true);
	}

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

void process_node(Context *context, Node *node) {
	Value* type = get_type(context, node);
	if (type != NULL) {
		return;
	}

	switch (node->kind) {
		case DEFINE_NODE: {
			process_define(context, node, NULL, NULL);
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
		case RETURN_NODE: {
			break;
		}
		case VARIABLE_NODE: {
			process_variable(context, node);
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
		default:
			assert(false);
	}
}

Context process(Node *root) {
	Context context = {
		.root = root,
		.current_function = NULL,
		.current_value = value_new(NONE_VALUE),
		.call_argument_types = NULL
	};

	process_node(&context, root);

	return context;
}
