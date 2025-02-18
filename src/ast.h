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
} Array_Node;

typedef Identifier_Type_Pair Function_Argument;

typedef struct {
	Function_Argument *arguments; // std_ds
	Node *return_;
	bool variadic;
} Function_Type_Node;

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
	Node *structure;
	char *item;
} Structure_Access_Node;

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
} Function_Node;

typedef Identifier_Type_Pair Struct_Item;

typedef struct {
	Struct_Item *items; // stb_ds
} Structure_Node;

typedef enum {
	POINTER_NODE,
	ARRAY_NODE,
	FUNCTION_TYPE_NODE,
	STRING_NODE,
	NUMBER_NODE,
	IDENTIFIER_NODE,
	CALL_NODE,
	REFERENCE_NODE,
	STRUCTURE_ACCESS_NODE,
	RETURN_NODE,
	ASSIGN_NODE,
	VARIABLE_NODE,
	IF_NODE,
	MODULE_NODE,
	BINARY_OPERATOR_NODE,
	DEFINE_NODE,
	BLOCK_NODE,
	FUNCTION_NODE,
	STRUCTURE_NODE
} Node_Kind;

struct Node {
	Node_Kind kind;
	union {
		Pointer_Node pointer;
		Array_Node array;
		Function_Type_Node function_type;
		String_Node string;
		Number_Node number;
		Identifier_Node identifier;
		Call_Node call;
		Reference_Node reference;
		Structure_Access_Node structure_access;
		Return_Node return_;
		Assign_Node assign;
		Variable_Node variable;
		If_Node if_;
		Module_Node module;
		Binary_Operator_Node binary_operator;
		Define_Node define;
		Block_Node block;
		Function_Node function;
		Structure_Node structure;
	};
	Source_Location location;
};

Node *ast_new(Node_Kind kind, Source_Location location);

#endif
