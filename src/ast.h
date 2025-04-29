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
} Optional_Node;

typedef struct {
	Node *inner;
	Node *size;
} Array_Type_Node;

typedef struct {
	Node *inner;
} Array_View_Type_Node;

typedef struct {
	char *identifier;
	Node *type;
	bool static_;
	bool inferred;
} Function_Argument;

typedef struct {
	Function_Argument *arguments; // std_ds
	Node *return_;
	bool variadic;
} Function_Type_Node;

typedef Identifier_Type_Pair Struct_Item;
typedef struct { char *operator; Node *function; } Operator_Definition;
typedef struct {
	Struct_Item *items; // stb_ds
	Operator_Definition *operators; // stb_ds
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
	char *value;
} Character_Node;

typedef struct {
	enum {
		INTEGER_NUMBER,
		DECIMAL_NUMBER
	} tag;
	union {
		long integer;
		double decimal;
	};
} Number_Node;

typedef struct {
	bool value;
} Boolean_Node;

typedef struct {
	Node **values; // stb_ds
} Structure_Node;

typedef struct {
	Node *value;
} Run_Node;

typedef struct {
	Node *module;
	char *value;
} Identifier_Node;

typedef struct {
	Node *function;
	Node **arguments; // stb_ds
} Call_Node;

typedef struct {
	Node *argument1;
	char *method;
	Node **arguments; // stb_ds
} Call_Method_Node;

typedef struct {
	Node *node;
} Reference_Node;

typedef struct {
	Node *node;
} Dereference_Node;

typedef struct {
	Node *node;
} Deoptional_Node;

typedef struct {
	Node *node;
} Deoption_Node;

typedef struct {
	Node *node;
} Deoption_Present_Node;

typedef struct {
	Node *structure;
	char *item;
} Structure_Access_Node;

typedef struct {
	Node *array;
	Node *index;
} Array_Access_Node;

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
	Node *value;
} Break_Node;

typedef struct {
	Node *condition;
	Node *if_body;
	Node *else_body;
	bool static_;
	char **bindings;
} If_Node;

typedef struct {
	Node *condition;
	Node *body;
	Node *else_body;
} While_Node;

typedef struct {
	Node *item;
	Node *body;
	char **bindings;
} For_Node;

typedef struct {
	Node *check;
	Node *body;
} Switch_Case;

typedef struct {
	Node *value;
	Switch_Case *cases; // stb_ds
} Switch_Node;

typedef struct {
	Node *body;
} Module_Node;

typedef enum {
	OPERATOR_EQUALS,
	OPERATOR_NOT_EQUALS,
	OPERATOR_LESS,
	OPERATOR_LESS_EQUALS,
	OPERATOR_GREATER,
	OPERATOR_GREATER_EQUALS,
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

typedef struct {
	char *identifier;
	Node *expression;
} Define_Node;

typedef struct {
	Node **statements; // stb_ds
	bool has_result;
} Block_Node;

typedef struct {
	Node *function_type;
	Node *body;
	char *extern_name;
} Function_Node;

typedef enum {
	POINTER_NODE,
	OPTIONAL_NODE,
	ARRAY_TYPE_NODE,
	ARRAY_VIEW_TYPE_NODE,
	FUNCTION_TYPE_NODE,
	STRUCT_TYPE_NODE,
	UNION_TYPE_NODE,
	ENUM_TYPE_NODE,
	STRING_NODE,
	CHARACTER_NODE,
	NUMBER_NODE,
	NULL_NODE,
	BOOLEAN_NODE,
	STRUCT_NODE,
	RUN_NODE,
	IDENTIFIER_NODE,
	CALL_NODE,
	CALL_METHOD_NODE,
	REFERENCE_NODE,
	DEREFERENCE_NODE,
	DEOPTIONAL_NODE,
	STRUCTURE_ACCESS_NODE,
	ARRAY_ACCESS_NODE,
	RETURN_NODE,
	ASSIGN_NODE,
	VARIABLE_NODE,
	BREAK_NODE,
	IF_NODE,
	WHILE_NODE,
	FOR_NODE,
	SWITCH_NODE,
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
		Optional_Node optional;
		Array_Type_Node array_type;
		Array_View_Type_Node array_view_type;
		Function_Type_Node function_type;
		Struct_Type_Node struct_type;
		Union_Type_Node union_type;
		Enum_Type_Node enum_type;
		String_Node string;
		Character_Node character;
		Number_Node number;
		Boolean_Node boolean;
		Structure_Node structure;
		Run_Node run;
		Identifier_Node identifier;
		Call_Node call;
		Call_Method_Node call_method;
		Reference_Node reference;
		Dereference_Node dereference;
		Deoptional_Node deoptional;
		Deoption_Node deoption;
		Deoption_Present_Node deoption_present;
		Structure_Access_Node structure_access;
		Array_Access_Node array_access;
		Return_Node return_;
		Assign_Node assign;
		Variable_Node variable;
		Break_Node break_;
		If_Node if_;
		While_Node while_;
		For_Node for_;
		Switch_Node switch_;
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
