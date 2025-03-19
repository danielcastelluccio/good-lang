#include "common.h"

typedef enum {
	IDENTIFIER,
	KEYWORD,
	STRING,
	INTEGER,
	DECIMAL,
	COLON,
	SEMICOLON,
	COMMA,
	EQUALS,
	ASTERISK,
	CARET,
	QUESTION,
	QUESTION_QUESTION,
	AMPERSAND,
	PERIOD,
	PERIOD_PERIOD,
	PERIOD_PERIOD_PERIOD,
	LESS,
	LESS_EQUALS,
	GREATER,
	GREATER_EQUALS,
	MINUS,
	MINUS_GREATER,
	PLUS,
	SLASH,
	AT,
	UNDERSCORE,
	HASHTAG,
	EQUALS_EQUALS,
	COLON_COLON,
	OPEN_PARENTHESIS,
	CLOSED_PARENTHESIS,
	OPEN_CURLY_BRACE,
	CLOSED_CURLY_BRACE,
	OPEN_BRACE,
	CLOSED_BRACE,
	END_OF_FILE,
	INVALID
} Token_Kind;

typedef struct {
	Token_Kind kind;
	Source_Location location;
	union {
		char *string;
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
