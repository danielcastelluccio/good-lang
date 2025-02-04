#include <assert.h>
#include <stdbool.h>

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
	struct { Node *key; LLVMValueRef value; } *variables; // stb_ds
	LLVMValueRef *function_arguments; // stb_ds
	LLVMValueRef current_function;
} State;

static LLVMValueRef generate_node(Node *node, State *state);
static LLVMValueRef generate_value(Value *value, State *state);

static LLVMTypeRef create_llvm_type(Value *node);

static LLVMTypeRef create_llvm_function_literal_type(Value *value) {
	assert(value->tag == FUNCTION_TYPE_VALUE);
	Function_Type_Value function_type = value->function_type;

	LLVMTypeRef *arguments = NULL;
	for (long int i = 0; i < arrlen(function_type.arguments); i++) {
		arrput(arguments, create_llvm_type(function_type.arguments[i].type));
	}

	LLVMTypeRef return_type = create_llvm_type(function_type.return_type);

	return LLVMFunctionType(return_type, arguments, arrlen(arguments), function_type.variadic);
}

static LLVMTypeRef create_llvm_type(Value *value) {
	switch (value->tag) {
		case POINTER_TYPE_VALUE: {
			Pointer_Type_Value pointer = value->pointer_type;
			return LLVMPointerType(create_llvm_type(pointer.inner), 0);
		}
		case ARRAY_TYPE_VALUE: {
			Array_Type_Value array = value->array_type;
			return LLVMArrayType(create_llvm_type(array.inner), 0);
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

	Value *value = data->define.value;
	if (value == NULL) {
		return;
	}

	if (value->tag != FUNCTION_VALUE) {
		return;
	}

	LLVMValueRef expression_llvm_value = generate_value(value, state);
	if (define.extern_) {
		if (LLVMIsAFunction(expression_llvm_value)) {
			LLVMSetValueName(expression_llvm_value, define.identifier);
		} else {
			assert(false);
		}
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
		arrput(arguments, generate_node(call.arguments[i], state));
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
			Value *variable_type = get_data(&state->context, node)->variable.type;
			LLVMValueRef variable_llvm_value = hmget(state->variables, variable);
			LLVMValueRef loaded_llvm_value = LLVMBuildLoad2(state->llvm_builder, create_llvm_type(variable_type), variable_llvm_value, "");
			return loaded_llvm_value;
		}
		case IDENTIFIER_ARGUMENT: {
			size_t argument = get_data(&state->context, node)->identifier.argument_index;
			return state->function_arguments[argument];
		}
		case IDENTIFIER_VALUE: {
			return generate_value(identifier_data->identifier.value, state);
		}
		default:
			assert(false);
	}
}

static LLVMValueRef generate_string(Node *node, State *state) {
	assert(node->kind == STRING_NODE);

	Node_Data *data = get_data(&state->context, node);
	char *string_value = data->string.value;

	LLVMValueRef global = LLVMBuildGlobalString(state->llvm_builder, string_value, "");
	LLVMValueRef llvm_value = LLVMBuildPointerCast(state->llvm_builder, global, LLVMPointerType(LLVMInt8Type(), 0), "");

	return llvm_value; 
}

static LLVMValueRef generate_number(Node *node, State *state) {
	assert(node->kind == NUMBER_NODE);
	Number_Node number = node->number;
	Value *type = get_data(&state->context, node)->number.type;

	assert(type->tag == INTERNAL_VALUE);

	if (strcmp(type->internal.identifier, "uint") == 0) {
		return LLVMConstInt(create_llvm_type(type), number.integer, false);
	} else {
		assert(false);
	}
}

static LLVMValueRef generate_return(Node *node, State *state) {
	assert(node->kind == RETURN_NODE);
	LLVMBuildRetVoid(state->llvm_builder);
	return NULL;
}

static LLVMValueRef generate_variable(Node *node, State *state) {
	assert(node->kind == VARIABLE_NODE);
	Variable_Node variable = node->variable;

	LLVMValueRef llvm_value = generate_node(variable.value, state);
	LLVMValueRef allocated_variable_llvm = LLVMBuildAlloca(state->llvm_builder, create_llvm_type(get_data(&state->context, node)->variable.type), "");
	LLVMBuildStore(state->llvm_builder, llvm_value, allocated_variable_llvm);

	hmput(state->variables, node, allocated_variable_llvm);

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
		assert(false);
	}

	return NULL;
}

static void generate_module(Node *node, State *state) {
	assert(node->kind == MODULE_NODE);
	Module_Node module = node->module;

	generate_node(module.body, state);
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
		case RETURN_NODE:
			return generate_return(node, state);
		case VARIABLE_NODE:
			return generate_variable(node, state);
		case IF_NODE:
			return generate_if(node, state);
		case MODULE_NODE:
			generate_module(node, state);
			return NULL;
		default:
			assert(false);
	}
}

static LLVMValueRef generate_function(Value *value, State *state) {
	assert(value->tag == FUNCTION_VALUE);
	Function_Value function = value->function;

	size_t saved_generic_id = state->context.generic_id;
	state->context.generic_id = function.generic_id;

	if (hmget(state->context.compile_only_functions, value)) {
		return NULL;
	}

	LLVMValueRef llvm_function = LLVMAddFunction(state->llvm_module, "", create_llvm_function_literal_type(function.type));
	hmput(state->generated_cache, value, llvm_function);

	LLVMBuilderRef saved_llvm_builder = state->llvm_builder;
	state->llvm_builder = LLVMCreateBuilder();

	LLVMValueRef *saved_function_arguments = state->function_arguments;
	state->function_arguments = NULL;
	for (long int i = 0; i < LLVMCountParams(llvm_function); i++) {
		arrpush(state->function_arguments, LLVMGetParam(llvm_function, i));
	}

	if (function.body != NULL) {
		LLVMBasicBlockRef entry = LLVMAppendBasicBlock(llvm_function, "");
		LLVMPositionBuilderAtEnd(state->llvm_builder, entry);

		generate_node(function.body, state);
	}

	state->function_arguments = saved_function_arguments;
	state->context.generic_id = saved_generic_id;
	state->llvm_builder = saved_llvm_builder;

	return llvm_function;
}

static LLVMValueRef generate_value(Value *value, State *state) {
	LLVMValueRef cached_result = hmget(state->generated_cache, value);
	if (cached_result != NULL) {
		return cached_result;
	}

	LLVMValueRef result;
	switch (value->tag) {
		case FUNCTION_VALUE:
			result = generate_function(value, state);
			break;
		default:
			assert(false);
	}

	hmput(state->generated_cache, value, result);
	return result;
}

void build_llvm(Context context, Node *root) {
	assert(root->kind == BLOCK_NODE);

	LLVMContextRef llvm_context = LLVMContextCreate();
    LLVMModuleRef llvm_module = LLVMModuleCreateWithNameInContext("main", llvm_context);
    LLVMBuilderRef llvm_builder = LLVMCreateBuilderInContext(llvm_context);

	State state = {
		.context = context,
		.llvm_module = llvm_module,
		.llvm_builder = llvm_builder,
		.generated_cache = NULL
	};

	generate_node(root, &state);

	LLVMPrintModuleToFile(llvm_module, "main.ll", NULL);

	LLVMInitializeAllTargetInfos();
	LLVMInitializeAllTargets();
	LLVMInitializeAllTargetMCs();
	LLVMInitializeAllAsmParsers();
	LLVMInitializeAllAsmPrinters();

	LLVMTargetRef target;
	char* error;
	LLVMGetTargetFromTriple(LLVMGetDefaultTargetTriple(), &target, &error);
	LLVMTargetMachineRef targetMachine = LLVMCreateTargetMachine(
    	target, LLVMGetDefaultTargetTriple(), "generic", "",
    	LLVMCodeGenLevelDefault, LLVMRelocDefault, LLVMCodeModelDefault
	);
	LLVMSetTarget(llvm_module, LLVMGetDefaultTargetTriple());
	LLVMTargetMachineEmitToFile(targetMachine, llvm_module, "output.o", LLVMObjectFile, &error);
}
