#ifndef COMMON_H
#define COMMON_H

#include "ast.h"

typedef struct Node_Data Node_Data;
typedef struct Value_Data Value_Data;

typedef struct {
	Value_Data *value;
	Node *node;
} Value;

typedef struct {
	Value value;
	Value type;
} Typed_Value;

typedef struct {
	Value type;
	size_t index;
} Binding;

typedef struct {
	Node *node;
	Node_Data *node_data;
} Variable_Definition;

typedef struct {
	enum {
		SCOPE_INVALID,
		SCOPE_VARIABLE,
		SCOPE_BINDING,
		SCOPE_STATIC_BINDING,
		SCOPE_STATIC_VARIABLE
	} tag;
	union {
		Variable_Definition variable;
		Binding binding;
		Typed_Value static_binding;
		Variable_Definition static_variable;
	};
} Scope_Identifier;

typedef struct {
	struct { size_t key; Scope_Identifier value; } *identifiers;
	Node *node;
	Value node_type;
	Value current_type;
} Scope;

typedef struct {
	enum {
		IDENTIFIER_VARIABLE,
		IDENTIFIER_ARGUMENT,
		IDENTIFIER_BINDING,
		IDENTIFIER_VALUE,
		IDENTIFIER_STATIC_VARIABLE,
		IDENTIFIER_UNDERSCORE,
		IDENTIFIER_SPECIAL_VALUE
	} kind;
	union {
		Node *variable;
		Variable_Definition static_variable;
		size_t argument_index;
		Value value;
		struct {
			Node *node;
			size_t index;
		} binding;
	};
	Value type;
	bool want_pointer;
} Identifier_Data;

typedef struct {
	Value type;
} Variable_Data;

typedef struct {
	Value type;
	String_View value;
} String_Data;

typedef struct {
	char value;
} Character_Data;

typedef struct {
	Value type;
} Number_Data;

typedef struct {
	Value type;
} Structure_Data;

typedef struct {
	Value value;
} Run_Data;

typedef struct {
	Value from_type;
	Value to_type;
} Cast_Data;

typedef struct {
	Value type;
} Null_Data;

typedef struct {
	Value function_type;
	Value function_value;
	Value struct_type;
} Call_Data;

typedef struct {
	Value_Data *function;
	Value function_type;
} Custom_Operator_Function;

typedef struct {
	Node **arguments;
	Custom_Operator_Function custom_operator_function;
} Call_Method_Data;

typedef struct {
	bool static_condition;
	Value result_type;
	Value type;
	bool returned;
	bool then_returned;
	bool else_returned;
} If_Data;

typedef struct {
	Value type;
	bool returned;
	bool *cases_returned; // stb_ds
	int static_case;
} Switch_Data;

typedef struct {
	Typed_Value typed_value;
} Define_Data;

typedef struct {
	Value *static_arguments;
	Typed_Value value;
} Static_Argument_Variation;

typedef struct {
	bool compile_only;
	bool returned;
	Static_Argument_Variation *function_values;
} Function_Data;

typedef struct {
	Value value;
} Function_Type_Data;

typedef struct {
	Value_Data *value;
} Module_Access_Data;

typedef struct {
	Value structure_type;
	Value item_type;
	bool want_pointer;
	bool pointer_access;
} Structure_Access_Data;

typedef struct {
	Value type;
} Dereference_Data;

typedef struct {
	Value type;
} Deoptional_Data;

typedef struct {
	Value value;
	Value type;
} Is_Data;

typedef struct {
	Value type;
	bool returned;
} Catch_Data;

typedef struct {
	Node **defers;
} Block_Data;

typedef struct {
	Value array_type;
	Value item_type;
	Custom_Operator_Function custom_operator_function;
	bool want_pointer;
	bool pointer_access;
} Array_Access_Data;

typedef struct {
	Value array_type;
	Value item_type;
	bool pointer_access;
} Slice_Data;

typedef struct {
	Value type;
} Binary_Operator_Data;

typedef struct {
	Value wanted_type;
	Value type;
	bool has_type;
	size_t *static_ids; // stb_ds
} While_Data;

typedef struct {
	Value type;
} Return_Data;

typedef struct {
	Value *types;
	size_t *static_ids; // stb_ds
} For_Data;

typedef struct {
	Node *while_;
} Break_Data;

typedef struct {
	Value value;
	Node *node;
} Internal_Data;

typedef struct {
	Typed_Value typed_value;
} Operator_Data;

struct Node_Data {
	Node_Kind kind;
	union {
		Identifier_Data identifier;
		Variable_Data variable;
		String_Data string;
		Character_Data character;
		Number_Data number;
		Structure_Data structure;
		Run_Data run;
		Cast_Data cast;
		Null_Data null_;
		Call_Data call;
		Call_Method_Data call_method;
		If_Data if_;
		Switch_Data switch_;
		Define_Data define;
		Function_Data function;
		Function_Type_Data function_type;
		Module_Access_Data module_access;
		Structure_Access_Data structure_access;
		Dereference_Data dereference;
		Deoptional_Data deoptional;
		Is_Data is;
		Catch_Data catch;
		Block_Data block;
		Array_Access_Data array_access;
		Slice_Data slice;
		Binary_Operator_Data binary_operator;
		Return_Data return_;
		Break_Data break_;
		While_Data while_;
		For_Data for_;
		Internal_Data internal;
		Operator_Data operator;
	};
};

Node_Data *data_new(Node_Kind kind);

typedef struct { Node *key; Value value; } *Node_Types;

typedef struct { Node *key; Node_Data *value; } *Node_Datas;

typedef struct { char *key; Node *value; } *Define_Operators;

typedef struct Context Context;

typedef enum {
	C_CHAR_SIZE,
	C_SHORT_SIZE,
	C_INT_SIZE,
	C_LONG_SIZE
} C_Size_Fn_Input;

typedef struct {
	size_t (*size_fn)(Value_Data *, void *data);
	size_t (*c_size_fn)(C_Size_Fn_Input input);
	size_t (*alignment_fn)(Value_Data *, void *data);
	void (*build_fn)(Context context, Node *root, void *data);
	size_t default_integer_size;
	void *data;
} Codegen;

typedef struct {
	char *path;
	Value value;
} Cached_File;

typedef struct {
	Node *assign_node;
	Value wanted_type;
	bool want_pointer;
	Value *call_argument_types;
	Value call_wanted_type;
} Temporary_Context;

typedef struct {
	char **source_files; // stb_ds
} Data;

struct Context {
	Node_Types **types; // stb_ds
	struct { size_t key; Node_Datas *value; } *datas; // stb_ds
	struct { Node *key; Define_Operators *value; } *operators; // stb_ds
	struct { Node_Data *key; Value value; } *static_variables; // stb_ds
	bool returned;
	Scope *scopes; // stb_ds
	bool compile_only;
	size_t static_id;
	size_t static_id_counter;
	Temporary_Context temporary_context;
	Codegen codegen;
	Cached_File *cached_files; // stb_ds
	Node *internal_root;
	Data *data;
};

Value get_type(Context *context, Node *node);
void set_type(Context *context, Node *node, Value type);

Node_Data *get_data(Context *context, Node *node);
void set_data(Context *context, Node *node, Node_Data *data);

void reset_node(Context *context, Node *node);

#endif
