#define STB_DS_IMPLEMENTATION

#include <stdlib.h>
#include <string.h>

void *data2 = NULL;
size_t data2_index = 0;

void *new_realloc(void *p, size_t s) {
	if (data2 == NULL || data2_index + s >= 65536) {
		if (s >= 65536) {
			data2 = malloc(s);
		} else {
			data2 = malloc(65536);
		}
		data2_index = 0;
	}

	void *result = data2 + data2_index;
	if (p != NULL) {
		memcpy(result, p, s);
	}

	data2_index += s;
	return result;
}

#define STBDS_REALLOC(c,p,s) new_realloc(p,s)
#define STBDS_FREE(c,p) do { (void) c; (void) p; } while (0)

#include "ds.h"
