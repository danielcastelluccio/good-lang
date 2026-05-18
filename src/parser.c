#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "stb/ds.h"

#include "ast.h"
#include "lexer.h"
#include "parser.h"
#include "util.h"

static void handle_token_error(Lexer *lexer, Token_Kind expected, Token_Data actual) {
	printf("%s:%u:%u: Unexpected token '%s', expected '%s'\n", lexer->path, actual.location.row, actual.location.column, token_to_string(actual.kind), token_to_string(expected));
	exit(1);
}

static void handle_token_error_no_expected(Lexer *lexer, Token_Data actual) {
	printf("%s:%u:%u: Unexpected token '%s'\n", lexer->path, actual.location.row, actual.location.column, token_to_string(actual.kind));
	exit(1);
}

static Token_Data lexer_peek(Lexer *lexer) {
	return lexer_next(lexer, false);
}

static Token_Data lexer_consume(Lexer *lexer) {
	return lexer_next(lexer, true);
}

static bool lexer_peek_check(Lexer *lexer, Token_Kind kind) {
	Token_Data next = lexer_peek(lexer);
	return next.kind == kind;
}

static Token_Data lexer_consume_check(Lexer *lexer, Token_Kind kind) {
	Token_Data data = lexer_next(lexer, true);
	if (data.kind != kind) {
		handle_token_error(lexer, kind, data);
	}

	return data;
}

static Node *parse_expression(Lexer *lexer);
static Node *parse_statement(Lexer *lexer);

static Node *parse_expression_or_nothing(Lexer *lexer) {
	Token_Data token = lexer_peek(lexer);
	if (token.kind == CURLY_BRACE_CLOSED || token.kind == PARENTHESIS_CLOSED || token.kind == SEMICOLON || token.kind == COLON || token.kind == COMMA || token.kind == EQUALS || token.kind == VERTICAL_BAR || token.kind == KEYWORD_EXTERN) {
		return NULL;
	}

	return parse_expression(lexer);
}

static Node *parse_separated_statement(Lexer *lexer) {
	if (lexer_peek(lexer).kind != CURLY_BRACE_OPEN) {
		lexer_consume_check(lexer, EQUALS_GREATER);
	}

	return parse_statement(lexer);
}

static Node *parse_separated_statement_or_nothing(Lexer *lexer) {
	if (lexer_peek(lexer).kind == CURLY_BRACE_CLOSED || lexer_peek(lexer).kind == PARENTHESIS_CLOSED || lexer_peek(lexer).kind == SEMICOLON || lexer_peek(lexer).kind == EQUALS) {
		return NULL;
	}

	if (lexer_peek(lexer).kind != CURLY_BRACE_OPEN) {
		lexer_consume_check(lexer, EQUALS_GREATER);
	}

	return parse_statement(lexer);
}

static Node *parse_string(Lexer *lexer) {
	Token_Data token = lexer_consume_check(lexer, STRING);

	Node *string = ast_new(STRING_NODE, token.location);
	string->string.value = token.string;

	return string;
}

static Node *parse_character(Lexer *lexer) {
	Token_Data token = lexer_consume_check(lexer, CHARACTER);

	Node *character = ast_new(CHARACTER_NODE, token.location);
	character->character.value = token.string;

	return character;
}

static Node *parse_number(Lexer *lexer) {
	Token_Data token = lexer_consume(lexer);

	Node *number = ast_new(NUMBER_NODE, token.location);
	if (token.kind == INTEGER) {
		number->number.tag = INTEGER_NUMBER;
		number->number.integer = token.integer;
	} else {
		number->number.tag = DECIMAL_NUMBER;
		number->number.decimal = token.decimal;
	}

	return number;
}

static Node *parse_structure(Lexer *lexer) {
	Token_Data token = lexer_consume_check(lexer, PERIOD_CURLY_BRACE_OPEN);

	Node *structure = ast_new(STRUCTURE_NODE, token.location);
	structure->structure.values = NULL;

	if (lexer_peek(lexer).kind != CURLY_BRACE_CLOSED) {
		while (true) {
			Structure_Member_Value item_value = {};

			if (lexer_peek(lexer).kind == PERIOD) {
				lexer_consume(lexer);
				item_value.identifier = lexer_consume_check(lexer, IDENTIFIER).string;
				lexer_consume_check(lexer, EQUALS);
			}

			item_value.node = parse_expression(lexer);
			arrpush(structure->structure.values, item_value);

			Token_Data token = lexer_peek(lexer);
			if (token.kind == COMMA) {
				lexer_consume(lexer);
			} else if (token.kind == CURLY_BRACE_CLOSED) {
				break;
			} else {
				handle_token_error_no_expected(lexer, token);
			}
		}
	}

	lexer_consume_check(lexer, CURLY_BRACE_CLOSED);

	return structure;
}

static Node *parse_actual_identifier(Token_Data token, bool polymorphic) {
	Node *identifier = ast_new(IDENTIFIER_NODE, token.location);
	identifier->identifier.value = token.string;
	identifier->identifier.assign_value = NULL;
	identifier->identifier.polymorphic = polymorphic;

	return identifier;
}

static Node *parse_declaration(Lexer *lexer, bool polymorphic, String_View identifier, Source_Location location) {
	lexer_consume(lexer);

	Token_Data next = lexer_peek(lexer);

	Node *type = parse_expression_or_nothing(lexer);

	next = lexer_peek(lexer);
	if (next.kind == COLON) {
		lexer_consume(lexer);

		Node *define = ast_new(DEFINE_NODE, location);
		define->define.identifier = identifier;
		define->define.public = true;
		define->define.type = type;
		define->define.expression = parse_expression(lexer);
		return define;
	}

	Node *variable = ast_new(VARIABLE_NODE, location);
	variable->variable.name = identifier;
	variable->variable.polymorphic = polymorphic;
	variable->variable.static_ = polymorphic;
	variable->variable.type = type;
	if (next.kind == EQUALS) {
		lexer_consume(lexer);
		variable->variable.value = parse_expression(lexer);
	} else {
		variable->variable.value = NULL;
	}

	return variable;
}

static Node *parse_identifier(Lexer *lexer, bool polymorphic) {
	Token_Data token = lexer_consume_check(lexer, IDENTIFIER);

	switch (token.string.ptr[0]) {
		case 'b':
			if (sv_eq_cstr(token.string, "bool")) {
				Node *internal = ast_new(INTERNAL_NODE, token.location);
				internal->internal.kind = INTERNAL_BOOL;
				internal->internal.assign_value = NULL;
				return internal;
			} else if (sv_eq_cstr(token.string, "byte")) {
				Node *internal = ast_new(INTERNAL_NODE, token.location);
				internal->internal.kind = INTERNAL_BYTE;
				internal->internal.assign_value = NULL;
				return internal;
			}
			break;
		case 'c':
			if (sv_eq_cstr(token.string, "compile_error")) {
				Node *internal = ast_new(INTERNAL_NODE, token.location);
				internal->internal.kind = INTERNAL_COMPILE_ERROR;
				internal->internal.inputs = NULL;
				internal->internal.assign_value = NULL;
				
				lexer_consume_check(lexer, PARENTHESIS_OPEN);
				arrpush(internal->internal.inputs, parse_expression(lexer));
				lexer_consume_check(lexer, PARENTHESIS_CLOSED);

				return internal;
			} else if (sv_eq_cstr(token.string, "context")) {
				Node *internal = ast_new(INTERNAL_NODE, token.location);
				internal->internal.kind = INTERNAL_CONTEXT;
				return internal;
			}
			break;
		case 'C':
			if (sv_eq_cstr(token.string, "C_CHAR_SIZE")) {
				Node *internal = ast_new(INTERNAL_NODE, token.location);
				internal->internal.kind = INTERNAL_C_CHAR_SIZE;
				internal->internal.assign_value = NULL;
				return internal;
			} else if (sv_eq_cstr(token.string, "C_SHORT_SIZE")) {
				Node *internal = ast_new(INTERNAL_NODE, token.location);
				internal->internal.kind = INTERNAL_C_SHORT_SIZE;
				internal->internal.assign_value = NULL;
				return internal;
			} else if (sv_eq_cstr(token.string, "C_INT_SIZE")) {
				Node *internal = ast_new(INTERNAL_NODE, token.location);
				internal->internal.kind = INTERNAL_C_INT_SIZE;
				internal->internal.assign_value = NULL;
				return internal;
			} else if (sv_eq_cstr(token.string, "C_LONG_SIZE")) {
				Node *internal = ast_new(INTERNAL_NODE, token.location);
				internal->internal.kind = INTERNAL_C_LONG_SIZE;
				internal->internal.assign_value = NULL;
				return internal;
			}
			break;
		case 'e':
			if (sv_eq_cstr(token.string, "embed")) {
				Node *internal = ast_new(INTERNAL_NODE, token.location);
				internal->internal.kind = INTERNAL_EMBED;
				internal->internal.inputs = NULL;
				internal->internal.assign_value = NULL;

				lexer_consume_check(lexer, PARENTHESIS_OPEN);
				while (lexer_peek(lexer).kind != PARENTHESIS_CLOSED) {
					arrpush(internal->internal.inputs, parse_expression(lexer));
					if (lexer_peek(lexer).kind == COMMA) {
						lexer_consume(lexer);
					}
				}
				lexer_consume(lexer);

				return internal;
			} else if (sv_eq_cstr(token.string, "err")) {
				Node *internal = ast_new(INTERNAL_NODE, token.location);
				internal->internal.kind = INTERNAL_ERR;
				internal->internal.inputs = NULL;
				internal->internal.assign_value = NULL;

				lexer_consume_check(lexer, PARENTHESIS_OPEN);
				if (lexer_peek(lexer).kind != PARENTHESIS_CLOSED) {
					arrpush(internal->internal.inputs, parse_expression(lexer));
				}
				lexer_consume_check(lexer, PARENTHESIS_CLOSED);

				return internal;
			}
			break;
		case 'g':
			if (sv_eq_cstr(token.string, "global_value")) {
				Node *internal = ast_new(INTERNAL_NODE, token.location);
				internal->internal.kind = INTERNAL_GLOBAL_VALUE;
				internal->internal.assign_value = NULL;

				internal->internal.inputs = NULL;
				lexer_consume_check(lexer, PARENTHESIS_OPEN);
				arrpush(internal->internal.inputs, parse_expression(lexer));
				lexer_consume_check(lexer, PARENTHESIS_CLOSED);
				return internal;
			}
			break;
		case 'f':
			if (sv_eq_cstr(token.string, "false")) {
				Node *boolean = ast_new(BOOLEAN_NODE, token.location);
				boolean->boolean.value = false;
				return boolean;
			} else if (sv_eq_cstr(token.string, "flt64")) {
				Node *internal = ast_new(INTERNAL_NODE, token.location);
				internal->internal.kind = INTERNAL_FLT64;
				internal->internal.assign_value = NULL;
				return internal;
			}
			break;
		case 'i':
			if (sv_eq_cstr(token.string, "int")) {
				Node *internal = ast_new(INTERNAL_NODE, token.location);
				internal->internal.kind = INTERNAL_INT;
				internal->internal.assign_value = NULL;
				return internal;
			}
			break;
		case 'n':
			if (sv_eq_cstr(token.string, "null")) {
				return ast_new(NULL_NODE, token.location);
			}
			break;
		case 'o':
			if (sv_eq_cstr(token.string, "ok")) {
				Node *internal = ast_new(INTERNAL_NODE, token.location);
				internal->internal.kind = INTERNAL_OK;
				internal->internal.inputs = NULL;
				internal->internal.assign_value = NULL;

				lexer_consume_check(lexer, PARENTHESIS_OPEN);
				if (lexer_peek(lexer).kind != PARENTHESIS_CLOSED) {
					arrpush(internal->internal.inputs, parse_expression(lexer));
				}
				lexer_consume_check(lexer, PARENTHESIS_CLOSED);

				return internal;
			}
			break;
		case 'O':
			if (sv_eq_cstr(token.string, "OS")) {
				Node *internal = ast_new(INTERNAL_NODE, token.location);
				internal->internal.kind = INTERNAL_OS;
				internal->internal.assign_value = NULL;
				return internal;
			}
			break;
		case 'p':
			// if (sv_eq_cstr(token.string, "print")) {
			// 	Node *internal = ast_new(INTERNAL_NODE, token.location);
			// 	internal->internal.kind = INTERNAL_PRINT;

			// 	lexer_consume_check(lexer, PARENTHESIS_OPEN);
			// 	arrpush(internal->internal.inputs, parse_expression(lexer));
			// 	while (lexer_peek(lexer).kind == COMMA) {
			// 		lexer_consume(lexer);
			// 		arrpush(internal->internal.inputs, parse_expression(lexer));
			// 	}
			// 	lexer_consume_check(lexer, PARENTHESIS_CLOSED);

			// 	return internal;
			// }
			break;
		case 's':
			if (sv_eq_cstr(token.string, "self")) {
				Node *internal = ast_new(INTERNAL_NODE, token.location);
				internal->internal.kind = INTERNAL_SELF;
				internal->internal.assign_value = NULL;
				return internal;
			} else if (sv_eq_cstr(token.string, "sint")) {
				Node *internal = ast_new(INTERNAL_NODE, token.location);
				internal->internal.kind = INTERNAL_SINT;
				internal->internal.assign_value = NULL;
				return internal;
			} else if (sv_eq_cstr(token.string, "size_of")) {
				Node *internal = ast_new(INTERNAL_NODE, token.location);
				internal->internal.kind = INTERNAL_SIZE_OF;
				internal->internal.inputs = NULL;
				internal->internal.assign_value = NULL;

				lexer_consume_check(lexer, PARENTHESIS_OPEN);
				arrpush(internal->internal.inputs, parse_expression(lexer));
				lexer_consume_check(lexer, PARENTHESIS_CLOSED);

				return internal;
			} else if (sv_eq_cstr(token.string, "s8")) {
				Node *internal = ast_new(INTERNAL_NODE, token.location);
				internal->internal.kind = INTERNAL_S8;
				internal->internal.assign_value = NULL;
				return internal;
			} else if (sv_eq_cstr(token.string, "s16")) {
				Node *internal = ast_new(INTERNAL_NODE, token.location);
				internal->internal.kind = INTERNAL_S16;
				internal->internal.assign_value = NULL;
				return internal;
			} else if (sv_eq_cstr(token.string, "s32")) {
				Node *internal = ast_new(INTERNAL_NODE, token.location);
				internal->internal.kind = INTERNAL_S32;
				internal->internal.assign_value = NULL;
				return internal;
			} else if (sv_eq_cstr(token.string, "s64")) {
				Node *internal = ast_new(INTERNAL_NODE, token.location);
				internal->internal.kind = INTERNAL_S64;
				internal->internal.assign_value = NULL;
				return internal;
			} else if (sv_eq_cstr(token.string, "string")) {
				Node *internal = ast_new(INTERNAL_NODE, token.location);
				internal->internal.kind = INTERNAL_STRING;
				internal->internal.assign_value = NULL;
				return internal;
			}
			break;
		case 't':
			if (sv_eq_cstr(token.string, "true")) {
				Node *boolean = ast_new(BOOLEAN_NODE, token.location);
				boolean->boolean.value = true;
				return boolean;
			} else if (sv_eq_cstr(token.string, "type")) {
				Node *internal = ast_new(INTERNAL_NODE, token.location);
				internal->internal.kind = INTERNAL_TYPE;
				internal->internal.assign_value = NULL;
				return internal;
			} else if (sv_eq_cstr(token.string, "type_of")) {
				Node *internal = ast_new(INTERNAL_NODE, token.location);
				internal->internal.kind = INTERNAL_TYPE_OF;
				internal->internal.inputs = NULL;
				internal->internal.assign_value = NULL;

				lexer_consume_check(lexer, PARENTHESIS_OPEN);
				arrpush(internal->internal.inputs, parse_expression(lexer));
				lexer_consume_check(lexer, PARENTHESIS_CLOSED);

				return internal;
			} else if (sv_eq_cstr(token.string, "type_info_of")) {
				Node *internal = ast_new(INTERNAL_NODE, token.location);
				internal->internal.kind = INTERNAL_TYPE_INFO_OF;
				internal->internal.inputs = NULL;
				internal->internal.assign_value = NULL;

				lexer_consume_check(lexer, PARENTHESIS_OPEN);
				arrpush(internal->internal.inputs, parse_expression(lexer));
				lexer_consume_check(lexer, PARENTHESIS_CLOSED);

				return internal;
			}
			break;
		case 'T':
			if (sv_eq_cstr(token.string, "Type")) {
				Node *internal = ast_new(INTERNAL_NODE, token.location);
				internal->internal.kind = INTERNAL_TYPE;
				internal->internal.assign_value = NULL;
				return internal;
			}
			break;
		case 'u':
			if (sv_eq_cstr(token.string, "uint")) {
				Node *internal = ast_new(INTERNAL_NODE, token.location);
				internal->internal.kind = INTERNAL_UINT;
				internal->internal.assign_value = NULL;
				return internal;
			} else if (sv_eq_cstr(token.string, "uint8")) {
				Node *internal = ast_new(INTERNAL_NODE, token.location);
				internal->internal.kind = INTERNAL_UINT8;
				internal->internal.assign_value = NULL;
				return internal;
			} else if (sv_eq_cstr(token.string, "u8")) {
				Node *internal = ast_new(INTERNAL_NODE, token.location);
				internal->internal.kind = INTERNAL_U8;
				internal->internal.assign_value = NULL;
				return internal;
			} else if (sv_eq_cstr(token.string, "u16")) {
				Node *internal = ast_new(INTERNAL_NODE, token.location);
				internal->internal.kind = INTERNAL_U8;
				internal->internal.assign_value = NULL;
				return internal;
			} else if (sv_eq_cstr(token.string, "u32")) {
				Node *internal = ast_new(INTERNAL_NODE, token.location);
				internal->internal.kind = INTERNAL_U32;
				internal->internal.assign_value = NULL;
				return internal;
			} else if (sv_eq_cstr(token.string, "u64")) {
				Node *internal = ast_new(INTERNAL_NODE, token.location);
				internal->internal.kind = INTERNAL_U64;
				internal->internal.assign_value = NULL;
				return internal;
			}
			break;
		default:
			break;
	}

	if (lexer_peek(lexer).kind == COLON) {
		return parse_declaration(lexer, polymorphic, token.string, token.location);
	} else {
		Node *node = parse_actual_identifier(token, polymorphic);
		return node;
	}
}

static Node *parse_result(Lexer *lexer, Node *node) {
	Token_Data token = lexer_consume(lexer);

	Node *result = ast_new(RESULT_NODE, token.location);
	result->result_type.value = node;
	result->result_type.error = parse_expression(lexer);
	return result;
}

static Node *parse_range(Lexer *lexer, Node *start) {
	Token_Data token = lexer_consume(lexer);

	Node *end = parse_expression_or_nothing(lexer);

	Node *range = ast_new(RANGE_NODE, token.location);
	range->range.start = start;
	range->range.end = end;

	return range;
}

static Node *parse_is(Lexer *lexer, Node *node) {
	Token_Data token = lexer_consume(lexer);

	Node *is = ast_new(IS_NODE, token.location);
	is->is.node = node;
	is->is.check = parse_expression(lexer);
	return is;
}

static Node *parse_catch(Lexer *lexer, Node *node) {
	Token_Data token = lexer_consume(lexer);

	Node *catch = ast_new(CATCH_NODE, token.location);
	catch->catch.value = node;
	catch->catch.binding = (String_View) {};

	if (lexer_peek(lexer).kind == VERTICAL_BAR) {
		lexer_consume(lexer);
		catch->catch.binding = lexer_consume_check(lexer, IDENTIFIER).string;
		lexer_consume_check(lexer, VERTICAL_BAR);
	}

	catch->catch.error = parse_statement(lexer);

	return catch;
}

static Node *parse_call(Lexer *lexer, Node *function) {
	Token_Data first_token = lexer_consume_check(lexer, PARENTHESIS_OPEN);

	Node *call = ast_new(CALL_NODE, first_token.location);
	call->call.function = function;
	call->call.arguments = NULL;

	if (lexer_peek(lexer).kind != PARENTHESIS_CLOSED) {
		while (true) {
			String_View identifier = {};
			Node *argument = parse_expression(lexer);

			if (lexer_peek(lexer).kind == EQUALS) {
				identifier = argument->identifier.value;
				lexer_consume(lexer);
				argument = parse_expression(lexer);
			}

			Call_Argument call_argument = {
				.identifier = identifier,
				.node = argument
			};
			arrpush(call->call.arguments, call_argument);

			Token_Data token = lexer_peek(lexer);
			if (token.kind == COMMA) {
				lexer_consume(lexer);
			} else if (token.kind == PARENTHESIS_CLOSED) {
				break;
			} else {
				handle_token_error_no_expected(lexer, token);
			}
		}
	}

	lexer_consume_check(lexer, PARENTHESIS_CLOSED);

	return call;
}

static void add_inferred_arguments(Function_Argument **arguments, Node *argument) {
	if (argument == NULL) return;

	switch (argument->kind) {
		case ARRAY_TYPE_NODE:
			add_inferred_arguments(arguments, argument->array_type.size);
			add_inferred_arguments(arguments, argument->array_type.inner);
			break;
		case ARRAY_VIEW_TYPE_NODE:
			add_inferred_arguments(arguments, argument->array_view_type.inner);
			break;
		case IDENTIFIER_NODE: {
			Identifier_Node *identifier = &argument->identifier;
			if (identifier->polymorphic) {
				Function_Argument argument = {
					.identifier = identifier->value,
					.static_ = true,
					.inferred = true
				};
				arrpush(*arguments, argument);
			}
			break;
		}
		case POINTER_NODE:
			add_inferred_arguments(arguments, argument->pointer_type.inner);
			break;
		case INTERNAL_NODE:
			break;
		default:
			assert(false);
	}
}

typedef struct {
	Function_Argument *arguments;
	bool variadic;
} Parse_Arguments_Result;

static Parse_Arguments_Result parse_arguments(Lexer *lexer) {
	Function_Argument *arguments = NULL;

	lexer_consume_check(lexer, PARENTHESIS_OPEN);

	bool variadic = false;
	if (lexer_peek(lexer).kind != PARENTHESIS_CLOSED) {
		while (true) {
			if (lexer_peek(lexer).kind == PERIOD_PERIOD) {
				lexer_consume(lexer);
				variadic = true;
			} else {
				Node *argument_node = parse_expression(lexer);

				Function_Argument argument = {
					.identifier = argument_node->variable.name,
					.type = argument_node->variable.type,
					.default_value = argument_node->variable.value,
					.static_ = argument_node->variable.polymorphic
				};
				add_inferred_arguments(&arguments, argument_node->variable.type);
				arrpush(arguments, argument);
			}

			Token_Data token = lexer_peek(lexer);
			if (token.kind == COMMA) {
				lexer_consume(lexer);
			} else if (token.kind == PARENTHESIS_CLOSED) {
				break;
			} else {
				handle_token_error_no_expected(lexer, token);
			}
		}
	}

	lexer_consume_check(lexer, PARENTHESIS_CLOSED);

	return (Parse_Arguments_Result) {
		.arguments = arguments,
		.variadic = variadic
	};
}

static Node *parse_parenthesis(Lexer *lexer) {
	lexer_consume(lexer);

	Node *result = parse_expression(lexer);

	lexer_consume_check(lexer, PARENTHESIS_CLOSED);
	return result;
}

static size_t get_precedence(Binary_Op_Node_Kind kind) {
	switch (kind) {
		case OP_MULTIPLY:
		case OP_DIVIDE:
		case OP_MODULUS:
			return 3;
		case OP_ADD:
		case OP_SUBTRACT:
			return 2;
		case OP_EQUALS:
		case OP_NOT_EQUALS:
		case OP_LESS:
		case OP_GREATER:
		case OP_LESS_EQUALS:
		case OP_GREATER_EQUALS:
			return 1;
		case OP_AND:
		case OP_OR:
			return 0;
		default:
			assert(false);
			return 0;
	}
}

static Node *parse_binary_operator(Lexer *lexer, Node *left) {
	Token_Data first_token = lexer_consume(lexer);

	Node *binary_operator = ast_new(BINARY_OP_NODE, first_token.location);
	binary_operator->binary_op.left = left;
	switch (first_token.kind) {
		case EQUALS_EQUALS:
			binary_operator->binary_op.operator = OP_EQUALS;
			break;
		case EXCLAMATION_EQUALS:
			binary_operator->binary_op.operator = OP_NOT_EQUALS;
			break;
		case LESS:
			binary_operator->binary_op.operator = OP_LESS;
			break;
		case LESS_EQUALS:
			binary_operator->binary_op.operator = OP_LESS_EQUALS;
			break;
		case GREATER:
			binary_operator->binary_op.operator = OP_GREATER;
			break;
		case GREATER_EQUALS:
			binary_operator->binary_op.operator = OP_GREATER_EQUALS;
			break;
		case PLUS:
			binary_operator->binary_op.operator = OP_ADD;
			break;
		case MINUS:
			binary_operator->binary_op.operator = OP_SUBTRACT;
			break;
		case ASTERISK:
			binary_operator->binary_op.operator = OP_MULTIPLY;
			break;
		case SLASH:
			binary_operator->binary_op.operator = OP_DIVIDE;
			break;
		case PERCENT:
			binary_operator->binary_op.operator = OP_MODULUS;
			break;
		case KEYWORD_AND:
			binary_operator->binary_op.operator = OP_AND;
			break;
		case KEYWORD_OR:
			binary_operator->binary_op.operator = OP_OR;
			break;
		default:
			assert(false);
	}

	bool swap = false;
	Node *right = parse_expression(lexer);
	Node **swap_location = &right;
	while ((*swap_location)->kind == BINARY_OP_NODE) {
		swap = get_precedence((*swap_location)->binary_op.operator) <= get_precedence(binary_operator->binary_op.operator);
		swap_location = &(*swap_location)->binary_op.left;
	}

	if (swap) {
		binary_operator->binary_op.right = *swap_location;
		*swap_location = binary_operator;
		return right;
	} else {
		binary_operator->binary_op.right = right;
		return binary_operator;
	}
}

static Node *parse_structure_access(Lexer *lexer, Node *structure) {
	Token_Data first_token = lexer_consume(lexer);

	Token_Data member = lexer_peek(lexer);
	if (member.kind == ASTERISK) {
		lexer_consume(lexer);

		Node *dereference = ast_new(DEREFERENCE_NODE, first_token.location);
		dereference->dereference.node = structure;
		dereference->dereference.assign_value = NULL;

		return dereference;
	} else if (member.kind == QUESTION) {
		lexer_consume(lexer);

		Node *dereference = ast_new(DEOPTIONAL_NODE, first_token.location);
		dereference->dereference.node = structure;
		dereference->dereference.assign_value = NULL;

		return dereference;
	} else {
		Node *structure_access = ast_new(STRUCTURE_ACCESS_NODE, first_token.location);
		structure_access->structure_access.parent = structure;
		structure_access->structure_access.name = lexer_consume_check(lexer, IDENTIFIER).string;
		structure_access->structure_access.assign_value = NULL;

		return structure_access;
	}
}

static Node *parse_array_access_or_slice(Lexer *lexer, Node *array) {
	Token_Data first_token = lexer_consume(lexer);

	Node *first = parse_expression(lexer);

	if (lexer_peek(lexer).kind == COMMA) {
		lexer_consume(lexer);

		Node *slice = ast_new(SLICE_NODE, first_token.location);
		slice->slice.parent = array;
		slice->slice.start = first;
		slice->slice.end = parse_expression(lexer);

		lexer_consume_check(lexer, BRACE_CLOSED);
		return slice;
	} else {
		Node *array_access = ast_new(ARRAY_ACCESS_NODE, first_token.location);
		array_access->array_access.parent = array;
		array_access->array_access.index = first;
		array_access->array_access.assign_value = NULL;
		lexer_consume_check(lexer, BRACE_CLOSED);
		return array_access;
	}
}

static Node *parse_assign(Lexer *lexer, Node *target, bool static_) {
	lexer_consume(lexer);

	Node *assign_value = parse_expression(lexer);

	set_assign_value(target, assign_value, static_);
	return target;
}

static Node *parse_struct_type(Lexer *lexer) {
	Token_Data first_token = lexer_consume(lexer);

	Node *struct_ = ast_new(STRUCT_TYPE_NODE, first_token.location);
	
	// struct_->struct_type.inherit_function = false;
	Function_Argument *arguments = NULL;
	if (lexer_peek(lexer).kind == PARENTHESIS_OPEN) {
		arguments = parse_arguments(lexer).arguments;
		// struct_->struct_type.inherit_function = true;
		(void) arguments;
	}

	struct_->struct_type.members = NULL;
	lexer_consume_check(lexer, CURLY_BRACE_OPEN);
	while (lexer_peek(lexer).kind != CURLY_BRACE_CLOSED) {
		Token_Data identifier = lexer_consume_check(lexer, IDENTIFIER);
		lexer_consume_check(lexer, COLON);
		Node *type = parse_expression(lexer);

		Structure_Member member = {
			.name = identifier.string,
			.type = type
		};
		arrpush(struct_->struct_type.members, member);

		lexer_consume_check(lexer, SEMICOLON);
	}

	lexer_consume_check(lexer, CURLY_BRACE_CLOSED);

	if (arguments) {
		Node *function_type = ast_new(FUNCTION_TYPE_NODE, first_token.location);
		function_type->function_type.arguments = arguments;
		function_type->function_type.variadic = false;
		function_type->function_type.return_ = NULL;

		Node *function = ast_new(FUNCTION_NODE, first_token.location);
		function->function.function_type = function_type;
		function->function.body = struct_;
		function->function.static_id_counter = 0;

		return function;
	}

	return struct_;
}

static Node *parse_union_type(Lexer *lexer) {
	Token_Data first_token = lexer_consume(lexer);

	Node *union_ = ast_new(UNION_TYPE_NODE, first_token.location);

	lexer_consume_check(lexer, CURLY_BRACE_OPEN);
	if (lexer_peek(lexer).kind != CURLY_BRACE_CLOSED) {
		while (true) {
			String_View name = lexer_consume_check(lexer, IDENTIFIER).string;
			lexer_consume_check(lexer, COLON);
			Node *type = parse_expression(lexer);

			Structure_Member member = {
				.name = name,
				.type = type
			};
			arrpush(union_->union_type.members, member);

			Token_Data token = lexer_peek(lexer);
			if (token.kind == COMMA) {
				lexer_consume(lexer);
			} else if (token.kind == CURLY_BRACE_CLOSED) {
				break;
			} else {
				handle_token_error_no_expected(lexer, token);
			}
		}
	}
	lexer_consume_check(lexer, CURLY_BRACE_CLOSED);

	return union_;
}

static Node *parse_tagged_union_type(Lexer *lexer) {
	Token_Data first_token = lexer_consume(lexer);

	Node *tagged_union = ast_new(TAGGED_UNION_TYPE_NODE, first_token.location);

	tagged_union->tagged_union_type.members = NULL;
	lexer_consume_check(lexer, CURLY_BRACE_OPEN);
	while (lexer_peek(lexer).kind != CURLY_BRACE_CLOSED) {
		String_View name = lexer_consume_check(lexer, IDENTIFIER).string;
		lexer_consume_check(lexer, COLON);
		Node *type = parse_expression(lexer);

		Structure_Member member = {
			.name = name,
			.type = type
		};
		arrpush(tagged_union->tagged_union_type.members, member);

		lexer_consume_check(lexer, SEMICOLON);
	}
	lexer_consume_check(lexer, CURLY_BRACE_CLOSED);

	return tagged_union;
}

static Node *parse_enum_type(Lexer *lexer) {
	Token_Data first_token = lexer_consume(lexer);

	Node *enum_ = ast_new(ENUM_TYPE_NODE, first_token.location);

	enum_->enum_type.items = NULL;
	lexer_consume_check(lexer, CURLY_BRACE_OPEN);
	while (lexer_peek(lexer).kind != CURLY_BRACE_CLOSED) {
		Token_Data identifier = lexer_consume_check(lexer, IDENTIFIER);

		arrpush(enum_->enum_type.items, identifier.string);

		lexer_consume_check(lexer, SEMICOLON);
	}
	lexer_consume_check(lexer, CURLY_BRACE_CLOSED);

	return enum_;
}

static Node *parse_return(Lexer *lexer) {
	Token_Data first_token = lexer_consume(lexer);

	Node *return_ = ast_new(RETURN_NODE, first_token.location);
	return_->return_.value = parse_expression_or_nothing(lexer);

	return return_;
}

static Node *parse_break(Lexer *lexer) {
	Token_Data first_token = lexer_consume(lexer);

	Node *break_ = ast_new(BREAK_NODE, first_token.location);
	break_->break_.value = parse_expression_or_nothing(lexer);

	return break_;
}

static Node *parse_if(Lexer *lexer, bool static_) {
	Token_Data first_token = lexer_consume(lexer);

	Node *if_ = ast_new(IF_NODE, first_token.location);
	if_->if_.static_ = static_;
	if_->if_.condition = parse_expression(lexer);
	if_->if_.bindings = NULL;

	if (lexer_peek(lexer).kind == VERTICAL_BAR) {
		lexer_consume_check(lexer, VERTICAL_BAR);
		while (true) {
			Token_Data identifier = lexer_consume_check(lexer, IDENTIFIER);
			arrpush(if_->if_.bindings, identifier.string);

			Token_Data token = lexer_peek(lexer);
			if (token.kind == COMMA) {
				lexer_consume(lexer);
			} else if (token.kind == VERTICAL_BAR) {
				break;
			} else {
				handle_token_error_no_expected(lexer, token);
			}
		}

		lexer_consume_check(lexer, VERTICAL_BAR);
	}

	if_->if_.if_body = parse_separated_statement(lexer);

	if (lexer_peek_check(lexer, KEYWORD_ELSE)) {
		lexer_consume(lexer);
		if_->if_.else_body = parse_statement(lexer);
	} else {
		if_->if_.else_body = NULL;
	}

	return if_;
}

static Node *parse_while(Lexer *lexer, bool static_) {
	Token_Data first_token = lexer_consume(lexer);
	Node *while_ = ast_new(WHILE_NODE, first_token.location);
	while_->while_.static_id_counter = 0;
	while_->while_.static_ = static_;
	while_->while_.condition = parse_expression(lexer);
	while_->while_.body = parse_separated_statement(lexer);

	if (lexer_peek_check(lexer, KEYWORD_ELSE)) {
		lexer_consume(lexer);
		while_->while_.else_body = parse_statement(lexer);
	} else {
		while_->while_.else_body = NULL;
	}

	return while_;
}

static Node *parse_import(Lexer *lexer) {
	Token_Data first_token = lexer_consume(lexer);

	Node *import = ast_new(IMPORT_NODE, first_token.location);
	import->import.module = parse_expression(lexer);

	return import;
}

static Node *parse_load(Lexer *lexer) {
	Token_Data first_token = lexer_consume(lexer);

	Node *load = ast_new(LOAD_NODE, first_token.location);
	load->load.path = parse_expression(lexer);
	return load;
}

static Node *parse_for(Lexer *lexer, bool static_) {
	Token_Data first_token = lexer_consume(lexer);

	Node *for_ = ast_new(FOR_NODE, first_token.location);
	for_->for_.static_id_counter = 0;
	for_->for_.static_ = static_;

	for_->for_.items = NULL;
	bool has_more = true;
	while (has_more) {
		arrpush(for_->for_.items, parse_expression(lexer));

		if (lexer_peek(lexer).kind == COMMA) {
			has_more = true;
			lexer_consume(lexer);
		} else {
			has_more = false;
		}
	}

	for_->for_.bindings = NULL;
	if (lexer_peek(lexer).kind == VERTICAL_BAR) {
		lexer_consume_check(lexer, VERTICAL_BAR);
		while (true) {
			Token_Data identifier = lexer_consume_check(lexer, IDENTIFIER);
			arrpush(for_->for_.bindings, identifier.string);

			Token_Data token = lexer_peek(lexer);
			if (token.kind == COMMA) {
				lexer_consume(lexer);
			} else if (token.kind == VERTICAL_BAR) {
				break;
			} else {
				handle_token_error_no_expected(lexer, token);
			}
		}
		lexer_consume_check(lexer, VERTICAL_BAR);
	}

	for_->for_.body = parse_separated_statement(lexer);

	return for_;
}

static Node *parse_global(Lexer *lexer) {
	Token_Data first_token = lexer_consume(lexer);

	Node *global = ast_new(GLOBAL_NODE, first_token.location);

	if (lexer_peek_check(lexer, COLON)) {
		lexer_consume(lexer);
		global->global.type = parse_expression(lexer);
	} else {
		global->global.type = NULL;
	}

	if (lexer_peek_check(lexer, KEYWORD_EXTERN)) {
		lexer_consume(lexer);
		global->global.extern_ = lexer_consume_check(lexer, STRING).string;
	} else {
		global->global.extern_ = (String_View) {};
	}

	global->global.value = parse_separated_statement_or_nothing(lexer);

	return global;
}

static Node *parse_switch(Lexer *lexer, bool static_) {
	Token_Data first_token = lexer_consume(lexer);
	Node *switch_ = ast_new(SWITCH_NODE, first_token.location);

	switch_->switch_.static_ = static_;

	switch_->switch_.condition = parse_expression(lexer);

	switch_->switch_.cases = NULL;
	lexer_consume_check(lexer, CURLY_BRACE_OPEN);
	while (lexer_peek_check(lexer, KEYWORD_CASE)) {
		lexer_consume(lexer);
		Node *check = parse_expression(lexer);

		String_View binding = {};
		if (lexer_peek(lexer).kind == VERTICAL_BAR) {
			lexer_consume_check(lexer, VERTICAL_BAR);
			binding = lexer_consume_check(lexer, IDENTIFIER).string;
			lexer_consume_check(lexer, VERTICAL_BAR);
		}

		Switch_Case switch_case = {
			.value = check,
			.body = parse_separated_statement(lexer),
			.binding = binding
		};
		arrpush(switch_->switch_.cases, switch_case);
	}

	lexer_consume_check(lexer, CURLY_BRACE_CLOSED);

	return switch_;
}

static bool needs_semicolon(Node *node) {
	Node_Kind kind = node->kind;
	return !(kind == IF_NODE || kind == WHILE_NODE || kind == FOR_NODE || (kind == DEFINE_NODE && (node->define.expression->kind == FUNCTION_NODE || node->define.expression->kind == STRUCT_TYPE_NODE || node->define.expression->kind == UNION_TYPE_NODE || node->define.expression->kind == TAGGED_UNION_TYPE_NODE || node->define.expression->kind == ENUM_TYPE_NODE)));
}

static Node *parse_block(Lexer *lexer) {
	Token_Data first_token = lexer_consume_check(lexer, CURLY_BRACE_OPEN);

	Node *block = ast_new(BLOCK_NODE, first_token.location);
	block->block.has_result = false;

	block->block.statements = NULL;
	while (lexer_peek(lexer).kind != CURLY_BRACE_CLOSED) {
		Node *statement = parse_statement(lexer);
		arrpush(block->block.statements, statement);
		if (lexer_peek(lexer).kind == CURLY_BRACE_CLOSED) {
			block->block.has_result = true;
		} else {
			if (needs_semicolon(statement)) {
				lexer_consume_check(lexer, SEMICOLON);
			} else if (lexer_peek(lexer).kind == SEMICOLON) {
				lexer_consume(lexer);
			}
		}
	}

	lexer_consume_check(lexer, CURLY_BRACE_CLOSED);

	return block;
}

static Node *parse_pointer(Lexer *lexer) {
	Token_Data first_token = lexer_consume(lexer);

	Node *pointer = ast_new(POINTER_NODE, first_token.location);
	Node *inner = parse_expression_or_nothing(lexer);

	Node *result;
	if (inner != NULL && inner->kind == RESULT_NODE) {
		pointer->pointer_type.inner = inner->result_type.value;
		inner->result_type.value = pointer;
		result = inner;
	} else {
		pointer->pointer_type.inner = inner;
		result = pointer;
	}

	return result;
}

static Node *parse_optional(Lexer *lexer) {
	Token_Data first_token = lexer_consume_check(lexer, QUESTION);

	Node *optional = ast_new(OPTIONAL_NODE, first_token.location);
	optional->optional_type.inner = parse_expression(lexer);

	return optional;
}

static Node *parse_reference(Lexer *lexer) {
	Token_Data first_token = lexer_consume_check(lexer, AMPERSAND);

	Node *reference = ast_new(REFERENCE_NODE, first_token.location);
	reference->reference.node = parse_expression(lexer);

	return reference;
}

static Node *parse_array_or_array_view_type(Lexer *lexer) {
	Token_Data first_token = lexer_consume_check(lexer, BRACE_OPEN);

	Node *size = NULL;
	if (lexer_peek(lexer).kind != BRACE_CLOSED) {
		size = parse_expression(lexer);

		Node *sentinel = NULL;
		if (lexer_peek(lexer).kind == COMMA) {
			lexer_consume(lexer);
			sentinel = parse_expression(lexer);
		}

		lexer_consume_check(lexer, BRACE_CLOSED);

		Node *array_type = ast_new(ARRAY_TYPE_NODE, first_token.location);
		array_type->array_type.size = size;
		array_type->array_type.sentinel = sentinel;
		array_type->array_type.inner = parse_expression(lexer);
		return array_type;
	} else {
		lexer_consume_check(lexer, BRACE_CLOSED);

		Node *array_view_type = ast_new(ARRAY_VIEW_TYPE_NODE, first_token.location);
		array_view_type->array_view_type.inner = parse_expression(lexer);
		return array_view_type;
	}
}

static Node *parse_module_or_module_type(Lexer *lexer) {
	Token_Data first_token = lexer_consume(lexer);

	if (lexer_peek(lexer).kind != CURLY_BRACE_OPEN) {
		return ast_new(MODULE_TYPE_NODE, first_token.location);
	}

	Node *module = ast_new(MODULE_NODE, first_token.location);
	Node *body = parse_expression(lexer);
	module->module.body = body;
	return module;
}

static Node *parse_not(Lexer *lexer) {
	Token_Data first_token = lexer_consume(lexer);

	Node *not = ast_new(NOT_NODE, first_token.location);
	not->not.node = parse_expression(lexer);
	return not;
}

static Node *parse_op(Lexer *lexer) {
	Token_Data first_token = lexer_consume(lexer);

	String_View identifier = {};
	Token_Data token = lexer_consume(lexer);
	switch (token.kind) {
		case PLUS:
			identifier = cstr_to_sv("+");
			break;
		default:
			assert(false);
	}

	return parse_declaration(lexer, false, identifier, first_token.location);
}

static Node *parse_run(Lexer *lexer) {
	Token_Data first_token = lexer_consume(lexer);
	Node *run = ast_new(RUN_NODE, first_token.location);
	run->run.node = parse_expression(lexer);
	return run;
}

static Node *parse_cast(Lexer *lexer) {
	Token_Data first_token = lexer_consume(lexer);
	Node *cast = ast_new(CAST_NODE, first_token.location);
	lexer_consume_check(lexer, PARENTHESIS_OPEN);
	cast->cast.type = parse_expression(lexer);
	lexer_consume_check(lexer, PARENTHESIS_CLOSED);
	cast->cast.node = parse_expression(lexer);
	return cast;
}

static Node *parse_defer(Lexer *lexer) {
	Token_Data first_token = lexer_consume(lexer);
	Node *defer = ast_new(DEFER_NODE, first_token.location);
	defer->defer.node = parse_expression(lexer);
	return defer;
}

static Node *parse_function_or_function_type(Lexer *lexer) {
	Token_Data first_token = lexer_consume(lexer);
	
	Parse_Arguments_Result result = parse_arguments(lexer);
	Function_Argument *arguments = result.arguments;
	bool variadic = result.variadic;

	Node *return_ = NULL;
	if (lexer_peek(lexer).kind == MINUS_ARROW) {
		lexer_consume(lexer);
		return_ = parse_expression(lexer);
	}

	Node *function_type = ast_new(FUNCTION_TYPE_NODE, first_token.location);
	function_type->function_type = (Function_Type_Node) {
		.arguments = arguments,
		.return_ = return_,
		.variadic = variadic
	};

	Node *body = NULL;
	String_View extern_ = {};
	if (lexer_peek_check(lexer, KEYWORD_EXTERN)) {
		lexer_consume(lexer);
		extern_ = lexer_consume_check(lexer, STRING).string;
	} else {
		body = parse_separated_statement_or_nothing(lexer);
	}

	if (body != NULL || extern_.ptr != NULL) {
		Node *function = ast_new(FUNCTION_NODE, first_token.location);
		function->function.function_type = function_type;
		function->function.body = body;
		function->function.extern_ = extern_;
		function->function.static_id_counter = 0;
		return function;
	} else {
		return function_type;
	}
}

static Node *parse_statement(Lexer *lexer) {
	Node *result = parse_expression(lexer);

	bool operating = true;
	while (operating) {
		Token_Data next = lexer_peek(lexer);
		switch (next.kind) {
			case DOLLAR:
				lexer_consume(lexer);
				result = parse_assign(lexer, result, true);
				break;
			case EQUALS:
				result = parse_assign(lexer, result, false);
				break;
			default:
				operating = false;
				break;
		}
	}

	return result;
}

static Node *parse_expression(Lexer *lexer) {
	Token_Data token = lexer_peek(lexer);
	Node *result = NULL;
	switch (token.kind) {
		case STRING: {
			result = parse_string(lexer);
			break;
		}
		case CHARACTER: {
			result = parse_character(lexer);
			break;
		}
		case INTEGER:
		case DECIMAL: {
			result = parse_number(lexer);
			break;
		}
		case DOLLAR: {
			lexer_consume(lexer);

			token = lexer_peek(lexer);
			switch (token.kind) {
				case IDENTIFIER:
					result = parse_identifier(lexer, true);
					break;
				case KEYWORD_FOR:
					result = parse_for(lexer, true);
					break;
				case KEYWORD_IF:
					result = parse_if(lexer, true);
					break;
				case KEYWORD_SWITCH:
					result = parse_switch(lexer, true);
					break;
				case KEYWORD_WHILE:
					result = parse_while(lexer, true);
					break;
				default:
					handle_token_error_no_expected(lexer, token);
			}
			break;
		}
		case PERIOD_CURLY_BRACE_OPEN: {
			result = parse_structure(lexer);
			break;
		}
		case IDENTIFIER: {
			result = parse_identifier(lexer, false);
			break;
		}
		case KEYWORD_BREAK: {
			result = parse_break(lexer);
			break;
		}
		case KEYWORD_CAST: {
			result = parse_cast(lexer);
			break;
		}
		case KEYWORD_DEFER: {
			result = parse_defer(lexer);
			break;
		}
		case KEYWORD_ENUM: {
			result = parse_enum_type(lexer);
			break;
		}
		case KEYWORD_FN: {
			result = parse_function_or_function_type(lexer);
			break;
		}
		case KEYWORD_FOR: {
			result = parse_for(lexer, false);
			break;
		}
		case KEYWORD_GLOBAL: {
			result = parse_global(lexer);
			break;
		}
		case KEYWORD_IF: {
			result = parse_if(lexer, false);
			break;
		}
		case KEYWORD_IMPORT: {
			result = parse_import(lexer);
			break;
		}
		case KEYWORD_LOAD: {
			result = parse_load(lexer);
			break;
		}
		case KEYWORD_MOD: {
			result = parse_module_or_module_type(lexer);
			break;
		}
		case KEYWORD_NOT: {
			result = parse_not(lexer);
			break;
		}
		case KEYWORD_OP: {
			result = parse_op(lexer);
			break;
		}
		case KEYWORD_RETURN: {
			result = parse_return(lexer);
			break;
		}
		case KEYWORD_RUN: {
			result = parse_run(lexer);
			break;
		}
		case KEYWORD_STRUCT: {
			result = parse_struct_type(lexer);
			break;
		}
		case KEYWORD_SWITCH: {
			result = parse_switch(lexer, false);
			break;
		}
		case KEYWORD_TAGGED_UNION: {
			result = parse_tagged_union_type(lexer);
			break;
		}
		case KEYWORD_UNION: {
			result = parse_union_type(lexer);
			break;
		}
		case KEYWORD_WHILE: {
			result = parse_while(lexer, false);
			break;
		}
		case CURLY_BRACE_OPEN: {
			result = parse_block(lexer);
			break;
		}
		case ASTERISK: {
			result = parse_pointer(lexer);
			break;
		}
		case QUESTION: {
			result = parse_optional(lexer);
			break;
		}
		case AMPERSAND: {
			result = parse_reference(lexer);
			break;
		}
		case BRACE_OPEN: {
			result = parse_array_or_array_view_type(lexer);
			break;
		}
		case EXCLAMATION: {
			result = parse_result(lexer, NULL);
			break;
		}
		case PARENTHESIS_OPEN: {
			result = parse_parenthesis(lexer);
			break;
		}
		default:
			handle_token_error_no_expected(lexer, token);
	}

	bool operating = true;
	while (operating) {
		Token_Data next = lexer_peek(lexer);
		switch (next.kind) {
			case PARENTHESIS_OPEN:
				result = parse_call(lexer, result);
				break;
			case EQUALS_EQUALS:
			case EXCLAMATION_EQUALS:
			case LESS:
			case LESS_EQUALS:
			case GREATER:
			case GREATER_EQUALS:
			case PLUS:
			case MINUS:
			case ASTERISK:
			case SLASH:
			case PERCENT:
			case KEYWORD_AND:
			case KEYWORD_OR:
				result = parse_binary_operator(lexer, result);
				break;
			case PERIOD:
				result = parse_structure_access(lexer, result);
				break;
			case BRACE_OPEN:
				result = parse_array_access_or_slice(lexer, result);
				break;
			case EXCLAMATION:
				result = parse_result(lexer, result);
				break;
			case PERIOD_PERIOD:
				result = parse_range(lexer, result);
				break;
			case KEYWORD_CATCH:
				result = parse_catch(lexer, result);
				break;
			case KEYWORD_IS:
				result = parse_is(lexer, result);
				break;
			default:
				operating = false;
				break;
		}
	}

	return result;
}

Node *parse_source_statement(Data *data, char *source, size_t length, size_t path_ref, size_t row, size_t column) {
	(void) data;
	Lexer lexer = lexer_create(source, length, path_ref);
	lexer.constant_row_column = true;
	lexer.row = row;
	lexer.column = column + 1;
	return parse_statement(&lexer);
}

Node *parse_source(Data *data, char *source, size_t length, char *path) {
	Lexer lexer = lexer_create(source, length, arrlen(data->source_files));
	arrpush(data->source_files, path);
	lexer.path = path;

	Node *root = ast_new(ROOT_NODE, (Source_Location) {});
	root->root.statements = NULL;
	while (lexer_peek(&lexer).kind != END_OF_FILE) {
		Node *expression = parse_expression(&lexer);
		arrpush(root->root.statements, expression);

		if (needs_semicolon(expression)) {
			lexer_consume_check(&lexer, SEMICOLON);
		} else if (lexer_peek(&lexer).kind == SEMICOLON) {
			lexer_consume(&lexer);
		}
	}

	return root;
}

Node *parse_file(Data *data, char *path) {
	FILE *file = fopen(path, "r");
	if (!file) {
		printf("Failed to open path '%s'\n", path);
		exit(1);
	}

	fseek(file, 0, SEEK_END);
	long length = ftell(file);
	fseek(file, 0, SEEK_SET);

	char *contents = malloc(length);
	fread(contents, length, 1, file);

	fclose(file);

	return parse_source(data, contents, length, path);
}
