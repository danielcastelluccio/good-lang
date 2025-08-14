#ifndef AST_H
#define AST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "string_view.h"

typedef struct {
	uint32_t path_ref;
	uint32_t row;
	uint32_t column;
} Source_Location;

typedef struct Node Node;

typedef struct {
	String_View name;
	Node *type;
} Structure_Member;

typedef struct {
	String_View name;
	Node *function;
} Operator_Overload;

// Node Types

typedef struct {
	Node *parent;
	Node *index;
	Node *assign_value;
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
	OP_DIVIDE,
	OP_MODULUS,
	OP_AND,
	OP_OR
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
	Node *function;
	Node **arguments; // stb_ds
} Call_Node;

typedef struct {
	Node *argument1;
	String_View method;
	Node **arguments; // stb_ds
} Call_Method_Node;

typedef struct {
	String_View value;
} Character_Node;

typedef struct {
	Node *value;
	String_View binding;
	Node *error;
} Catch_Node;

typedef struct {
	Node *node;
} Defer_Node;

typedef struct {
	String_View identifier;
	Node *type;
	Node *expression;
	bool var;
} Define_Node;

typedef struct {
	Node *node;
	Node *assign_value;
} Deoptional_Node;

typedef struct {
	Node *node;
	Node *assign_value;
} Dereference_Node;

typedef struct {
	String_View *items; // stb_ds
} Enum_Type_Node;

typedef struct {
	Node **items;
	Node *body;
	String_View *bindings;
	bool static_;
} For_Node;

typedef struct {
	Node *function_type;
	Node *body;
	String_View extern_;
} Function_Node;

typedef struct {
	String_View identifier;
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
	Node *type;
	Node *value;
	String_View extern_;
} Global_Node;

typedef struct {
	Node *module;
	String_View value;
	Node *assign_value;
	bool assign_static;
} Identifier_Node;

typedef struct {
	Node *condition;
	Node *if_body;
	Node *else_body;
	bool static_;
	String_View *bindings;
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
		INTERNAL_SELF,
		INTERNAL_SIZE_OF,
		INTERNAL_IMPORT,
		INTERNAL_TYPE_INFO_OF,
		INTERNAL_OS,
		INTERNAL_OK,
		INTERNAL_ERR,
		INTERNAL_COMPILE_ERROR
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
	String_View identifier;
	Node *expression;
} Operator_Node;

typedef struct {
	Node *inner;
} Optional_Type_Node;

typedef struct {
	Node *inner;
} Pointer_Type_Node;

typedef struct {
	Node *start;
	Node *end;
} Range_Node;

typedef struct {
	Node *node;
} Reference_Node;

typedef struct {
	Node *value;
	Node *error;
} Result_Type_Node;

typedef struct {
	Node *value;
} Return_Node;

typedef struct {
	Node *node;
} Run_Node;

typedef struct {
	Node *parent;
	Node *start;
	Node *end;
} Slice_Node;

typedef struct {
	String_View value;
} String_Node;

typedef struct {
	Structure_Member *members; // stb_ds
	Node **operators; // stb_ds
} Struct_Type_Node;

typedef struct {
	String_View identifier;
	Node *node;
} Structure_Member_Value;

typedef struct {
	Structure_Member_Value *values; // stb_ds
} Structure_Node;

typedef struct {
	Node *parent;
	String_View name;
	Node *assign_value;
} Structure_Access_Node;

typedef struct {
	Node *value;
	Node *body;
	String_View binding;
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
	String_View name;
	Node *type;
	Node *value;
	bool static_;
} Variable_Node;

typedef struct {
	Node *condition;
	Node *body;
	Node *else_body;
	bool static_;
} While_Node;

typedef enum {
	ARRAY_ACCESS_NODE,
	ARRAY_TYPE_NODE,
	ARRAY_VIEW_TYPE_NODE,
	BINARY_OP_NODE,
	BLOCK_NODE,
	BOOLEAN_NODE,
	BREAK_NODE,
	CAST_NODE,
	CALL_NODE,
	CALL_METHOD_NODE,
	CHARACTER_NODE,
	CATCH_NODE,
	DEFER_NODE,
	DEFINE_NODE,
	DEOPTIONAL_NODE,
	DEREFERENCE_NODE,
	ENUM_TYPE_NODE,
	FOR_NODE,
	FUNCTION_NODE,
	FUNCTION_TYPE_NODE,
	GLOBAL_NODE,
	IDENTIFIER_NODE,
	IF_NODE,
	INTERNAL_NODE,
	IS_NODE,
	MODULE_NODE,
	MODULE_TYPE_NODE,
	NULL_NODE,
	NUMBER_NODE,
	OPERATOR_NODE,
	OPTIONAL_NODE,
	POINTER_NODE,
	RANGE_NODE,
	REFERENCE_NODE,
	RESULT_NODE,
	RETURN_NODE,
	RUN_NODE,
	SLICE_NODE,
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
	Source_Location location;
	union {
		Array_Access_Node array_access;
		Array_Type_Node array_type;
		Array_View_Type_Node array_view_type;
		Binary_Op_Node binary_op;
		Block_Node block;
		Boolean_Node boolean;
		Break_Node break_;
		Cast_Node cast;
		Call_Node call;
		Call_Method_Node call_method;
		Character_Node character;
		Catch_Node catch;
		Defer_Node defer;
		Define_Node define;
		Deoptional_Node deoptional;
		Dereference_Node dereference;
		Enum_Type_Node enum_type;
		For_Node for_;
		Function_Node function;
		Function_Type_Node function_type;
		Global_Node global;
		Identifier_Node identifier;
		If_Node if_;
		Internal_Node internal;
		Is_Node is;
		Module_Node module;
		Number_Node number;
		Operator_Node operator;
		Optional_Type_Node optional_type;
		Pointer_Type_Node pointer_type;
		Range_Node range;
		Reference_Node reference;
		Result_Type_Node result_type;
		Return_Node return_;
		Run_Node run;
		Slice_Node slice;
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
};

Node *ast_new(Node_Kind kind, Source_Location location);

#endif
