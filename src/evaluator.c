#include <assert.h>
#include <stdlib.h>

#include "stb/ds.h"

#include "ast.h"
#include "evaluator.h"

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

			if (hmget(context->compile_only_function_nodes, node)) {
				hmput(context->compile_only_functions, function_value, true);
			}

			function_value->function.generic_id = context->generic_id;

			return function_value;
		}
		case MODULE_NODE: {
			Module_Node module = node->module;
			Value *module_value = value_new(MODULE_VALUE);

			Scope *scopes = NULL;
			for (long int i = 0; i < arrlen(context->scopes); i++) {
				arrpush(scopes, context->scopes[i]);
			}

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
		case CALL_NODE: {
			Call_Node call = node->call;

			Value *function = evaluate(context, call.function);

			Value **arguments = NULL;
			for (long int i = 0; i < arrlen(call.arguments); i++) {
				arrput(arguments, evaluate(context, call.arguments[i]));
			}

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
		case BINARY_OPERATOR_NODE: {
			Binary_Operatory_Node binary_operator = node->binary_operator;

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
