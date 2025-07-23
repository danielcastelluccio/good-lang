#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "llvm_codegen.h"
#include "parser.h"
#include "processor.h"

#include "internal_source.h"

int main(int argc, char **argv) {
	if (argc != 2) {
		printf("Usage: %s [SOURCE]\n", argv[0]);
		return 1;
	}

	// Node *internal_root = parse_source((char *) src_internal_lang, src_internal_lang_len, "internal");

	char *source_file = argv[1];
	FILE *file = fopen(realpath(source_file, NULL), "r");

	fseek(file, 0, SEEK_END);
	long length = ftell(file);
	fseek(file, 0, SEEK_SET);

	char *contents = malloc(length);
	fread(contents, length, 1, file);
	fclose(file);

	for (long int i = 0; i < 10000; i++) {
		Node *root = parse_source(contents, length, "test.lang");
		(void) root;
	}

	// char *source_file = argv[1];
	// Node *root = parse_file(realpath(source_file, NULL));

	// Codegen codegen = llvm_codegen();

	// Context context = process(internal_root, codegen);
	// context.internal_root = internal_root;
	// process_node(&context, root);
	// codegen.build_fn(context, root, codegen.data);

	return 0;
}
