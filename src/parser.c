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
	if (token.kind == CURLY_BRACE_CLOSED || token.kind == PARENTHESIS_CLOSED || token.kind == SEMICOLON || token.kind == COMMA || token.kind == EQUALS || token.kind == VERTICAL_BAR || token.kind == KEYWORD_EXTERN) {
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
				handle_token_error_no_expected(lexer, token);
			}
		}
	}

	lexer_consume_check(lexer, CURLY_BRACE_CLOSED);

	return structure;
}

static Node *parse_identifier(Lexer *lexer, Node *module) {
	Token_Data token = lexer_consume_check(lexer, IDENTIFIER);

	if (module == NULL) {
		switch (token.string.ptr[0]) {
			case 'b':
				if (sv_eq_cstr(token.string, "bool")) {
					Node *internal = ast_new(INTERNAL_NODE, token.location);
					internal->internal.kind = INTERNAL_BOOL;
					return internal;
				} else if (sv_eq_cstr(token.string, "byte")) {
					Node *internal = ast_new(INTERNAL_NODE, token.location);
					internal->internal.kind = INTERNAL_BYTE;
					return internal;
				}
				break;
			case 'c':
				if (sv_eq_cstr(token.string, "compile_error")) {
					Node *internal = ast_new(INTERNAL_NODE, token.location);
					internal->internal.kind = INTERNAL_COMPILE_ERROR;
					
					lexer_consume_check(lexer, PARENTHESIS_OPEN);
					arrpush(internal->internal.inputs, parse_expression(lexer));
					lexer_consume_check(lexer, PARENTHESIS_CLOSED);

					return internal;
				}
				break;
			case 'C':
				if (sv_eq_cstr(token.string, "C_CHAR_SIZE")) {
					Node *internal = ast_new(INTERNAL_NODE, token.location);
					internal->internal.kind = INTERNAL_C_CHAR_SIZE;
					return internal;
				} else if (sv_eq_cstr(token.string, "C_SHORT_SIZE")) {
					Node *internal = ast_new(INTERNAL_NODE, token.location);
					internal->internal.kind = INTERNAL_C_SHORT_SIZE;
					return internal;
				} else if (sv_eq_cstr(token.string, "C_INT_SIZE")) {
					Node *internal = ast_new(INTERNAL_NODE, token.location);
					internal->internal.kind = INTERNAL_C_INT_SIZE;
					return internal;
				} else if (sv_eq_cstr(token.string, "C_LONG_SIZE")) {
					Node *internal = ast_new(INTERNAL_NODE, token.location);
					internal->internal.kind = INTERNAL_C_LONG_SIZE;
					return internal;
				}
				break;
			case 'e':
				if (sv_eq_cstr(token.string, "embed")) {
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
				} else if (sv_eq_cstr(token.string, "err")) {
					Node *internal = ast_new(INTERNAL_NODE, token.location);
					internal->internal.kind = INTERNAL_ERR;

					lexer_consume_check(lexer, PARENTHESIS_OPEN);
					if (lexer_peek(lexer).kind != PARENTHESIS_CLOSED) {
						arrpush(internal->internal.inputs, parse_expression(lexer));
					}
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
					return internal;
				}
				break;
			case 'i':
				if (sv_eq_cstr(token.string, "int")) {
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
				} else if (sv_eq_cstr(token.string, "import")) {
					Node *internal = ast_new(INTERNAL_NODE, token.location);
					internal->internal.kind = INTERNAL_IMPORT;

					lexer_consume_check(lexer, PARENTHESIS_OPEN);
					arrpush(internal->internal.inputs, parse_expression(lexer));
					lexer_consume_check(lexer, PARENTHESIS_CLOSED);

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
					return internal;
				} else if (sv_eq_cstr(token.string, "size_of")) {
					Node *internal = ast_new(INTERNAL_NODE, token.location);
					internal->internal.kind = INTERNAL_SIZE_OF;

					lexer_consume_check(lexer, PARENTHESIS_OPEN);
					arrpush(internal->internal.inputs, parse_expression(lexer));
					lexer_consume_check(lexer, PARENTHESIS_CLOSED);

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
					return internal;
				} else if (sv_eq_cstr(token.string, "type_of")) {
					Node *internal = ast_new(INTERNAL_NODE, token.location);
					internal->internal.kind = INTERNAL_TYPE_OF;

					lexer_consume_check(lexer, PARENTHESIS_OPEN);
					arrpush(internal->internal.inputs, parse_expression(lexer));
					lexer_consume_check(lexer, PARENTHESIS_CLOSED);

					return internal;
				} else if (sv_eq_cstr(token.string, "type_info_of")) {
					Node *internal = ast_new(INTERNAL_NODE, token.location);
					internal->internal.kind = INTERNAL_TYPE_INFO_OF;

					lexer_consume_check(lexer, PARENTHESIS_OPEN);
					arrpush(internal->internal.inputs, parse_expression(lexer));
					lexer_consume_check(lexer, PARENTHESIS_CLOSED);

					return internal;
				}
				break;
			case 'u':
				if (sv_eq_cstr(token.string, "uint")) {
					Node *internal = ast_new(INTERNAL_NODE, token.location);
					internal->internal.kind = INTERNAL_UINT;
					return internal;
				} else if (sv_eq_cstr(token.string, "uint8")) {
					Node *internal = ast_new(INTERNAL_NODE, token.location);
					internal->internal.kind = INTERNAL_UINT8;
					return internal;
				}
				break;
			default:
				break;
		}
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
				handle_token_error_no_expected(lexer, token);
			}
		}
	}

	lexer_consume_check(lexer, PARENTHESIS_CLOSED);

	return call;
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

	Node *structure_access = ast_new(STRUCTURE_ACCESS_NODE, first_token.location);
	structure_access->structure_access.parent = structure;
	structure_access->structure_access.name = lexer_consume_check(lexer, IDENTIFIER).string;

	return structure_access;
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
		lexer_consume_check(lexer, BRACE_CLOSED);
		return array_access;
	}
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
				handle_token_error_no_expected(lexer, token);
			}
		}
	}
	lexer_consume_check(lexer, PARENTHESIS_CLOSED);

	return call_method;
}

static Node *parse_assign(Lexer *lexer, Node *target) {
	lexer_consume(lexer);

	bool static_ = false;
	if (lexer_peek(lexer).kind == KEYWORD_STATIC) {
		lexer_consume(lexer);
		static_ = true;
	}

	Node *assign_value = parse_expression(lexer);

	switch (target->kind) {
		case STRUCTURE_ACCESS_NODE: {
			target->structure_access.assign_value = assign_value;
			return target;
		}
		case IDENTIFIER_NODE: {
			target->identifier.assign_value = assign_value;
			target->identifier.assign_static = static_;
			return target;
		}
		case ARRAY_ACCESS_NODE: {
			target->array_access.assign_value = assign_value;
			return target;
		}
		case DEOPTIONAL_NODE: {
			target->deoptional.assign_value = assign_value;
			return target;
		}
		case DEREFERENCE_NODE: {
			target->dereference.assign_value = assign_value;
			return target;
		}
		default:
			assert(false);
	}
}

static Node *parse_operator(Lexer *lexer) {
	Token_Data first_token = lexer_consume_check(lexer, KEYWORD_OP);

	Token_Data identifier = lexer_consume(lexer);
	String_View operator_id = {};
	switch (identifier.kind) {
		case IDENTIFIER:
			operator_id = identifier.string;
			break;
		case BRACE_OPEN:
			lexer_consume_check(lexer, BRACE_CLOSED);
			operator_id = cstr_to_sv("[]");
			break;
		default:
			assert(false);
	}

	lexer_consume_check(lexer, EQUALS);

	Node *expression = parse_expression(lexer);

	Node *operator = ast_new(OPERATOR_NODE, first_token.location);
	operator->operator.identifier = operator_id;
	operator->operator.expression = expression;
	return operator;
}

static Node *parse_struct_type(Lexer *lexer) {
	Token_Data first_token = lexer_consume(lexer);

	Node *struct_ = ast_new(STRUCT_TYPE_NODE, first_token.location);

	if (lexer_peek(lexer).kind == AT) {
		lexer_consume(lexer);
		struct_->struct_type.inherit_function = true;
	}

	lexer_consume_check(lexer, CURLY_BRACE_OPEN);
	if (lexer_peek(lexer).kind != CURLY_BRACE_CLOSED && !lexer_peek_check(lexer, KEYWORD_OP)) {
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
				handle_token_error_no_expected(lexer, token);
			}
		}
	}

	if (lexer_peek_check(lexer, KEYWORD_OP)) {
		while (lexer_peek(lexer).kind != CURLY_BRACE_CLOSED) {
			arrpush(struct_->struct_type.operators, parse_operator(lexer));
			lexer_consume_check(lexer, SEMICOLON);
	 	}
	}

	lexer_consume_check(lexer, CURLY_BRACE_CLOSED);

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
			arrpush(tagged_union->tagged_union_type.members, member);

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

	return tagged_union;
}

static Node *parse_enum_type(Lexer *lexer) {
	Token_Data first_token = lexer_consume(lexer);

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
				handle_token_error_no_expected(lexer, token);
			}
		}
	}
	lexer_consume_check(lexer, CURLY_BRACE_CLOSED);

	return enum_;
}

static Node *parse_define(Lexer *lexer) {
	Token_Data first_token = lexer_consume(lexer);

	Node *define = ast_new(DEFINE_NODE, first_token.location);

	if (lexer_peek_check(lexer, ASTERISK)) {
		lexer_consume(lexer);
		define->define.special = true;
	}

	Token_Data identifier = lexer_consume_check(lexer, IDENTIFIER);

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
	Token_Data first_token = lexer_consume(lexer);

	Node *return_ = ast_new(RETURN_NODE, first_token.location);
	return_->return_.value = parse_expression_or_nothing(lexer);

	return return_;
}

static Node *parse_variable(Lexer *lexer) {
	Token_Data first_token = lexer_consume(lexer);

	Node *variable = ast_new(VARIABLE_NODE, first_token.location);

	if (lexer_peek_check(lexer, KEYWORD_STATIC)) {
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
	Token_Data first_token = lexer_consume(lexer);

	Node *break_ = ast_new(BREAK_NODE, first_token.location);
	break_->break_.value = parse_expression_or_nothing(lexer);

	return break_;
}

static Node *parse_if(Lexer *lexer) {
	Token_Data first_token = lexer_consume(lexer);

	Node *if_ = ast_new(IF_NODE, first_token.location);

	if (lexer_peek_check(lexer, KEYWORD_STATIC)) {
		lexer_consume(lexer);
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
				handle_token_error_no_expected(lexer, token);
			}
		}

		lexer_consume_check(lexer, VERTICAL_BAR);
	}

	if_->if_.if_body = parse_separated_statement(lexer);

	if (lexer_peek_check(lexer, KEYWORD_ELSE)) {
		lexer_consume(lexer);
		if_->if_.else_body = parse_statement(lexer);
	}
	return if_;
}

static Node *parse_while(Lexer *lexer) {
	Token_Data first_token = lexer_consume(lexer);
	Node *while_ = ast_new(WHILE_NODE, first_token.location);

	if (lexer_peek_check(lexer, KEYWORD_STATIC)) {
		lexer_consume(lexer);
		while_->while_.static_ = true;
	}

	while_->while_.condition = parse_expression(lexer);
	while_->while_.body = parse_separated_statement(lexer);

	if (lexer_peek_check(lexer, KEYWORD_ELSE)) {
		lexer_consume(lexer);
		while_->while_.else_body = parse_statement(lexer);
	}

	return while_;
}

static Node *parse_for(Lexer *lexer) {
	Token_Data first_token = lexer_consume(lexer);

	Node *for_ = ast_new(FOR_NODE, first_token.location);

	if (lexer_peek_check(lexer, KEYWORD_STATIC)) {
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
	}

	if (lexer_peek_check(lexer, KEYWORD_EXTERN)) {
		lexer_consume(lexer);
		global->global.extern_ = lexer_consume_check(lexer, STRING).string;
	}

	global->global.value = parse_separated_statement_or_nothing(lexer);

	return global;
}

static Node *parse_const(Lexer *lexer) {
	Token_Data first_token = lexer_consume(lexer);

	Node *const_ = ast_new(CONST_NODE, first_token.location);

	const_->const_.value = parse_expression(lexer);

	return const_;
}

static Node *parse_switch(Lexer *lexer) {
	Token_Data first_token = lexer_consume(lexer);
	Node *switch_ = ast_new(SWITCH_NODE, first_token.location);

	if (lexer_peek_check(lexer, KEYWORD_STATIC)) {
		lexer_consume(lexer);
		switch_->switch_.static_ = true;
	}

	switch_->switch_.condition = parse_expression(lexer);

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

		Node *sentinel = NULL;
		if (lexer_peek(lexer).kind == SEMICOLON) {
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

static Node *parse_run(Lexer *lexer) {
	Token_Data first_token = lexer_consume(lexer);
	Node *run = ast_new(RUN_NODE, first_token.location);
	run->run.node = parse_statement(lexer);
	return run;
}

static Node *parse_cast(Lexer *lexer) {
	Token_Data first_token = lexer_consume(lexer);
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
	Token_Data first_token = lexer_consume(lexer);
	Node *defer = ast_new(DEFER_NODE, first_token.location);
	defer->defer.node = parse_expression(lexer);
	return defer;
}

static Node *parse_function_or_function_type(Lexer *lexer) {
	Token_Data first_token = lexer_consume(lexer);
	
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
					handle_token_error_no_expected(lexer, token);
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
				if (lexer_peek_check(lexer, KEYWORD_STATIC)) {
					lexer_consume(lexer);
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
				handle_token_error_no_expected(lexer, token);
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

	String_View extern_ = {};
	if (lexer_peek_check(lexer, KEYWORD_EXTERN)) {
		lexer_consume(lexer);
		extern_ = lexer_consume_check(lexer, STRING).string;
	}

	Node *body = parse_separated_statement_or_nothing(lexer);

	if (body != NULL || extern_.ptr != NULL) {
		Node *function = ast_new(FUNCTION_NODE, first_token.location);
		function->function.function_type = function_type;
		function->function.body = body;
		function->function.extern_ = extern_;
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
		case KEYWORD_BREAK: {
			result = parse_break(lexer);
			break;
		}
		case KEYWORD_CAST: {
			result = parse_cast(lexer);
			break;
		}
		case KEYWORD_CONST: {
			result = parse_const(lexer);
			break;
		}
		case KEYWORD_DEF: {
			result = parse_define(lexer);
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
			result = parse_for(lexer);
			break;
		}
		case KEYWORD_GLOBAL: {
			result = parse_global(lexer);
			break;
		}
		case KEYWORD_IF: {
			result = parse_if(lexer);
			break;
		}
		case KEYWORD_MOD: {
			result = parse_module_or_module_type(lexer);
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
			result = parse_switch(lexer);
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
		case KEYWORD_VAR: {
			result = parse_variable(lexer);
			break;
		}
		case KEYWORD_WHILE: {
			result = parse_while(lexer);
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
		case PARENTHESIS_OPEN: {
			lexer_consume(lexer);
			result = parse_expression(lexer);
			lexer_consume_check(lexer, PARENTHESIS_CLOSED);
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

Node *parse_source_expr(Data *data, char *source, size_t length, char *path) {
	Lexer lexer = lexer_create(path, source, length, arrlen(data->source_files));
	return parse_expression(&lexer);
}

Node *parse_source(Data *data, char *source, size_t length, char *path) {
	Lexer lexer = lexer_create(path, source, length, arrlen(data->source_files));
	arrpush(data->source_files, path);

	Node *root = ast_new(MODULE_NODE, (Source_Location) {});
	Node *block = ast_new(BLOCK_NODE, (Source_Location) {});
	while (lexer_peek(&lexer).kind != END_OF_FILE) {
		arrpush(block->block.statements, parse_expression(&lexer));
		lexer_consume_check(&lexer, SEMICOLON);
	}
	root->module.body = block;

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
