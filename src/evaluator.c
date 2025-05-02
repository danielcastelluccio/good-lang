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

#include <setjmp.h>

#include <stdio.h>

jmp_buf jmp;
Value jmp_result;

Value *function_arguments;

bool value_equal(Value_Data *value1, Value_Data *value2) {
	if (value2 == NULL) return false;
	if (value1->tag != value2->tag) return false;

	switch (value1->tag) {
		case POINTER_TYPE_VALUE: {
			return value_equal(value1->pointer_type.inner.value, value2->pointer_type.inner.value);
		}
		case ARRAY_TYPE_VALUE: {
			if ((value1->array_type.size.value == NULL && value2->array_type.size.value != NULL) || (value1->array_type.size.value != NULL && value2->array_type.size.value == NULL)) return false;
			if (value1->array_type.size.value != NULL) {
				if (!value_equal(value1->array_type.size.value, value2->array_type.size.value)) return false;
			}

			return value_equal(value1->array_type.inner.value, value2->array_type.inner.value);
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
		case TYPE_TYPE_VALUE: {
			return true;
		}
		case VOID_TYPE_VALUE: {
			return true;
		}
		case INTEGER_VALUE: {
			return value1->integer.value == value2->integer.value;
		}
		default:
			assert(false);
	}
}

bool type_assignable(Value_Data *type1, Value_Data *type2) {
	if (type2 == NULL) return false;
	if (type1->tag != type2->tag) return false;

	switch (type1->tag) {
		case POINTER_TYPE_VALUE: {
			if (type1->pointer_type.inner.value->tag == VOID_TYPE_VALUE || type2->pointer_type.inner.value->tag == VOID_TYPE_VALUE) {
				return true;
			}

			return type_assignable(type1->pointer_type.inner.value, type2->pointer_type.inner.value);
		}
		case OPTIONAL_TYPE_VALUE: {
			return type_assignable(type1->optional_type.inner.value, type2->optional_type.inner.value);
		}
		case ARRAY_TYPE_VALUE: {
			if ((type1->array_type.size.value == NULL && type2->array_type.size.value != NULL)) return type_assignable(type1->array_type.inner.value, type2->array_type.inner.value);
			if (type1->array_type.size.value != NULL && type2->array_type.size.value == NULL) return false;

			if (type1->array_type.size.value != NULL) {
				if (!type_assignable(type1->array_type.size.value, type2->array_type.size.value)) return false;
			}

			return type_assignable(type1->array_type.inner.value, type2->array_type.inner.value);
		}
		case ARRAY_VIEW_TYPE_VALUE: {
			return type_assignable(type1->array_view_type.inner.value, type2->array_view_type.inner.value);
		}
		case STRUCT_TYPE_VALUE: {
			if (type1->struct_type.node != type2->struct_type.node) return false;
			if (arrlen(type1->struct_type.items) != arrlen(type2->struct_type.items)) return false;

			for (long int i = 0; i < arrlen(type1->struct_type.items); i++) {
				if (strcmp(type1->struct_type.items[i].identifier, type2->struct_type.items[i].identifier) != 0) return false;
				if (!type_assignable(type1->struct_type.items[i].type.value, type2->struct_type.items[i].type.value)) return false;
			}

			return true;
		}
		case TAGGED_UNION_TYPE_VALUE: {
			if (type1->tagged_union_type.node != type2->tagged_union_type.node) return false;
			if (arrlen(type1->tagged_union_type.items) != arrlen(type2->tagged_union_type.items)) return false;

			for (long int i = 0; i < arrlen(type1->tagged_union_type.items); i++) {
				if (strcmp(type1->tagged_union_type.items[i].identifier, type2->tagged_union_type.items[i].identifier) != 0) return false;
				if (!type_assignable(type1->tagged_union_type.items[i].type.value, type2->tagged_union_type.items[i].type.value)) return false;
			}

			return true;
		}
		case ENUM_TYPE_VALUE: {
			if (arrlen(type1->enum_type.items) != arrlen(type2->enum_type.items)) return false;

			for (long int i = 0; i < arrlen(type1->enum_type.items); i++) {
				if (strcmp(type1->enum_type.items[i], type2->enum_type.items[i]) != 0) return false;
			}

			return true;
		}
		case FUNCTION_TYPE_VALUE: {
			if (arrlen(type1->function_type.arguments) != arrlen(type2->function_type.arguments)) return false;
			for (long int i = 0; i < arrlen(type1->function_type.arguments); i++) {
				if (strcmp(type1->function_type.arguments[i].identifier, type2->function_type.arguments[i].identifier) != 0) return false;
				if (!type_assignable(type1->function_type.arguments[i].type.value, type2->function_type.arguments[i].type.value)) return false;
			}

			if ((type1->function_type.return_type.value == NULL && type2->function_type.return_type.value != NULL) || (type1->function_type.return_type.value != NULL && type2->function_type.return_type.value == NULL)) return false;
			if (type1->function_type.return_type.value != NULL) {
				if (!type_assignable(type1->function_type.return_type.value, type2->function_type.return_type.value)) {
					return false;
				}
			}

			if (type1->function_type.variadic != type2->function_type.variadic) return false;

			return true;
		}
		case BYTE_TYPE_VALUE: {
			return true;
		}
		case TYPE_TYPE_VALUE: {
			return true;
		}
		case BOOLEAN_TYPE_VALUE: {
			return true;
		}
		case INTEGER_TYPE_VALUE: {
			return type1->integer_type.signed_ == type2->integer_type.signed_ && type1->integer_type.size == type2->integer_type.size;
		}
		case INTEGER_VALUE: {
			return type1->integer.value == type2->integer.value;
		}
		default:
			assert(false);
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

#define handle_evaluate_error(/* Source_Location */ location, /* char * */ fmt, ...) { \
	printf("%s:%zu:%zu: " fmt "\n", location.path, location.row, location.column __VA_OPT__(,) __VA_ARGS__); \
	exit(1); \
}

Value create_value_data(Value_Data *value, Node *node) {
	return (Value) { .value = value, .node = node };
}

Value evaluate(Context *context, Node *node) {
	switch (node->kind) {
		case FUNCTION_NODE: {
			Function_Node function = node->function;

			Node_Data *function_type_data = get_data(context, function.function_type);
			Value function_type_value = function_type_data->function_type.value;

			Value_Data *function_value = value_new(FUNCTION_VALUE);
			function_value->function.type = function_type_value.value;
			if (function.body != NULL) {
				function_value->function.body = function.body;
			}

			function_value->function.compile_only = get_data(context, node)->function.compile_only;
			function_value->function.returned = get_data(context, node)->function.returned;
			function_value->function.static_argument_id = context->static_argument_id;
			function_value->function.extern_name = function.extern_name;
			function_value->function.node = node;

			Scope *scopes = NULL;
			for (long int i = 0; i < arrlen(context->scopes); i++) {
				arrpush(scopes, context->scopes[i]);
			}

			function_value->function.scopes = scopes;

			return create_value_data(function_value, node);
		}
		case FUNCTION_TYPE_NODE: {
			return create_value_data(get_data(context, node)->function_type.value.value, node);
		}
		case STRUCT_TYPE_NODE: {
			Struct_Type_Node struct_type = node->struct_type;

			Value_Data *struct_value_data = value_new(STRUCT_TYPE_VALUE);
			struct_value_data->struct_type.node = node;
			Value struct_value = create_value_data(struct_value_data, node);

			Scope scope = {
				.node = node,
				.current_type = struct_value
			};
			arrpush(context->scopes, scope);

			struct_value_data->struct_type.items = NULL;
			for (long int i = 0; i < arrlen(struct_type.items); i++) {
				Struct_Item_Value item = {
					.identifier = struct_type.items[i].identifier,
					.type = evaluate(context, struct_type.items[i].type)
				};
				arrpush(struct_value_data->struct_type.items, item);
			}

			struct_value_data->struct_type.arguments = function_arguments;

			for (long int i = 0; i < arrlen(struct_type.operators); i++) {
				process_node(context, struct_type.operators[i].function);
				Operator_Value_Definition item = {
					.operator = struct_type.operators[i].operator,
					.function = evaluate(context, struct_type.operators[i].function)
				};
				arrpush(struct_value.value->struct_type.operators, item);
			}

			(void) arrpop(context->scopes);

			return struct_value;
		}
		case UNION_TYPE_NODE: {
			Union_Type_Node union_type = node->union_type;

			Value_Data *union_value = value_new(UNION_TYPE_VALUE);
			union_value->union_type.items = NULL;
			for (long int i = 0; i < arrlen(union_type.items); i++) {
				Union_Item_Value item = {
					.identifier = union_type.items[i].identifier,
					.type = evaluate(context, union_type.items[i].type)
				};
				arrpush(union_value->union_type.items, item);
			}

			return create_value_data(union_value, node);
		}
		case TAGGED_UNION_TYPE_NODE: {
			Tagged_Union_Type_Node tagged_union_type = node->tagged_union_type;

			Value_Data *tagged_union_value = value_new(TAGGED_UNION_TYPE_VALUE);
			tagged_union_value->tagged_union_type.node = node;
			tagged_union_value->tagged_union_type.items = NULL;
			Value_Data *enum_value = value_new(ENUM_TYPE_VALUE);
			tagged_union_value->tagged_union_type.enum_ = enum_value;
			for (long int i = 0; i < arrlen(tagged_union_type.items); i++) {
				Tagged_Union_Item_Value item = {
					.identifier = tagged_union_type.items[i].identifier,
					.type = evaluate(context, tagged_union_type.items[i].type)
				};
				arrpush(tagged_union_value->tagged_union_type.items, item);
				arrpush(enum_value->enum_type.items, tagged_union_type.items[i].identifier);
			}

			return create_value_data(tagged_union_value, node);
		}
		case ENUM_TYPE_NODE: {
			Enum_Type_Node enum_type = node->enum_type;

			Value_Data *enum_value = value_new(ENUM_TYPE_VALUE);
			enum_value->enum_type.items = NULL;
			for (long int i = 0; i < arrlen(enum_type.items); i++) {
				arrpush(enum_value->enum_type.items, enum_type.items[i]);
			}

			return create_value_data(enum_value, node);
		}
		case MODULE_NODE: {
			Module_Node module = node->module;
			Value_Data *module_value = value_new(MODULE_VALUE);

			Scope *scopes = NULL;
			for (long int i = 0; i < arrlen(context->scopes); i++) {
				arrpush(scopes, context->scopes[i]);
			}

			arrpush(scopes, (Scope) { .node = module.body });

			module_value->module.body = module.body;
			module_value->module.scopes = scopes;

			return create_value_data(module_value, node);
		}
		case POINTER_NODE: {
			Pointer_Node pointer = node->pointer;

			Value_Data *pointer_type_value = value_new(POINTER_TYPE_VALUE);
			pointer_type_value->pointer_type.inner = evaluate(context, pointer.inner);
			return create_value_data(pointer_type_value, node);
		}
		case OPTIONAL_NODE: {
			Optional_Node optional = node->optional;

			Value_Data *optional_type_value = value_new(OPTIONAL_TYPE_VALUE);
			optional_type_value->optional_type.inner = evaluate(context, optional.inner);
			return create_value_data(optional_type_value, node);
		}
		case RESULT_NODE: {
			Result_Node result = node->result;

			Value_Data *result_type_value = value_new(RESULT_TYPE_VALUE);
			result_type_value->result_type.value = evaluate(context, result.value);
			result_type_value->result_type.error = evaluate(context, result.error);
			return create_value_data(result_type_value, node);
		}
		case ARRAY_TYPE_NODE: {
			Array_Type_Node array_type = node->array_type;

			Value_Data *array_type_value = value_new(ARRAY_TYPE_VALUE);
			array_type_value->array_type.size = evaluate(context, array_type.size);
			array_type_value->array_type.inner = evaluate(context, array_type.inner);
			return create_value_data(array_type_value, node);
		}
		case ARRAY_VIEW_TYPE_NODE: {
			Array_View_Type_Node array_view_type = node->array_view_type;

			Value_Data *array_type_value = value_new(ARRAY_VIEW_TYPE_VALUE);
			array_type_value->array_view_type.inner = evaluate(context, array_view_type.inner);
			return create_value_data(array_type_value, node);
		}
		case IDENTIFIER_NODE: {
			Identifier_Data identifier_data = get_data(context, node)->identifier;

			switch (identifier_data.kind) {
				case IDENTIFIER_VALUE:
					return identifier_data.value;
				case IDENTIFIER_ARGUMENT:
					return function_arguments[identifier_data.argument_index];
				case IDENTIFIER_SELF:
					for (long int i = arrlen(context->scopes) - 1; i >= 0; i--) {
						if (context->scopes[i].current_type.value != NULL) {
							return context->scopes[i].current_type;
						}
					}
					break;
				case IDENTIFIER_UNDERSCORE:
					return create_value_data(NULL, node);
					break;
				default:
					handle_evaluate_error(node->location, "Cannot evaluate identifier at compile time");
					break;
			}

			assert(false);
			break;
		}
		case STRING_NODE: {
			String_Data string_data = get_data(context, node)->string;
			char *string_value = string_data.value;
			size_t string_length = string_data.length;

			if (string_data.type.value->tag == ARRAY_VIEW_TYPE_VALUE && string_data.type.value->array_view_type.inner.value->tag == BYTE_TYPE_VALUE) {
				char *data = malloc(string_length);
				for (size_t i = 0; i < string_length; i++) {
					data[i] = string_value[i];
				}

				Value_Data *string = value_new(ARRAY_VIEW_VALUE);
				string->array_view.length = string_length;
				for (size_t i = 0; i < string_length; i++) {
					Value_Data *byte_value = value_new(BYTE_VALUE);
					byte_value->byte.value = string_value[i];
					arrpush(string->array_view.values, byte_value);
				}

				return create_value_data(string, node);
			} else {
				Value_Data **values = malloc(sizeof(Value_Data *) * string_length);
				for (size_t i = 0; i < string_length; i++) {
					Value_Data *byte = value_new(BYTE_VALUE);
					byte->byte.value = string_value[i];
					values[i] = byte;
				}

				Value_Data *pointer = value_new(POINTER_VALUE);
				Value_Data *array = value_new(ARRAY_VALUE);
				array->array.length = string_length;
				array->array.values = values;
				pointer->pointer.value = array;

				return create_value_data(pointer, node);
			}
		}
		case NUMBER_NODE: {
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
			break;
		}
		case BOOLEAN_NODE: {
			Boolean_Node boolean = node->boolean;

			Value_Data *value = value_new(BOOLEAN_VALUE);
			value->integer.value = boolean.value;
			return create_value_data(value, node);
		}
		case CALL_NODE: {
			Call_Node call = node->call;
			Call_Data call_data = get_data(context, node)->call;

			Value function;
			if (call_data.function_value.value != NULL) {
				function = call_data.function_value;
			} else {
				function = evaluate(context, call.function);
			}

			Value *arguments = NULL;
			for (long int i = 0; i < arrlen(call.arguments); i++) {
				arrpush(arguments, evaluate(context, call.arguments[i]));
			}

			switch (function.value->tag) {
				case IMPORT_FUNCTION_VALUE: {
					Value string = arguments[0];
					char *source = malloc(string.value->array_view.length + 1);
					source[string.value->array_view.length] = '\0';
					for (size_t i = 0; i < string.value->array_view.length; i++) {
						source[i] = string.value->array_view.values[i]->byte.value;
					}

					if (strcmp(source, "core") == 0) {
						char *cwd = getcwd(NULL, PATH_MAX);
						source = malloc(strlen(cwd) + 15);
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

					Value value = get_cached_file(context, source);
					if (value.value != NULL) {
						return value;
					}

					Node *file_node = parse_file(source);

					Scope *saved_scopes = context->scopes;
					context->scopes = NULL;
					process_node(context, file_node);
					value = evaluate(context, file_node);
					context->scopes = saved_scopes;

					add_cached_file(context, source, value);

					return value;
				}
				case SIZE_OF_FUNCTION_VALUE: {
					Value_Data *value = value_new(INTEGER_VALUE);
					value->integer.value = context->codegen.size_fn(arguments[0].value, context->codegen.data);
					return create_value_data(value, node);
				}
				case INTN_FUNCTION_VALUE: {
					Value_Data *value = value_new(INTEGER_TYPE_VALUE);
					value->integer_type.signed_ = arguments[0].value->boolean.value;
					value->integer_type.size = arguments[1].value->integer.value;
					return create_value_data(value, node);
				}
				case FUNCTION_VALUE: {
					size_t saved_static_argument_id = context->static_argument_id;
					context->static_argument_id = function.value->function.static_argument_id;

					Value *saved_function_arguments = function_arguments;
					function_arguments = arguments;

					jmp_buf prev_jmp;
					memcpy(&prev_jmp, &jmp, sizeof(jmp_buf));
					Value result;
					if (!setjmp(jmp)) {
						result = evaluate(context, function.value->function.body);
					} else {
						result = jmp_result;
					}
					memcpy(&jmp, &prev_jmp, sizeof(jmp_buf));

					function_arguments = saved_function_arguments;

					context->static_argument_id = saved_static_argument_id;

					return create_value_data(result.value, node);
				}
				default:
					assert(false);
			}

			return (Value) {};
		}
		case BINARY_OPERATOR_NODE: {
			Binary_Operator_Node binary_operator = node->binary_operator;

			Value left_value = evaluate(context, binary_operator.left);
			Value right_value = evaluate(context, binary_operator.right);

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
		case BLOCK_NODE: {
			Block_Node block = node->block;

			Value result = create_value_data(value_new(NONE_VALUE), node);
			for (long int i = 0; i < arrlen(block.statements); i++) {
				Value value = evaluate(context, block.statements[i]);
				if (block.has_result && i == arrlen(block.statements) - 1) {
					result = value;
				}
			}

			return result;
		}
		case RETURN_NODE: {
			Return_Node return_ = node->return_;

			Value result;
			if (return_.value != NULL) {
				result = evaluate(context, return_.value);
			} else {
				result = create_value_data(value_new(NONE_VALUE), node);
			}

			jmp_result = result;
			longjmp(jmp, 1);
		}
		default:
			assert(false);
	}
}
