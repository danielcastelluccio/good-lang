#include <assert.h>
#include <stdbool.h>
#include <unistd.h>

#include <llvm-c/Core.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>

#include "stb/ds.h"

#include "llvm_codegen.h"

typedef struct {
	LLVMValueRef llvm_value;
	Value *type;
} Value_Type_Pair;

typedef struct {
	LLVMModuleRef llvm_module;
	LLVMBuilderRef llvm_builder;
	Context context;
	struct { Value *key; LLVMValueRef value; } *generated_cache; // stb_ds
	struct { Node_Data *key; LLVMValueRef value; } *variables; // stb_ds
	LLVMValueRef *function_arguments; // stb_ds
	LLVMValueRef current_function;
	LLVMValueRef main_function;
} State;

static LLVMValueRef generate_node(Node *node, State *state);
static LLVMValueRef generate_value(Value *value, State *state);

static LLVMTypeRef create_llvm_type(Value *node);

static LLVMTypeRef create_llvm_function_literal_type(Value *value) {
	assert(value->tag == FUNCTION_TYPE_VALUE);
	Function_Type_Value function_type = value->function_type;

	LLVMTypeRef *arguments = NULL;
	for (long int i = 0; i < arrlen(function_type.arguments); i++) {
		arrpush(arguments, create_llvm_type(function_type.arguments[i].type));
	}

	LLVMTypeRef return_type = create_llvm_type(function_type.return_type);

	return LLVMFunctionType(return_type, arguments, arrlen(arguments), function_type.variadic);
}

static LLVMTypeRef create_llvm_type(Value *value) {
	assert(value != NULL);

	switch (value->tag) {
		case POINTER_TYPE_VALUE: {
			Pointer_Type_Value pointer = value->pointer_type;
			return LLVMPointerType(create_llvm_type(pointer.inner), 0);
		}
		case ARRAY_TYPE_VALUE: {
			Array_Type_Value array = value->array_type;
			return LLVMArrayType(create_llvm_type(array.inner), array.size != NULL ? array.size->integer.value : 0);
		}
		case SLICE_TYPE_VALUE: {
			Slice_Type_Value slice = value->slice_type;

			LLVMTypeRef *items = NULL;
			arrpush(items, LLVMInt64Type());
			arrpush(items, LLVMPointerType(LLVMArrayType(create_llvm_type(slice.inner), 0), 0));

			return LLVMStructType(items, arrlen(items), false);
		}
		case FUNCTION_TYPE_VALUE: {
			return LLVMPointerType(create_llvm_function_literal_type(value), 0);
		}
		case INTERNAL_VALUE: {
			Internal_Value internal = value->internal;

			if (strcmp(internal.identifier, "byte") == 0) return LLVMInt8Type();
			else if (strcmp(internal.identifier, "void") == 0) return LLVMVoidType();
			else if (strcmp(internal.identifier, "uint") == 0) return LLVMInt64Type();
			else assert(false);
			break;
		}
		case STRUCTURE_TYPE_VALUE: {
			Structure_Type_Value structure_type = value->structure_type;

			LLVMTypeRef *items = NULL;
			for (long int i = 0; i < arrlen(structure_type.items); i++) {
				arrpush(items, create_llvm_type(structure_type.items[i].type));
			}

			return LLVMStructType(items, arrlen(items), false);
		}
		case DEFINE_DATA_VALUE: {
			Define_Data_Value define_data = value->define_data;
			return create_llvm_type(define_data.value);
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

	if (data->define.kind != DEFINE_SINGLE) {
		return;
	}

	Value *value = data->define.value.binding;
	if (value == NULL) {
		return;
	}
	value = strip_define_data(value);

	LLVMValueRef llvm_value = generate_value(value, state);
	if (strcmp(define.identifier, "main") == 0) {
		state->main_function = llvm_value;
	}
}

static LLVMValueRef generate_block(Node *node, State *state) {
	assert(node->kind == BLOCK_NODE);
	Block_Node block = node->block;

	for (long int i = 0; i < arrlen(block.statements); i++) {
		generate_node(block.statements[i], state);
	}

	return NULL;
}

static LLVMValueRef generate_call(Node *node, State *state) {
	assert(node->kind == CALL_NODE);
	Call_Node call = node->call;

	LLVMValueRef function_llvm_value = generate_node(call.function, state);

	LLVMValueRef *arguments = NULL;
	for (unsigned int i = 0; i < arrlen(call.arguments); i++) {
		arrpush(arguments, generate_node(call.arguments[i], state));
	}

	Value *function_type = get_data(&state->context, node)->call.function_type;
	return LLVMBuildCall2(state->llvm_builder, create_llvm_function_literal_type(function_type), function_llvm_value, arguments, arrlen(arguments), "");
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
					return LLVMBuildLoad2(state->llvm_builder, create_llvm_type(identifier_data->identifier.type), variable_llvm_value, "");
				}
			}
		}
		case IDENTIFIER_ARGUMENT: {
			size_t argument = get_data(&state->context, node)->identifier.argument_index;
			LLVMValueRef value_pointer = state->function_arguments[argument];
			if (identifier_data->identifier.want_pointer) {
				return value_pointer;
			} else {
				return LLVMBuildLoad2(state->llvm_builder, create_llvm_type(identifier_data->identifier.type), value_pointer, "");
			}
		}
		case IDENTIFIER_VALUE: {
			return generate_value(strip_define_data(identifier_data->identifier.value), state);
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

	if (string_data.type->tag == SLICE_TYPE_VALUE) {
		LLVMValueRef slice_value = LLVMBuildAlloca(state->llvm_builder, create_llvm_type(string_data.type), "");

		LLVMValueRef length_pointer = LLVMBuildStructGEP2(state->llvm_builder, create_llvm_type(string_data.type), slice_value, 0, "");
		LLVMBuildStore(state->llvm_builder, LLVMConstInt(LLVMInt64Type(), string_data.length, false), length_pointer);

		LLVMValueRef pointer_pointer = LLVMBuildStructGEP2(state->llvm_builder, create_llvm_type(string_data.type), slice_value, 1, "");
		LLVMBuildStore(state->llvm_builder, pointer_llvm_value, pointer_pointer);

		return LLVMBuildLoad2(state->llvm_builder, create_llvm_type(string_data.type), slice_value, "");
	} else {
		return pointer_llvm_value;
	}
}

static LLVMValueRef generate_number(Node *node, State *state) {
	assert(node->kind == NUMBER_NODE);
	Number_Node number = node->number;
	Value *type = get_data(&state->context, node)->number.type;
	return LLVMConstInt(create_llvm_type(type), number.integer, false);
}

static LLVMValueRef generate_null(Node *node, State *state) {
	assert(node->kind == NULL_NODE);
	Value *type = get_data(&state->context, node)->null_.type;
	return LLVMConstNull(create_llvm_type(type));
}

static LLVMValueRef generate_structure(Node *node, State *state) {
	assert(node->kind == STRUCTURE_NODE);
	Structure_Node structure = node->structure;

	Value *type = get_data(&state->context, node)->structure.type;
	type = strip_define_data(type);

	switch (type->tag) {
		case STRUCTURE_TYPE_VALUE: {
			LLVMValueRef structure_value = LLVMBuildAlloca(state->llvm_builder, create_llvm_type(type), "");
			for (long int i = 0; i < arrlen(type->structure_type.items); i++) {
				LLVMValueRef item_pointer = LLVMBuildStructGEP2(state->llvm_builder, create_llvm_type(type), structure_value, i, "");
				LLVMBuildStore(state->llvm_builder, generate_node(structure.values[i], state), item_pointer);
			}

			return LLVMBuildLoad2(state->llvm_builder, create_llvm_type(type), structure_value, "");
		}
		case ARRAY_TYPE_VALUE: {
			LLVMValueRef array_value = LLVMBuildAlloca(state->llvm_builder, create_llvm_type(type), "");
			for (long int i = 0; i < type->array_type.size->integer.value; i++) {
				LLVMValueRef indices[2] = {
					LLVMConstInt(LLVMInt64Type(), 0, false),
					LLVMConstInt(LLVMInt64Type(), i, false)
				};

				LLVMValueRef item_pointer = LLVMBuildGEP2(state->llvm_builder, create_llvm_type(type), array_value, indices, 2, "");
				LLVMBuildStore(state->llvm_builder, generate_node(structure.values[i], state), item_pointer);
			}

			return LLVMBuildLoad2(state->llvm_builder, create_llvm_type(type), array_value, "");
		}
		default:
			assert(false);
	}
}

static LLVMValueRef generate_run(Node *node, State *state) {
	assert(node->kind == RUN_NODE);
	Value *value = get_data(&state->context, node)->run.value;
	return generate_value(value, state);
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
		return LLVMBuildLoad2(state->llvm_builder, create_llvm_type(dereference_data.type), pointer_llvm_value, "");
	}
}

static LLVMValueRef generate_structure_access(Node *node, State *state) {
	assert(node->kind == STRUCTURE_ACCESS_NODE);
	Structure_Access_Node structure_access = node->structure_access;

	Structure_Access_Data data = get_data(&state->context, node)->structure_access;
	Value *structure_type = data.structure_value;
	structure_type = strip_define_data(structure_type);

	unsigned int index = 0;
	Value *type = NULL;
	for (long int i = 0; i < arrlen(structure_type->structure_type.items); i++) {
		if (strcmp(structure_access.item, structure_type->structure_type.items[i].identifier) == 0) {
			index = i;
			type = structure_type->structure_type.items[i].type;
			break;
		}
	}

	LLVMValueRef structure_llvm_value = generate_node(structure_access.structure, state);
	LLVMValueRef element_pointer = LLVMBuildStructGEP2(state->llvm_builder, create_llvm_type(structure_type), structure_llvm_value, index, "");

	if (data.assign_value != NULL) {
		LLVMBuildStore(state->llvm_builder, generate_node(data.assign_value, state), element_pointer);
		return NULL;
	} else {
		if (data.want_pointer) {
			return element_pointer;
		} else  {
			return LLVMBuildLoad2(state->llvm_builder, create_llvm_type(type), element_pointer, "");
		}
	}
}

static LLVMValueRef generate_array_access(Node *node, State *state) {
	assert(node->kind == ARRAY_ACCESS_NODE);
	Array_Access_Node array_access = node->array_access;

	Array_Access_Data array_access_data = get_data(&state->context, node)->array_access;
	Value *array_like_type = array_access_data.array_like_type;
	strip_define_data(array_like_type);

	Value *type = NULL;

	LLVMValueRef array_llvm_value = generate_node(array_access.array, state);
	LLVMValueRef element_pointer = NULL;

	LLVMValueRef indices[2] = {
		LLVMConstInt(LLVMInt64Type(), 0, false),
		generate_node(array_access.index, state)
	};
	if (array_like_type->pointer_type.inner->tag == ARRAY_TYPE_VALUE) {
		type = array_like_type->pointer_type.inner->array_type.inner;
		element_pointer = LLVMBuildGEP2(state->llvm_builder, create_llvm_type(array_like_type->pointer_type.inner), array_llvm_value, indices, 2, "");
	} else {
		type = array_like_type->pointer_type.inner->slice_type.inner;
		LLVMValueRef pointer_pointer = LLVMBuildStructGEP2(state->llvm_builder, create_llvm_type(array_like_type->pointer_type.inner), array_llvm_value, 1, "");
		LLVMValueRef pointer_value = LLVMBuildLoad2(state->llvm_builder, LLVMPointerType(LLVMArrayType(create_llvm_type(type), 0), 0), pointer_pointer, "");
		element_pointer = LLVMBuildGEP2(state->llvm_builder, LLVMArrayType(create_llvm_type(type), 0), pointer_value, indices, 2, "");
	}

	if (array_access_data.assign_value != NULL) {
		LLVMBuildStore(state->llvm_builder, generate_node(array_access_data.assign_value, state), element_pointer);
		return NULL;
	} else {
		if (array_access_data.want_pointer) {
			return element_pointer;
		} else  {
			return LLVMBuildLoad2(state->llvm_builder, create_llvm_type(type), element_pointer, "");
		}
	}
}

static LLVMValueRef generate_slice(Node *node, State *state) {
	assert(node->kind == SLICE_NODE);
	Slice_Node slice = node->slice;

	Slice_Data slice_data = get_data(&state->context, node)->slice;
	Value *array_like_type = slice_data.array_like_type;
	strip_define_data(array_like_type);

	Value *type = NULL;

	LLVMValueRef array_llvm_value = generate_node(slice.array, state);
	LLVMValueRef slice_pointer = NULL;

	LLVMValueRef start = generate_node(slice.start_index, state);
	LLVMValueRef end = generate_node(slice.end_index, state);

	LLVMValueRef indices[2] = {
		LLVMConstInt(LLVMInt64Type(), 0, false),
		start
	};
	if (array_like_type->pointer_type.inner->tag == ARRAY_TYPE_VALUE) {
		type = array_like_type->pointer_type.inner->array_type.inner;
		// element_pointer = LLVMBuildGEP2(state->llvm_builder, create_llvm_type(array_like_type->pointer_type.inner), array_llvm_value, indices, 2, "");
		slice_pointer = LLVMBuildGEP2(state->llvm_builder, create_llvm_type(array_like_type->pointer_type.inner), array_llvm_value, indices, 2, "");
	} else {
		type = array_like_type->pointer_type.inner->slice_type.inner;
		LLVMValueRef pointer_pointer = LLVMBuildStructGEP2(state->llvm_builder, create_llvm_type(array_like_type->pointer_type.inner), array_llvm_value, 1, "");
		LLVMValueRef pointer_value = LLVMBuildLoad2(state->llvm_builder, LLVMPointerType(LLVMArrayType(create_llvm_type(type), 0), 0), pointer_pointer, "");
		slice_pointer = LLVMBuildGEP2(state->llvm_builder, LLVMArrayType(create_llvm_type(type), 0), pointer_value, indices, 2, "");
	}

	Value *slice_type = create_slice_type(type);

	LLVMValueRef slice_length = LLVMBuildSub(state->llvm_builder, end, start, "");

	LLVMValueRef slice_value = LLVMBuildAlloca(state->llvm_builder, create_llvm_type(slice_type), "");

	LLVMValueRef length_pointer = LLVMBuildStructGEP2(state->llvm_builder, create_llvm_type(slice_type), slice_value, 0, "");
	LLVMBuildStore(state->llvm_builder, slice_length, length_pointer);

	LLVMValueRef pointer_pointer = LLVMBuildStructGEP2(state->llvm_builder, create_llvm_type(slice_type), slice_value, 1, "");
	LLVMBuildStore(state->llvm_builder, slice_pointer, pointer_pointer);

	return LLVMBuildLoad2(state->llvm_builder, create_llvm_type(slice_type), slice_value, "");
}

static bool is_type_signed(Value *type) {
	(void) type;
	return false;
}

static LLVMValueRef generate_binary_operator(Node *node, State *state) {
	assert(node->kind == BINARY_OPERATOR_NODE);
	Binary_Operator_Node binary_operator = node->binary_operator;
	Binary_Operator_Data binary_operator_data = get_data(&state->context, node)->binary_operator;

	LLVMValueRef left_value = generate_node(binary_operator.left, state);
	LLVMValueRef right_value = generate_node(binary_operator.right, state);

	switch (binary_operator.operator) {
		case OPERATOR_EQUALS:
			return LLVMBuildICmp(state->llvm_builder, LLVMIntEQ, left_value, right_value, "");
		case OPERATOR_ADD:
			return LLVMBuildAdd(state->llvm_builder, left_value, right_value, "");
		case OPERATOR_SUBTRACT:
			return LLVMBuildSub(state->llvm_builder, left_value, right_value, "");
		case OPERATOR_MULTIPLY:
			return LLVMBuildMul(state->llvm_builder, left_value, right_value, "");
		case OPERATOR_DIVIDE:
			if (is_type_signed(binary_operator_data.type)) {
				return LLVMBuildSDiv(state->llvm_builder, left_value, right_value, "");
			} else {
				return LLVMBuildUDiv(state->llvm_builder, left_value, right_value, "");
			}
		default:
			assert(false);
	}
}

static LLVMValueRef generate_return(Node *node, State *state) {
	assert(node->kind == RETURN_NODE);
	Return_Node return_ = node->return_;

	if (return_.value != NULL) {
		LLVMBuildRet(state->llvm_builder, generate_node(return_.value, state));
	} else {
		LLVMBuildRetVoid(state->llvm_builder);
	}
	return NULL;
}

static LLVMValueRef generate_assign(Node *node, State *state) {
	assert(node->kind == ASSIGN_NODE);
	Assign_Node assign = node->assign;

	return generate_node(assign.container, state);
}

static LLVMValueRef generate_variable(Node *node, State *state) {
	assert(node->kind == VARIABLE_NODE);
	Variable_Node variable = node->variable;

	LLVMValueRef allocated_variable_llvm = LLVMBuildAlloca(state->llvm_builder, create_llvm_type(get_data(&state->context, node)->variable.type), "");
	if (variable.value != NULL) {
		LLVMValueRef llvm_value = generate_node(variable.value, state);
		LLVMBuildStore(state->llvm_builder, llvm_value, allocated_variable_llvm);
	}

	Node_Data *node_data = get_data(&state->context, node);

	hmput(state->variables, node_data, allocated_variable_llvm);

	return NULL;
}

static LLVMValueRef generate_if(Node *node, State *state) {
	assert(node->kind == IF_NODE);
	If_Node if_ = node->if_;

	if (if_.static_) {
		if (get_data(&state->context, node)->if_.static_condition) {
			generate_node(if_.if_body, state);
		} else {
			if (if_.else_body != NULL) {
				generate_node(if_.else_body, state);
			}
		}
	} else {
		LLVMValueRef value = generate_node(if_.condition, state);

		LLVMBasicBlockRef if_block = LLVMAppendBasicBlock(state->current_function, "");
		LLVMBasicBlockRef else_block = LLVMAppendBasicBlock(state->current_function, "");
		LLVMBasicBlockRef done_block = LLVMAppendBasicBlock(state->current_function, "");
		LLVMBuildCondBr(state->llvm_builder, value, if_block, else_block);
		LLVMPositionBuilderAtEnd(state->llvm_builder, if_block);
		generate_node(if_.if_body, state);
		LLVMBuildBr(state->llvm_builder, done_block);
		LLVMPositionBuilderAtEnd(state->llvm_builder, else_block);
		if (if_.else_body != NULL) {
			generate_node(if_.else_body, state);
		}
		LLVMBuildBr(state->llvm_builder, done_block);
		LLVMPositionBuilderAtEnd(state->llvm_builder, done_block);
	}

	return NULL;
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
		case IDENTIFIER_NODE:
			return generate_identifier(node, state);
		case STRING_NODE:
			return generate_string(node, state);
		case NUMBER_NODE:
			return generate_number(node, state);
		case NULL_NODE:
			return generate_null(node, state);
		case STRUCTURE_NODE:
			return generate_structure(node, state);
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
		case SLICE_NODE:
			return generate_slice(node, state);
		case BINARY_OPERATOR_NODE:
			return generate_binary_operator(node, state);
		case RETURN_NODE:
			return generate_return(node, state);
		case ASSIGN_NODE:
			return generate_assign(node, state);
		case VARIABLE_NODE:
			return generate_variable(node, state);
		case IF_NODE:
			return generate_if(node, state);
		default:
			assert(false);
	}
}

static LLVMValueRef generate_function(Value *value, State *state) {
	assert(value->tag == FUNCTION_VALUE);
	Function_Value function = value->function;

	size_t saved_generic_id = state->context.generic_id;
	state->context.generic_id = function.generic_id;

	if (function.compile_only) {
		return NULL;
	}

	LLVMValueRef llvm_function = LLVMAddFunction(state->llvm_module, "", create_llvm_function_literal_type(function.type));
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

		for (long int i = 0; i < LLVMCountParams(llvm_function); i++) {
			LLVMValueRef allocated = LLVMBuildAlloca(state->llvm_builder, create_llvm_type(function.type->function_type.arguments[i].type), "");
			LLVMBuildStore(state->llvm_builder, LLVMGetParam(llvm_function, i), allocated);
			arrpush(state->function_arguments, allocated);
		}

		generate_node(function.body, state);
	}

	state->function_arguments = saved_function_arguments;
	state->current_function = saved_current_function;
	state->context.generic_id = saved_generic_id;
	state->llvm_builder = saved_llvm_builder;

	if (function.extern_name != NULL) {
		LLVMSetValueName(llvm_function, function.extern_name);
	}

	return llvm_function;
}

static void generate_module(Value *value, State *state) {
	assert(value->tag == MODULE_VALUE);
	Module_Value module = value->module;

	generate_node(module.body, state);
}

static LLVMValueRef generate_integer(Value *value, State *state) {
	(void) state;
	assert(value->tag == INTEGER_VALUE);
	Integer_Value integer = value->integer;

	return LLVMConstInt(LLVMInt64Type(), integer.value, false);
}

static LLVMValueRef generate_value(Value *value, State *state) {
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
		case STRUCTURE_TYPE_VALUE:
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

size_t size_llvm(Value *value, void *data) {
	LLVMTypeRef llvm_type = create_llvm_type(value);
	return LLVMStoreSizeOfType(LLVMCreateTargetDataLayout(((LLVM_Data *) data)->target_machine), llvm_type);
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
		.generated_cache = NULL
	};

	generate_node(root->module.body, &state);
	generate_main(&state);

	LLVMPrintModuleToFile(llvm_module, "main.ll", NULL);
	LLVMTargetMachineEmitToFile(llvm_target_machine, llvm_module, "output.o", LLVMObjectFile, NULL);
	
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
