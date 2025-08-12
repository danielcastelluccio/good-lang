#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "lexer.h"

Lexer lexer_create(char *path, char *source, size_t source_length, uint32_t path_ref) {
	return (Lexer) {
		.source = source,
		.source_length = source_length,
		.position = 0,
		.path = path,
		.row = 1,
		.column = 1,
		.has_cached = false,
		.path_ref = path_ref
	};
}

static bool is_alphabetical_underscore(char character) {
	return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') || character == '_';
}

static bool is_numeric(char character) {
	return (character >= '0' && character <= '9');
}

static bool is_alphanumeric_underscore(char character) {
	return is_alphabetical_underscore(character) || is_numeric(character);
}

static bool is_alphanumericplus(char character) {
	return is_alphanumeric_underscore(character) || character == '_';
}

static bool is_whitespace(char character) {
	return character == ' ' || character == '\n' || character == '\t';
}

static Token_Data create_token(Token_Kind kind, Lexer *lexer) {
	return (Token_Data) {
		.kind = kind,
		.location = { .path_ref = lexer->path_ref, .row = lexer->row, .column = lexer->column - 1 }
	};
}

static String_View extract_string(char *source, size_t start, size_t end) {
	return (String_View) {
		.ptr = &source[start],
		.len = end - start
	};
}

static void extract_string_to_buffer(char *source, size_t start, size_t end, char *buffer, int buffer_len) {
	(void) buffer_len;
	size_t length = end - start;
	buffer[length] = '\0';
	memcpy(buffer, source + start, length);
}

static size_t extract_integer(char *source, size_t start, size_t end) {
	char buffer[24];
	extract_string_to_buffer(source, start, end, buffer, 24);
	return atoll(buffer);
}

static double extract_decimal(char *source, size_t start, size_t end) {
	char buffer[24];
	extract_string_to_buffer(source, start, end, buffer, 24);
	return strtod(buffer, NULL);
}

static void increment_position(Lexer *lexer) {
	lexer->position++;
	lexer->column++;
}

static Token_Kind get_range_token_kind(char *source, size_t start, size_t end) {
	char saved = source[end];
	source[end] = '\0';

	Token_Kind result = IDENTIFIER;
	char *identifier = &source[start];
	switch (identifier[0]) {
		case 'a':
			if (strcmp(identifier, "and") == 0) result = KEYWORD_AND;
			break;
		case 'b':
			if (strcmp(identifier, "break") == 0) result = KEYWORD_BREAK;
			break;
		case 'c':
			if (strcmp(identifier, "cast") == 0) result = KEYWORD_CAST;
			else if (strcmp(identifier, "case") == 0) result = KEYWORD_CASE;
			else if (strcmp(identifier, "catch") == 0) result = KEYWORD_CATCH;
			break;
		case 'd':
			if (strcmp(identifier, "def") == 0) result = KEYWORD_DEF;
			else if (strcmp(identifier, "defer") == 0) result = KEYWORD_DEFER;
			break;
		case 'e':
			if (strcmp(identifier, "extern") == 0) result = KEYWORD_EXTERN;
			else if (strcmp(identifier, "else") == 0) result = KEYWORD_ELSE;
			else if (strcmp(identifier, "enum") == 0) result = KEYWORD_ENUM;
			break;
		case 'f':
			if (strcmp(identifier, "fn") == 0) result = KEYWORD_FN;
			else if (strcmp(identifier, "for") == 0) result = KEYWORD_FOR;
			break;
		case 'i':
			if (strcmp(identifier, "if") == 0) result = KEYWORD_IF;
			else if (strcmp(identifier, "is") == 0) result = KEYWORD_IS;
			break;
		case 'm':
			if (strcmp(identifier, "mod") == 0) result = KEYWORD_MOD;
			break;
		case 'o':
			if (strcmp(identifier, "op") == 0) result = KEYWORD_OP;
			else if (strcmp(identifier, "or") == 0) result = KEYWORD_OR;
			break;
		case 'r':
			if (strcmp(identifier, "return") == 0) result = KEYWORD_RETURN;
			else if (strcmp(identifier, "run") == 0) result = KEYWORD_RUN;
			break;
		case 's':
			if (strcmp(identifier, "static") == 0) result = KEYWORD_STATIC;
			else if (strcmp(identifier, "struct") == 0) result = KEYWORD_STRUCT;
			else if (strcmp(identifier, "switch") == 0) result = KEYWORD_SWITCH;
			break;
		case 't':
			if (strcmp(identifier, "tagged_union") == 0) result = KEYWORD_TAGGED_UNION;
			break;
		case 'u':
			if (strcmp(identifier, "union") == 0) result = KEYWORD_UNION;
			break;
		case 'v':
			if (strcmp(identifier, "var") == 0) result = KEYWORD_VAR;
			break;
		case 'w':
			if (strcmp(identifier, "while") == 0) result = KEYWORD_WHILE;
			break;
	}
	source[end] = saved;
	return result;
}

static bool is_comment(Lexer *lexer) {
	return lexer->position < lexer->source_length - 1 && lexer->source[lexer->position] == '/' && lexer->source[lexer->position + 1] == '/';
}

Token_Data lexer_next(Lexer *lexer, bool advance) {
	if (lexer->has_cached) {
		lexer->has_cached = !advance;
		return lexer->cached_token;
	}
	
	if (lexer->position == lexer->source_length) {
		return create_token(END_OF_FILE, lexer);
	}

	bool is_current_comment = false;
	while (is_whitespace(lexer->source[lexer->position]) || is_comment(lexer) || is_current_comment) {
		if (lexer->source[lexer->position] == '\n') {
			lexer->row++;
			lexer->column = 0;
			is_current_comment = false;
		}

		if (is_comment(lexer)) {
			is_current_comment = true;
		}

		increment_position(lexer);

		if (lexer->position == lexer->source_length) {
			return create_token(END_OF_FILE, lexer);
		}
	}

	Token_Data result = {};
	char character = lexer->source[lexer->position];
	increment_position(lexer);
	switch (character) {
		case ':':
			if (lexer->source[lexer->position] == ':') {
				result = create_token(COLON_COLON, lexer);
				increment_position(lexer);
				break;
			}
			result = create_token(COLON, lexer);
			break;
		case ';':
			result = create_token(SEMICOLON, lexer);
			break;
		case ',':
			result = create_token(COMMA, lexer);
			break;
		case '=':
			if (lexer->source[lexer->position] == '=') {
				result = create_token(EQUALS_EQUALS, lexer);
				increment_position(lexer);
				break;
			} else if (lexer->source[lexer->position] == '>') {
				result = create_token(EQUALS_GREATER, lexer);
				increment_position(lexer);
				break;
			}
			result = create_token(EQUALS, lexer);
			break;
		case '!':
			if (lexer->source[lexer->position] == '=') {
				result = create_token(EXCLAMATION_EQUALS, lexer);
				increment_position(lexer);
				break;
			}
			result = create_token(EXCLAMATION, lexer);
			break;
		case '*':
			result = create_token(ASTERISK, lexer);
			break;
		case '^':
			result = create_token(CARET, lexer);
			break;
		case '?':
			if (lexer->source[lexer->position] == '?') {
				result = create_token(QUESTION_QUESTION, lexer);
				increment_position(lexer);
				break;
			}
			result = create_token(QUESTION, lexer);
			break;
		case '+':
			result = create_token(PLUS, lexer);
			break;
		case '/':
			result = create_token(SLASH, lexer);
			break;
		case '&':
			result = create_token(AMPERSAND, lexer);
			break;
		case '@':
			result = create_token(AT, lexer);
			break;
		case '|':
			result = create_token(VERTICAL_BAR, lexer);
			break;
		case '(':
			result = create_token(PARENTHESIS_OPEN, lexer);
			break;
		case ')':
			result = create_token(PARENTHESIS_CLOSED, lexer);
			break;
		case '{':
			result = create_token(CURLY_BRACE_OPEN, lexer);
			break;
		case '}':
			result = create_token(CURLY_BRACE_CLOSED, lexer);
			break;
		case '[':
			result = create_token(BRACE_OPEN, lexer);
			break;
		case ']':
			result = create_token(BRACE_CLOSED, lexer);
			break;
		case '<':
			result = create_token(LESS, lexer);
			break;
		case '>':
			result = create_token(GREATER, lexer);
			break;
		case '%':
			result = create_token(PERCENT, lexer);
			break;
		case '.': {
			if (lexer->source[lexer->position] == '.') {
				increment_position(lexer);
				result = create_token(PERIOD_PERIOD, lexer);
				break;
			} else if (lexer->source[lexer->position] == '{') {
				increment_position(lexer);
				result = create_token(PERIOD_CURLY_BRACE_OPEN, lexer);
				break;
			}
			result = create_token(PERIOD, lexer);
			break;
		}
		case '-':
			if (lexer->source[lexer->position] == '>') {
				result = create_token(MINUS_GREATER, lexer);
				increment_position(lexer);
				break;
			}
			result = create_token(MINUS, lexer);
			break;
		case '"': {
			size_t string_start = lexer->position;
			size_t string_start_row = lexer->row;
			size_t string_start_column = lexer->column - 1;

			while (lexer->source[lexer->position] != '"') {
				increment_position(lexer);
			}
			increment_position(lexer);

			size_t string_end = lexer->position - 1;

			result = (Token_Data) {
				.kind = STRING,
				.string = extract_string(lexer->source, string_start, string_end),
				.location = { .path_ref = lexer->path_ref, .row = string_start_row, .column = string_start_column }
			};
			break;
		}
		case '\'': {
			size_t string_start = lexer->position;
			size_t string_start_row = lexer->row;
			size_t string_start_column = lexer->column - 1;

			while (lexer->source[lexer->position] != '\'') {
				increment_position(lexer);
			}
			increment_position(lexer);

			size_t string_end = lexer->position - 1;

			result = (Token_Data) {
				.kind = CHARACTER,
				.string = extract_string(lexer->source, string_start, string_end),
				.location = { .path_ref = lexer->path_ref, .row = string_start_row, .column = string_start_column }
			};
			break;
		}
		default: {
			if (is_alphabetical_underscore(character)) {
				size_t string_start = lexer->position - 1;
				size_t string_start_row = lexer->row;
				size_t string_start_column = lexer->column - 1;

				while (is_alphanumericplus(lexer->source[lexer->position]) && lexer->position < lexer->source_length) {
					increment_position(lexer);
				}

				size_t string_end = lexer->position;

				Token_Kind kind = get_range_token_kind(lexer->source, string_start, string_end);
				if (kind == IDENTIFIER) {
					String_View extracted_string = extract_string(lexer->source, string_start, string_end);

					result = (Token_Data) {
						.kind = IDENTIFIER,
						.string = extracted_string,
						.location = { .path_ref = lexer->path_ref, .row = string_start_row, .column = string_start_column }
					};
				} else {
					result = (Token_Data) {
						.kind = kind,
						.location = { .path_ref = lexer->path_ref, .row = string_start_row, .column = string_start_column }
					};
				}
				break;
			} else if (is_numeric(character)) {
				size_t number_start = lexer->position - 1;
				size_t number_start_row = lexer->row;
				size_t number_start_column = lexer->column - 1;

				while (is_numeric(lexer->source[lexer->position])) {
					increment_position(lexer);
				}

				if (lexer->source[lexer->position] == '.' && lexer->source[lexer->position + 1] != '.') {
					increment_position(lexer);
					while (is_numeric(lexer->source[lexer->position])) {
						increment_position(lexer);
					}

					size_t number_end = lexer->position;
					double extracted_decimal = extract_decimal(lexer->source, number_start, number_end);

					result = (Token_Data) {
						.kind = DECIMAL,
						.decimal = extracted_decimal,
						.location = { .path_ref = lexer->path_ref, .row = number_start_row, .column = number_start_column }
					};
				} else {
					size_t number_end = lexer->position;
					size_t extracted_integer = extract_integer(lexer->source, number_start, number_end);

					result = (Token_Data) {
						.kind = INTEGER,
						.integer = extracted_integer,
						.location = { .path_ref = lexer->path_ref, .row = number_start_row, .column = number_start_column }
					};
				}
				break;
			} else {
				result = create_token(INVALID, lexer);
				break;
			}
		}
	}

	if (!advance) {
		lexer->cached_token = result;
		lexer->has_cached = true;
	}

	return result;
}

char *token_to_string(Token_Kind kind) {
	switch (kind) {
		case IDENTIFIER:
			return "Identifier";
		case STRING:
			return "String";
		case CHARACTER:
			return "Character";
		case INTEGER:
			return "Integer";
		case DECIMAL:
			return "Decimal";
		case KEYWORD_AND:
			return "and";
		case KEYWORD_BREAK:
			return "break";
		case KEYWORD_CAST:
			return "cast";
		case KEYWORD_CASE:
			return "case";
		case KEYWORD_CATCH:
			return "catch";
		case KEYWORD_DEF:
			return "def";
		case KEYWORD_DEFER:
			return "defer";
		case KEYWORD_EXTERN:
			return "extern";
		case KEYWORD_ELSE:
			return "else";
		case KEYWORD_ENUM:
			return "enum";
		case KEYWORD_FN:
			return "fn";
		case KEYWORD_FOR:
			return "for";
		case KEYWORD_IF:
			return "if";
		case KEYWORD_IS:
			return "is";
		case KEYWORD_MOD:
			return "mod";
		case KEYWORD_OP:
			return "op";
		case KEYWORD_RETURN:
			return "return";
		case KEYWORD_RUN:
			return "run";
		case KEYWORD_STATIC:
			return "static";
		case KEYWORD_STRUCT:
			return "struct";
		case KEYWORD_SWITCH:
			return "switch";
		case KEYWORD_TAGGED_UNION:
			return "tagged_union";
		case KEYWORD_OR:
			return "or";
		case KEYWORD_UNION:
			return "union";
		case KEYWORD_VAR:
			return "var";
		case KEYWORD_WHILE:
			return "while";
		case COLON:
			return ":";
		case SEMICOLON:
			return ";";
		case COMMA:
			return ",";
		case EQUALS:
			return "=";
		case ASTERISK:
			return "*";
		case CARET:
			return "^";
		case QUESTION:
			return "?";
		case QUESTION_QUESTION:
			return "??";
		case PLUS:
			return "+";
		case MINUS:
			return "-";
		case SLASH:
			return "/";
		case AMPERSAND:
			return "&";
		case AT:
			return "@";
		case VERTICAL_BAR:
			return "|";
		case LESS:
			return "<";
		case LESS_EQUALS:
			return "<=";
		case GREATER:
			return ">";
		case GREATER_EQUALS:
			return ">=";
		case PERIOD:
			return ".";
		case PERIOD_PERIOD:
			return "..";
		case PERIOD_CURLY_BRACE_OPEN:
			return ".{";
		case MINUS_GREATER:
			return "->";
		case EQUALS_EQUALS:
			return "==";
		case EXCLAMATION:
			return "!";
		case EXCLAMATION_EQUALS:
			return "!=";
		case EQUALS_GREATER:
			return "=>";
		case COLON_COLON:
			return "::";
		case PARENTHESIS_OPEN:
			return "(";
		case PARENTHESIS_CLOSED:
			return ")";
		case PERCENT:
			return "%";
		case CURLY_BRACE_OPEN:
			return "{";
		case CURLY_BRACE_CLOSED:
			return "}";
		case BRACE_OPEN:
			return "[";
		case BRACE_CLOSED:
			return "]";
		case END_OF_FILE:
			return "Eof";
		case INVALID:
			return "Invalid";
	}
	return "Unimplemented";
}
