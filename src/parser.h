#include <stdbool.h>

#include "ast.h"
#include "common.h"

Node *parse_file(Data *data, char *path);
Node *parse_source(Data *data, char *source, size_t length, char *path);
Node *parse_source_expr(Data *data, char *source, size_t length, char *path);
