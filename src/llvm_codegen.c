#include <assert.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/Types.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>

#include <llvm-c/Core.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>

#include "ast.h"
#include "stb/ds.h"
#include "util.h"
#include "value.h"

#include "llvm_codegen.h"

typedef struct {
	LLVMBasicBlockRef block;
	LLVMValueRef value;
} Block_Codegen_Data;

typedef struct {
	LLVMBasicBlockRef block;
	LLVMValueRef value;
} While_Codegen_Data;

typedef struct {
	LLVMValueRef *bindings; // stb_ds
} For_Codegen_Data;

typedef struct {
	LLVMValueRef binding;
} If_Codegen_Data;

typedef struct {
	LLVMValueRef binding;
} Catch_Codegen_Data;

typedef struct {
	LLVMModuleRef llvm_module;
	LLVMBuilderRef llvm_builder;
	LLVMTargetMachineRef llvm_target;
	Context context;
	struct { Value_Data *key; LLVMValueRef value; } *generated_cache; // stb_ds
	struct { Node_Data *key; LLVMValueRef value; } *variables; // stb_ds
	struct { Node_Data *key; Block_Codegen_Data value; } *blocks; // stb_ds
	struct { Node_Data *key; While_Codegen_Data value; } *whiles; // stb_ds
	struct { Node_Data *key; For_Codegen_Data value; } *fors; // stb_ds
	struct { Node_Data *key; If_Codegen_Data value; } *ifs; // stb_ds
	struct { Node_Data *key; Catch_Codegen_Data value; } *catchs; // stb_ds
	LLVMValueRef *function_arguments; // stb_ds
	LLVMValueRef current_function;
	LLVMValueRef main_function;
	bool main_takes_arguments;
} State;

static LLVMValueRef generate_node(Node *node, State *state);
static LLVMValueRef generate_value(Value_Data *value, Value_Data *type, State *state);

static LLVMTypeRef create_llvm_type(Value_Data *node, State *state);

static LLVMTypeRef create_llvm_function_literal_type(Value_Data *value, State *state) {
	assert(value->tag == FUNCTION_TYPE_VALUE);
	Function_Type_Value function_type = value->function_type;

	LLVMTypeRef *arguments = NULL;
	for (long int i = 0; i < arrlen(function_type.arguments); i++) {
		if (function_type.arguments[i].static_) continue;
		arrpush(arguments, create_llvm_type(function_type.arguments[i].type.value, state));
	}

	LLVMTypeRef return_type = LLVMVoidType();
	if (function_type.return_type.value != NULL) {
		return_type = create_llvm_type(function_type.return_type.value, state);
	}

	return LLVMFunctionType(return_type, arguments, arrlen(arguments), function_type.variadic);
}

static LLVMTypeRef create_llvm_type(Value_Data *value, State *state) {
	switch (value->tag) {
		case POINTER_TYPE_VALUE: {
			Pointer_Type_Value pointer = value->pointer_type;
			LLVMTypeRef inner_type = LLVMVoidType();
			if (pointer.inner.value != NULL) {
				inner_type = create_llvm_type(pointer.inner.value, state);
			}

			return LLVMPointerType(inner_type, 0);
		}
		case ARRAY_TYPE_VALUE: {
			Array_Type_Value array = value->array_type;
			return LLVMArrayType(create_llvm_type(array.inner.value, state), array.size.value != NULL ? array.size.value->integer.value : 0);
		}
		case FUNCTION_TYPE_VALUE: {
			return LLVMPointerType(create_llvm_function_literal_type(value, state), 0);
		}
		case STRUCT_TYPE_VALUE: {
			Struct_Type_Value struct_type = value->struct_type;

			LLVMTypeRef *items = NULL;
			for (long int i = 0; i < arrlen(struct_type.members); i++) {
				arrpush(items, create_llvm_type(struct_type.members[i].value, state));
			}

			return LLVMStructType(items, arrlen(items), false);
		}
		case TUPLE_TYPE_VALUE: {
			Tuple_Type_Value tuple_type = value->tuple_type;

			LLVMTypeRef *items = NULL;
			for (long int i = 0; i < arrlen(tuple_type.members); i++) {
				arrpush(items, create_llvm_type(tuple_type.members[i].value, state));
			}

			return LLVMStructType(items, arrlen(items), false);
		}
		case UNION_TYPE_VALUE: {
			Union_Type_Value union_type = value->union_type;

			size_t max_size = 0;
			for (long int i = 0; i < arrlen(union_type.items); i++) {
				size_t size = LLVMABISizeOfType(LLVMCreateTargetDataLayout(state->llvm_target), create_llvm_type(union_type.items[i].type.value, state));
				if (size > max_size) max_size = size;
			}

			return LLVMArrayType(LLVMInt8Type(), max_size);
		}
		case TAGGED_UNION_TYPE_VALUE: {
			Tagged_Union_Type_Value tagged_union_type = value->tagged_union_type;

			size_t max_size = 0;
			for (long int i = 0; i < arrlen(tagged_union_type.items); i++) {
				size_t size = LLVMABISizeOfType(LLVMCreateTargetDataLayout(state->llvm_target), create_llvm_type(tagged_union_type.items[i].type.value, state));
				if (size > max_size) max_size = size;
			}

			LLVMTypeRef items[2] = {
				LLVMInt64Type(),
				LLVMArrayType(LLVMInt8Type(), max_size)
			};

			return LLVMStructType(items, 2, false);
		}
		case RESULT_TYPE_VALUE: {
			Result_Type_Value result_type = value->result_type;

			size_t max_size = LLVMABISizeOfType(LLVMCreateTargetDataLayout(state->llvm_target), create_llvm_type(result_type.value.value, state));
			size_t error_size = LLVMABISizeOfType(LLVMCreateTargetDataLayout(state->llvm_target), create_llvm_type(result_type.error.value, state));
			if (error_size > max_size) {
				max_size = error_size;
			}

			LLVMTypeRef items[2] = {
				LLVMInt64Type(),
				LLVMArrayType(LLVMInt8Type(), max_size)
			};

			return LLVMStructType(items, 2, false);
		}
		case ENUM_TYPE_VALUE: {
			return LLVMInt64Type();
		}
		case BYTE_TYPE_VALUE: {
			return LLVMInt8Type();
		}
		case BOOLEAN_TYPE_VALUE: {
			return LLVMInt1Type();
		}
		case INTEGER_TYPE_VALUE: {
			Integer_Type_Value integer_type = value->integer_type;
			return LLVMIntType(integer_type.size);
		}
		case FLOAT_TYPE_VALUE: {
			Float_Type_Value float_type = value->float_type;
			switch (float_type.size) {
				case 16:
					return LLVMHalfType();
				case 32:
					return LLVMFloatType();
				case 64:
					return LLVMDoubleType();
				default:
					assert(false);
			}
			return NULL;
		}
		case ARRAY_VIEW_TYPE_VALUE: {
			LLVMTypeRef *items = malloc(sizeof(LLVMTypeRef) * 2);
			items[0] = LLVMInt64Type();
			items[1] = LLVMPointerType(LLVMArrayType2(create_llvm_type(value->array_view_type.inner.value, state), 0), 0);
			return LLVMStructType(items, 2, false);
		}
		case OPTIONAL_TYPE_VALUE: {
			Optional_Type_Value optional_type = value->optional_type;

			LLVMTypeRef inner_type = create_llvm_type(optional_type.inner.value, state);
			if (optional_type.inner.value->tag == POINTER_TYPE_VALUE) {
				return inner_type;
			} else {
				LLVMTypeRef *items = malloc(sizeof(LLVMTypeRef) * 2);
				items[0] = LLVMInt1Type();
				items[1] = inner_type;
				return LLVMStructType(items, 2, false);
			}
		}
		case RANGE_TYPE_VALUE: {
			Range_Type_Value range_type = value->range_type;

			LLVMTypeRef inner_type = create_llvm_type(range_type.type.value, state);
			LLVMTypeRef *items = malloc(sizeof(LLVMTypeRef) * 2);
			items[0] = inner_type;
			items[1] = inner_type;
			return LLVMStructType(items, 2, false);
		}
		default:
			assert(false);
	}
}

static void generate_define(Node *node, State *state) {
	assert(node->kind == DEFINE_NODE);
	Define_Node define = node->define;

	Node_Data *data = get_data(&state->context, node);
	if (data == NULL) {
		return;
	}

	Typed_Value typed_value = data->define.typed_value;
	if (typed_value.value.value == NULL) {
		return;
	}

	LLVMValueRef llvm_value = generate_value(typed_value.value.value, typed_value.type.value, state);
	if (strcmp(define.identifier, "main") == 0) {
		state->main_function = llvm_value;
		state->main_takes_arguments = arrlen(typed_value.value.value->function.type->function_type.arguments) > 0;
	}
}

static LLVMValueRef generate_block(Node *node, State *state) {
	assert(node->kind == BLOCK_NODE);
	Block_Node block = node->block;
	Block_Data block_data = get_data(&state->context, node)->block;

	LLVMValueRef result = NULL;
	for (long int i = 0; i < arrlen(block.statements); i++) {
		if (block.statements[i]->kind == DEFER_NODE) continue;

		LLVMValueRef value = generate_node(block.statements[i], state);
		if (block.has_result && i == arrlen(block.statements) - 1) {
			result = value;
		}
	}

	for (long int i = 0; i < arrlen(block_data.defers); i++) {
		generate_node(block_data.defers[i], state);
	}

	if (block.has_result) {
		return result;
	}
	return NULL;
}

static LLVMValueRef generate_call_generic(LLVMValueRef function_llvm_value, Value_Data *function_type, Node **arguments, State *state) {
	assert(function_llvm_value != NULL);

	LLVMValueRef *llvm_arguments = NULL;
	long int j = 0;
	for (long int i = 0; i < arrlen(arguments); i++) {
		while (function_type->function_type.arguments[j].inferred) j++;
		if (j >= arrlen(function_type->function_type.arguments) || !function_type->function_type.arguments[j].static_) {
			arrpush(llvm_arguments, generate_node(arguments[i], state));
		}
		j++;
	}

	return LLVMBuildCall2(state->llvm_builder, create_llvm_function_literal_type(function_type, state), function_llvm_value, llvm_arguments, arrlen(llvm_arguments), "");
}

static LLVMValueRef generate_call(Node *node, State *state) {
	assert(node->kind == CALL_NODE);
	Call_Node call = node->call;

	Call_Data call_data = get_data(&state->context, node)->call;
	Value function_type = call_data.function_type;

	if (call_data.function_value.value != NULL) {
		return generate_call_generic(generate_value(call_data.function_value.value, call_data.function_type.value, state), function_type.value, call.arguments, state);
	} else {
		return generate_call_generic(generate_node(call.function, state), function_type.value, call.arguments, state);
	}
}

static LLVMValueRef generate_call_method(Node *node, State *state) {
	assert(node->kind == CALL_METHOD_NODE);
	Call_Method_Data call_method_data = get_data(&state->context, node)->call_method;

	Custom_Operator_Function operator = call_method_data.custom_operator_function;
	return generate_call_generic(generate_value(operator.function, operator.function_type.value, state), call_method_data.custom_operator_function.function_type.value, call_method_data.arguments, state);
}

static LLVMValueRef generate_identifier(Node *node, State *state) {
	assert(node->kind == IDENTIFIER_NODE);

	Node_Data *identifier_data = get_data(&state->context, node);
	switch (identifier_data->identifier.kind) {
		case IDENTIFIER_VARIABLE: {
			Node *variable = get_data(&state->context, node)->identifier.variable;
			Node_Data *variable_data = get_data(&state->context, variable);
			LLVMValueRef variable_llvm_value = hmget(state->variables, variable_data);
			if (identifier_data->identifier.assign_value != NULL) {
				LLVMBuildStore(state->llvm_builder, generate_node(identifier_data->identifier.assign_value, state), variable_llvm_value);
				return NULL;
			} else {
				if (identifier_data->identifier.want_pointer) {
					return variable_llvm_value;
				} else {
					return LLVMBuildLoad2(state->llvm_builder, create_llvm_type(identifier_data->identifier.type.value, state), variable_llvm_value, "");
				}
			}
		}
		case IDENTIFIER_ARGUMENT: {
			size_t argument_index = get_data(&state->context, node)->identifier.argument_index;

			LLVMValueRef value_pointer = state->function_arguments[argument_index];
			if (identifier_data->identifier.want_pointer) {
				return value_pointer;
			} else {
				return LLVMBuildLoad2(state->llvm_builder, create_llvm_type(identifier_data->identifier.type.value, state), value_pointer, "");
			}
		}
		case IDENTIFIER_BINDING: {
			Node *node = identifier_data->identifier.binding.node;
			Node_Data *node_data = get_data(&state->context, node);
			size_t index = identifier_data->identifier.binding.index;
			if (node->kind == FOR_NODE) {
				LLVMValueRef binding_llvm_value = hmget(state->fors, node_data).bindings[index];
				if (identifier_data->identifier.want_pointer || get_type(&state->context, node->for_.items[index]).value->tag == RANGE_TYPE_VALUE) {
					return binding_llvm_value;
				} else {
					return LLVMBuildLoad2(state->llvm_builder, LLVMTypeOf(binding_llvm_value), binding_llvm_value, "");
				}
			} else if (node->kind == IF_NODE) {
				LLVMValueRef binding_llvm_value = hmget(state->ifs, node_data).binding;
				return binding_llvm_value;
			} else if (node->kind == CATCH_NODE) {
				LLVMValueRef binding_llvm_value = hmget(state->catchs, node_data).binding;
				return binding_llvm_value;
			} else {
				assert(false);
			}
			return NULL;
		}
		case IDENTIFIER_VALUE: {
			return generate_value(identifier_data->identifier.value.value, identifier_data->identifier.type.value, state);
		}
		case IDENTIFIER_STATIC_VARIABLE: {
			return generate_value(hmget(state->context.static_variables, identifier_data->identifier.static_variable.node_data).value, identifier_data->identifier.type.value, state);
		}
		case IDENTIFIER_UNDERSCORE: {
			if (identifier_data->identifier.assign_value != NULL) {
				generate_node(identifier_data->identifier.assign_value, state);
			}
			return NULL;
		}
		default:
			assert(false);
	}
}

static LLVMValueRef generate_string(Node *node, State *state) {
	assert(node->kind == STRING_NODE);

	String_Data string_data = get_data(&state->context, node)->string;

	LLVMValueRef global = LLVMAddGlobal(state->llvm_module, LLVMArrayType(LLVMInt8Type(), string_data.length), "");
	LLVMSetLinkage(global, LLVMPrivateLinkage);
	LLVMSetGlobalConstant(global, true);
	LLVMSetUnnamedAddr(global, true);
	LLVMSetInitializer(global, LLVMConstString(string_data.value, string_data.length, true));

	LLVMValueRef pointer_llvm_value = LLVMBuildPointerCast(state->llvm_builder, global, LLVMPointerType(LLVMInt8Type(), 0), "");

	if (string_data.type.value->tag == ARRAY_VIEW_TYPE_VALUE && string_data.type.value->array_view_type.inner.value->tag == BYTE_TYPE_VALUE) {
		LLVMValueRef string_value = LLVMBuildAlloca(state->llvm_builder, create_llvm_type(string_data.type.value, state), "");

		LLVMValueRef length_pointer = LLVMBuildStructGEP2(state->llvm_builder, create_llvm_type(string_data.type.value, state), string_value, 0, "");
		LLVMBuildStore(state->llvm_builder, LLVMConstInt(LLVMInt64Type(), string_data.length, false), length_pointer);

		LLVMValueRef pointer_pointer = LLVMBuildStructGEP2(state->llvm_builder, create_llvm_type(string_data.type.value, state), string_value, 1, "");
		LLVMBuildStore(state->llvm_builder, pointer_llvm_value, pointer_pointer);

		return LLVMBuildLoad2(state->llvm_builder, create_llvm_type(string_data.type.value, state), string_value, "");
	} else {
		return pointer_llvm_value;
	}
}

static LLVMValueRef generate_character(Node *node, State *state) {
	assert(node->kind == CHARACTER_NODE);

	Character_Data character_data = get_data(&state->context, node)->character;
	return LLVMConstInt(LLVMInt8Type(), character_data.value, false);
}

static LLVMValueRef generate_number(Node *node, State *state) {
	assert(node->kind == NUMBER_NODE);
	Number_Node number = node->number;
	Value_Data *type = get_data(&state->context, node)->number.type.value;
	if (number.tag == DECIMAL_NUMBER) {
		return LLVMConstReal(create_llvm_type(type, state), number.decimal);
	} else {
		return LLVMConstInt(create_llvm_type(type, state), number.integer, false);
	}
}

static LLVMValueRef generate_null(Node *node, State *state) {
	assert(node->kind == NULL_NODE);
	Value_Data *type = get_data(&state->context, node)->null_.type.value;
	switch (type->tag) {
		case POINTER_TYPE_VALUE:
			return LLVMConstNull(create_llvm_type(type, state));
		case OPTIONAL_TYPE_VALUE: {
			if (type->optional_type.inner.value->tag == POINTER_TYPE_VALUE) {
				return LLVMConstNull(create_llvm_type(type, state));
			} else {
				LLVMTypeRef optional_llvm_type = create_llvm_type(type, state);
				LLVMValueRef value_ptr = LLVMBuildAlloca(state->llvm_builder, optional_llvm_type, "");
				LLVMValueRef present_ptr = LLVMBuildStructGEP2(state->llvm_builder, optional_llvm_type, value_ptr, 0, "");
				LLVMBuildStore(state->llvm_builder, LLVMConstInt(LLVMInt1Type(), 0, false), present_ptr);
				return LLVMBuildLoad2(state->llvm_builder, optional_llvm_type, value_ptr, "");
			}
		}
		default:
			assert(false);
	}
}

static LLVMValueRef generate_boolean(Node *node, State *state) {
	assert(node->kind == BOOLEAN_NODE);
	(void) state;
	Boolean_Node boolean = node->boolean;
	return LLVMConstInt(LLVMInt1Type(), boolean.value, false);
}

static LLVMValueRef generate_structure(Node *node, State *state) {
	assert(node->kind == STRUCTURE_NODE);
	Structure_Node structure = node->structure;

	Value_Data *type = get_data(&state->context, node)->structure.type.value;

	switch (type->tag) {
		case STRUCT_TYPE_VALUE: {
			LLVMValueRef struct_value = LLVMBuildAlloca(state->llvm_builder, create_llvm_type(type, state), "");
			for (long int i = 0; i < arrlen(type->struct_type.members); i++) {
				LLVMValueRef item_pointer = LLVMBuildStructGEP2(state->llvm_builder, create_llvm_type(type, state), struct_value, i, "");
				LLVMBuildStore(state->llvm_builder, generate_node(structure.values[i].node, state), item_pointer);
			}

			return LLVMBuildLoad2(state->llvm_builder, create_llvm_type(type, state), struct_value, "");
		}
		case TUPLE_TYPE_VALUE: {
			LLVMValueRef struct_value = LLVMBuildAlloca(state->llvm_builder, create_llvm_type(type, state), "");
			for (long int i = 0; i < arrlen(type->tuple_type.members); i++) {
				LLVMValueRef item_pointer = LLVMBuildStructGEP2(state->llvm_builder, create_llvm_type(type, state), struct_value, i, "");
				LLVMBuildStore(state->llvm_builder, generate_node(structure.values[i].node, state), item_pointer);
			}

			return LLVMBuildLoad2(state->llvm_builder, create_llvm_type(type, state), struct_value, "");
		}
		case ARRAY_TYPE_VALUE: {
			LLVMValueRef array_value = LLVMBuildAlloca(state->llvm_builder, create_llvm_type(type, state), "");
			for (long int i = 0; i < type->array_type.size.value->integer.value; i++) {
				LLVMValueRef indices[2] = {
					LLVMConstInt(LLVMInt64Type(), 0, false),
					LLVMConstInt(LLVMInt64Type(), i, false)
				};

				LLVMValueRef item_pointer = LLVMBuildGEP2(state->llvm_builder, create_llvm_type(type, state), array_value, indices, 2, "");
				LLVMBuildStore(state->llvm_builder, generate_node(structure.values[i].node, state), item_pointer);
			}

			return LLVMBuildLoad2(state->llvm_builder, create_llvm_type(type, state), array_value, "");
		}
		case TAGGED_UNION_TYPE_VALUE: {
			LLVMValueRef tagged_union_value = LLVMBuildAlloca(state->llvm_builder, create_llvm_type(type, state), "");
			char *identifier = structure.values[0].identifier;
			Node *node = structure.values[0].node;
			for (long int i = 0; i < arrlen(type->tagged_union_type.items); i++) {
				if (streq(type->tagged_union_type.items[i].identifier, identifier)) {
					LLVMValueRef tag_pointer = LLVMBuildStructGEP2(state->llvm_builder, create_llvm_type(type, state), tagged_union_value, 0, "");
					LLVMBuildStore(state->llvm_builder, LLVMConstInt(LLVMInt64Type(), i, false), tag_pointer);
					LLVMValueRef data_pointer = LLVMBuildStructGEP2(state->llvm_builder, create_llvm_type(type, state), tagged_union_value, 1, "");
					data_pointer = LLVMBuildBitCast(state->llvm_builder, data_pointer, LLVMPointerType(create_llvm_type(type->tagged_union_type.items[i].type.value, state), 0), "");
					LLVMBuildStore(state->llvm_builder, generate_node(node, state), data_pointer);

					return LLVMBuildLoad2(state->llvm_builder, create_llvm_type(type, state), tagged_union_value, "");
				}
			}

			assert(false);
			return NULL;
		}
		case UNION_TYPE_VALUE: {
			LLVMValueRef union_value = LLVMBuildAlloca(state->llvm_builder, create_llvm_type(type, state), "");
			char *identifier = structure.values[0].identifier;
			Node *node = structure.values[0].node;
			for (long int i = 0; i < arrlen(type->tagged_union_type.items); i++) {
				if (streq(type->tagged_union_type.items[i].identifier, identifier)) {
					LLVMValueRef element_pointer = LLVMBuildBitCast(state->llvm_builder, union_value, LLVMPointerType(create_llvm_type(type->tagged_union_type.items[i].type.value, state), 0), "");
					LLVMBuildStore(state->llvm_builder, generate_node(node, state), element_pointer);

					return LLVMBuildLoad2(state->llvm_builder, create_llvm_type(type, state), union_value, "");
				}
			}

			assert(false);
			return NULL;
		}
		default:
			assert(false);
	}
}

static LLVMValueRef generate_run(Node *node, State *state) {
	assert(node->kind == RUN_NODE);
	Value value = get_data(&state->context, node)->run.value;

	if (value.value != NULL) {
		return generate_value(value.value, get_type(&state->context, node).value, state);
	}

	return NULL;
}

static LLVMValueRef generate_cast(Node *node, State *state) {
	assert(node->kind == CAST_NODE);
	Cast_Node cast = node->cast;
	return generate_node(cast.node, state);
}

static LLVMValueRef generate_reference(Node *node, State *state) {
	assert(node->kind == REFERENCE_NODE);
	Reference_Node reference = node->reference;
	return generate_node(reference.node, state);
}

static LLVMValueRef generate_dereference(Node *node, State *state) {
	assert(node->kind == DEREFERENCE_NODE);
	Dereference_Node dereference = node->dereference;
	LLVMValueRef pointer_llvm_value = generate_node(dereference.node, state);

	Dereference_Data dereference_data = get_data(&state->context, node)->dereference;
	if (dereference_data.assign_value != NULL) {
		LLVMBuildStore(state->llvm_builder, generate_node(dereference_data.assign_value, state), pointer_llvm_value);
		return NULL;
	} else {
		return LLVMBuildLoad2(state->llvm_builder, create_llvm_type(dereference_data.type.value, state), pointer_llvm_value, "");
	}
}

static LLVMValueRef generate_deoptional(Node *node, State *state) {
	assert(node->kind == DEOPTIONAL_NODE);
	Deoptional_Node deoptional = node->deoptional;
	LLVMValueRef optional_llvm_value = generate_node(deoptional.node, state);

	Deoptional_Data deoptional_data = get_data(&state->context, node)->deoptional;
	LLVMTypeRef optional_llvm_type = create_llvm_type(create_optional_type(deoptional_data.type).value, state);

	if (deoptional_data.assign_value != NULL) {
		if (deoptional_data.type.value->tag == POINTER_TYPE_VALUE) {
			LLVMBuildStore(state->llvm_builder, generate_node(deoptional_data.assign_value, state), optional_llvm_value);
		} else {
			LLVMValueRef present_ptr = LLVMBuildStructGEP2(state->llvm_builder, optional_llvm_type, optional_llvm_value, 0, "");
			LLVMValueRef value_ptr = LLVMBuildStructGEP2(state->llvm_builder, optional_llvm_type, optional_llvm_value, 1, "");
			LLVMBuildStore(state->llvm_builder, LLVMConstInt(LLVMInt1Type(), 1, false), present_ptr);
			LLVMBuildStore(state->llvm_builder, generate_node(deoptional_data.assign_value, state), value_ptr);
		}
		return NULL;
	} else {
		assert(false);
	}
}

static LLVMValueRef generate_is(Node *node, State *state) {
	assert(node->kind == IS_NODE);
	Is_Node is = node->is;
	Is_Data is_data = get_data(&state->context, node)->is;

	LLVMValueRef value = generate_node(is.node, state);
	LLVMValueRef value_tag = LLVMBuildExtractValue(state->llvm_builder, value, 0, "");

	LLVMValueRef value_data = LLVMBuildExtractValue(state->llvm_builder, value, 1, "");
	LLVMValueRef value_temp_storage = LLVMBuildAlloca(state->llvm_builder, LLVMTypeOf(value_data), "");
	LLVMBuildStore(state->llvm_builder, value_data, value_temp_storage);

	value_temp_storage = LLVMBuildBitCast(state->llvm_builder, value_temp_storage, create_llvm_type(create_pointer_type(is_data.type.value->optional_type.inner).value, state), "bitcast");

	value_data = LLVMBuildLoad2(state->llvm_builder, create_llvm_type(is_data.type.value->optional_type.inner.value, state), value_temp_storage, "");
	LLVMValueRef check_value = generate_value(is_data.value.value, is_data.type.value, state);

	LLVMTypeRef optional_llvm_type = create_llvm_type(is_data.type.value, state);
	if (is_data.type.value->optional_type.inner.value->tag == POINTER_TYPE_VALUE) {
		LLVMValueRef optional_ptr = LLVMBuildAlloca(state->llvm_builder, optional_llvm_type, "");

		LLVMBasicBlockRef present_block = LLVMAppendBasicBlock(state->current_function, "");
		LLVMBasicBlockRef empty_block = LLVMAppendBasicBlock(state->current_function, "");
		LLVMBasicBlockRef done_block = LLVMAppendBasicBlock(state->current_function, "");

		LLVMValueRef check = LLVMBuildICmp(state->llvm_builder, LLVMIntEQ, value_tag, check_value, "");
		LLVMBuildCondBr(state->llvm_builder, check, present_block, empty_block);

		LLVMPositionBuilderAtEnd(state->llvm_builder, present_block);
		LLVMBuildStore(state->llvm_builder, value_data, optional_ptr);
		LLVMBuildBr(state->llvm_builder, done_block);

		LLVMPositionBuilderAtEnd(state->llvm_builder, empty_block);
		LLVMBuildStore(state->llvm_builder, LLVMConstNull(optional_llvm_type), optional_ptr);
		LLVMBuildBr(state->llvm_builder, done_block);

		LLVMPositionBuilderAtEnd(state->llvm_builder, done_block);
		return LLVMBuildLoad2(state->llvm_builder, optional_llvm_type, optional_ptr, "");
	} else {
		LLVMValueRef optional_ptr = LLVMBuildAlloca(state->llvm_builder, optional_llvm_type, "");
		LLVMValueRef optional_present_ptr = LLVMBuildStructGEP2(state->llvm_builder, optional_llvm_type, optional_ptr, 0, "");
		LLVMValueRef optional_data_ptr = LLVMBuildStructGEP2(state->llvm_builder, optional_llvm_type, optional_ptr, 1, "");
		LLVMBuildStore(state->llvm_builder, LLVMBuildICmp(state->llvm_builder, LLVMIntEQ, value_tag, check_value, ""), optional_present_ptr);
		LLVMBuildStore(state->llvm_builder, value_data, optional_data_ptr);
		return LLVMBuildLoad2(state->llvm_builder, optional_llvm_type, optional_ptr, "");
	}
}

static LLVMValueRef generate_catch(Node *node, State *state) {
	assert(node->kind == CATCH_NODE);
	Catch_Node catch = node->catch;
	Node_Data *data = get_data(&state->context, node);
	Catch_Data catch_data = data->catch;

	LLVMBasicBlockRef value_block = LLVMAppendBasicBlock(state->current_function, "");
	LLVMBasicBlockRef error_block = LLVMAppendBasicBlock(state->current_function, "");
	LLVMBasicBlockRef end_block = LLVMAppendBasicBlock(state->current_function, "");

	LLVMTypeRef result_value_type = create_llvm_type(catch_data.type.value->result_type.value.value, state);
	LLVMTypeRef result_error_type = create_llvm_type(catch_data.type.value->result_type.error.value, state);
	
	LLVMValueRef result = LLVMBuildAlloca(state->llvm_builder, result_value_type, "");
	LLVMValueRef value = generate_node(catch.value, state);
	LLVMValueRef value_tag = LLVMBuildExtractValue(state->llvm_builder, value, 0, "");
	LLVMValueRef cmp_result = LLVMBuildICmp(state->llvm_builder, LLVMIntEQ, value_tag, LLVMConstInt(LLVMInt64Type(), 0, false), "");

	LLVMValueRef value_data = LLVMBuildExtractValue(state->llvm_builder, value, 1, "");
	LLVMValueRef value_data_ptr = LLVMBuildAlloca(state->llvm_builder, LLVMPointerType(LLVMTypeOf(value_data), 0), "");
	LLVMBuildStore(state->llvm_builder, value_data, value_data_ptr);

	LLVMBuildCondBr(state->llvm_builder, cmp_result, value_block, error_block);

	LLVMPositionBuilderAtEnd(state->llvm_builder, value_block);
	LLVMBuildStore(state->llvm_builder, LLVMBuildLoad2(state->llvm_builder, result_value_type, LLVMBuildBitCast(state->llvm_builder, value_data_ptr, LLVMPointerType(result_value_type, 0), ""), ""), result);
	LLVMBuildBr(state->llvm_builder, end_block);

	LLVMPositionBuilderAtEnd(state->llvm_builder, error_block);
	if (catch.binding != NULL) {
		Catch_Codegen_Data catch_codegen_data = {
			.binding = LLVMBuildLoad2(state->llvm_builder, result_error_type, LLVMBuildBitCast(state->llvm_builder, value_data_ptr, LLVMPointerType(result_error_type, 0), ""), "")
		};
		hmput(state->catchs, data, catch_codegen_data);
	}

	LLVMValueRef error_result = generate_node(catch.error, state);
	if (error_result != NULL) {
		LLVMBuildStore(state->llvm_builder, error_result, result);
	}

	if (!catch_data.returned) LLVMBuildBr(state->llvm_builder, end_block);

	LLVMPositionBuilderAtEnd(state->llvm_builder, end_block);

	return LLVMBuildLoad2(state->llvm_builder, result_value_type, result, "");
}

static LLVMValueRef generate_structure_access(Node *node, State *state) {
	assert(node->kind == STRUCTURE_ACCESS_NODE);
	Structure_Access_Node structure_access = node->structure_access;

	Structure_Access_Data data = get_data(&state->context, node)->structure_access;
	Value_Data *structure_type = data.structure_type.value;

	unsigned int index = 0;
	Value_Data *item_type = data.item_type.value;
	switch (structure_type->tag) {
		case STRUCT_TYPE_VALUE: {
			for (long int i = 0; i < arrlen(structure_type->struct_type.members); i++) {
				if (strcmp(structure_access.name, structure_type->struct_type.node->struct_type.members[i].name) == 0) {
					index = i;
					break;
				}
			}
			break;
		}
		case TUPLE_TYPE_VALUE: {
			index = strtoul(structure_access.name + 1, NULL, 10);
			break;
		}
		case UNION_TYPE_VALUE: {
			for (long int i = 0; i < arrlen(structure_type->union_type.items); i++) {
				if (strcmp(structure_access.name, structure_type->union_type.items[i].identifier) == 0) {
					index = i;
					break;
				}
			}
			break;
		}
		case ARRAY_VIEW_TYPE_VALUE: {
			if (strcmp(structure_access.name, "len") == 0) {
				index = 0;
			} else if (strcmp(structure_access.name, "ptr") == 0) {
				index = 1;
			} else {
				assert(false);
			}
			break;
		}
		default:
			assert(false);
	}

	LLVMValueRef structure_llvm_value = generate_node(structure_access.parent, state);

	if (data.pointer_access) {
		LLVMValueRef element_pointer = NULL;
		if (structure_type->tag == STRUCT_TYPE_VALUE || structure_type->tag == ARRAY_VIEW_TYPE_VALUE) {
			element_pointer = LLVMBuildStructGEP2(state->llvm_builder, create_llvm_type(structure_type, state), structure_llvm_value, index, "");
		} else if (structure_type->tag == UNION_TYPE_VALUE) {
			element_pointer = LLVMBuildBitCast(state->llvm_builder, structure_llvm_value, LLVMPointerType(create_llvm_type(item_type, state), 0), "");
		} else {
			assert(false);
		}

		if (data.assign_value != NULL) {
			LLVMBuildStore(state->llvm_builder, generate_node(data.assign_value, state), element_pointer);
			return NULL;
		} else {
			if (data.want_pointer) {
				return element_pointer;
			} else  {
				return LLVMBuildLoad2(state->llvm_builder, create_llvm_type(item_type, state), element_pointer, "");
			}
		}
	} else {
		if (structure_type->tag == STRUCT_TYPE_VALUE || structure_type->tag == TUPLE_TYPE_VALUE || structure_type->tag == ARRAY_VIEW_TYPE_VALUE) {
			return LLVMBuildExtractValue(state->llvm_builder, structure_llvm_value, index, "");
		} else if (structure_type->tag == UNION_TYPE_VALUE) {
			LLVMValueRef temp = LLVMBuildAlloca(state->llvm_builder, LLVMTypeOf(structure_llvm_value), "");
			LLVMBuildStore(state->llvm_builder, structure_llvm_value, temp);

			LLVMTypeRef item_type_llvm = create_llvm_type(data.item_type.value, state);
			return LLVMBuildLoad2(state->llvm_builder, item_type_llvm, LLVMBuildBitCast(state->llvm_builder, temp, LLVMPointerType(item_type_llvm, 0), ""), "");
		} else {
			assert(false);
		}
	}
}

static LLVMValueRef generate_array_access(Node *node, State *state) {
	assert(node->kind == ARRAY_ACCESS_NODE);
	Array_Access_Node array_access = node->array_access;

	Array_Access_Data array_access_data = get_data(&state->context, node)->array_access;

	Value_Data *array_type = array_access_data.array_type.value;

	LLVMValueRef array_llvm_value = generate_node(array_access.parent, state);
	LLVMValueRef element_pointer = NULL;

	if (array_access_data.custom_operator_function.function != NULL) {
		Node **arguments = NULL;
		arrpush(arguments, array_access.parent);
		arrpush(arguments, array_access.index);

		Custom_Operator_Function operator = array_access_data.custom_operator_function;
		element_pointer = generate_call_generic(generate_value(operator.function, operator.function_type.value, state), array_access_data.custom_operator_function.function_type.value, arguments, state);
	} else {
		LLVMValueRef indices[2] = {
			LLVMConstInt(LLVMInt64Type(), 0, false),
			generate_node(array_access.index, state)
		};

		LLVMTypeRef array_llvm_type = create_llvm_type(array_type->pointer_type.inner.value, state);
		switch (array_type->pointer_type.inner.value->tag) {
			case ARRAY_TYPE_VALUE: {
				element_pointer = LLVMBuildGEP2(state->llvm_builder, array_llvm_type, array_llvm_value, indices, 2, "");
				break;
			}
			case ARRAY_VIEW_TYPE_VALUE: {
				LLVMTypeRef inner_type = create_llvm_type(array_type->pointer_type.inner.value->array_view_type.inner.value, state);
				LLVMTypeRef actual_array_llvm_type = LLVMArrayType2(inner_type, 0);
				LLVMValueRef elements_pointer = LLVMBuildLoad2(state->llvm_builder, LLVMPointerType(actual_array_llvm_type, 0), LLVMBuildStructGEP2(state->llvm_builder, array_llvm_type, array_llvm_value, 1, ""), "");
				element_pointer = LLVMBuildGEP2(state->llvm_builder, actual_array_llvm_type, elements_pointer, indices, 2, "");
				break;
			}
			default:
				assert(false);
		}
	}

	if (array_access_data.assign_value != NULL) {
		LLVMBuildStore(state->llvm_builder, generate_node(array_access_data.assign_value, state), element_pointer);
		return NULL;
	} else {
		if (array_access_data.want_pointer) {
			return element_pointer;
		} else  {
			return LLVMBuildLoad2(state->llvm_builder, create_llvm_type(array_access_data.item_type.value, state), element_pointer, "");
		}
	}
}

static bool is_type_signed(Value_Data *type) {
	(void) type;
	return false;
}

static bool is_type_float(Value_Data *type) {
	if (type->tag == FLOAT_TYPE_VALUE) return true;
	return false;
}

static LLVMValueRef values_equal(Value_Data *type, LLVMValueRef value1, LLVMValueRef value2, State *state) {
	switch (type->tag) {
		case BYTE_TYPE_VALUE: {
			return LLVMBuildICmp(state->llvm_builder, LLVMIntEQ, value1, value2, "");
		}
		case INTEGER_TYPE_VALUE: {
			return LLVMBuildICmp(state->llvm_builder, LLVMIntEQ, value1, value2, "");
		}
		case ENUM_TYPE_VALUE: {
			return LLVMBuildICmp(state->llvm_builder, LLVMIntEQ, value1, value2, "");
		}
		default:
			assert(false);
	}
}

static LLVMValueRef generate_binary_op(Node *node, State *state) {
	assert(node->kind == BINARY_OP_NODE);
	Binary_Op_Node binary_operator = node->binary_op;
	Binary_Operator_Data binary_operator_data = get_data(&state->context, node)->binary_operator;

	LLVMValueRef left_value = generate_node(binary_operator.left, state);
	LLVMValueRef right_value = generate_node(binary_operator.right, state);

	switch (binary_operator.operator) {
		case OP_EQUALS:
			return values_equal(binary_operator_data.type.value, left_value, right_value, state);
		case OP_NOT_EQUALS:
			return LLVMBuildNot(state->llvm_builder, values_equal(binary_operator_data.type.value, left_value, right_value, state), "");
		case OP_LESS:
			if (is_type_signed(binary_operator_data.type.value)) {
				return LLVMBuildICmp(state->llvm_builder, LLVMIntSLT, left_value, right_value, "");
			} else {
				return LLVMBuildICmp(state->llvm_builder, LLVMIntULT, left_value, right_value, "");
			}
		case OP_LESS_EQUALS:
			if (is_type_signed(binary_operator_data.type.value)) {
				return LLVMBuildICmp(state->llvm_builder, LLVMIntSLE, left_value, right_value, "");
			} else {
				return LLVMBuildICmp(state->llvm_builder, LLVMIntULE, left_value, right_value, "");
			}
		case OP_GREATER:
			if (is_type_signed(binary_operator_data.type.value)) {
				return LLVMBuildICmp(state->llvm_builder, LLVMIntSGT, left_value, right_value, "");
			} else {
				return LLVMBuildICmp(state->llvm_builder, LLVMIntUGT, left_value, right_value, "");
			}
		case OP_GREATER_EQUALS:
			if (is_type_signed(binary_operator_data.type.value)) {
				return LLVMBuildICmp(state->llvm_builder, LLVMIntSGE, left_value, right_value, "");
			} else {
				return LLVMBuildICmp(state->llvm_builder, LLVMIntUGE, left_value, right_value, "");
			}
		case OP_ADD:
			if (is_type_float(binary_operator_data.type.value)) {
				return LLVMBuildFAdd(state->llvm_builder, left_value, right_value, "");
			} else {
				return LLVMBuildAdd(state->llvm_builder, left_value, right_value, "");
			}
		case OP_SUBTRACT:
			if (is_type_float(binary_operator_data.type.value)) {
				return LLVMBuildFSub(state->llvm_builder, left_value, right_value, "");
			} else {
				return LLVMBuildSub(state->llvm_builder, left_value, right_value, "");
			}
		case OP_MULTIPLY:
			if (is_type_float(binary_operator_data.type.value)) {
				return LLVMBuildFMul(state->llvm_builder, left_value, right_value, "");
			} else {
				return LLVMBuildMul(state->llvm_builder, left_value, right_value, "");
			}
		case OP_DIVIDE:
			if (is_type_float(binary_operator_data.type.value)) {
				return LLVMBuildFDiv(state->llvm_builder, left_value, right_value, "");
			} else {
				if (is_type_signed(binary_operator_data.type.value)) {
					return LLVMBuildSDiv(state->llvm_builder, left_value, right_value, "");
				} else {
					return LLVMBuildUDiv(state->llvm_builder, left_value, right_value, "");
				}
			}
		default:
			assert(false);
	}
}

static LLVMValueRef generate_return(Node *node, State *state) {
	assert(node->kind == RETURN_NODE);
	Return_Node return_ = node->return_;
	Return_Data return_data = get_data(&state->context, node)->return_;

	if (return_data.type.value != NULL && return_data.type.value->tag == RESULT_TYPE_VALUE && return_.type != RETURN_STANDARD) {
		LLVMTypeRef return_type = create_llvm_type(return_data.type.value, state);
		LLVMValueRef result_value = LLVMBuildAlloca(state->llvm_builder, return_type, "");

		LLVMValueRef tag_pointer = LLVMBuildStructGEP2(state->llvm_builder, return_type, result_value, 0, "");
		LLVMValueRef data_pointer = LLVMBuildStructGEP2(state->llvm_builder, return_type, result_value, 1, "");

		if (return_.type == RETURN_SUCCESS) {
			LLVMBuildStore(state->llvm_builder, LLVMConstInt(LLVMInt64Type(), 0, false), tag_pointer);
			data_pointer = LLVMBuildBitCast(state->llvm_builder, data_pointer, LLVMPointerType(create_llvm_type(return_data.type.value->result_type.value.value, state), 0), "");
			if (return_.value != NULL) LLVMBuildStore(state->llvm_builder, generate_node(return_.value, state), data_pointer);
		} else if (return_.type == RETURN_ERROR) {
			LLVMBuildStore(state->llvm_builder, LLVMConstInt(LLVMInt64Type(), 1, false), tag_pointer);
			data_pointer = LLVMBuildBitCast(state->llvm_builder, data_pointer, LLVMPointerType(create_llvm_type(return_data.type.value->result_type.error.value, state), 0), "");
			if (return_.value != NULL) LLVMBuildStore(state->llvm_builder, generate_node(return_.value, state), data_pointer);
		} else {
			assert(false);
		}

		LLVMBuildRet(state->llvm_builder, LLVMBuildLoad2(state->llvm_builder, return_type, result_value, ""));
	} else {
		if (return_data.type.value != NULL) {
			LLVMBuildRet(state->llvm_builder, generate_node(return_.value, state));
		} else {
			LLVMBuildRetVoid(state->llvm_builder);
		}
	}
	return NULL;
}

static LLVMValueRef generate_assign(Node *node, State *state) {
	assert(node->kind == ASSIGN_NODE);
	Assign_Node assign = node->assign;

	return generate_node(assign.target, state);
}

static LLVMValueRef generate_variable(Node *node, State *state) {
	assert(node->kind == VARIABLE_NODE);
	Variable_Node variable = node->variable;

	if (variable.static_) {
		return NULL;
	}

	LLVMValueRef allocated_variable_llvm = LLVMBuildAlloca(state->llvm_builder, create_llvm_type(get_data(&state->context, node)->variable.type.value, state), "");
	if (variable.value != NULL) {
		LLVMValueRef llvm_value = generate_node(variable.value, state);
		LLVMBuildStore(state->llvm_builder, llvm_value, allocated_variable_llvm);
	}

	Node_Data *node_data = get_data(&state->context, node);
	hmput(state->variables, node_data, allocated_variable_llvm);

	return NULL;
}

static LLVMValueRef generate_break(Node *node, State *state) {
	assert(node->kind == BREAK_NODE);
	Break_Node break_ = node->break_;
	Break_Data break_data = get_data(&state->context, node)->break_;
	Node_Data *while_data = get_data(&state->context, break_data.while_);
	While_Codegen_Data while_codegen_data = hmget(state->whiles, while_data);
	if (break_.value != NULL) {
		LLVMBuildStore(state->llvm_builder, generate_node(break_.value, state), while_codegen_data.value);
	}
	LLVMBuildBr(state->llvm_builder, while_codegen_data.block);
	LLVMBasicBlockRef block = LLVMAppendBasicBlock(state->current_function, "");
	LLVMPositionBuilderAtEnd(state->llvm_builder, block);

	return NULL;
}

static LLVMValueRef generate_if(Node *node, State *state) {
	assert(node->kind == IF_NODE);
	If_Node if_ = node->if_;
	Node_Data *data = get_data(&state->context, node);
	If_Data if_data = data->if_;

	if (if_.static_) {
		if (if_data.static_condition) {
			return generate_node(if_.if_body, state);
		} else {
			if (if_.else_body != NULL) {
				return generate_node(if_.else_body, state);
			}
		}

		return NULL;
	} else {
		LLVMValueRef value = NULL;
		if (if_data.result_type.value != NULL) {
			value = LLVMBuildAlloca(state->llvm_builder, create_llvm_type(if_data.result_type.value, state), "");
		}

		LLVMValueRef condition = generate_node(if_.condition, state);
		if (if_data.type.value->tag == OPTIONAL_TYPE_VALUE) {
			LLVMValueRef optional_llvm_value = condition;
			If_Codegen_Data if_codegen_data = {
				.binding = if_data.type.value->optional_type.inner.value->tag == POINTER_TYPE_VALUE ? optional_llvm_value : LLVMBuildExtractValue(state->llvm_builder, optional_llvm_value, 1, "")
			};
			hmput(state->ifs, data, if_codegen_data);

			if (if_data.type.value->optional_type.inner.value->tag == POINTER_TYPE_VALUE) {
				condition = LLVMBuildICmp(state->llvm_builder, LLVMIntNE, optional_llvm_value, LLVMConstNull(LLVMTypeOf(optional_llvm_value)), "");
			} else {
				condition = LLVMBuildExtractValue(state->llvm_builder, optional_llvm_value, 0, "");
			}
		} else if (if_data.type.value->tag == RESULT_TYPE_VALUE) {
			LLVMValueRef result_llvm_value = condition;

			LLVMValueRef value_data = LLVMBuildExtractValue(state->llvm_builder, result_llvm_value, 1, "");
			LLVMValueRef value_data_ptr = LLVMBuildAlloca(state->llvm_builder, LLVMPointerType(LLVMTypeOf(value_data), 0), "");
			LLVMBuildStore(state->llvm_builder, value_data, value_data_ptr);

			LLVMTypeRef result_value_type = create_llvm_type(if_data.type.value->result_type.value.value, state);
			If_Codegen_Data if_codegen_data = {
				.binding = LLVMBuildLoad2(state->llvm_builder, result_value_type, LLVMBuildBitCast(state->llvm_builder, value_data_ptr, LLVMPointerType(result_value_type, 0), ""), "")
			};
			hmput(state->ifs, data, if_codegen_data);

			condition = LLVMBuildICmp(state->llvm_builder, LLVMIntEQ, LLVMBuildExtractValue(state->llvm_builder, result_llvm_value, 0, ""), LLVMConstInt(LLVMInt64Type(), 0, false), "");
		}

		LLVMBasicBlockRef if_block = LLVMAppendBasicBlock(state->current_function, "");
		LLVMBasicBlockRef else_block = LLVMAppendBasicBlock(state->current_function, "");
		LLVMBasicBlockRef done_block = LLVMAppendBasicBlock(state->current_function, "");
		LLVMBuildCondBr(state->llvm_builder, condition, if_block, else_block);
		LLVMPositionBuilderAtEnd(state->llvm_builder, if_block);

		LLVMValueRef if_value = generate_node(if_.if_body, state);
		if (if_data.result_type.value != NULL) {
			LLVMBuildStore(state->llvm_builder, if_value, value);
		}
		if (!if_data.then_returned) LLVMBuildBr(state->llvm_builder, done_block);
		LLVMPositionBuilderAtEnd(state->llvm_builder, else_block);
		if (if_.else_body != NULL) {
			LLVMValueRef else_value = generate_node(if_.else_body, state);
			if (if_data.result_type.value != NULL && else_value != NULL) {
				LLVMBuildStore(state->llvm_builder, else_value, value);
			}
		}
		if (!if_data.else_returned) LLVMBuildBr(state->llvm_builder, done_block);
		LLVMPositionBuilderAtEnd(state->llvm_builder, done_block);

		if (if_data.returned) {
			LLVMBuildUnreachable(state->llvm_builder);
		}

		if (if_data.result_type.value != NULL) {
			return LLVMBuildLoad2(state->llvm_builder, create_llvm_type(if_data.result_type.value, state), value, "");
		} else {
			return NULL;
		}
	}
}

static LLVMValueRef generate_switch(Node *node, State *state) {
	assert(node->kind == SWITCH_NODE);
	Switch_Node switch_ = node->switch_;
	Switch_Data switch_data = get_data(&state->context, node)->switch_;

	if (switch_.static_) {
		if (switch_data.static_case >= 0) {
			Switch_Case switch_case = switch_.cases[switch_data.static_case];
			return generate_node(switch_case.body, state);
		} else {
			return NULL;
		}
	}

	LLVMValueRef value = NULL;
	if (switch_data.type.value != NULL) {
		value = LLVMBuildAlloca(state->llvm_builder, create_llvm_type(switch_data.type.value, state), "");
	}

	LLVMValueRef switched_value = generate_node(switch_.condition, state);

	bool added_else_block = false;
	LLVMBasicBlockRef else_block = LLVMAppendBasicBlock(state->current_function, "");
	LLVMBasicBlockRef done_block = LLVMAppendBasicBlock(state->current_function, "");
	LLVMValueRef switch_llvm_value = LLVMBuildSwitch(state->llvm_builder, switched_value, else_block, arrlen(switch_.cases));
	for (long int i = 0; i < arrlen(switch_.cases); i++) {
		Switch_Case switch_case = switch_.cases[i];

		LLVMBasicBlockRef case_block = LLVMAppendBasicBlock(state->current_function, "");
		LLVMPositionBuilderAtEnd(state->llvm_builder, case_block);
		LLVMValueRef case_value = generate_node(switch_case.body, state);
		if (switch_data.type.value != NULL) {
			LLVMBuildStore(state->llvm_builder, case_value, value);
		}

		if (switch_case.value != NULL) {
			LLVMAddCase(switch_llvm_value, generate_node(switch_case.value, state), case_block);
			if (!switch_data.cases_returned[i]) LLVMBuildBr(state->llvm_builder, done_block);
		} else {
			LLVMPositionBuilderAtEnd(state->llvm_builder, else_block);
			LLVMBuildBr(state->llvm_builder, case_block);
			added_else_block = true;
		}
	}

	if (!added_else_block) {
		LLVMPositionBuilderAtEnd(state->llvm_builder, else_block);
		LLVMBuildBr(state->llvm_builder, done_block);
	}

	LLVMPositionBuilderAtEnd(state->llvm_builder, done_block);
	if (switch_data.returned) {
		LLVMBuildUnreachable(state->llvm_builder);
	}

	if (switch_data.type.value != NULL) {
		return LLVMBuildLoad2(state->llvm_builder, create_llvm_type(switch_data.type.value, state), value, "");
	} else {
		return NULL;
	}
}

static LLVMValueRef generate_while(Node *node, State *state) {
	assert(node->kind == WHILE_NODE);
	While_Node while_ = node->while_;
	Node_Data *data = get_data(&state->context, node);
	While_Data while_data = data->while_;

	LLVMBasicBlockRef check_block = LLVMAppendBasicBlock(state->current_function, "");
	LLVMBasicBlockRef body_block = LLVMAppendBasicBlock(state->current_function, "");
	LLVMBasicBlockRef else_block = LLVMAppendBasicBlock(state->current_function, "");
	LLVMBasicBlockRef done_block = LLVMAppendBasicBlock(state->current_function, "");

	LLVMValueRef value = NULL;
	if (while_data.type.value != NULL) {
		value = LLVMBuildAlloca(state->llvm_builder, create_llvm_type(while_data.type.value, state), "");
	}

	While_Codegen_Data while_codegen_data = {
		.block = done_block,
		.value = value
	};

	hmput(state->whiles, data, while_codegen_data);
	LLVMBuildBr(state->llvm_builder, check_block);
	LLVMPositionBuilderAtEnd(state->llvm_builder, check_block);
	LLVMValueRef condition = generate_node(while_.condition, state);
	LLVMBuildCondBr(state->llvm_builder, condition, body_block, else_block);
	LLVMPositionBuilderAtEnd(state->llvm_builder, body_block);
	generate_node(while_.body, state);
	LLVMBuildBr(state->llvm_builder, check_block);
	LLVMPositionBuilderAtEnd(state->llvm_builder, else_block);
	if (while_.else_body != NULL) {
		LLVMValueRef else_value = generate_node(while_.else_body, state);
		LLVMBuildStore(state->llvm_builder, else_value, value);
	}
	LLVMBuildBr(state->llvm_builder, done_block);
	LLVMPositionBuilderAtEnd(state->llvm_builder, done_block);

	if (while_data.type.value != NULL) {
		return LLVMBuildLoad2(state->llvm_builder, create_llvm_type(while_data.type.value, state), value, "");
	} else {
		return NULL;
	}
}

static LLVMValueRef generate_for(Node *node, State *state) {
	assert(node->kind == FOR_NODE);
	For_Node for_ = node->for_;
	Node_Data *for_data = get_data(&state->context, node);

	if (for_.static_) {
		size_t saved_static_id = state->context.static_id;
		for (long int i = 0; i < arrlen(for_data->for_.static_ids); i++) {
			state->context.static_id = for_data->for_.static_ids[i];
			generate_node(for_.body, state);
		}
		state->context.static_id = saved_static_id;
		return NULL;
	}

	LLVMBasicBlockRef check_block = LLVMAppendBasicBlock(state->current_function, "");
	LLVMBasicBlockRef body_block = LLVMAppendBasicBlock(state->current_function, "");
	LLVMBasicBlockRef done_block = LLVMAppendBasicBlock(state->current_function, "");

	LLVMValueRef *items = NULL;
	for (long int i = 0; i < arrlen(for_.items); i++) {
		arrpush(items, generate_node(for_.items[i], state));
	}

	LLVMValueRef min_len = LLVMConstInt(LLVMInt64Type(), -1, false);
	for (long int i = 0; i < arrlen(for_.items); i++) {
		LLVMValueRef len;
		switch (for_data->for_.types[i].value->tag) {
			case ARRAY_VIEW_TYPE_VALUE: {
				len = LLVMBuildExtractValue(state->llvm_builder, items[i], 0, "");
				break;
			}
			case RANGE_TYPE_VALUE: {
				len = LLVMBuildExtractValue(state->llvm_builder, items[i], 1, "");
				break;
			}
			default:
				assert(false);
		}

		LLVMValueRef condition = LLVMBuildICmp(state->llvm_builder, LLVMIntULT, len, min_len, "");
		min_len = LLVMBuildSelect(state->llvm_builder, condition, len, min_len, "");
	}

	LLVMValueRef i = LLVMBuildAlloca(state->llvm_builder, LLVMInt64Type(), "");
	LLVMBuildStore(state->llvm_builder, LLVMConstInt(LLVMInt64Type(), 0, false), i);
	LLVMBuildBr(state->llvm_builder, check_block);
	LLVMPositionBuilderAtEnd(state->llvm_builder, check_block);
	LLVMValueRef i_value = LLVMBuildLoad2(state->llvm_builder, LLVMInt64Type(), i, "");
	LLVMValueRef condition = LLVMBuildICmp(state->llvm_builder, LLVMIntULT, i_value, min_len, "");
	LLVMBuildCondBr(state->llvm_builder, condition, body_block, done_block);
	LLVMPositionBuilderAtEnd(state->llvm_builder, body_block);

	LLVMValueRef *bindings = NULL;
	for (long int i = 0; i < arrlen(for_.items); i++) {
		switch (for_data->for_.types[i].value->tag) {
			case ARRAY_VIEW_TYPE_VALUE: {
				LLVMValueRef indices[2] = {
					LLVMConstInt(LLVMInt64Type(), 0, false),
					i_value
				};
				LLVMTypeRef element_type = create_llvm_type(for_data->for_.types[i].value->array_view_type.inner.value, state);
				LLVMValueRef item_ptr = LLVMBuildExtractValue(state->llvm_builder, items[i], 1, "");
				LLVMValueRef element = LLVMBuildGEP2(state->llvm_builder, LLVMArrayType2(element_type, 0), item_ptr, indices, 2, "");

				arrpush(bindings, element);
				break;
			}
			case RANGE_TYPE_VALUE: {
				LLVMValueRef start = LLVMBuildExtractValue(state->llvm_builder, items[i], 0, "");
				LLVMValueRef element = LLVMBuildAdd(state->llvm_builder, start, i_value, "");

				arrpush(bindings, element);
				break;
			}
			default:
				assert(false);
		}
	}

	For_Codegen_Data for_codegen_data = {
		.bindings = bindings
	};
	hmput(state->fors, for_data, for_codegen_data);

	generate_node(for_.body, state);

	LLVMBuildStore(state->llvm_builder, LLVMBuildAdd(state->llvm_builder, LLVMBuildLoad2(state->llvm_builder, LLVMInt64Type(), i, ""), LLVMConstInt(LLVMInt64Type(), 1, false), ""), i);

	LLVMBuildBr(state->llvm_builder, check_block);
	LLVMPositionBuilderAtEnd(state->llvm_builder, done_block);

	return NULL;
}

static LLVMValueRef generate_range(Node *node, State *state) {
	assert(node->kind == RANGE_NODE);
	Range_Node range = node->range;
	Value range_type = get_type(&state->context, node);
	LLVMTypeRef range_type_llvm = create_llvm_type(range_type.value, state);

	LLVMValueRef range_value = LLVMBuildAlloca(state->llvm_builder, range_type_llvm, "");

	LLVMValueRef start_pointer = LLVMBuildStructGEP2(state->llvm_builder, range_type_llvm, range_value, 0, "");
	LLVMBuildStore(state->llvm_builder, generate_node(range.start, state), start_pointer);

	LLVMValueRef end_pointer = LLVMBuildStructGEP2(state->llvm_builder, range_type_llvm, range_value, 1, "");
	if (range.end != NULL) {
		LLVMBuildStore(state->llvm_builder, generate_node(range.end, state), end_pointer);
	} else {
		LLVMTypeRef element_type_llvm = create_llvm_type(get_type(&state->context, range.start).value, state);
		LLVMBuildStore(state->llvm_builder, LLVMConstInt(element_type_llvm, (1ULL << LLVMGetIntTypeWidth(element_type_llvm)) - 2, false), end_pointer);
	}

	return LLVMBuildLoad2(state->llvm_builder, range_type_llvm, range_value, "");
}

static LLVMValueRef generate_internal(Node *node, State *state) {
	assert(node->kind == INTERNAL_NODE);
	Internal_Node internal = node->internal;
	Internal_Data internal_data = get_data(&state->context, node)->internal;

	assert(internal.kind == INTERNAL_EMBED);
	return generate_node(internal_data.node, state);
}

static LLVMValueRef generate_node(Node *node, State *state) {
	switch (node->kind) {
		case DEFINE_NODE:
			generate_define(node, state);
			return NULL;
		case BLOCK_NODE:
			return generate_block(node, state);
		case CALL_NODE:
			return generate_call(node, state);
		case CALL_METHOD_NODE:
			return generate_call_method(node, state);
		case IDENTIFIER_NODE:
			return generate_identifier(node, state);
		case STRING_NODE:
			return generate_string(node, state);
		case CHARACTER_NODE:
			return generate_character(node, state);
		case NUMBER_NODE:
			return generate_number(node, state);
		case NULL_NODE:
			return generate_null(node, state);
		case BOOLEAN_NODE:
			return generate_boolean(node, state);
		case STRUCTURE_NODE:
			return generate_structure(node, state);
		case RUN_NODE:
			return generate_run(node, state);
		case CAST_NODE:
			return generate_cast(node, state);
		case REFERENCE_NODE:
			return generate_reference(node, state);
		case DEREFERENCE_NODE:
			return generate_dereference(node, state);
		case DEOPTIONAL_NODE:
			return generate_deoptional(node, state);
		case IS_NODE:
			return generate_is(node, state);
		case CATCH_NODE:
			return generate_catch(node, state);
		case STRUCTURE_ACCESS_NODE:
			return generate_structure_access(node, state);
		case ARRAY_ACCESS_NODE:
			return generate_array_access(node, state);
		case BINARY_OP_NODE:
			return generate_binary_op(node, state);
		case RETURN_NODE:
			return generate_return(node, state);
		case ASSIGN_NODE:
			return generate_assign(node, state);
		case VARIABLE_NODE:
			return generate_variable(node, state);
		case BREAK_NODE:
			return generate_break(node, state);
		case IF_NODE:
			return generate_if(node, state);
		case SWITCH_NODE:
			return generate_switch(node, state);
		case WHILE_NODE:
			return generate_while(node, state);
		case FOR_NODE:
			return generate_for(node, state);
		case RANGE_NODE:
			return generate_range(node, state);
		case INTERNAL_NODE:
			return generate_internal(node, state);
		default:
			assert(false);
	}
}

static LLVMValueRef generate_function(Value_Data *value, State *state) {
	assert(value->tag == FUNCTION_VALUE);
	Function_Value function = value->function;

	size_t saved_static_argument_id = state->context.static_id;
	state->context.static_id = function.static_id;

	if (function.type->function_type.incomplete) {
		return NULL;
	}

	Function_Data function_data = get_data(&state->context, function.node)->function;
	if (function_data.compile_only) {
		return NULL;
	}

	LLVMValueRef llvm_function = LLVMAddFunction(state->llvm_module, "", create_llvm_function_literal_type(function.type, state));
	hmput(state->generated_cache, value, llvm_function);

	LLVMBuilderRef saved_llvm_builder = state->llvm_builder;
	state->llvm_builder = LLVMCreateBuilder();

	LLVMValueRef saved_current_function = state->current_function;
	LLVMValueRef *saved_function_arguments = state->function_arguments;
	state->current_function = llvm_function;
	state->function_arguments = NULL;

	if (function.body != NULL) {
		LLVMBasicBlockRef entry = LLVMAppendBasicBlock(llvm_function, "");
		LLVMPositionBuilderAtEnd(state->llvm_builder, entry);

		long int j = 0;
		for (long int i = 0; i < LLVMCountParams(llvm_function); i++) {
			while (function.type->function_type.arguments[j].static_) {
				j++;
			}

			LLVMTypeRef type = create_llvm_type(function.type->function_type.arguments[j].type.value, state);
			LLVMValueRef allocated = LLVMBuildAlloca(state->llvm_builder, type, "");
			LLVMBuildStore(state->llvm_builder, LLVMGetParam(llvm_function, i), allocated);
			arrpush(state->function_arguments, allocated);
			j++;
		}

		LLVMValueRef value = generate_node(function.body, state);

		if (!function_data.returned) {
			if (function.type->function_type.return_type.value != NULL) {
				LLVMBuildRet(state->llvm_builder, value);
			} else {
				LLVMBuildRetVoid(state->llvm_builder);
			}
		}
	}

	state->function_arguments = saved_function_arguments;
	state->current_function = saved_current_function;
	state->context.static_id = saved_static_argument_id;
	state->llvm_builder = saved_llvm_builder;

	return llvm_function;
}

static LLVMValueRef generate_extern(Value_Data *value, State *state) {
	assert(value->tag == EXTERN_VALUE);
	Extern_Value extern_ = value->extern_;

	LLVMValueRef llvm_function = LLVMAddFunction(state->llvm_module, "", create_llvm_function_literal_type(extern_.type.value, state));
	hmput(state->generated_cache, value, llvm_function);

	LLVMSetValueName(llvm_function, extern_.name);

	return llvm_function;
}

static void generate_module(Value_Data *value, State *state) {
	assert(value->tag == MODULE_VALUE);
	Module_Value module = value->module;

	generate_node(module.body, state);
}

static LLVMValueRef generate_integer(Value_Data *value, State *state) {
	(void) state;
	assert(value->tag == INTEGER_VALUE);
	Integer_Value integer = value->integer;

	return LLVMConstInt(LLVMInt64Type(), integer.value, false);
}

static LLVMValueRef generate_byte(Value_Data *value, State *state) {
	(void) state;
	assert(value->tag == BYTE_VALUE);
	Byte_Value byte = value->byte;

	return LLVMConstInt(LLVMInt8Type(), byte.value, false);
}

static LLVMValueRef generate_enum(Value_Data *value, State *state) {
	(void) state;
	assert(value->tag == ENUM_VALUE);
	Enum_Value enum_ = value->enum_;

	return LLVMConstInt(LLVMInt64Type(), enum_.value, false);
}

static LLVMValueRef generate_struct(Value_Data *value, Value_Data *type, State *state) {
	Struct_Value struct_ = value->struct_;

	LLVMValueRef struct_value = LLVMGetUndef(create_llvm_type(type, state));
	for (long int i = 0; i < arrlen(struct_.values); i++) {
		struct_value = LLVMBuildInsertValue(state->llvm_builder, struct_value, generate_value(struct_.values[i], type->struct_type.members[i].value, state), i, "");
	}
	return struct_value;
}

static LLVMValueRef generate_array_view(Value_Data *value, Value_Data *type, State *state) {
	Array_View_Value array_view = value->array_view;

	Value_Data *inner_type = type->array_view_type.inner.value;
	LLVMTypeRef inner_llvm_type = create_llvm_type(inner_type, state);

	LLVMValueRef array_value = LLVMGetUndef(LLVMArrayType(inner_llvm_type, array_view.length));
	for (size_t i = 0; i < array_view.length; i++) {
		array_value = LLVMBuildInsertValue(state->llvm_builder, array_value, generate_value(array_view.values[i], inner_type, state), i, "");
	}
	LLVMValueRef global_array = LLVMAddGlobal(state->llvm_module, LLVMArrayType(inner_llvm_type, array_view.length), "");
	LLVMSetLinkage(global_array, LLVMPrivateLinkage);
	LLVMSetGlobalConstant(global_array, true);
	LLVMSetUnnamedAddr(global_array, true);
	LLVMSetInitializer(global_array, array_value);

	LLVMValueRef struct_value = LLVMGetUndef(create_llvm_type(type, state));
	struct_value = LLVMBuildInsertValue(state->llvm_builder, struct_value, LLVMConstInt(LLVMInt64Type(), array_view.length, false), 0, "");
	struct_value = LLVMBuildInsertValue(state->llvm_builder, struct_value, LLVMBuildPointerCast(state->llvm_builder, global_array, LLVMPointerType(inner_llvm_type, 0), ""), 1, "");

	return struct_value;
}

static LLVMValueRef generate_internal_value(Value_Data *value, State *state) {
	(void) state;

	Internal_Value internal = value->internal;
	if (streq(internal.identifier, "operating_system")) {
		#if defined(__linux__)
			return LLVMConstInt(LLVMInt64Type(), 0, false);
		#elif defined(__APPLE__)
			return LLVMConstInt(LLVMInt64Type(), 1, false);
		#elif defined(_WIN32)
			return LLVMConstInt(LLVMInt64Type(), 2, false);
		#endif
		return NULL;
	} else {
		assert(false);
	}
}

static LLVMValueRef generate_value(Value_Data *value, Value_Data *type, State *state) {
	LLVMValueRef cached_result = hmget(state->generated_cache, value);
	if (cached_result != NULL) {
		return cached_result;
	}

	LLVMValueRef result = NULL;
	switch (value->tag) {
		case FUNCTION_VALUE:
			result = generate_function(value, state);
			break;
		case EXTERN_VALUE:
			result = generate_extern(value, state);
			break;
		case MODULE_VALUE:
			generate_module(value, state);
			break;
		case INTEGER_VALUE:
			result = generate_integer(value, state);
			break;
		case BYTE_VALUE:
			result = generate_byte(value, state);
			break;
		case ENUM_VALUE:
			result = generate_enum(value, state);
			break;
		case ARRAY_VIEW_VALUE:
			result = generate_array_view(value, type, state);
			break;
		case STRUCT_VALUE:
			result = generate_struct(value, type, state);
			break;
		case INTERNAL_VALUE:
			result = generate_internal_value(value, state);
			break;
		default:
			break;
	}

	hmput(state->generated_cache, value, result);
	return result;
}

static void generate_main(State *state) {
	LLVMTypeRef argument_types[2] = {
		LLVMInt32Type(),
		LLVMPointerType(LLVMPointerType(LLVMInt8Type(), 0), 0)
	};

	LLVMValueRef llvm_function = LLVMAddFunction(state->llvm_module, "", LLVMFunctionType(LLVMVoidType(), argument_types, 2, false));
	LLVMSetValueName(llvm_function, "main");

	LLVMBasicBlockRef entry = LLVMAppendBasicBlock(llvm_function, "");
	LLVMPositionBuilderAtEnd(state->llvm_builder, entry);

	if (!state->main_takes_arguments) {
		LLVMBuildCall2(state->llvm_builder, LLVMGlobalGetValueType(state->main_function), state->main_function, NULL, 0, "");
		LLVMBuildRetVoid(state->llvm_builder);
		return;
	}

	LLVMValueRef argc = LLVMGetParam(llvm_function, 0);
	LLVMValueRef argv = LLVMGetParam(llvm_function, 1);
	LLVMValueRef i = LLVMBuildAlloca(state->llvm_builder, LLVMInt64Type(), "");
	LLVMBuildStore(state->llvm_builder, LLVMConstInt(LLVMInt64Type(), 0, false), i);

	LLVMTypeRef string_type = create_llvm_type(create_array_view_type(create_value(BYTE_TYPE_VALUE)).value, state);
	LLVMValueRef array = LLVMBuildArrayAlloca(state->llvm_builder, string_type, argc, "");
	LLVMBasicBlockRef start_check = LLVMAppendBasicBlock(llvm_function, "");
	LLVMBasicBlockRef start_loop = LLVMAppendBasicBlock(llvm_function, "");
	LLVMBasicBlockRef end_loop = LLVMAppendBasicBlock(llvm_function, "");

	LLVMBuildBr(state->llvm_builder, start_check);
	LLVMPositionBuilderAtEnd(state->llvm_builder, start_check);
	LLVMValueRef i_load = LLVMBuildLoad2(state->llvm_builder, LLVMInt64Type(), i, "");
	LLVMValueRef cmp_result = LLVMBuildICmp(state->llvm_builder, LLVMIntULT, i_load, LLVMBuildZExt(state->llvm_builder, argc, LLVMInt64Type(), ""), "");
	LLVMBuildCondBr(state->llvm_builder, cmp_result, start_loop, end_loop);
	LLVMPositionBuilderAtEnd(state->llvm_builder, start_loop);

	LLVMValueRef i_load2 = LLVMBuildLoad2(state->llvm_builder, LLVMInt64Type(), i, "");
	LLVMValueRef indices[2] = {
		LLVMConstInt(LLVMInt64Type(), 0, false),
		i_load2
	};
	LLVMValueRef argv_i_ptr = LLVMBuildGEP2(state->llvm_builder, LLVMArrayType2(LLVMPointerType(LLVMArrayType(LLVMInt8Type(), 0), 0), 0), argv, indices, 2, "");
	
	LLVMValueRef raw_string_value = LLVMBuildLoad2(state->llvm_builder, LLVMPointerType(LLVMArrayType2(LLVMInt8Type(), 0), 0), argv_i_ptr, "");

	LLVMBasicBlockRef start_length_check = LLVMAppendBasicBlock(llvm_function, "");
	LLVMBasicBlockRef start_length_loop = LLVMAppendBasicBlock(llvm_function, "");
	LLVMBasicBlockRef end_length_loop = LLVMAppendBasicBlock(llvm_function, "");

	LLVMValueRef j = LLVMBuildAlloca(state->llvm_builder, LLVMInt64Type(), "");
	LLVMBuildStore(state->llvm_builder, LLVMConstInt(LLVMInt64Type(), 0, false), j);

	LLVMBuildBr(state->llvm_builder, start_length_check);
	LLVMPositionBuilderAtEnd(state->llvm_builder, start_length_check);

	LLVMValueRef j_load = LLVMBuildLoad2(state->llvm_builder, LLVMInt64Type(), j, "");
	LLVMValueRef indices2[2] = {
		LLVMConstInt(LLVMInt64Type(), 0, false),
		j_load
	};
	LLVMValueRef length_cmp_result = LLVMBuildICmp(state->llvm_builder, LLVMIntNE, LLVMBuildLoad2(state->llvm_builder, LLVMInt8Type(), LLVMBuildGEP2(state->llvm_builder, LLVMArrayType2(LLVMInt8Type(), 0), raw_string_value, indices2, 2, ""), ""), LLVMConstInt(LLVMInt8Type(), 0, false), "");
	LLVMBuildCondBr(state->llvm_builder, length_cmp_result, start_length_loop, end_length_loop);
	LLVMPositionBuilderAtEnd(state->llvm_builder, start_length_loop);
	LLVMBuildStore(state->llvm_builder, LLVMBuildAdd(state->llvm_builder, LLVMBuildLoad2(state->llvm_builder, LLVMInt64Type(), j, ""), LLVMConstInt(LLVMInt64Type(), 1, false), ""), j);
	LLVMBuildBr(state->llvm_builder, start_length_check);
	LLVMPositionBuilderAtEnd(state->llvm_builder, end_length_loop);

	LLVMValueRef arg = LLVMBuildAlloca(state->llvm_builder, string_type, "");
	LLVMBuildStore(state->llvm_builder, LLVMBuildLoad2(state->llvm_builder, LLVMInt64Type(), j, ""), LLVMBuildStructGEP2(state->llvm_builder, string_type, arg, 0, ""));
	LLVMBuildStore(state->llvm_builder, raw_string_value, LLVMBuildStructGEP2(state->llvm_builder, string_type, arg, 1, ""));

	LLVMValueRef array_i_ptr = LLVMBuildGEP2(state->llvm_builder, LLVMArrayType2(string_type, 0), array, indices, 2, "");
	LLVMBuildStore(state->llvm_builder, LLVMBuildLoad2(state->llvm_builder, string_type, arg, ""), array_i_ptr);

	LLVMBuildStore(state->llvm_builder, LLVMBuildAdd(state->llvm_builder, LLVMBuildLoad2(state->llvm_builder, LLVMInt64Type(), i, ""), LLVMConstInt(LLVMInt64Type(), 1, false), ""), i);

	LLVMBuildBr(state->llvm_builder, start_check);
	LLVMPositionBuilderAtEnd(state->llvm_builder, end_loop);

	LLVMTypeRef args_view_type = create_llvm_type(create_array_view_type(create_array_view_type(create_value(BYTE_TYPE_VALUE))).value, state);
	LLVMValueRef args_view = LLVMBuildAlloca(state->llvm_builder, args_view_type, "");
	LLVMBuildStore(state->llvm_builder, LLVMBuildZExt(state->llvm_builder, argc, LLVMInt64Type(), ""), LLVMBuildStructGEP2(state->llvm_builder, args_view_type, args_view, 0, ""));
	LLVMBuildStore(state->llvm_builder, array, LLVMBuildStructGEP2(state->llvm_builder, args_view_type, args_view, 1, ""));
	LLVMValueRef arguments[1] = {
		LLVMBuildLoad2(state->llvm_builder, args_view_type, args_view, "")
	};
	LLVMBuildCall2(state->llvm_builder, LLVMGlobalGetValueType(state->main_function), state->main_function, arguments, 1, "");
	LLVMBuildRetVoid(state->llvm_builder);
}

typedef struct {
	LLVMContextRef context;
	LLVMModuleRef module;
	LLVMTargetMachineRef target_machine;
} LLVM_Data;

size_t size_llvm(Value_Data *value, void *data) {
	State state = { .llvm_target = ((LLVM_Data *) data)->target_machine };
	LLVMTypeRef llvm_type = create_llvm_type(value, &state);
	return LLVMABISizeOfType(LLVMCreateTargetDataLayout(state.llvm_target), llvm_type);
}

size_t c_size_llvm(C_Size_Fn_Input input) {
	switch (input) {
		case C_CHAR_SIZE:
			return 8;
		case C_SHORT_SIZE:
			return 16;
		case C_INT_SIZE:
			return 32;
		case C_LONG_SIZE:
			return 64;
	}
	return 0;
}

size_t alignment_llvm(Value_Data *value, void *data) {
	State state = { .llvm_target = ((LLVM_Data *) data)->target_machine };
	LLVMTypeRef llvm_type = create_llvm_type(value, &state);
	return LLVMABIAlignmentOfType(LLVMCreateTargetDataLayout(state.llvm_target), llvm_type);
}

void build_llvm(Context context, Node *root, void *data) {
	assert(root->kind == MODULE_NODE);

	LLVMModuleRef llvm_module = ((LLVM_Data *) data)->module;
    LLVMTargetMachineRef llvm_target_machine = ((LLVM_Data *) data)->target_machine;
    LLVMBuilderRef llvm_builder = LLVMCreateBuilder();

	State state = {
		.context = context,
		.llvm_module = llvm_module,
		.llvm_builder = llvm_builder,
		.llvm_target = llvm_target_machine,
		.generated_cache = NULL
	};

	generate_node(root->module.body, &state);
	generate_main(&state);

	LLVMPrintModuleToFile(llvm_module, "output.ll", NULL);

	char *error = NULL;
	LLVMVerifyModule(llvm_module, LLVMAbortProcessAction, &error);
	if (error != NULL) printf("%s", error);

	LLVMTargetMachineEmitToFile(llvm_target_machine, llvm_module, "output.o", LLVMObjectFile, NULL);
	// LLVMTargetMachineEmitToFile(llvm_target_machine, llvm_module, "output.s", LLVMAssemblyFile, NULL);

	char *args[] = {
		"gcc",
		"-no-pie",
		"output.o",
		"-o",
		"output",
		NULL
	};
	execvp(args[0], args);
}

Codegen llvm_codegen() {
    LLVMModuleRef llvm_module = LLVMModuleCreateWithName("main");

	LLVMInitializeAllTargetInfos();
	LLVMInitializeAllTargets();
	LLVMInitializeAllTargetMCs();
	LLVMInitializeAllAsmParsers();
	LLVMInitializeAllAsmPrinters();

	LLVMTargetRef target;
	LLVMGetTargetFromTriple(LLVMGetDefaultTargetTriple(), &target, NULL);
	LLVMTargetMachineRef target_machine = LLVMCreateTargetMachine(
    	target, LLVMGetDefaultTargetTriple(), "generic", "",
    	LLVMCodeGenLevelDefault, LLVMRelocDefault, LLVMCodeModelDefault
	);
	LLVMSetTarget(llvm_module, LLVMGetDefaultTargetTriple());

	LLVM_Data *data = malloc(sizeof(LLVM_Data));
	data->module = llvm_module;
	data->target_machine = target_machine;

	return (Codegen) {
		.size_fn = size_llvm,
		.c_size_fn = c_size_llvm,
		.alignment_fn = alignment_llvm,
		.build_fn = build_llvm,
		.default_integer_size = 64,
		.data = data
	};
}
