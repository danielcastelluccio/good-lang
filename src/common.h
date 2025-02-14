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
	STRUCTURE_VALUE,
	POINTER_TYPE_VALUE,
	ARRAY_TYPE_VALUE,
	BOOLEAN_VALUE,
	MODULE_VALUE,
	MODULE_TYPE_VALUE,
	DEFINE_DATA_VALUE,
	NONE_VALUE,
	INTERNAL_VALUE
} Value_Tag;

typedef struct {
	char *identifier;
	Value *type;
} Identifier_Value_Pair;

typedef struct {
	Value *type;
	Node *body;
	size_t generic_id;
	bool compile_only;
} Function_Value;

typedef Identifier_Value_Pair Function_Argument_Value;

typedef struct {
	Function_Argument_Value *arguments; // stb_ds
	Value *return_type;
	bool variadic;
} Function_Type_Value;

typedef Identifier_Value_Pair Structure_Item_Value;

typedef struct {
	Structure_Item_Value *items;
} Structure_Type_Value;

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
	Node *define_node;
	Generic_Binding *bindings;
	Value *value;
} Define_Data_Value;

struct Value {
	Value_Tag tag;
	union {
		Function_Value function;
		Structure_Type_Value structure_type;
		Module_Value module;
		Function_Type_Value function_type;
		Pointer_Type_Value pointer_type;
		Array_Type_Value array_type;
		Boolean_Value boolean;
		Internal_Value internal;
		Define_Data_Value define_data;
	};
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
	bool want_pointer;
	Node *assign_value;
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
	Generic_Binding *generics;
	Generic_Binding value;
} Generic_Value;

typedef struct {
	enum {
		DEFINE_SINGLE,
		DEFINE_GENERIC
	} kind;
	union {
		Generic_Binding value;
		Generic_Value *generic_values;
	};
} Define_Data;

typedef struct {
	bool compile_only;
} Function_Data;

typedef struct {
	Value *value;
} Function_Type_Data;

typedef struct {
	Value *value;
} Module_Access_Data;

typedef struct {
	Value *structure_value;
	Node *assign_value;
	bool want_pointer;
} Structure_Access_Data;

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
		Function_Data function;
		Function_Type_Data function_type;
		Module_Access_Data module_access;
		Structure_Access_Data structure_access;
	};
} Node_Data;

Node_Data *node_data_new(Node_Kind kind);

typedef struct { Node *key; Value *value; } *Node_Types;

typedef struct { Node *key; Node_Data *value; } *Node_Datas;

typedef struct {
	Node *assign_value;
	Value *wanted_type;
	bool want_pointer;
	Value **call_argument_types;
} Temporary_Context;

typedef struct {
	struct { size_t key; Node_Types *value; } *node_types; // stb_ds
	struct { size_t key; Node_Datas *value; } *node_datas; // stb_ds
	Node *current_function;
	Scope *scopes; // stb_ds
	bool compile_only;
	size_t generic_id;
	size_t generic_id_counter;
	Temporary_Context temporary_context;
} Context;

Value *get_type(Context *context, Node *node);
void set_type(Context *context, Node *node, Value *value);

Node_Data *get_data(Context *context, Node *node);
void set_data(Context *context, Node *node, Node_Data *value);

void reset_node(Context *context, Node *node);

Value *strip_define_data(Value *value);

#endif
