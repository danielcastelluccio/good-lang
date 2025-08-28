#include <string.h>

#include "string_view.h"

bool sv_eq_cstr(String_View sv, char *cstr) {
	for (size_t i = 0; i < sv.len; i++) {
		char cstr_char = cstr[i];

		if (cstr_char == '\0' || cstr_char != sv.ptr[i]) {
			return false;
		}
	}

	return cstr[sv.len] == '\0';
}

bool sv_eq(String_View sv1, String_View sv2) {
	if (sv1.len != sv2.len) return false;

	for (size_t i = 0; i < sv1.len; i++) {
		if (sv1.ptr[i] != sv2.ptr[i]) return false;
	}

	return true;
}

String_View cstr_to_sv(char *cstr) {
	return (String_View) {
		.ptr = cstr,
		.len = strlen(cstr)
	};
}

#define STBDS_SIZE_T_BITS           ((sizeof (size_t)) * 8)
#define STBDS_ROTATE_LEFT(val, n)   (((val) << (n)) | ((val) >> (STBDS_SIZE_T_BITS - (n))))
#define STBDS_ROTATE_RIGHT(val, n)  (((val) >> (n)) | ((val) << (STBDS_SIZE_T_BITS - (n))))

size_t sv_hash(String_View sv)
{
	size_t seed = 0;
	size_t hash = seed;
	for (size_t i = 0; i < sv.len; i++) {
		hash = STBDS_ROTATE_LEFT(hash, 9) + (unsigned char) sv.ptr[i];
	}

	// Thomas Wang 64-to-32 bit mix function, hopefully also works in 32 bits
	hash ^= seed;
	hash = (~hash) + (hash << 18);
	hash ^= hash ^ STBDS_ROTATE_RIGHT(hash,31);
	hash = hash * 21;
	hash ^= hash ^ STBDS_ROTATE_RIGHT(hash,11);
	hash += (hash << 6);
	hash ^= STBDS_ROTATE_RIGHT(hash,22);
	return hash+seed;
}
