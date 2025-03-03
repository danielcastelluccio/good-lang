#ifndef AST_H
#define AST_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
	size_t row;
	size_t column;
	char *path;
} Source_Location;

typedef struct Node Node;

typedef struct {
	char *identifier;
	Node *type;
} Identifier_Type_Pair;

typedef struct {
	Node *inner;
} Pointer_Node;

typedef struct {
	Node *inner;
	Node *size;
} Array_Type_Node;

typedef struct {
	Node *inner;
} Slice_Type_Node;

typedef Identifier_Type_Pair Function_Argument;

typedef struct {
	Function_Argument *arguments; // std_ds
	Node *return_;
	bool variadic;
} Function_Type_Node;

typedef Identifier_Type_Pair Struct_Item;
typedef struct {
	Struct_Item *items; // stb_ds
} Struct_Type_Node;

typedef Identifier_Type_Pair Union_Item;
typedef struct {
	Union_Item *items; // stb_ds
} Union_Type_Node;

typedef struct {
	char **items; // stb_ds
} Enum_Type_Node;

typedef struct {
	char *value;
} String_Node;

typedef struct {
	enum {
		INTEGER_NUMBER
	} tag;
	union {
		long integer;
	};
} Number_Node;

typedef struct {
	Node **values; // stb_ds
} Structure_Node;

typedef struct {
	Node *value;
} Run_Node;

typedef struct {
	Node *module;
	char *value;
	Node **generics; // stb_ds
} Identifier_Node;

typedef struct {
	Node *function;
	Node **arguments; // stb_ds
} Call_Node;

typedef struct {
	Node *node;
} Reference_Node;

typedef struct {
	Node *node;
} Dereference_Node;

typedef struct {
	Node *structure;
	char *item;
} Structure_Access_Node;

typedef struct {
	Node *array;
	Node *index;
} Array_Access_Node;

typedef struct {
	Node *array;
	Node *start_index;
	Node *end_index;
} Slice_Node;

typedef struct {
	Node *value;
} Return_Node;

typedef struct {
	Node *container;
	Node *value;
} Assign_Node;

typedef struct {
	char *identifier;
	Node *type;
	Node *value;
} Variable_Node;

typedef struct {
	Node *condition;
	Node *if_body;
	Node *else_body;
	bool static_;
} If_Node;

typedef struct {
	Node *body;
} Module_Node;

typedef enum {
	OPERATOR_EQUALS,
	OPERATOR_ADD,
	OPERATOR_SUBTRACT,
	OPERATOR_MULTIPLY,
	OPERATOR_DIVIDE
} Binary_Operatory_Node_Kind;

typedef struct {
	Node *left;
	Node *right;
	Binary_Operatory_Node_Kind operator;
} Binary_Operator_Node;

typedef Identifier_Type_Pair Generic_Argument;

typedef struct {
	char *identifier;
	Node *expression;
	Generic_Argument *generics; // stb_ds
	Node *generic_constraint;
	bool extern_;
} Define_Node;

typedef struct {
	Node **statements; // stb_ds
} Block_Node;

typedef struct {
	Node *function_type;
	Node *body;
	char *extern_name;
} Function_Node;

typedef enum {
	POINTER_NODE,
	ARRAY_TYPE_NODE,
	SLICE_TYPE_NODE,
	FUNCTION_TYPE_NODE,
	STRUCT_TYPE_NODE,
	UNION_TYPE_NODE,
	ENUM_TYPE_NODE,
	STRING_NODE,
	NUMBER_NODE,
	NULL_NODE,
	STRUCT_NODE,
	RUN_NODE,
	IDENTIFIER_NODE,
	CALL_NODE,
	REFERENCE_NODE,
	DEREFERENCE_NODE,
	STRUCTURE_ACCESS_NODE,
	ARRAY_ACCESS_NODE,
	SLICE_NODE,
	RETURN_NODE,
	ASSIGN_NODE,
	VARIABLE_NODE,
	IF_NODE,
	MODULE_NODE,
	BINARY_OPERATOR_NODE,
	DEFINE_NODE,
	BLOCK_NODE,
	FUNCTION_NODE
} Node_Kind;

struct Node {
	Node_Kind kind;
	union {
		Pointer_Node pointer;
		Array_Type_Node array_type;
		Slice_Type_Node slice_type;
		Function_Type_Node function_type;
		Struct_Type_Node struct_type;
		Union_Type_Node union_type;
		Enum_Type_Node enum_type;
		String_Node string;
		Number_Node number;
		Structure_Node structure;
		Run_Node run;
		Identifier_Node identifier;
		Call_Node call;
		Reference_Node reference;
		Dereference_Node dereference;
		Structure_Access_Node structure_access;
		Array_Access_Node array_access;
		Slice_Node slice;
		Return_Node return_;
		Assign_Node assign;
		Variable_Node variable;
		If_Node if_;
		Module_Node module;
		Binary_Operator_Node binary_operator;
		Define_Node define;
		Block_Node block;
		Function_Node function;
	};
	Source_Location location;
};

Node *ast_new(Node_Kind kind, Source_Location location);

#endif
