#include "string.h"
#include "util.h"

bool streq(char *s1, char *s2) {
	return strcmp(s1, s2) == 0;
}
