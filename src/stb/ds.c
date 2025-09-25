#define STB_DS_IMPLEMENTATION

#include <stdlib.h>
#include <string.h>

void *data = NULL;
size_t data_index = 0;

void *new_realloc(void *p, size_t s) {
	if (data == NULL || data_index + s >= 65536) {
		if (s >= 65536) {
			data = malloc(s);
		} else {
			data = malloc(65536);
		}
		data_index = 0;
	}

	void *result = data + data_index;
	if (p != NULL) {
		memcpy(result, p, s);
	}

	data_index += s;
	return result;
}

void fake_free(void *c, void *p) {
	(void) c;
	(void) p;
}

// #define STBDS_REALLOC(c,p,s) new_realloc(p,s)
// #define STBDS_FREE(c,p) fake_free(c, p)

#include "ds.h"
