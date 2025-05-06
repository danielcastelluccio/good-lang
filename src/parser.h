#include <stdbool.h>

#include "ast.h"

Node *parse_file(char *path);
Node *parse_source(char *source, size_t length, char *path);
Node *parse_source_expr(char *source, size_t length, char *path);
