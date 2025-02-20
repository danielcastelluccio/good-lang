#include <assert.h>
#include <stdlib.h>

#include "stb/ds.h"

#include "ast.h"
#include "evaluator.h"
#include "parser.h"

#include <setjmp.h>

#include <stdio.h>

jmp_buf jmp;
Value *jmp_result;

bool value_equal(Value *value1, Value *value2) {
	if (value1->tag != value2->tag) return false;

	switch (value1->tag) {
		case POINTER_TYPE_VALUE: {
			return value_equal(value1->pointer_type.inner, value2->pointer_type.inner);
		}
		case ARRAY_TYPE_VALUE: {
			return value_equal(value1->array_type.inner, value2->array_type.inner);
		}
		case INTERNAL_VALUE: {
			return strcmp(value1->internal.identifier, value2->internal.identifier) == 0;
		}
		case STRUCTURE_TYPE_VALUE: {
			if (arrlen(value1->structure_type.items) != arrlen(value2->structure_type.items)) return false;

			for (long int i = 0; i < arrlen(value1->structure_type.items); i++) {
				if (strcmp(value1->structure_type.items[i].identifier, value2->structure_type.items[i].identifier) != 0) return false;
				if (!value_equal(value1->structure_type.items[i].type, value2->structure_type.items[i].type)) return false;
			}

			return true;
		}
		case DEFINE_DATA_VALUE: {
			return value_equal(value1->define_data.value, value2->define_data.value);
		}
		default:
			assert(false);
	}
}

bool type_assignable(Value *type1, Value *type2) {
	if (type1->tag != type2->tag) return false;

	switch (type1->tag) {
		case POINTER_TYPE_VALUE: {
			if ((type1->pointer_type.inner->tag == INTERNAL_VALUE && strcmp(type1->pointer_type.inner->internal.identifier, "void") == 0) || (type2->pointer_type.inner->tag == INTERNAL_VALUE && strcmp(type2->pointer_type.inner->internal.identifier, "void") == 0)) {
				return true;
			}

			return value_equal(type1->pointer_type.inner, type2->pointer_type.inner);
		}
		case ARRAY_TYPE_VALUE: {
			return value_equal(type1->array_type.inner, type2->array_type.inner);
		}
		case INTERNAL_VALUE: {
			return strcmp(type1->internal.identifier, type2->internal.identifier) == 0;
		}
		case STRUCTURE_TYPE_VALUE: {
			if (arrlen(type1->structure_type.items) != arrlen(type2->structure_type.items)) return false;

			for (long int i = 0; i < arrlen(type1->structure_type.items); i++) {
				if (strcmp(type1->structure_type.items[i].identifier, type2->structure_type.items[i].identifier) != 0) return false;
				if (!value_equal(type1->structure_type.items[i].type, type2->structure_type.items[i].type)) return false;
			}

			return true;
		}
		case DEFINE_DATA_VALUE: {
			return value_equal(type1->define_data.value, type2->define_data.value);
		}
		default:
			assert(false);
	}
}

Value *evaluate(Context *context, Node *node) {
	switch (node->kind) {
		case FUNCTION_NODE: {
			Function_Node function = node->function;
			Value *function_type_value = get_data(context, function.function_type)->function_type.value;

			Value *function_value = value_new(FUNCTION_VALUE);
			function_value->function.type = function_type_value;
			if (function.body != NULL) {
				function_value->function.body = function.body;
			}

			function_value->function.compile_only = get_data(context, node)->function.compile_only;
			function_value->function.generic_id = context->generic_id;
			function_value->function.extern_name = function.extern_name;

			return function_value;
		}
		case STRUCTURE_NODE: {
			Structure_Node structure = node->structure;

			Value *structure_value = value_new(STRUCTURE_TYPE_VALUE);
			structure_value->structure_type.items = NULL;
			for (long int i = 0; i < arrlen(structure.items); i++) {
				Structure_Item_Value item = {
					.identifier = structure.items[i].identifier,
					.type = evaluate(context, structure.items[i].type)
				};
				arrpush(structure_value->structure_type.items, item);
			}

			return structure_value;
		}
		case MODULE_NODE: {
			Module_Node module = node->module;
			Value *module_value = value_new(MODULE_VALUE);

			Scope *scopes = NULL;
			for (long int i = 0; i < arrlen(context->scopes); i++) {
				arrpush(scopes, context->scopes[i]);
			}

			arrpush(scopes, (Scope) { .node = module.body });

			module_value->module.body = module.body;
			module_value->module.generic_id = context->generic_id;
			module_value->module.scopes = scopes;

			return module_value;
		}
		case POINTER_NODE: {
			Pointer_Node pointer = node->pointer;

			Value *pointer_type_value = value_new(POINTER_TYPE_VALUE);
			pointer_type_value->pointer_type.inner = evaluate(context, pointer.inner);
			return pointer_type_value;
		}
		case ARRAY_NODE: {
			Array_Node array = node->array;

			Value *array_type_value = value_new(ARRAY_TYPE_VALUE);
			if (array.size != NULL) array_type_value->array_type.size = evaluate(context, array.size);
			array_type_value->array_type.inner = evaluate(context, array.inner);
			return array_type_value;
		}
		case IDENTIFIER_NODE: {
			Value *value = get_data(context, node)->identifier.value;
			if (value != NULL) {
				return value;
			}

			assert(false);
			break;
		}
		case STRING_NODE: {
			String_Data string_data = get_data(context, node)->string;
			char *string_value = string_data.value;
			size_t string_length = strlen(string_value);

			Value *pointer = value_new(POINTER_VALUE);
			Value *array = value_new(ARRAY_VALUE);
			pointer->pointer.value = array;

			array->array.length = string_length;
			array->array.values = malloc(sizeof(Value *) * string_length);
			for (size_t i = 0; i < string_length; i++) {
				Value *byte = value_new(BYTE_VALUE);
				byte->byte.value = string_value[i];
				array->array.values[i] = byte;
			}

			return pointer;
		}
		case NUMBER_NODE: {
			Number_Node number = node->number;
			switch (number.tag) {
				case INTEGER_NUMBER: {
					Value *value = value_new(INTEGER_VALUE);
					value->integer.value = number.integer;
					return value;
				}
				default:
					assert(false);
			}
			return NULL;
		}
		case CALL_NODE: {
			Call_Node call = node->call;

			Value *function = evaluate(context, call.function);

			Value **arguments = NULL;
			for (long int i = 0; i < arrlen(call.arguments); i++) {
				arrpush(arguments, evaluate(context, call.arguments[i]));
			}

			switch (function->tag) {
				case INTERNAL_VALUE: {
					char *identifier = function->internal.identifier;
					if (strcmp(identifier, "import") == 0) {
						Value *array = arguments[0]->pointer.value;
						char *source = malloc(array->array.length + 1);
						source[array->array.length] = '\0';
						for (size_t i = 0; i < array->array.length; i++) {
							source[i] = array->array.values[i]->byte.value;
						}

						Node *file_node = parse_file(source);
						return evaluate(context, file_node);
					} else {
						assert(false);
					}
					return NULL;
				}
				case FUNCTION_VALUE: {
					jmp_buf prev_jmp;
					memcpy(&prev_jmp, &jmp, sizeof(jmp_buf));
					Value *result;
					if (!setjmp(jmp)) {
						result = evaluate(context, function->function.body);
					} else {
						result = jmp_result;
					}
					memcpy(&jmp, &prev_jmp, sizeof(jmp_buf));
					return result;
				}
				default:
					assert(false);
			}

			return NULL;
		}
		case BINARY_OPERATOR_NODE: {
			Binary_Operator_Node binary_operator = node->binary_operator;

			Value *left_value = evaluate(context, binary_operator.left);
			Value *right_value = evaluate(context, binary_operator.right);

			if (value_equal(left_value, right_value)) {
				Value *true_value = value_new(BOOLEAN_VALUE);
				true_value->boolean.value = true;
				return true_value;
			} else {
				Value *false_value = value_new(BOOLEAN_VALUE);
				false_value->boolean.value = false;
				return false_value;
			}
		}
		case BLOCK_NODE: {
			Block_Node block = node->block;

			for (long int i = 0; i < arrlen(block.statements); i++) {
				evaluate(context, block.statements[i]);
			}

			return value_new(NONE_VALUE);
		}
		case RETURN_NODE: {
			Return_Node return_ = node->return_;

			Value *result;
			if (return_.value != NULL) {
				result = evaluate(context, return_.value);
			} else {
				result = value_new(NONE_VALUE);
			}

			jmp_result = result;
			longjmp(jmp, 1);
		}
		default:
			assert(false);
	}
}
