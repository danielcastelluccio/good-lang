#ifndef COMMON_H
#define COMMON_H

#include "ast.h"

typedef struct Value Value;

typedef struct {
	Value *binding;
	Value *type;
} Generic_Binding;

typedef struct {
	struct { char *key; Node *value; } *variables; // stb_ds
	struct { char *key; Generic_Binding value; } *generic_bindings; // stb_ds
	Node *node;
} Scope;

typedef struct Value Value;

typedef enum {
	FUNCTION_VALUE,
	FUNCTION_TYPE_VALUE,
	POINTER_TYPE_VALUE,
	ARRAY_TYPE_VALUE,
	BOOLEAN_VALUE,
	MODULE_VALUE,
	MODULE_TYPE_VALUE,
	NONE_VALUE,
	INTERNAL_VALUE
} Value_Tag;

typedef struct {
	Value *type;
	Node *body;
	size_t generic_id;
} Function_Value;

typedef struct {
	char *identifier;
	Value *type;
} Function_Argument_Value;

typedef struct {
	Function_Argument_Value *arguments; // stb_ds
	Value *return_type;
	bool variadic;
} Function_Type_Value;

typedef struct {
	Node *body;
	Scope *scopes;
	size_t generic_id;
} Module_Value;

typedef struct {
	Value *inner;
} Pointer_Type_Value;

typedef struct {
	Value *inner;
} Array_Type_Value;

typedef struct {
	bool value;
} Boolean_Value;

typedef struct {
	char *identifier;
} Internal_Value;

typedef struct {
	Node *node;
	Generic_Binding *bindings; // stb_ds
} Value_Generic;

struct Value {
	Value_Tag tag;
	union {
		Function_Value function;
		Module_Value module;
		Function_Type_Value function_type;
		Pointer_Type_Value pointer_type;
		Array_Type_Value array_type;
		Boolean_Value boolean;
		Internal_Value internal;
	};
	Value_Generic *generics;
};

Value *value_new(Value_Tag tag);

typedef struct {
	enum {
		IDENTIFIER_VARIABLE,
		IDENTIFIER_ARGUMENT,
		IDENTIFIER_VALUE
	} kind;
	union {
		Node *variable_definition;
		size_t argument_index;
		Value *value;
	};
} Identifier_Node_Data;

typedef struct {
	Value *type;
} Variable_Node_Data;

typedef struct {
	Value *type;
	char *value;
} String_Node_Data;

typedef struct {
	Value *type;
} Number_Node_Data;

typedef struct {
	Value *function_type;
} Call_Node_Data;

typedef struct {
	bool static_condition;
} If_Node_Data;

typedef struct {
	Value *value;
} Define_Data;

typedef struct {
	Value *value;
} Function_Type_Data;

typedef struct {
	Value *value;
} Module_Access_Data;

typedef struct {
	Node_Kind kind;
	union {
		Identifier_Node_Data identifier;
		Variable_Node_Data variable;
		String_Node_Data string;
		Number_Node_Data number;
		Call_Node_Data call;
		If_Node_Data if_;
		Define_Data define;
		Function_Type_Data function_type;
		Module_Access_Data module_access;
	};
} Node_Data;

Node_Data *node_data_new(Node_Kind kind);

typedef struct { Node *key; Value *value; } *Node_Types;

typedef struct { Node *key; Node_Data *value; } *Node_Datas;

typedef struct {
	Node *root;
	struct { size_t key; Node_Types *value; } *node_types; // stb_ds
	struct { Node *key; bool value; } *compile_only_function_nodes; // stb_ds
	struct { Value *key; bool value; } *compile_only_functions; // stb_ds
	struct { size_t key; Node_Datas *value; } *node_datas; // stb_ds
	Node *current_function;
	Value *current_value;
	Scope *scopes; // stb_ds
	bool compile_only;
	Value *wanted_type;
	Value **call_argument_types;
	size_t generic_id;
	size_t generic_id_counter;
} Context;

Value *get_type(Context *context, Node *node);
void set_type(Context *context, Node *node, Value *value);

Node_Data *get_data(Context *context, Node *node);
void set_data(Context *context, Node *node, Node_Data *value);

void reset_node(Context *context, Node *node);

#endif
