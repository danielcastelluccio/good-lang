#include "common.h"
#include "string_view.h"

typedef enum {
	CHARACTER,
	DECIMAL,
	IDENTIFIER,
	STRING,
	INTEGER,

	KEYWORD_BREAK,
	KEYWORD_CAST,
	KEYWORD_CASE,
	KEYWORD_CATCH,
	KEYWORD_DEF,
	KEYWORD_DEFER,
	KEYWORD_EXTERN,
	KEYWORD_ELSE,
	KEYWORD_ENUM,
	KEYWORD_FN,
	KEYWORD_FOR,
	KEYWORD_IF,
	KEYWORD_INTERNAL,
	KEYWORD_IS,
	KEYWORD_MOD,
	KEYWORD_OP,
	KEYWORD_RETURN,
	KEYWORD_RUN,
	KEYWORD_STATIC,
	KEYWORD_STRUCT,
	KEYWORD_SWITCH,
	KEYWORD_TAGGED_UNION,
	KEYWORD_UNION,
	KEYWORD_VAR,
	KEYWORD_WHILE,

	AMPERSAND,
	ASTERISK,
	AT,
	BRACE_CLOSED,
	BRACE_OPEN,
	CARET,
	COLON,
	COLON_COLON,
	COMMA,
	CURLY_BRACE_CLOSED,
	CURLY_BRACE_OPEN,
	EQUALS,
	EQUALS_EQUALS,
	EQUALS_GREATER,
	EXCLAMATION,
	EXCLAMATION_EQUALS,
	GREATER,
	GREATER_EQUALS,
	LESS,
	LESS_EQUALS,
	MINUS,
	MINUS_GREATER,
	PARENTHESIS_CLOSED,
	PARENTHESIS_OPEN,
	PERIOD,
	PERIOD_PERIOD,
	PERIOD_CURLY_BRACE_OPEN,
	PLUS,
	QUESTION,
	QUESTION_QUESTION,
	SEMICOLON,
	SLASH,
	VERTICAL_BAR,

	END_OF_FILE,
	INVALID
} Token_Kind;

typedef struct {
	Token_Kind kind;
	Source_Location location;
	union {
		String_View string;
		long integer;
		double decimal;
	};
} Token_Data;

typedef struct {
	char *source;
	size_t source_length;
	size_t position;
	char *path;
	size_t row;
	size_t column;
	Token_Data cached_token;
	bool has_cached;
} Lexer;

Lexer lexer_create(char *path, char *source, size_t source_length);

Token_Data lexer_next(Lexer *lexer, bool advance);

char *token_to_string(Token_Kind kind);
