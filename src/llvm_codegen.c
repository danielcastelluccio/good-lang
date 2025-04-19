#include <assert.h>
#include <llvm-c/Types.h>
#include <stdbool.h>
#include <unistd.h>

#include <llvm-c/Core.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>

#include "common.h"
#include "stb/ds.h"

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
	LLVMModuleRef llvm_module;
	LLVMBuilderRef llvm_builder;
	LLVMTargetMachineRef llvm_target;
	Context context;
	struct { Value_Data *key; LLVMValueRef value; } *generated_cache; // stb_ds
	struct { Node_Data *key; LLVMValueRef value; } *variables; // stb_ds
	struct { Node_Data *key; Block_Codegen_Data value; } *blocks; // stb_ds
	struct { Node_Data *key; While_Codegen_Data value; } *whiles; // stb_ds
	LLVMValueRef *function_arguments; // stb_ds
	LLVMValueRef current_function;
	LLVMValueRef main_function;
} State;

static LLVMValueRef generate_node(Node *node, State *state);
static LLVMValueRef generate_value(Value_Data *value, State *state);

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
	assert(value != NULL);

	switch (value->tag) {
		case POINTER_TYPE_VALUE: {
			Pointer_Type_Value pointer = value->pointer_type;
			return LLVMPointerType(create_llvm_type(pointer.inner.value, state), 0);
		}
		case ARRAY_TYPE_VALUE: {
			Array_Type_Value array = value->array_type;
			return LLVMArrayType(create_llvm_type(array.inner.value, state), array.size.value != NULL ? array.size.value->integer.value : 0);
		}
		case FUNCTION_TYPE_VALUE: {
			return LLVMPointerType(create_llvm_function_literal_type(value, state), 0);
		}
		case INTERNAL_VALUE: {
			Internal_Value internal = value->internal;

			if (strcmp(internal.identifier, "byte") == 0) return LLVMInt8Type();
			else if (strcmp(internal.identifier, "void") == 0) return LLVMStructType(NULL, 0, false);
			else if (strcmp(internal.identifier, "uint") == 0) return LLVMInt64Type();
			else if (strcmp(internal.identifier, "flt") == 0) return LLVMDoubleType();
			else if (strcmp(internal.identifier, "bool") == 0) return LLVMInt1Type();
			else if (strcmp(internal.identifier, "str") == 0) {
				LLVMTypeRef *items = malloc(sizeof(LLVMTypeRef) * 2);
				items[0] = LLVMInt64Type();
				items[1] = LLVMPointerType(LLVMVoidType(), 0);
				return LLVMStructType(items, 2, false);
			}
			else assert(false);
			break;
		}
		case STRUCT_TYPE_VALUE: {
			Struct_Type_Value struct_type = value->struct_type;

			LLVMTypeRef *items = NULL;
			for (long int i = 0; i < arrlen(struct_type.items); i++) {
				arrpush(items, create_llvm_type(struct_type.items[i].type.value, state));
			}

			return LLVMStructType(items, arrlen(items), false);
		}
		case UNION_TYPE_VALUE: {
			Union_Type_Value union_type = value->union_type;

			size_t max_size = 0;
			for (long int i = 0; i < arrlen(union_type.items); i++) {
				size_t size = LLVMStoreSizeOfType(LLVMCreateTargetDataLayout(state->llvm_target), create_llvm_type(union_type.items[i].type.value, state));
				if (size > max_size) max_size = size;
			}

			return LLVMArrayType(LLVMInt8Type(), max_size);
		}
		case ENUM_TYPE_VALUE: {
			return LLVMInt64Type();
		}
		// case DEFINE_DATA_VALUE: {
		// 	Define_Data_Value define_data = value->define_data;
		// 	return create_llvm_type(define_data.value, state);
		// }
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

	Value value = data->define.typed_value.value;
	if (value.value == NULL) {
		return;
	}

	LLVMValueRef llvm_value = generate_value(value.value, state);
	if (strcmp(define.identifier, "main") == 0) {
		state->main_function = llvm_value;
	}
}

static LLVMValueRef generate_block(Node *node, State *state) {
	assert(node->kind == BLOCK_NODE);
	Block_Node block = node->block;
	Node_Data *data = get_data(&state->context, node);
	Block_Data block_data = data->block;

	LLVMBasicBlockRef llvm_block = LLVMAppendBasicBlock(state->current_function, "");
	LLVMValueRef value = NULL;
	if (block_data.type.value != NULL) {
		value = LLVMBuildAlloca(state->llvm_builder, create_llvm_type(block_data.type.value, state), "");
	}

	Block_Codegen_Data block_codegen_data = {
		.block = llvm_block,
		.value = value
	};
	hmput(state->blocks, data, block_codegen_data);
	for (long int i = 0; i < arrlen(block.statements); i++) {
		generate_node(block.statements[i], state);
	}
	LLVMBuildBr(state->llvm_builder, llvm_block);
	LLVMPositionBuilderAtEnd(state->llvm_builder, llvm_block);

	if (block_data.type.value != NULL) {
		return LLVMBuildLoad2(state->llvm_builder, create_llvm_type(block_data.type.value, state), value, "");
	}
	return NULL;
}

static LLVMValueRef generate_call_generic(LLVMValueRef function_llvm_value, Value_Data *function_type, Node **arguments, State *state) {
	LLVMValueRef *llvm_arguments = NULL;
	long int j = 0;
	for (long int i = 0; i < arrlen(arguments); i++) {
		while (function_type->function_type.arguments[j].inferred) j++;
		if (j >= arrlen(function_type->function_type.arguments) || !function_type->function_type.arguments[j].static_) {
			arrpush(llvm_arguments, generate_node(arguments[i], state));
		}
	}

	return LLVMBuildCall2(state->llvm_builder, create_llvm_function_literal_type(function_type, state), function_llvm_value, llvm_arguments, arrlen(llvm_arguments), "");
}

static LLVMValueRef generate_call(Node *node, State *state) {
	assert(node->kind == CALL_NODE);
	Call_Node call = node->call;

	Call_Data call_data = get_data(&state->context, node)->call;
	Value function_type = call_data.function_type;

	if (call_data.function_value.value != NULL) {
		return generate_call_generic(generate_value(call_data.function_value.value, state), function_type.value, call.arguments, state);
	} else {
		return generate_call_generic(generate_node(call.function, state), function_type.value, call.arguments, state);
	}
}

static LLVMValueRef generate_call_method(Node *node, State *state) {
	assert(node->kind == CALL_METHOD_NODE);
	Call_Method_Data call_method_data = get_data(&state->context, node)->call_method;

	Value_Data *function_value = call_method_data.custom_operator_function.function;
	return generate_call_generic(generate_value(function_value, state), call_method_data.custom_operator_function.function_type.value, call_method_data.arguments, state);
}

static LLVMValueRef generate_identifier(Node *node, State *state) {
	assert(node->kind == IDENTIFIER_NODE);

	Node_Data *identifier_data = get_data(&state->context, node);
	switch (identifier_data->identifier.kind) {
		case IDENTIFIER_VARIABLE: {
			Node *variable = get_data(&state->context, node)->identifier.variable_definition;
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
		
		case IDENTIFIER_VALUE: {
			return generate_value(identifier_data->identifier.value.value, state);
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

	if (string_data.type.value->tag == INTERNAL_VALUE && strcmp(string_data.type.value->internal.identifier, "str") == 0) {
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

static LLVMValueRef generate_struct(Node *node, State *state) {
	assert(node->kind == STRUCT_NODE);
	Structure_Node structure = node->structure;

	Value_Data *type = get_data(&state->context, node)->structure.type.value;

	switch (type->tag) {
		case STRUCT_TYPE_VALUE: {
			LLVMValueRef structure_value = LLVMBuildAlloca(state->llvm_builder, create_llvm_type(type, state), "");
			for (long int i = 0; i < arrlen(type->struct_type.items); i++) {
				LLVMValueRef item_pointer = LLVMBuildStructGEP2(state->llvm_builder, create_llvm_type(type, state), structure_value, i, "");
				LLVMBuildStore(state->llvm_builder, generate_node(structure.values[i], state), item_pointer);
			}

			return LLVMBuildLoad2(state->llvm_builder, create_llvm_type(type, state), structure_value, "");
		}
		case ARRAY_TYPE_VALUE: {
			LLVMValueRef array_value = LLVMBuildAlloca(state->llvm_builder, create_llvm_type(type, state), "");
			for (long int i = 0; i < type->array_type.size.value->integer.value; i++) {
				LLVMValueRef indices[2] = {
					LLVMConstInt(LLVMInt64Type(), 0, false),
					LLVMConstInt(LLVMInt64Type(), i, false)
				};

				LLVMValueRef item_pointer = LLVMBuildGEP2(state->llvm_builder, create_llvm_type(type, state), array_value, indices, 2, "");
				LLVMBuildStore(state->llvm_builder, generate_node(structure.values[i], state), item_pointer);
			}

			return LLVMBuildLoad2(state->llvm_builder, create_llvm_type(type, state), array_value, "");
		}
		default:
			assert(false);
	}
}

static LLVMValueRef generate_run(Node *node, State *state) {
	assert(node->kind == RUN_NODE);
	Value value = get_data(&state->context, node)->run.value;
	return generate_value(value.value, state);
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

static LLVMValueRef generate_structure_access(Node *node, State *state) {
	assert(node->kind == STRUCTURE_ACCESS_NODE);
	Structure_Access_Node structure_access = node->structure_access;

	Structure_Access_Data data = get_data(&state->context, node)->structure_access;
	Value_Data *structure_type = data.structure_value.value;

	unsigned int index = 0;
	Value_Data *type = NULL;
	for (long int i = 0; i < arrlen(structure_type->struct_type.items); i++) {
		if (strcmp(structure_access.item, structure_type->struct_type.items[i].identifier) == 0) {
			index = i;
			type = structure_type->struct_type.items[i].type.value;
			break;
		}
	}

	LLVMValueRef structure_llvm_value = generate_node(structure_access.structure, state);
	LLVMValueRef element_pointer = NULL;
	if (structure_type->tag == STRUCT_TYPE_VALUE) {
		element_pointer = LLVMBuildStructGEP2(state->llvm_builder, create_llvm_type(structure_type, state), structure_llvm_value, index, "");
	} else {
		element_pointer = LLVMBuildBitCast(state->llvm_builder, structure_llvm_value, LLVMPointerType(create_llvm_type(type, state), 0), "");
	}

	if (data.assign_value != NULL) {
		LLVMBuildStore(state->llvm_builder, generate_node(data.assign_value, state), element_pointer);
		return NULL;
	} else {
		if (data.want_pointer) {
			return element_pointer;
		} else  {
			return LLVMBuildLoad2(state->llvm_builder, create_llvm_type(type, state), element_pointer, "");
		}
	}
}

static LLVMValueRef generate_array_access(Node *node, State *state) {
	assert(node->kind == ARRAY_ACCESS_NODE);
	Array_Access_Node array_access = node->array_access;

	Array_Access_Data array_access_data = get_data(&state->context, node)->array_access;

	Value_Data *array_type = array_access_data.array_type.value;

	LLVMValueRef array_llvm_value = generate_node(array_access.array, state);
	LLVMValueRef element_pointer = NULL;

	if (array_access_data.custom_operator_function.function != NULL) {
		Node **arguments = NULL;
		arrpush(arguments, array_access.array);
		arrpush(arguments, array_access.index);

		Value_Data *function_value = array_access_data.custom_operator_function.function;
		element_pointer = generate_call_generic(generate_value(function_value, state), array_access_data.custom_operator_function.function_type.value, arguments, state);
	} else {
		LLVMValueRef indices[2] = {
			LLVMConstInt(LLVMInt64Type(), 0, false),
			generate_node(array_access.index, state)
		};
		element_pointer = LLVMBuildGEP2(state->llvm_builder, create_llvm_type(array_type->pointer_type.inner.value, state), array_llvm_value, indices, 2, "");
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
	if (type->tag == INTERNAL_VALUE && strcmp(type->internal.identifier, "flt") == 0) return true;
	return false;
}

static LLVMValueRef values_equal(Value_Data *type, LLVMValueRef value1, LLVMValueRef value2, State *state) {
	switch (type->tag) {
		case INTERNAL_VALUE: {
			char *internal = type->internal.identifier;
			if (strcmp(internal, "uint") == 0 || strcmp(internal, "byte") == 0) {
				return LLVMBuildICmp(state->llvm_builder, LLVMIntEQ, value1, value2, "");
			}
			assert(false);
			return NULL;
		}
		default:
			assert(false);
	}
}

static LLVMValueRef generate_binary_operator(Node *node, State *state) {
	assert(node->kind == BINARY_OPERATOR_NODE);
	Binary_Operator_Node binary_operator = node->binary_operator;
	Binary_Operator_Data binary_operator_data = get_data(&state->context, node)->binary_operator;

	LLVMValueRef left_value = generate_node(binary_operator.left, state);
	LLVMValueRef right_value = generate_node(binary_operator.right, state);

	switch (binary_operator.operator) {
		case OPERATOR_EQUALS:
			return values_equal(binary_operator_data.type.value, left_value, right_value, state);
		case OPERATOR_LESS:
			if (is_type_signed(binary_operator_data.type.value)) {
				return LLVMBuildICmp(state->llvm_builder, LLVMIntSLT, left_value, right_value, "");
			} else {
				return LLVMBuildICmp(state->llvm_builder, LLVMIntULT, left_value, right_value, "");
			}
		case OPERATOR_LESS_EQUALS:
			if (is_type_signed(binary_operator_data.type.value)) {
				return LLVMBuildICmp(state->llvm_builder, LLVMIntSLE, left_value, right_value, "");
			} else {
				return LLVMBuildICmp(state->llvm_builder, LLVMIntULE, left_value, right_value, "");
			}
		case OPERATOR_GREATER:
			if (is_type_signed(binary_operator_data.type.value)) {
				return LLVMBuildICmp(state->llvm_builder, LLVMIntSGT, left_value, right_value, "");
			} else {
				return LLVMBuildICmp(state->llvm_builder, LLVMIntUGT, left_value, right_value, "");
			}
		case OPERATOR_GREATER_EQUALS:
			if (is_type_signed(binary_operator_data.type.value)) {
				return LLVMBuildICmp(state->llvm_builder, LLVMIntSGE, left_value, right_value, "");
			} else {
				return LLVMBuildICmp(state->llvm_builder, LLVMIntUGE, left_value, right_value, "");
			}
		case OPERATOR_ADD:
			if (is_type_float(binary_operator_data.type.value)) {
				return LLVMBuildFAdd(state->llvm_builder, left_value, right_value, "");
			} else {
				return LLVMBuildAdd(state->llvm_builder, left_value, right_value, "");
			}
		case OPERATOR_SUBTRACT:
			if (is_type_float(binary_operator_data.type.value)) {
				return LLVMBuildFSub(state->llvm_builder, left_value, right_value, "");
			} else {
				return LLVMBuildSub(state->llvm_builder, left_value, right_value, "");
			}
		case OPERATOR_MULTIPLY:
			if (is_type_float(binary_operator_data.type.value)) {
				return LLVMBuildFMul(state->llvm_builder, left_value, right_value, "");
			} else {
				return LLVMBuildMul(state->llvm_builder, left_value, right_value, "");
			}
		case OPERATOR_DIVIDE:
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

// static LLVMValueRef generate_return(Node *node, State *state) {
// 	assert(node->kind == RETURN_NODE);
// 	Return_Node return_ = node->return_;
//
// 	if (return_.value != NULL) {
// 		LLVMBuildRet(state->llvm_builder, generate_node(return_.value, state));
// 	} else {
// 		LLVMBuildRetVoid(state->llvm_builder);
// 	}
// 	return NULL;
// }

static LLVMValueRef generate_assign(Node *node, State *state) {
	assert(node->kind == ASSIGN_NODE);
	Assign_Node assign = node->assign;

	return generate_node(assign.container, state);
}

static LLVMValueRef generate_variable(Node *node, State *state) {
	assert(node->kind == VARIABLE_NODE);
	Variable_Node variable = node->variable;

	LLVMValueRef allocated_variable_llvm = LLVMBuildAlloca(state->llvm_builder, create_llvm_type(get_data(&state->context, node)->variable.type.value, state), "");
	if (variable.value != NULL) {
		LLVMValueRef llvm_value = generate_node(variable.value, state);
		LLVMBuildStore(state->llvm_builder, llvm_value, allocated_variable_llvm);
	}

	Node_Data *node_data = get_data(&state->context, node);

	hmput(state->variables, node_data, allocated_variable_llvm);

	return NULL;
}

static LLVMValueRef generate_yield(Node *node, State *state) {
	assert(node->kind == YIELD_NODE);
	Yield_Node yield = node->yield;
	Yield_Data yield_data = get_data(&state->context, node)->yield;
	Node_Data *block_data = get_data(&state->context, yield_data.block);
	Block_Codegen_Data block_codegen_data = hmget(state->blocks, block_data);
	LLVMBuildStore(state->llvm_builder, generate_node(yield.value, state), block_codegen_data.value);
	LLVMBuildBr(state->llvm_builder, block_codegen_data.block);
	LLVMBasicBlockRef block = LLVMAppendBasicBlock(state->current_function, "");
	LLVMPositionBuilderAtEnd(state->llvm_builder, block);

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
	If_Data if_data = get_data(&state->context, node)->if_;

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
		if (if_data.type.value != NULL) {
			value = LLVMBuildAlloca(state->llvm_builder, create_llvm_type(if_data.type.value, state), "");
		}

		LLVMValueRef condition = generate_node(if_.condition, state);

		LLVMBasicBlockRef if_block = LLVMAppendBasicBlock(state->current_function, "");
		LLVMBasicBlockRef else_block = LLVMAppendBasicBlock(state->current_function, "");
		LLVMBasicBlockRef done_block = LLVMAppendBasicBlock(state->current_function, "");
		LLVMBuildCondBr(state->llvm_builder, condition, if_block, else_block);
		LLVMPositionBuilderAtEnd(state->llvm_builder, if_block);
		LLVMValueRef if_value = generate_node(if_.if_body, state);
		if (if_data.type.value != NULL) {
			LLVMBuildStore(state->llvm_builder, if_value, value);
		}
		LLVMBuildBr(state->llvm_builder, done_block);
		LLVMPositionBuilderAtEnd(state->llvm_builder, else_block);
		if (if_.else_body != NULL) {
			LLVMValueRef else_value = generate_node(if_.else_body, state);
			if (if_data.type.value != NULL) {
				LLVMBuildStore(state->llvm_builder, else_value, value);
			}
		}
		LLVMBuildBr(state->llvm_builder, done_block);
		LLVMPositionBuilderAtEnd(state->llvm_builder, done_block);

		if (if_data.type.value != NULL) {
			return LLVMBuildLoad2(state->llvm_builder, create_llvm_type(if_data.type.value, state), value, "");
		} else {
			return NULL;
		}
	}
}

static LLVMValueRef generate_switch(Node *node, State *state) {
	assert(node->kind == SWITCH_NODE);
	Switch_Node switch_ = node->switch_;
	Switch_Data switch_data = get_data(&state->context, node)->switch_;

	LLVMValueRef value = NULL;
	if (switch_data.type.value != NULL) {
		value = LLVMBuildAlloca(state->llvm_builder, create_llvm_type(switch_data.type.value, state), "");
	}

	LLVMValueRef switched_value = generate_node(switch_.value, state);

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

		if (switch_case.check != NULL) {
			LLVMAddCase(switch_llvm_value, generate_node(switch_case.check, state), case_block);
			LLVMBuildBr(state->llvm_builder, done_block);
		} else {
			LLVMBuildBr(state->llvm_builder, done_block);
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
		case NUMBER_NODE:
			return generate_number(node, state);
		case NULL_NODE:
			return generate_null(node, state);
		case BOOLEAN_NODE:
			return generate_boolean(node, state);
		case STRUCT_NODE:
			return generate_struct(node, state);
		case RUN_NODE:
			return generate_run(node, state);
		case REFERENCE_NODE:
			return generate_reference(node, state);
		case DEREFERENCE_NODE:
			return generate_dereference(node, state);
		case STRUCTURE_ACCESS_NODE:
			return generate_structure_access(node, state);
		case ARRAY_ACCESS_NODE:
			return generate_array_access(node, state);
		case BINARY_OPERATOR_NODE:
			return generate_binary_operator(node, state);
		// case RETURN_NODE:
		// 	return generate_return(node, state);
		case ASSIGN_NODE:
			return generate_assign(node, state);
		case VARIABLE_NODE:
			return generate_variable(node, state);
		case YIELD_NODE:
			return generate_yield(node, state);
		case BREAK_NODE:
			return generate_break(node, state);
		case IF_NODE:
			return generate_if(node, state);
		case SWITCH_NODE:
			return generate_switch(node, state);
		case WHILE_NODE:
			return generate_while(node, state);
		default:
			assert(false);
	}
}

static LLVMValueRef generate_function(Value_Data *value, State *state) {
	assert(value->tag == FUNCTION_VALUE);
	Function_Value function = value->function;

	size_t saved_static_argument_id = state->context.static_argument_id;
	state->context.static_argument_id = function.static_argument_id;

	if (function.compile_only || function.type->function_type.incomplete) {
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

		if (function.type->function_type.return_type.value != NULL) {
			LLVMBuildRet(state->llvm_builder, value);
		} else {
			LLVMBuildRetVoid(state->llvm_builder);
		}
	}

	state->function_arguments = saved_function_arguments;
	state->current_function = saved_current_function;
	state->context.static_argument_id = saved_static_argument_id;
	state->llvm_builder = saved_llvm_builder;

	if (function.extern_name != NULL) {
		LLVMSetValueName(llvm_function, function.extern_name);
	}

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

static LLVMValueRef generate_enum(Value_Data *value, State *state) {
	(void) state;
	assert(value->tag == ENUM_VALUE);
	Enum_Value enum_ = value->enum_;

	return LLVMConstInt(LLVMInt64Type(), enum_.value, false);
}

static LLVMValueRef generate_value(Value_Data *value, State *state) {
	LLVMValueRef cached_result = hmget(state->generated_cache, value);
	if (cached_result != NULL) {
		return cached_result;
	}

	LLVMValueRef result = NULL;
	switch (value->tag) {
		case FUNCTION_VALUE:
			result = generate_function(value, state);
			break;
		case MODULE_VALUE:
			generate_module(value, state);
			break;
		case INTEGER_VALUE:
			result = generate_integer(value, state);
			break;
		case ENUM_VALUE:
			result = generate_enum(value, state);
			break;
		case STRUCT_TYPE_VALUE:
			break;
		case UNION_TYPE_VALUE:
			break;
		case ENUM_TYPE_VALUE:
			break;
		default:
			assert(false);
	}

	hmput(state->generated_cache, value, result);
	return result;
}

static void generate_main(State *state) {
	LLVMValueRef llvm_function = LLVMAddFunction(state->llvm_module, "", LLVMFunctionType(LLVMVoidType(), NULL, 0, false));

	LLVMBasicBlockRef entry = LLVMAppendBasicBlock(llvm_function, "");
	LLVMPositionBuilderAtEnd(state->llvm_builder, entry);

	LLVMBuildCall2(state->llvm_builder, LLVMGlobalGetValueType(state->main_function), state->main_function, NULL, 0, "");
	LLVMBuildRetVoid(state->llvm_builder);

	LLVMSetValueName(llvm_function, "main");
}

typedef struct {
	LLVMContextRef context;
	LLVMModuleRef module;
	LLVMTargetMachineRef target_machine;
} LLVM_Data;

size_t size_llvm(Value_Data *value, void *data) {
	State state = { .llvm_target = ((LLVM_Data *) data)->target_machine };
	LLVMTypeRef llvm_type = create_llvm_type(value, &state);
	return LLVMStoreSizeOfType(LLVMCreateTargetDataLayout(state.llvm_target), llvm_type);
}

void build_llvm(Context context, Node *root, void *data) {
	assert(root->kind == MODULE_NODE);

	LLVMContextRef llvm_context = ((LLVM_Data *) data)->context;
	LLVMModuleRef llvm_module = ((LLVM_Data *) data)->module;
    LLVMTargetMachineRef llvm_target_machine = ((LLVM_Data *) data)->target_machine;
    LLVMBuilderRef llvm_builder = LLVMCreateBuilderInContext(llvm_context);

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
	LLVMTargetMachineEmitToFile(llvm_target_machine, llvm_module, "output.o", LLVMObjectFile, NULL);
	LLVMTargetMachineEmitToFile(llvm_target_machine, llvm_module, "output.s", LLVMAssemblyFile, NULL);

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
	LLVMContextRef llvm_context = LLVMContextCreate();
    LLVMModuleRef llvm_module = LLVMModuleCreateWithNameInContext("main", llvm_context);

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
	data->context = llvm_context;
	data->module = llvm_module;
	data->target_machine = target_machine;

	return (Codegen) {
		.size_fn = size_llvm,
		.build_fn = build_llvm,
		.data = data
	};
}
