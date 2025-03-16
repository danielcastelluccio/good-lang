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
	STRUCT_TYPE_VALUE,
	UNION_TYPE_VALUE,
	ENUM_TYPE_VALUE,
	POINTER_TYPE_VALUE,
	OPTION_TYPE_VALUE,
	ARRAY_TYPE_VALUE,
	SLICE_TYPE_VALUE,
	BOOLEAN_VALUE,
	MODULE_VALUE,
	POINTER_VALUE,
	ARRAY_VALUE,
	SLICE_VALUE,
	BYTE_VALUE,
	INTEGER_VALUE,
	ENUM_VALUE,
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
	char *extern_name;
} Function_Value;

typedef Identifier_Value_Pair Function_Argument_Value;

typedef struct {
	Function_Argument_Value *arguments; // stb_ds
	Value *return_type;
	bool variadic;
} Function_Type_Value;

typedef Identifier_Value_Pair Struct_Item_Value;
typedef struct {
	Struct_Item_Value *items; // stb_ds
} Struct_Type_Value;

typedef Identifier_Value_Pair Union_Item_Value;
typedef struct {
	Union_Item_Value *items; // stb_ds
} Union_Type_Value;

typedef struct {
	char **items; // stb_ds
} Enum_Type_Value;

typedef struct {
	Node *body;
	Scope *scopes;
	size_t generic_id;
} Module_Value;

typedef struct {
	Value *value;
} Pointer_Value;

typedef struct {
	Value **values;
	size_t length;
} Array_Value;

typedef struct {
	Value **values;
	size_t length;
} Slice_Value;

typedef struct {
	char value;
} Byte_Value;

typedef struct {
	long value;
} Integer_Value;

typedef struct {
	long value;
} Enum_Value;

typedef struct {
	Value *inner;
} Pointer_Type_Value;

typedef struct {
	Value *inner;
} Option_Type_Value;

typedef struct {
	Value *inner;
	Value *size;
} Array_Type_Value;

typedef struct {
	Value *inner;
} Slice_Type_Value;

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
		Struct_Type_Value struct_type;
		Union_Type_Value union_type;
		Enum_Type_Value enum_type;
		Module_Value module;
		Function_Type_Value function_type;
		Pointer_Type_Value pointer_type;
		Option_Type_Value option_type;
		Array_Type_Value array_type;
		Slice_Type_Value slice_type;
		Boolean_Value boolean;
		Pointer_Value pointer;
		Array_Value array;
		Slice_Value slice;
		Byte_Value byte;
		Integer_Value integer;
		Enum_Value enum_;
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
	Value *type;
	bool want_pointer;
	Node *assign_value;
} Identifier_Data;

typedef struct {
	Value *type;
} Variable_Data;

typedef struct {
	Value *type;
	char *value;
	size_t length;
} String_Data;

typedef struct {
	Value *type;
} Number_Data;

typedef struct {
	Value *type;
} Structure_Data;

typedef struct {
	Value *value;
} Run_Data;

typedef struct {
	Value *type;
} Null_Data;

typedef struct {
	Value *function_type;
} Call_Data;

typedef struct {
	bool static_condition;
	Value *type;
} If_Data;

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
	Value *type;
	Node *assign_value;
} Dereference_Data;

typedef struct {
	Value *type;
	Node *assign_value;
} Deoption_Data;

typedef struct {
	Value *type;
} Deoption_Present_Data;

typedef struct {
	Value *array_like_type;
	Node *assign_value;
	bool want_pointer;
} Array_Access_Data;

typedef struct {
	Value *array_like_type;
} Slice_Data;

typedef struct {
	Value *type;
} Binary_Operator_Data;

typedef struct {
	Value *wanted_type;
	Value *type;
	bool has_type;
} Block_Data;

typedef struct {
	Node *block;
} Yield_Data;

typedef struct {
	Node_Kind kind;
	union {
		Identifier_Data identifier;
		Variable_Data variable;
		String_Data string;
		Number_Data number;
		Structure_Data structure;
		Run_Data run;
		Null_Data null_;
		Call_Data call;
		If_Data if_;
		Define_Data define;
		Function_Data function;
		Function_Type_Data function_type;
		Module_Access_Data module_access;
		Structure_Access_Data structure_access;
		Dereference_Data dereference;
		Deoption_Data deoption;
		Deoption_Present_Data deoption_present;
		Array_Access_Data array_access;
		Slice_Data slice;
		Binary_Operator_Data binary_operator;
		Block_Data block;
		Yield_Data yield;
	};
} Node_Data;

Node_Data *node_data_new(Node_Kind kind);

typedef struct { Node *key; Value *value; } *Node_Types;

typedef struct { Node *key; Node_Data *value; } *Node_Datas;

typedef struct Context Context;

typedef struct {
	size_t (*size_fn)(Value *, void *data);
	void (*build_fn)(Context context, Node *root, void *data);
	void *data;
} Codegen;

typedef struct {
	char *path;
	Value *value;
} Cached_File;

typedef struct {
	Node *assign_value;
	Node *assign_node;
	Value *wanted_type;
	bool want_pointer;
	Value **call_argument_types;
	Value *call_wanted_type;
} Temporary_Context;

struct Context {
	struct { size_t key; Node_Types *value; } *node_types; // stb_ds
	struct { size_t key; Node_Datas *value; } *node_datas; // stb_ds
	Node **left_blocks; // stb_ds
	Scope *scopes; // stb_ds
	bool compile_only;
	size_t generic_id;
	size_t generic_id_counter;
	Temporary_Context temporary_context;
	Codegen codegen;
	Cached_File *cached_files; // stb_ds
};

Value *get_type(Context *context, Node *node);
void set_type(Context *context, Node *node, Value *value);

Node_Data *get_data(Context *context, Node *node);
void set_data(Context *context, Node *node, Node_Data *value);

void reset_node(Context *context, Node *node);

Value *strip_define_data(Value *value);

Value *create_string_type();
Value *create_boolean_type();
Value *create_slice_type(Value *inner);
Value *create_option_type(Value *inner);
Value *create_internal_type(char *identifier);

#endif
