#ifndef STRING_VIEW_H
#define STRING_VIEW_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
	char *ptr;
	size_t len;
} String_View;

bool sv_eq_cstr(String_View sv, char *cstr);
bool sv_eq(String_View sv1, String_View sv2);
String_View cstr_to_sv(char *cstr);

size_t sv_hash(String_View sv);

#endif
