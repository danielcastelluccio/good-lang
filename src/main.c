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

	Data data = {};

	for (int i = 0; i < 1; i++) {
		char *source_file = argv[1];
		Node *root = parse_file(&data, realpath(source_file, NULL));

		Node *internal_root = parse_source(&data, (char *) src_internal_lang, src_internal_lang_len, "internal");

		Codegen codegen = llvm_codegen();

		Context context = process(&data, internal_root, codegen);
		context.internal_root = internal_root;
		process_node(&context, root);

		codegen.build_fn(context, root, codegen.data);
	}

	return 0;
}
