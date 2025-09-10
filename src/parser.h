#include <stdbool.h>

#include "ast.h"
#include "common.h"

Node *parse_file(Data *data, char *path);
Node *parse_source(Data *data, char *source, size_t length, char *path);
Node *parse_source_statement(Data *data, char *source, size_t length, size_t path_ref, size_t row, size_t column);
