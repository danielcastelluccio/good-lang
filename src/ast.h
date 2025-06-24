#ifndef AST_H
#define AST_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
	char *path;
	size_t row;
	size_t column;
} Source_Location;

typedef struct Node Node;

typedef struct {
	char *name;
	Node *type;
} Structure_Member;

typedef struct {
	char *name;
	Node *function;
} Operator_Overload;

typedef struct {
	Node *target;
	Node *value;
} Assign_Node;

typedef struct {
	Node *parent;
	Node *index;
} Array_Access_Node;

typedef struct {
	Node *inner;
	Node *size;
} Array_Type_Node;

typedef struct {
	Node *inner;
} Array_View_Type_Node;

typedef enum {
	OP_EQUALS,
	OP_NOT_EQUALS,
	OP_LESS,
	OP_LESS_EQUALS,
	OP_GREATER,
	OP_GREATER_EQUALS,
	OP_ADD,
	OP_SUBTRACT,
	OP_MULTIPLY,
	OP_DIVIDE
} Binary_Op_Node_Kind;

typedef struct {
	Node *left;
	Node *right;
	Binary_Op_Node_Kind operator;
} Binary_Op_Node;

typedef struct {
	Node **statements; // stb_ds
	bool has_result;
} Block_Node;

typedef struct {
	bool value;
} Boolean_Node;

typedef struct {
	Node *value;
} Break_Node;

typedef struct {
	Node *node;
	Node *type;
} Cast_Node;

typedef struct {
	Node *argument1;
	char *method;
	Node **arguments; // stb_ds
} Call_Method_Node;

typedef struct {
	Node *function;
	Node **arguments; // stb_ds
} Call_Node;

typedef struct {
	char *value;
} Character_Node;

typedef struct {
	Node *value;
	char *binding;
	Node *error;
} Catch_Node;

typedef struct {
	Node *node;
} Defer_Node;

typedef struct {
	char *identifier;
	Node *type;
	Node *expression;
} Define_Node;

typedef struct {
	Node *node;
} Deoptional_Node;

typedef struct {
	Node *node;
} Dereference_Node;

typedef struct {
	char **items; // stb_ds
} Enum_Type_Node;

typedef struct {
	char *name;
} Extern_Node;

typedef struct {
	Node *item;
	Node *body;
	char **bindings;
	bool static_;
} For_Node;

typedef struct {
	Node *function_type;
	Node *body;
} Function_Node;

typedef struct {
	char *identifier;
	Node *type;
	bool static_;
	bool inferred;
	Node *default_value;
} Function_Argument;

typedef struct {
	Function_Argument *arguments; // std_ds
	Node *return_;
	bool variadic;
} Function_Type_Node;

typedef struct {
	Node *module;
	char *value;
} Identifier_Node;

typedef struct {
	Node *condition;
	Node *if_body;
	Node *else_body;
	bool static_;
	char **bindings;
} If_Node;

typedef struct {
	enum {
		INTERNAL_UINT,
		INTERNAL_UINT8,
		INTERNAL_TYPE,
		INTERNAL_BYTE,
		INTERNAL_FLT64,
		INTERNAL_BOOL,
		INTERNAL_TYPE_OF,
		INTERNAL_INT,
		INTERNAL_C_CHAR_SIZE,
		INTERNAL_C_SHORT_SIZE,
		INTERNAL_C_INT_SIZE,
		INTERNAL_C_LONG_SIZE,
		INTERNAL_PRINT,
		INTERNAL_EMBED,
		INTERNAL_EXPRESSION
	} kind;
	Node **inputs; // stb_ds
} Internal_Node;

typedef struct {
	Node *node;
	Node *check;
} Is_Node;

typedef struct {
	Node *body;
} Module_Node;

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
	Node *inner;
} Optional_Type_Node;

typedef struct {
	Node *inner;
} Pointer_Type_Node;

typedef struct {
	Node *node;
} Reference_Node;

typedef struct {
	Node *value;
	Node *error;
} Result_Type_Node;

typedef struct {
	Node *value;
	enum {
		RETURN_STANDARD,
		RETURN_SUCCESS,
		RETURN_ERROR
	} type;
} Return_Node;

typedef struct {
	Node *node;
} Run_Node;

typedef struct {
	char *value;
} String_Node;

typedef struct {
	Structure_Member *members; // stb_ds
	Operator_Overload *operator_overloads; // stb_ds
} Struct_Type_Node;

typedef struct {
	char *identifier;
	Node *node;
} Structure_Member_Value;

typedef struct {
	Structure_Member_Value *values; // stb_ds
} Structure_Node;

typedef struct {
	Node *parent;
	char *name;
} Structure_Access_Node;

typedef struct {
	Node *value;
	Node *body;
	char *binding;
} Switch_Case;

typedef struct {
	Node *condition;
	Switch_Case *cases; // stb_ds
	bool static_;
} Switch_Node;

typedef struct {
	Structure_Member *members; // stb_ds
} Tagged_Union_Type_Node;

typedef struct {
	Structure_Member *members; // stb_ds
} Union_Type_Node;

typedef struct {
	char *name;
	Node *type;
	Node *value;
	bool static_;
} Variable_Node;

typedef struct {
	Node *condition;
	Node *body;
	Node *else_body;
} While_Node;

typedef enum {
	ASSIGN_NODE,
	ARRAY_ACCESS_NODE,
	ARRAY_TYPE_NODE,
	ARRAY_VIEW_TYPE_NODE,
	BINARY_OP_NODE,
	BLOCK_NODE,
	BOOLEAN_NODE,
	BREAK_NODE,
	CAST_NODE,
	CALL_METHOD_NODE,
	CALL_NODE,
	CHARACTER_NODE,
	CATCH_NODE,
	DEFER_NODE,
	DEFINE_NODE,
	DEOPTIONAL_NODE,
	DEREFERENCE_NODE,
	ENUM_TYPE_NODE,
	EXTERN_NODE,
	FOR_NODE,
	FUNCTION_NODE,
	FUNCTION_TYPE_NODE,
	IDENTIFIER_NODE,
	IF_NODE,
	INTERNAL_NODE,
	IS_NODE,
	MODULE_NODE,
	MODULE_TYPE_NODE,
	NULL_NODE,
	NUMBER_NODE,
	OPTIONAL_NODE,
	POINTER_NODE,
	REFERENCE_NODE,
	RESULT_NODE,
	RETURN_NODE,
	RUN_NODE,
	STRING_NODE,
	STRUCT_TYPE_NODE,
	STRUCTURE_NODE,
	STRUCTURE_ACCESS_NODE,
	SWITCH_NODE,
	TAGGED_UNION_TYPE_NODE,
	UNION_TYPE_NODE,
	VARIABLE_NODE,
	WHILE_NODE
} Node_Kind;

struct Node {
	Node_Kind kind;
	union {
		Assign_Node assign;
		Array_Access_Node array_access;
		Array_Type_Node array_type;
		Array_View_Type_Node array_view_type;
		Binary_Op_Node binary_op;
		Block_Node block;
		Boolean_Node boolean;
		Break_Node break_;
		Cast_Node cast;
		Call_Method_Node call_method;
		Call_Node call;
		Character_Node character;
		Catch_Node catch;
		Defer_Node defer;
		Define_Node define;
		Deoptional_Node deoptional;
		Dereference_Node dereference;
		Enum_Type_Node enum_type;
		Extern_Node extern_;
		For_Node for_;
		Function_Node function;
		Function_Type_Node function_type;
		Identifier_Node identifier;
		If_Node if_;
		Internal_Node internal;
		Is_Node is;
		Module_Node module;
		Number_Node number;
		Optional_Type_Node optional_type;
		Pointer_Type_Node pointer_type;
		Reference_Node reference;
		Result_Type_Node result_type;
		Return_Node return_;
		Run_Node run;
		String_Node string;
		Struct_Type_Node struct_type;
		Structure_Node structure;
		Structure_Access_Node structure_access;
		Switch_Node switch_;
		Tagged_Union_Type_Node tagged_union_type;
		Union_Type_Node union_type;
		Variable_Node variable;
		While_Node while_;
	};
	Source_Location location;
};

Node *ast_new(Node_Kind kind, Source_Location location);

#endif
