#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "stb/ds.h"

#include "ast.h"
#include "lexer.h"
#include "parser.h"
#include "util.h"

static void handle_token_error(Token_Kind expected, Token_Data actual) {
	printf("%s:%zu:%zu: Unexpected token '%s', expected '%s'\n", actual.location.path, actual.location.row, actual.location.column, token_to_string(actual.kind), token_to_string(expected));
	exit(1);
}

static void handle_token_error_no_expected(Token_Data actual) {
	printf("%s:%zu:%zu: Unexpected token '%s'\n", actual.location.path, actual.location.row, actual.location.column, token_to_string(actual.kind));
	exit(1);
}

static Token_Data lexer_peek(Lexer *lexer) {
	return lexer_next(lexer, false);
}

static Token_Data lexer_consume(Lexer *lexer) {
	return lexer_next(lexer, true);
}

static bool lexer_peek_check_keyword(Lexer *lexer, char *keyword) {
	Token_Data next = lexer_peek(lexer);
	return next.kind == KEYWORD && streq(next.string, keyword);
}

static Token_Data lexer_consume_check(Lexer *lexer, Token_Kind kind) {
	Token_Data data = lexer_next(lexer, true);
	if (data.kind != kind) {
		handle_token_error(kind, data);
	}

	return data;
}

static Node *parse_expression(Lexer *lexer);
static Node *parse_statement(Lexer *lexer);

static Node *parse_expression_or_nothing(Lexer *lexer) {
	Token_Data token = lexer_peek(lexer);
	if (token.kind == CURLY_BRACE_CLOSED || token.kind == PARENTHESIS_CLOSED || token.kind == SEMICOLON || token.kind == COMMA|| token.kind == EQUALS || lexer_peek_check_keyword(lexer, "extern")) {
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
				handle_token_error_no_expected(token);
			}
		}
	}

	lexer_consume_check(lexer, CURLY_BRACE_CLOSED);

	return structure;
}

static Node *parse_identifier(Lexer *lexer, Node *module) {
	Token_Data token = lexer_consume_check(lexer, IDENTIFIER);

	switch (token.string[0]) {
		case 'n':
			if (streq(token.string, "null")) {
				return ast_new(NULL_NODE, token.location);
			}
			break;
		case 't':
			if (streq(token.string, "true")) {
				Node *boolean = ast_new(BOOLEAN_NODE, token.location);
				boolean->boolean.value = true;
				return boolean;
			} else if (streq(token.string, "type")) {
				Node *internal = ast_new(INTERNAL_NODE, token.location);
				internal->internal.kind = INTERNAL_TYPE;
				return internal;
			} else if (streq(token.string, "type_of")) {
				Node *internal = ast_new(INTERNAL_NODE, token.location);
				internal->internal.kind = INTERNAL_TYPE_OF;

				lexer_consume_check(lexer, PARENTHESIS_OPEN);
				arrpush(internal->internal.inputs, parse_expression(lexer));
				lexer_consume_check(lexer, PARENTHESIS_CLOSED);

				return internal;
			}
			break;
		case 'f':
			if (streq(token.string, "false")) {
				Node *boolean = ast_new(BOOLEAN_NODE, token.location);
				boolean->boolean.value = false;
				return boolean;
			} else if (streq(token.string, "flt64")) {
				Node *internal = ast_new(INTERNAL_NODE, token.location);
				internal->internal.kind = INTERNAL_FLT64;
				return internal;
			}
			break;
		case 'u':
			if (streq(token.string, "uint")) {
				Node *internal = ast_new(INTERNAL_NODE, token.location);
				internal->internal.kind = INTERNAL_UINT;
				return internal;
			} else if (streq(token.string, "uint8")) {
				Node *internal = ast_new(INTERNAL_NODE, token.location);
				internal->internal.kind = INTERNAL_UINT8;
				return internal;
			}
			break;
		case 'b':
			if (streq(token.string, "bool")) {
				Node *internal = ast_new(INTERNAL_NODE, token.location);
				internal->internal.kind = INTERNAL_BOOL;
				return internal;
			} else if (streq(token.string, "byte")) {
				Node *internal = ast_new(INTERNAL_NODE, token.location);
				internal->internal.kind = INTERNAL_BYTE;
				return internal;
			}
			break;
		case 'C':
			if (streq(token.string, "C_CHAR_SIZE")) {
				Node *internal = ast_new(INTERNAL_NODE, token.location);
				internal->internal.kind = INTERNAL_C_CHAR_SIZE;
				return internal;
			} else if (streq(token.string, "C_SHORT_SIZE")) {
				Node *internal = ast_new(INTERNAL_NODE, token.location);
				internal->internal.kind = INTERNAL_C_SHORT_SIZE;
				return internal;
			} else if (streq(token.string, "C_INT_SIZE")) {
				Node *internal = ast_new(INTERNAL_NODE, token.location);
				internal->internal.kind = INTERNAL_C_INT_SIZE;
				return internal;
			} else if (streq(token.string, "C_LONG_SIZE")) {
				Node *internal = ast_new(INTERNAL_NODE, token.location);
				internal->internal.kind = INTERNAL_C_LONG_SIZE;
				return internal;
			}
			break;
		case 'i':
			if (streq(token.string, "int")) {
				if (lexer_peek(lexer).kind == PARENTHESIS_OPEN) {
					Node *internal = ast_new(INTERNAL_NODE, token.location);
					internal->internal.kind = INTERNAL_INT;

					lexer_consume_check(lexer, PARENTHESIS_OPEN);
					arrpush(internal->internal.inputs, parse_expression(lexer));
					lexer_consume_check(lexer, COMMA);
					arrpush(internal->internal.inputs, parse_expression(lexer));
					lexer_consume_check(lexer, PARENTHESIS_CLOSED);

					return internal;
				}
			}
			break;
		case 'e':
			if (streq(token.string, "embed")) {
				Node *internal = ast_new(INTERNAL_NODE, token.location);
				internal->internal.kind = INTERNAL_EMBED;

				lexer_consume_check(lexer, PARENTHESIS_OPEN);
				while (lexer_peek(lexer).kind != PARENTHESIS_CLOSED) {
					arrpush(internal->internal.inputs, parse_expression(lexer));
					if (lexer_peek(lexer).kind == COMMA) {
						lexer_consume(lexer);
					}
				}
				lexer_consume(lexer);

				return internal;
			}
			break;
			// case 'p':
			//     if (streq(token.string, "print")) {
			//         Node *internal = ast_new(INTERNAL_NODE, token.location);
			//         internal->internal.kind = INTERNAL_PRINT;

			//         lexer_consume_check(lexer, OPEN_PARENTHESIS);
			//         arrpush(internal->internal.inputs, parse_expression(lexer));
			//         lexer_consume_check(lexer, CLOSED_PARENTHESIS);

			//         return internal;
			//     }
			//     break;
		default:
			break;
	}

	Node *identifier = ast_new(IDENTIFIER_NODE, token.location);
	identifier->identifier.module = module;
	identifier->identifier.value = token.string;

	return identifier;
}

static Node *parse_dereference(Lexer *lexer, Node *node) {
	Token_Data token = lexer_consume_check(lexer, CARET);

	Node *derefence = ast_new(DEREFERENCE_NODE, token.location);
	derefence->dereference.node = node;
	return derefence;
}

static Node *parse_deoptional(Lexer *lexer, Node *node) {
	Token_Data token = lexer_consume_check(lexer, QUESTION);

	Node *deoptional = ast_new(DEOPTIONAL_NODE, token.location);
	deoptional->deoptional.node = node;
	return deoptional;
}

static Node *parse_result(Lexer *lexer, Node *node) {
	Token_Data token = lexer_consume(lexer);

	Node *result = ast_new(RESULT_NODE, token.location);
	result->result_type.value = node;
	result->result_type.error = parse_expression(lexer);
	return result;
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

	if (lexer_peek(lexer).kind != PARENTHESIS_CLOSED) {
		while (true) {
			arrpush(call->call.arguments, parse_expression(lexer));

			Token_Data token = lexer_peek(lexer);
			if (token.kind == COMMA) {
				lexer_consume(lexer);
			} else if (token.kind == PARENTHESIS_CLOSED) {
				break;
			} else {
				handle_token_error_no_expected(token);
			}
		}
	}

	lexer_consume_check(lexer, PARENTHESIS_CLOSED);

	return call;
}

static size_t get_precedence(Binary_Op_Node_Kind kind) {
	switch (kind) {
		case OP_ADD:
		case OP_SUBTRACT:
			return 1;
		case OP_MULTIPLY:
		case OP_DIVIDE:
			return 2;
		case OP_EQUALS:
		case OP_LESS:
		case OP_GREATER:
		case OP_LESS_EQUALS:
		case OP_GREATER_EQUALS:
			return 0;
		default:
			assert(false);
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

	Node *structure_access = ast_new(STRUCTURE_ACCESS_NODE, first_token.location);
	structure_access->structure_access.parent = structure;
	structure_access->structure_access.name = lexer_consume_check(lexer, IDENTIFIER).string;

	return structure_access;
}

static Node *parse_array_access(Lexer *lexer, Node *array) {
	Token_Data first_token = lexer_consume(lexer);

	Node *array_access = ast_new(ARRAY_ACCESS_NODE, first_token.location);
	array_access->array_access.parent = array;
	array_access->array_access.index = parse_expression(lexer);

	lexer_consume_check(lexer, BRACE_CLOSED);
	return array_access;
}

static Node *parse_call_method(Lexer *lexer, Node *argument1) {
	Token_Data first_token = lexer_consume(lexer);

	Node *call_method = ast_new(CALL_METHOD_NODE, first_token.location);
	call_method->call_method.argument1 = argument1;
	call_method->call_method.method = lexer_consume_check(lexer, IDENTIFIER).string;

	lexer_consume_check(lexer, PARENTHESIS_OPEN);
	if (lexer_peek(lexer).kind != PARENTHESIS_CLOSED) {
		while (true) {
			arrpush(call_method->call_method.arguments, parse_expression(lexer));

			Token_Data token = lexer_peek(lexer);
			if (token.kind == COMMA) {
				lexer_consume(lexer);
			} else if (token.kind == PARENTHESIS_CLOSED) {
				break;
			} else {
				handle_token_error_no_expected(token);
			}
		}
	}
	lexer_consume_check(lexer, PARENTHESIS_CLOSED);

	return call_method;
}

static Node *parse_assign(Lexer *lexer, Node *structure) {
	Token_Data first_token = lexer_consume(lexer);

	Node *assign = ast_new(ASSIGN_NODE, first_token.location);
	assign->assign.target = structure;
	assign->assign.value = parse_expression(lexer);

	return assign;
}

static char *parse_operator(Lexer *lexer) {
	Token_Data operator_token = lexer_consume(lexer);
	switch (operator_token.kind) {
		case IDENTIFIER:
			return operator_token.string;
			break;
		case BRACE_OPEN:
			lexer_consume_check(lexer, BRACE_CLOSED);
			return "[]";
			break;
		default:
			assert(false);
	}
}

static Node *parse_struct_type(Lexer *lexer) {
	Token_Data first_token = lexer_consume_check(lexer, KEYWORD);

	Node *struct_ = ast_new(STRUCT_TYPE_NODE, first_token.location);

	lexer_consume_check(lexer, CURLY_BRACE_OPEN);
	if (lexer_peek(lexer).kind != CURLY_BRACE_CLOSED && lexer_peek(lexer).kind != KEYWORD && !lexer_peek_check_keyword(lexer, "op")) {
		while (true) {
			Token_Data identifier = lexer_consume_check(lexer, IDENTIFIER);
			lexer_consume_check(lexer, COLON);
			Node *type = parse_expression(lexer);

			Structure_Member member = {
				.name = identifier.string,
				.type = type
			};
			arrpush(struct_->struct_type.members, member);

			Token_Data token = lexer_peek(lexer);
			if (token.kind == COMMA) {
				lexer_consume(lexer);
			} else if (token.kind == SEMICOLON) {
				lexer_consume(lexer);
				break;
			} else if (token.kind == CURLY_BRACE_CLOSED) {
				break;
			} else {
				handle_token_error_no_expected(token);
			}
		}
	}

	if (lexer_peek_check_keyword(lexer, "op")) {
		while (lexer_peek(lexer).kind != CURLY_BRACE_CLOSED) {
			lexer_consume_check(lexer, KEYWORD);

			char *operator = parse_operator(lexer);

	 		lexer_consume_check(lexer, EQUALS);

	 		Node *function = parse_expression(lexer);

			Operator_Overload operator_definition = {
	 			.name = operator,
	 			.function = function
	 		};
			arrpush(struct_->struct_type.operator_overloads, operator_definition);

			lexer_consume_check(lexer, SEMICOLON);
	 	}
	}

	lexer_consume_check(lexer, CURLY_BRACE_CLOSED);

	return struct_;
}

static Node *parse_union_type(Lexer *lexer) {
	Token_Data first_token = lexer_consume_check(lexer, KEYWORD);

	Node *union_ = ast_new(UNION_TYPE_NODE, first_token.location);

	lexer_consume_check(lexer, CURLY_BRACE_OPEN);
	if (lexer_peek(lexer).kind != CURLY_BRACE_CLOSED) {
		while (true) {
			char *name = lexer_consume_check(lexer, IDENTIFIER).string;
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
				handle_token_error_no_expected(token);
			}
		}
	}
	lexer_consume_check(lexer, CURLY_BRACE_CLOSED);

	return union_;
}

static Node *parse_tagged_union_type(Lexer *lexer) {
	Token_Data first_token = lexer_consume_check(lexer, KEYWORD);

	Node *tagged_union = ast_new(TAGGED_UNION_TYPE_NODE, first_token.location);

	lexer_consume_check(lexer, CURLY_BRACE_OPEN);
	if (lexer_peek(lexer).kind != CURLY_BRACE_CLOSED) {
		while (true) {
			char *name = lexer_consume_check(lexer, IDENTIFIER).string;
			lexer_consume_check(lexer, COLON);
			Node *type = parse_expression(lexer);

			Structure_Member member = {
				.name = name,
				.type = type
			};
			arrpush(tagged_union->tagged_union_type.members, member);

			Token_Data token = lexer_peek(lexer);
			if (token.kind == COMMA) {
				lexer_consume(lexer);
			} else if (token.kind == CURLY_BRACE_CLOSED) {
				break;
			} else {
				handle_token_error_no_expected(token);
			}
		}
	}
	lexer_consume_check(lexer, CURLY_BRACE_CLOSED);

	return tagged_union;
}

static Node *parse_enum_type(Lexer *lexer) {
	Token_Data first_token = lexer_consume_check(lexer, KEYWORD);

	Node *enum_ = ast_new(ENUM_TYPE_NODE, first_token.location);

	lexer_consume_check(lexer, CURLY_BRACE_OPEN);
	if (lexer_peek(lexer).kind != CURLY_BRACE_CLOSED) {
		while (true) {
			Token_Data identifier = lexer_consume_check(lexer, IDENTIFIER);

			arrpush(enum_->enum_type.items, identifier.string);

			Token_Data token = lexer_peek(lexer);
			if (token.kind == COMMA) {
				lexer_consume(lexer);
			} else if (token.kind == CURLY_BRACE_CLOSED) {
				break;
			} else {
				handle_token_error_no_expected(token);
			}
		}
	}
	lexer_consume_check(lexer, CURLY_BRACE_CLOSED);

	return enum_;
}

static Node *parse_extern(Lexer *lexer) {
	Token_Data first_token = lexer_consume_check(lexer, KEYWORD);

	Node *extern_ = ast_new(EXTERN_NODE, first_token.location);
	extern_->extern_.name = lexer_consume_check(lexer, STRING).string;

	return extern_;
}

static Node *parse_define(Lexer *lexer) {
	Token_Data first_token = lexer_consume_check(lexer, KEYWORD);

	Token_Data identifier = lexer_consume_check(lexer, IDENTIFIER);

	Node *define = ast_new(DEFINE_NODE, first_token.location);

	if (lexer_peek(lexer).kind == COLON) {
		lexer_consume_check(lexer, COLON);
		define->define.type = parse_expression(lexer);
	}

	lexer_consume_check(lexer, EQUALS);

	define->define.identifier = identifier.string;
	define->define.expression = parse_expression(lexer);

	return define;
}

static Node *parse_return(Lexer *lexer) {
	Token_Data first_token = lexer_consume_check(lexer, KEYWORD);

	Node *return_ = ast_new(RETURN_NODE, first_token.location);
	return_->return_.type = RETURN_STANDARD;
	if (lexer_peek(lexer).kind == EXCLAMATION) {
		return_->return_.type = RETURN_SUCCESS;

		lexer_consume(lexer);

		if (lexer_peek(lexer).kind == EXCLAMATION) {
			return_->return_.type = RETURN_ERROR;

			lexer_consume(lexer);
		}
	}

	return_->return_.value = parse_expression_or_nothing(lexer);

	return return_;
}

static Node *parse_variable(Lexer *lexer) {
	Token_Data first_token = lexer_consume_check(lexer, KEYWORD);

	Node *variable = ast_new(VARIABLE_NODE, first_token.location);

	if (lexer_peek_check_keyword(lexer, "static")) {
		lexer_consume(lexer);
		variable->variable.static_ = true;
	}

	variable->variable.name = lexer_consume_check(lexer, IDENTIFIER).string;

	if (lexer_peek(lexer).kind == COLON) {
		lexer_consume_check(lexer, COLON);
		variable->variable.type = parse_expression(lexer);
	}

	if (lexer_peek(lexer).kind == EQUALS) {
		lexer_consume_check(lexer, EQUALS);
		variable->variable.value = parse_expression(lexer);
	}

	return variable;
}

static Node *parse_break(Lexer *lexer) {
	Token_Data first_token = lexer_consume_check(lexer, KEYWORD);

	Node *break_ = ast_new(BREAK_NODE, first_token.location);
	break_->break_.value = parse_expression_or_nothing(lexer);

	return break_;
}

static Node *parse_if(Lexer *lexer) {
	Token_Data first_token = lexer_consume_check(lexer, KEYWORD);

	Node *if_ = ast_new(IF_NODE, first_token.location);

	if (lexer_peek_check_keyword(lexer, "static")) {
		lexer_consume_check(lexer, KEYWORD);
		if_->if_.static_ = true;
	}

	if_->if_.condition = parse_expression(lexer);

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
				handle_token_error_no_expected(token);
			}
		}

		lexer_consume_check(lexer, VERTICAL_BAR);
	}

	if_->if_.if_body = parse_separated_statement(lexer);

	if (lexer_peek_check_keyword(lexer, "else")) {
		lexer_consume_check(lexer, KEYWORD);
		if_->if_.else_body = parse_statement(lexer);
	}
	return if_;
}

static Node *parse_internal(Lexer *lexer) {
	Token_Data first_token = lexer_consume_check(lexer, KEYWORD);

	Node *internal = ast_new(INTERNAL_NODE, first_token.location);
	internal->internal.kind = INTERNAL_EXPRESSION;

	arrpush(internal->internal.inputs, parse_expression(lexer));

	return internal;
}

static Node *parse_while(Lexer *lexer) {
	Token_Data first_token = lexer_consume_check(lexer, KEYWORD);
	Node *while_ = ast_new(WHILE_NODE, first_token.location);

	while_->while_.condition = parse_expression(lexer);
	while_->while_.body = parse_separated_statement(lexer);

	if (lexer_peek_check_keyword(lexer, "else")) {
		lexer_consume_check(lexer, KEYWORD);
		while_->while_.else_body = parse_statement(lexer);
	}

	return while_;
}

static Node *parse_for(Lexer *lexer) {
	Token_Data first_token = lexer_consume_check(lexer, KEYWORD);

	Node *for_ = ast_new(FOR_NODE, first_token.location);

	if (lexer_peek_check_keyword(lexer, "static")) {
		lexer_consume(lexer);
		for_->for_.static_ = true;
	}

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
			handle_token_error_no_expected(token);
		}
	}
	lexer_consume_check(lexer, VERTICAL_BAR);

	for_->for_.body = parse_separated_statement(lexer);

	return for_;
}

static Node *parse_switch(Lexer *lexer) {
	Token_Data first_token = lexer_consume_check(lexer, KEYWORD);
	Node *switch_ = ast_new(SWITCH_NODE, first_token.location);

	if (lexer_peek_check_keyword(lexer, "static")) {
		lexer_consume_check(lexer, KEYWORD);
		switch_->switch_.static_ = true;
	}

	switch_->switch_.condition = parse_expression(lexer);

	lexer_consume_check(lexer, CURLY_BRACE_OPEN);

	while (lexer_peek_check_keyword(lexer, "case")) {
		lexer_consume_check(lexer, KEYWORD);
		Node *check = parse_expression(lexer);

		char *binding = NULL;
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

static Node *parse_block(Lexer *lexer) {
	Token_Data first_token = lexer_consume_check(lexer, CURLY_BRACE_OPEN);

	Node *block = ast_new(BLOCK_NODE, first_token.location);

	while (lexer_peek(lexer).kind != CURLY_BRACE_CLOSED) {
		arrpush(block->block.statements, parse_statement(lexer));
		if (lexer_peek(lexer).kind == CURLY_BRACE_CLOSED) {
			block->block.has_result = true;
		} else {
			lexer_consume_check(lexer, SEMICOLON);
		}
	}

	lexer_consume_check(lexer, CURLY_BRACE_CLOSED);

	return block;
}

static Node *parse_pointer(Lexer *lexer) {
	Token_Data first_token = lexer_consume_check(lexer, CARET);

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
		lexer_consume_check(lexer, BRACE_CLOSED);

		Node *array_type = ast_new(ARRAY_TYPE_NODE, first_token.location);
		array_type->array_type.size = size;
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
	Token_Data first_token = lexer_consume_check(lexer, KEYWORD);

	if (lexer_peek(lexer).kind != CURLY_BRACE_OPEN) {
		return ast_new(MODULE_TYPE_NODE, first_token.location);
	}

	Node *module = ast_new(MODULE_NODE, first_token.location);
	Node *body = parse_expression(lexer);
	module->module.body = body;
	return module;
}

static Node *parse_run(Lexer *lexer) {
	Token_Data first_token = lexer_consume_check(lexer, KEYWORD);
	Node *run = ast_new(RUN_NODE, first_token.location);
	run->run.node = parse_statement(lexer);
	return run;
}

static Node *parse_cast(Lexer *lexer) {
	Token_Data first_token = lexer_consume_check(lexer, KEYWORD);
	Node *cast = ast_new(CAST_NODE, first_token.location);
	if (lexer_peek(lexer).kind == PARENTHESIS_OPEN) {
		lexer_consume(lexer);
		cast->cast.type = parse_expression(lexer);
		lexer_consume_check(lexer, PARENTHESIS_CLOSED);
	}
	cast->cast.node = parse_expression(lexer);
	return cast;
}

static Node *parse_defer(Lexer *lexer) {
	Token_Data first_token = lexer_consume_check(lexer, KEYWORD);
	Node *defer = ast_new(DEFER_NODE, first_token.location);
	defer->defer.node = parse_expression(lexer);
	return defer;
}

static Node *parse_function_or_function_type(Lexer *lexer) {
	Token_Data first_token = lexer_consume_check(lexer, KEYWORD);
	
	Function_Argument *arguments = NULL;

	if (lexer_peek(lexer).kind == BRACE_OPEN) {
		lexer_consume(lexer);

		if (lexer_peek(lexer).kind != BRACE_CLOSED) {
			while (true) {
				Token_Data identifier = lexer_consume_check(lexer, IDENTIFIER);

				Function_Argument argument = {
					.identifier = identifier.string,
					.static_ = true,
					.inferred = true
				};

				if (lexer_peek(lexer).kind == EQUALS) {
					lexer_consume(lexer);
					argument.default_value = parse_expression(lexer);
				}

				arrpush(arguments, argument);

				Token_Data token = lexer_peek(lexer);
				if (token.kind == COMMA) {
					lexer_consume(lexer);
				} else if (token.kind == BRACE_CLOSED) {
					break;
				} else {
					handle_token_error_no_expected(token);
				}
			}
		}

		lexer_consume(lexer);
	}

	lexer_consume_check(lexer, PARENTHESIS_OPEN);

	bool variadic = false;
	if (lexer_peek(lexer).kind != PARENTHESIS_CLOSED) {
		while (true) {
			if (lexer_peek(lexer).kind == PERIOD_PERIOD) {
				lexer_consume(lexer);
				variadic = true;
			} else {
				bool static_ = false;
				if (lexer_peek_check_keyword(lexer, "static")) {
					lexer_consume_check(lexer, KEYWORD);
					static_ = true;
				}

				Token_Data identifier = lexer_consume_check(lexer, IDENTIFIER);
				lexer_consume_check(lexer, COLON);
				Node *type = parse_expression(lexer);

				Function_Argument argument = {
					.identifier = identifier.string,
					.type = type,
					.static_ = static_
				};
				arrpush(arguments, argument);
			}

			Token_Data token = lexer_peek(lexer);
			if (token.kind == COMMA) {
				lexer_consume(lexer);
			} else if (token.kind == PARENTHESIS_CLOSED) {
				break;
			} else {
				handle_token_error_no_expected(token);
			}
		}
	}

	lexer_consume_check(lexer, PARENTHESIS_CLOSED);

	Node *return_ = NULL;
	if (lexer_peek(lexer).kind == COLON) {
		lexer_consume_check(lexer, COLON);
		return_ = parse_expression(lexer);
	}

	Node *function_type = ast_new(FUNCTION_TYPE_NODE, first_token.location);
	function_type->function_type = (Function_Type_Node) {
		.arguments = arguments,
		.return_ = return_,
		.variadic = variadic
	};

	Node *body = parse_separated_statement_or_nothing(lexer);

	if (body != NULL) {
		Node *function = ast_new(FUNCTION_NODE, first_token.location);
		function->function.function_type = function_type;
		function->function.body = body;
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
			case EQUALS:
				result = parse_assign(lexer, result);
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
		case PERIOD_CURLY_BRACE_OPEN: {
			result = parse_structure(lexer);
			break;
		}
		case IDENTIFIER: {
			result = parse_identifier(lexer, NULL);
			break;
		}
		case KEYWORD: {
			char *value = token.string;
			switch (value[0]) {
				case 'b':
					if (streq(value, "break")) result = parse_break(lexer);
					break;
				case 'c':
					if (streq(value, "cast")) result = parse_cast(lexer);
					break;
				case 'd':
					if (streq(value, "def")) result = parse_define(lexer);
					else if (streq(value, "defer")) result = parse_defer(lexer);
					break;
				case 'e':
					if (streq(value, "enum")) result = parse_enum_type(lexer);
					else if (streq(value, "extern")) result = parse_extern(lexer);
					break;
				case 'f':
					if (streq(value, "fn")) result = parse_function_or_function_type(lexer);
					else if (streq(value, "for")) result = parse_for(lexer);
					break;
				case 'i':
					if (streq(value, "if")) result = parse_if(lexer);
					else if (streq(value, "internal")) result = parse_internal(lexer);
					break;
				case 'm':
					if (streq(value, "mod")) result = parse_module_or_module_type(lexer);
					break;
				case 'r':
					if (streq(value, "run")) result = parse_run(lexer);
					else if (streq(value, "return")) result = parse_return(lexer);
					break;
				case 's':
					if (streq(value, "struct")) result = parse_struct_type(lexer);
					else if (streq(value, "switch")) result = parse_switch(lexer);
					break;
				case 't':
					if (streq(value, "tagged_union")) result = parse_tagged_union_type(lexer);
					break;
				case 'u':
					if (streq(value, "union")) result = parse_union_type(lexer);
					break;
				case 'v':
					if (streq(value, "var")) result = parse_variable(lexer);
					break;
				case 'w':
					if (streq(value, "while")) result = parse_while(lexer);
					break;
			}

			if (result == NULL) {
				printf("Unhandled keyword '%s'\n", value);
				exit(1);
			}
			break;
		}
		case CURLY_BRACE_OPEN: {
			result = parse_block(lexer);
			break;
		}
		case CARET: {
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
		default:
			handle_token_error_no_expected(token);
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
				result = parse_binary_operator(lexer, result);
				break;
			case PERIOD:
				result = parse_structure_access(lexer, result);
				break;
			case BRACE_OPEN:
				result = parse_array_access(lexer, result);
				break;
			case COLON:
				result = parse_call_method(lexer, result);
				break;
			case COLON_COLON:
				lexer_consume_check(lexer, COLON_COLON);
				result = parse_identifier(lexer, result);
				break;
			case CARET:
				result = parse_dereference(lexer, result);
				break;
			case QUESTION:
				result = parse_deoptional(lexer, result);
				break;
			case EXCLAMATION:
				result = parse_result(lexer, result);
				break;
			case KEYWORD:
				if (streq(next.string, "is")) {
					result = parse_is(lexer, result);
				} else if (streq(next.string, "catch")) {
					result = parse_catch(lexer, result);
				} else {
					operating = false;
				}
				break;
			default:
				operating = false;
				break;
		}
	}

	return result;
}

Node *parse_source_expr(char *source, size_t length, char *path) {
	Lexer lexer = lexer_create(path, source, length);
	return parse_expression(&lexer);
}

Node *parse_source(char *source, size_t length, char *path) {
	Lexer lexer = lexer_create(path, source, length);

	Node *root = ast_new(MODULE_NODE, (Source_Location) {});
	Node *block = ast_new(BLOCK_NODE, (Source_Location) {});
	while (lexer_peek(&lexer).kind != END_OF_FILE) {
		arrpush(block->block.statements, parse_expression(&lexer));
		lexer_consume_check(&lexer, SEMICOLON);
	}
	root->module.body = block;

	return root;
}

Node *parse_file(char *path) {
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

	return parse_source(contents, length, path);
}
