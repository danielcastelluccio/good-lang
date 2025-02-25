#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "stb/ds.h"

#include "parser.h"

#include "lexer.h"

static void handle_token_error(Token_Kind expected, Token_Data actual) {
	printf("%s:%zu:%zu: Unexpected token '%s', expected '%s'\n", actual.location.path, actual.location.row, actual.location.column, token_to_string(actual.kind), token_to_string(expected));
	exit(1);
}

static void handle_token_error_no_expected(Token_Data actual) {
	printf("%s:%zu:%zu: Unexpected token '%s'\n", actual.location.path, actual.location.row, actual.location.column, token_to_string(actual.kind));
	exit(1);
}

static Token_Data consume_check(Lexer *lexer, Token_Kind kind) {
	Token_Data data = lexer_next(lexer, true);
	if (data.kind != kind) {
		handle_token_error(kind, data);
	}

	return data;
}

static Node *parse_statement(Lexer *lexer);
static Node *parse_expression(Lexer *lexer);

static Node *parse_expression_or_nothing(Lexer *lexer) {
	if (lexer_next(lexer, false).kind == SEMICOLON || lexer_next(lexer, false).kind == CLOSED_CURLY_BRACE) {
		return NULL;
	}

	return parse_expression(lexer);
}

static Node *parse_string(Lexer *lexer) {
	Token_Data token = consume_check(lexer, STRING);

	Node *string = ast_new(STRING_NODE, token.location);
	string->string.value = token.string;

	return string;
}

static Node *parse_number(Lexer *lexer) {
	Token_Data token = consume_check(lexer, INTEGER);

	Node *number = ast_new(NUMBER_NODE, token.location);
	number->number.tag = INTEGER_NUMBER;
	number->number.integer = token.integer;

	return number;
}

static Node *parse_structure(Lexer *lexer) {
	Token_Data token = consume_check(lexer, PERIOD);
	consume_check(lexer, OPEN_CURLY_BRACE);

	Node *structure = ast_new(STRUCTURE_NODE, token.location);

	while (lexer_next(lexer, false).kind != CLOSED_CURLY_BRACE) {
		arrpush(structure->structure.values, parse_expression(lexer));

		Token_Data token = lexer_next(lexer, false);
		if (token.kind == COMMA) {
			lexer_next(lexer, true);
		} else if (token.kind == CLOSED_CURLY_BRACE) {
		} else {
			handle_token_error_no_expected(token);
		}
	}

	consume_check(lexer, CLOSED_CURLY_BRACE);

	return structure;
}

static Node *parse_identifier(Lexer *lexer, Node *module) {
	Token_Data token = consume_check(lexer, IDENTIFIER);

	if (strcmp(token.string, "null") == 0) {
		return ast_new(NULL_NODE, token.location);
	}

	Node *identifier = ast_new(IDENTIFIER_NODE, token.location);
	identifier->identifier.module = module;
	identifier->identifier.value = token.string;

	identifier->identifier.generics = NULL;
	if (lexer_next(lexer, false).kind == HASHTAG) {
		consume_check(lexer, HASHTAG);
		consume_check(lexer, OPEN_PARENTHESIS);
		while (lexer_next(lexer, false).kind != CLOSED_PARENTHESIS) {
			arrpush(identifier->identifier.generics, parse_expression(lexer));

			Token_Data token = lexer_next(lexer, false);
			if (token.kind == COMMA) {
				lexer_next(lexer, true);
			} else if (token.kind == CLOSED_PARENTHESIS) {
			} else {
				handle_token_error_no_expected(token);
			}
		}
		consume_check(lexer, CLOSED_PARENTHESIS);
	}

	return identifier;
}

static Node *parse_call(Lexer *lexer, Node *function) {
	Token_Data first_token = consume_check(lexer, OPEN_PARENTHESIS);

	Node *call = ast_new(CALL_NODE, first_token.location);
	call->call.function = function;

	while (lexer_next(lexer, false).kind != CLOSED_PARENTHESIS) {
		arrpush(call->call.arguments, parse_expression(lexer));

		Token_Data token = lexer_next(lexer, false);
		if (token.kind == COMMA) {
			lexer_next(lexer, true);
		} else if (token.kind == CLOSED_PARENTHESIS) {
		} else {
			handle_token_error_no_expected(token);
		}
	}

	consume_check(lexer, CLOSED_PARENTHESIS);

	return call;
}

static size_t get_precedence(Binary_Operatory_Node_Kind kind) {
	switch (kind) {
		case OPERATOR_ADD:
		case OPERATOR_SUBTRACT:
			return 1;
		case OPERATOR_MULTIPLY:
		case OPERATOR_DIVIDE:
			return 2;
		case OPERATOR_EQUALS:
			return 0;
		default:
			assert(false);
	}
}

static Node *parse_binary_operator(Lexer *lexer, Node *left) {
	Token_Data first_token = lexer_next(lexer, true);

	Node *binary_operator = ast_new(BINARY_OPERATOR_NODE, first_token.location);
	binary_operator->binary_operator.left = left;
	switch (first_token.kind) {
		case EQUAL_EQUALS:
			binary_operator->binary_operator.operator = OPERATOR_EQUALS;
			break;
		case PLUS:
			binary_operator->binary_operator.operator = OPERATOR_ADD;
			break;
		case MINUS:
			binary_operator->binary_operator.operator = OPERATOR_SUBTRACT;
			break;
		case ASTERISK:
			binary_operator->binary_operator.operator = OPERATOR_MULTIPLY;
			break;
		case SLASH:
			binary_operator->binary_operator.operator = OPERATOR_DIVIDE;
			break;
		default:
			assert(false);
	}

	bool swap = false;
	Node *right = parse_expression(lexer);
	Node **swap_location = &right;
	while ((*swap_location)->kind == BINARY_OPERATOR_NODE) {
		swap = get_precedence((*swap_location)->binary_operator.operator) <= get_precedence(binary_operator->binary_operator.operator);
		swap_location = &(*swap_location)->binary_operator.left;
	}

	if (swap) {
		binary_operator->binary_operator.right = *swap_location;
		*swap_location = binary_operator;
		return right;
	} else {
		binary_operator->binary_operator.right = right;
		return binary_operator;
	}
}

static Node *parse_structure_access(Lexer *lexer, Node *structure) {
	Token_Data first_token = lexer_next(lexer, true);

	Node *structure_access = ast_new(STRUCTURE_ACCESS_NODE, first_token.location);
	structure_access->structure_access.structure = structure;

	structure_access->structure_access.item = consume_check(lexer, IDENTIFIER).string;

	return structure_access;
}

static Node *parse_array_access(Lexer *lexer, Node *array) {
	Token_Data first_token = lexer_next(lexer, true);

	Node *array_access = ast_new(ARRAY_ACCESS_NODE, first_token.location);
	array_access->array_access.array = array;
	array_access->array_access.index = parse_expression(lexer);

	consume_check(lexer, CLOSED_BRACE);

	return array_access;
}

static Node *parse_assign(Lexer *lexer, Node *structure) {
	Token_Data first_token = lexer_next(lexer, true);

	Node *assign = ast_new(ASSIGN_NODE, first_token.location);
	assign->assign.container = structure;
	assign->assign.value = parse_expression(lexer);

	return assign;
}

static Node *parse_structure_type(Lexer *lexer) {
	Token_Data first_token = consume_check(lexer, KEYWORD);

	consume_check(lexer, OPEN_CURLY_BRACE);

	Struct_Item *items = NULL;
	while (lexer_next(lexer, false).kind != CLOSED_CURLY_BRACE) {
		Token_Data identifier = consume_check(lexer, IDENTIFIER);
		consume_check(lexer, COLON);
		Node *type = parse_expression(lexer);

		Struct_Item item = {
			.identifier = identifier.string,
			.type = type
		};
		arrpush(items, item);

		Token_Data token = lexer_next(lexer, false);
		if (token.kind == COMMA) {
			lexer_next(lexer, true);
		} else if (token.kind == CLOSED_CURLY_BRACE) {
		} else {
			handle_token_error_no_expected(token);
		}
	}
	consume_check(lexer, CLOSED_CURLY_BRACE);

	Node *structure = ast_new(STRUCTURE_TYPE_NODE, first_token.location);
	structure->structure_type.items = items;

	return structure;
}

static Node *parse_define(Lexer *lexer) {
	Token_Data first_token = consume_check(lexer, KEYWORD);

	Token_Data identifier = consume_check(lexer, IDENTIFIER);

	Node *constraint = NULL;
	Generic_Argument *generics = NULL;
	if (lexer_next(lexer, false).kind == HASHTAG) {
		consume_check(lexer, HASHTAG);
		consume_check(lexer, OPEN_PARENTHESIS);

		while (lexer_next(lexer, false).kind != CLOSED_PARENTHESIS) {
			Token_Data identifier = consume_check(lexer, IDENTIFIER);
			consume_check(lexer, COLON);
			Node *type = parse_expression(lexer);

			Function_Argument argument = {
				.identifier = identifier.string,
				.type = type
			};
			arrpush(generics, argument);

			Token_Data token = lexer_next(lexer, false);
			if (token.kind == COMMA) {
				lexer_next(lexer, true);
			} else if (token.kind == CLOSED_PARENTHESIS) {
			} else if (token.kind == SEMICOLON) {
				consume_check(lexer, SEMICOLON);
				constraint = parse_expression(lexer);
			} else {
				handle_token_error_no_expected(token);
			}
		}

		consume_check(lexer, CLOSED_PARENTHESIS);
	}

	consume_check(lexer, EQUALS);

	Node *expression = parse_expression(lexer);

	consume_check(lexer, SEMICOLON);

	Node *define = ast_new(DEFINE_NODE, first_token.location);
	define->define.identifier = identifier.string;
	define->define.expression = expression;
	define->define.generics = generics;
	define->define.generic_constraint = constraint;

	return define;
}

static Node *parse_return(Lexer *lexer) {
	Token_Data first_token = consume_check(lexer, KEYWORD);
	Node *return_ = ast_new(RETURN_NODE, first_token.location);
	return_->return_.value = parse_expression_or_nothing(lexer);

	consume_check(lexer, SEMICOLON);

	return return_;
}

static Node *parse_variable(Lexer *lexer) {
	Token_Data first_token = consume_check(lexer, KEYWORD);
	Node *variable = ast_new(VARIABLE_NODE, first_token.location);
	variable->variable.identifier = consume_check(lexer, IDENTIFIER).string;
	
	if (lexer_next(lexer, false).kind == COLON) {
		consume_check(lexer, COLON);
		variable->variable.type = parse_expression(lexer);
	}

	if (lexer_next(lexer, false).kind == EQUALS) {
		consume_check(lexer, EQUALS);
		variable->variable.value = parse_expression(lexer);
	}

	consume_check(lexer, SEMICOLON);
	return variable;
}

static Node *parse_if(Lexer *lexer) {
	Token_Data first_token = consume_check(lexer, KEYWORD);
	Node *if_ = ast_new(IF_NODE, first_token.location);

	Token_Data next_static = lexer_next(lexer, false);
	if (next_static.kind == KEYWORD && strcmp(next_static.string, "static") == 0) {
		consume_check(lexer, KEYWORD);
		if_->if_.static_ = true;
	}

	if_->if_.condition = parse_expression(lexer);
	if_->if_.if_body = parse_expression(lexer);

	Token_Data next = lexer_next(lexer, false);
	if (next.kind == KEYWORD && strcmp(next.string, "else") == 0) {
		consume_check(lexer, KEYWORD);
		if_->if_.else_body = parse_expression_or_nothing(lexer);
	}
	return if_;
}

static Node *parse_block(Lexer *lexer) {
	Token_Data first_token = consume_check(lexer, OPEN_CURLY_BRACE);

	Node *block = ast_new(BLOCK_NODE, first_token.location);

	while (lexer_next(lexer, false).kind != CLOSED_CURLY_BRACE) {
		arrpush(block->block.statements, parse_statement(lexer));
	}

	consume_check(lexer, CLOSED_CURLY_BRACE);

	return block;
}

static Node *parse_pointer(Lexer *lexer) {
	Token_Data first_token = consume_check(lexer, ASTERISK);

	Node *pointer = ast_new(POINTER_NODE, first_token.location);
	pointer->pointer.inner = parse_expression(lexer);

	return pointer;
}

static Node *parse_reference(Lexer *lexer) {
	Token_Data first_token = consume_check(lexer, AMPERSAND);

	Node *reference = ast_new(REFERENCE_NODE, first_token.location);
	reference->reference.node = parse_expression(lexer);

	return reference;
}

static Node *parse_array(Lexer *lexer) {
	Token_Data first_token = consume_check(lexer, OPEN_BRACE);
	Node *size = NULL;
	if (lexer_next(lexer, false).kind != CLOSED_BRACE) {
		size = parse_expression(lexer);
	}
	consume_check(lexer, CLOSED_BRACE);

	Node *array = ast_new(ARRAY_NODE, first_token.location);
	array->array.size = size;
	array->array.inner = parse_expression(lexer);

	return array;
}

static Node *parse_module(Lexer *lexer) {
	Token_Data first_token = consume_check(lexer, KEYWORD);
	Node *module = ast_new(MODULE_NODE, first_token.location);
	Node *body = parse_expression(lexer);
	module->module.body = body;
	return module;
}

static Node *parse_run(Lexer *lexer) {
	Token_Data first_token = consume_check(lexer, KEYWORD);
	Node *run = ast_new(RUN_NODE, first_token.location);
	run->run.value = parse_expression(lexer);
	return run;
}

static Node *parse_function_or_function_type(Lexer *lexer) {
	Token_Data first_token = consume_check(lexer, KEYWORD);
	consume_check(lexer, OPEN_PARENTHESIS);
	
	Function_Argument *arguments = NULL;

	bool variadic = false;
	while (lexer_next(lexer, false).kind != CLOSED_PARENTHESIS) {
		if (lexer_next(lexer, false).kind == PERIOD_PERIOD_PERIOD) {
			consume_check(lexer, PERIOD_PERIOD_PERIOD);
			variadic = true;
		} else {
			Token_Data identifier = consume_check(lexer, IDENTIFIER);
			consume_check(lexer, COLON);
			Node *type = parse_expression(lexer);

			Function_Argument argument = {
				.identifier = identifier.string,
				.type = type
			};
			arrpush(arguments, argument);
		}

		Token_Data token = lexer_next(lexer, false);
		if (token.kind == COMMA) {
			lexer_next(lexer, true);
		} else if (token.kind == CLOSED_PARENTHESIS) {
		} else {
			handle_token_error_no_expected(token);
		}
	}

	consume_check(lexer, CLOSED_PARENTHESIS);

	Node *return_ = NULL;
	if (lexer_next(lexer, false).kind == COLON) {
		consume_check(lexer, COLON);
		return_ = parse_expression(lexer);
	}

	Node *function_type = ast_new(FUNCTION_TYPE_NODE, first_token.location);
	function_type->function_type.arguments = arguments;
	function_type->function_type.return_ = return_;
	function_type->function_type.variadic = variadic;

	bool extern_ = false;
	char *extern_name = NULL;
	Token_Data maybe_extern = lexer_next(lexer, false);
	if (maybe_extern.kind == KEYWORD && strcmp(maybe_extern.string, "extern") == 0) extern_ = true;

	if (extern_) {
		lexer_next(lexer, true);
		extern_name = consume_check(lexer, STRING).string;
	}

	Node *body = NULL;
	if (!extern_) {
		body = parse_expression_or_nothing(lexer);
	}

	if (body != NULL || extern_) {
		Node *function = ast_new(FUNCTION_NODE, first_token.location);
		function->function.function_type = function_type;
		function->function.body = body;
		function->function.extern_name = extern_name;
		return function;
	} else {
		return function_type;
	}
}

static Node *parse_statement(Lexer *lexer) {
	Token_Data token = lexer_next(lexer, false);
	Node *result = NULL;
	switch (token.kind) {
		case KEYWORD: {
			char *value = token.string;
			if (strcmp(value, "def") == 0) result = parse_define(lexer);
			else if (strcmp(value, "return") == 0) result = parse_return(lexer);
			else if (strcmp(value, "var") == 0) result = parse_variable(lexer);
			break;
		}
		default:
			break;
	}

	if (result == NULL) {
		result = parse_expression(lexer);

		bool operating = true;
		while (operating) {
			Token_Data next = lexer_next(lexer, false);
			switch (next.kind) {
				case EQUALS:
					result = parse_assign(lexer, result);
					break;
				default:
					operating = false;
					break;
			}
		}

		consume_check(lexer, SEMICOLON);
	}

	return result;
}

static Node *parse_expression(Lexer *lexer) {
	Token_Data token = lexer_next(lexer, false);
	Node *result = NULL;
	switch (token.kind) {
		case STRING: {
			result = parse_string(lexer);
			break;
		}
		case INTEGER: {
			result = parse_number(lexer);
			break;
		}
		case PERIOD: {
			result = parse_structure(lexer);
			break;
		}
		case IDENTIFIER: {
			result = parse_identifier(lexer, NULL);
			break;
		}
		case KEYWORD: {
			char *value = token.string;
			if (strcmp(value, "fn") == 0) result = parse_function_or_function_type(lexer);
			else if (strcmp(value, "struct") == 0) result = parse_structure_type(lexer);
			else if (strcmp(value, "if") == 0) result = parse_if(lexer);
			else if (strcmp(value, "mod") == 0) result = parse_module(lexer);
			else if (strcmp(value, "run") == 0) result = parse_run(lexer);
			else {
				printf("Unhandled keyword '%s'\n", value);
				exit(1);
			}
			break;
		}
		case OPEN_CURLY_BRACE: {
			result = parse_block(lexer);
			break;
		}
		case ASTERISK: {
			result = parse_pointer(lexer);
			break;
		}
		case AMPERSAND: {
			result = parse_reference(lexer);
			break;
		}
		case OPEN_BRACE: {
			result = parse_array(lexer);
			break;
		}
		default:
			handle_token_error_no_expected(token);
	}

	bool operating = true;
	while (operating) {
		Token_Data next = lexer_next(lexer, false);
		switch (next.kind) {
			case OPEN_PARENTHESIS:
				result = parse_call(lexer, result);
				break;
			case EQUAL_EQUALS:
			case PLUS:
			case MINUS:
			case ASTERISK:
			case SLASH:
				result = parse_binary_operator(lexer, result);
				break;
			case PERIOD:
				result = parse_structure_access(lexer, result);
				break;
			case OPEN_BRACE:
				result = parse_array_access(lexer, result);
				break;
			case COLON_COLON:
				consume_check(lexer, COLON_COLON);
				result = parse_identifier(lexer, result);
				break;
			default:
				operating = false;
				break;
		}
	}

	return result;
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

	Lexer lexer = lexer_create(path, contents, length);

	Node *root = ast_new(MODULE_NODE, (Source_Location) {});
	Node *block = ast_new(BLOCK_NODE, (Source_Location) {});
	while (lexer_next(&lexer, false).kind != END_OF_FILE) {
		arrpush(block->block.statements, parse_statement(&lexer));
	}
	root->module.body = block;

	return root;
}
