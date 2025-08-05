#include "common.h"

typedef enum {
	ARRAY_VALUE,
	ARRAY_TYPE_VALUE,
	ARRAY_VIEW_VALUE,
	ARRAY_VIEW_TYPE_VALUE,
	BOOLEAN_VALUE,
	BOOLEAN_TYPE_VALUE,
	BYTE_VALUE,
	BYTE_TYPE_VALUE,
	ENUM_VALUE,
	ENUM_TYPE_VALUE,
	EXTERN_VALUE,
	FLOAT_TYPE_VALUE,
	FUNCTION_VALUE,
	FUNCTION_STUB_VALUE,
	FUNCTION_TYPE_VALUE,
	INTEGER_VALUE,
	INTEGER_TYPE_VALUE,
	MODULE_VALUE,
	MODULE_TYPE_VALUE,
	OPTIONAL_VALUE,
	OPTIONAL_TYPE_VALUE,
	POINTER_VALUE,
	POINTER_TYPE_VALUE,
	RANGE_VALUE,
	RANGE_TYPE_VALUE,
	RESULT_TYPE_VALUE,
	STRUCT_VALUE,
	STRUCT_TYPE_VALUE,
	TAGGED_UNION_VALUE,
	TAGGED_UNION_TYPE_VALUE,
	TUPLE_TYPE_VALUE,
	TYPE_TYPE_VALUE,
	UNION_VALUE,
	UNION_TYPE_VALUE,

	NONE_VALUE
} Value_Tag;

typedef struct {
	Value_Data **values;
	size_t length;
} Array_Value;

typedef struct {
	Value inner;
	Value size;
} Array_Type_Value;

typedef struct {
	size_t length;
	Value_Data **values;
} Array_View_Value;

typedef struct {
	Value inner;
} Array_View_Type_Value;

typedef struct {
	bool value;
} Boolean_Value;

typedef struct {
	long value;
} Enum_Value;

typedef struct {
	String_View *items; // stb_ds
} Enum_Type_Value;

typedef struct {
	String_View name;
	Value type;
} Extern_Value;

typedef struct {
	size_t size;
} Float_Type_Value;

typedef struct {
	Value_Data *type;
	Node *body;
	size_t static_id;
	Node *node;
} Function_Value;

typedef struct {
	Node *node;
	Scope *scopes;
} Function_Stub_Value;

typedef struct {
	String_View identifier;
	Value type;
	bool static_;
	bool inferred;
} Function_Argument_Value;

typedef struct {
	Function_Argument_Value *arguments; // stb_ds
	Value return_type;
	bool variadic;
	Node *node;
} Function_Type_Value;

typedef struct {
	long value;
} Integer_Value;

typedef struct {
	String_View operator;
	Value function;
} Operator_Value_Definition;

typedef struct {
	Value *members; // stb_ds
	Node *node;
	Value *arguments; // stb_ds
	Operator_Value_Definition *operators; // stb_ds
} Struct_Type_Value;

typedef struct {
	String_View identifier;
	Value type;
} Union_Item_Value;
typedef struct {
	Union_Item_Value *items; // stb_ds
} Union_Type_Value;

typedef struct {
	String_View identifier;
	Value type;
} Tagged_Union_Item_Value;

typedef struct {
	Tagged_Union_Item_Value *items; // stb_ds
	Node *node;
	Value_Data *enum_;
} Tagged_Union_Type_Value;

typedef struct {
	Value *members;
} Tuple_Type_Value;

typedef struct {
	Node *body;
	Scope *scopes;
} Module_Value;

typedef struct {
	Value_Data *value;
} Pointer_Value;

typedef struct {
	char value;
} Byte_Value;

typedef struct {
	Value_Data **values;
} Struct_Value;

typedef struct {
	Value_Data *value;
} Union_Value;

typedef struct {
	bool present;
	Value_Data *value;
} Optional_Value;

typedef struct {
	Value_Data *tag;
	Value_Data *data;
} Tagged_Union_Value;

typedef struct {
	Value inner;
} Pointer_Type_Value;

typedef struct {
	Value inner;
} Optional_Type_Value;

typedef struct {
	Value start;
	Value end;
} Range_Value;

typedef struct {
	Value type;
} Range_Type_Value;

typedef struct {
	Value value;
	Value error;
} Result_Type_Value;

typedef struct {
	bool signed_;
	size_t size;
} Integer_Type_Value;

struct Value_Data {
	Value_Tag tag;
	union {
		Array_Value array;
		Array_Type_Value array_type;
		Array_View_Value array_view;
		Array_View_Type_Value array_view_type;
		Boolean_Value boolean;
		Byte_Value byte;
		Enum_Value enum_;
		Enum_Type_Value enum_type;
		Extern_Value extern_;
		Float_Type_Value float_type;
		Function_Value function;
		Function_Stub_Value function_stub;
		Function_Type_Value function_type;
		Integer_Value integer;
		Integer_Type_Value integer_type;
		Module_Value module;
		Optional_Value optional;
		Optional_Type_Value optional_type;
		Pointer_Value pointer;
		Pointer_Type_Value pointer_type;
		Range_Value range;
		Range_Type_Value range_type;
		Result_Type_Value result_type;
		Struct_Value struct_;
		Struct_Type_Value struct_type;
		Tagged_Union_Value tagged_union;
		Tagged_Union_Type_Value tagged_union_type;
		Tuple_Type_Value tuple_type;
		Union_Value union_;
		Union_Type_Value union_type;
	};
};

Value_Data *value_new(Value_Tag tag);

Value create_value(Value_Tag tag);
Value create_pointer_type(Value value);
Value create_optional_type(Value value);
Value create_array_type(Value value);
Value create_array_view_type(Value value);
Value create_integer_type(bool signed_, size_t size);
Value create_float_type(size_t size);
Value create_range_type(Value value);

Value create_integer(size_t value);
Value create_byte(char value);
Value create_boolean(bool value);
Value create_enum(size_t value);
