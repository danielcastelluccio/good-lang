#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "llvm_codegen.h"
#include "parser.h"
#include "processor.h"

char *internal =
"";

int main(int argc, char **argv) {
	if (argc != 2) {
		printf("Usage: %s [SOURCE]\n", argv[0]);
		return 1;
	}

	Node *internal_root = parse_source(internal, strlen(internal), "internal");

	char *source = argv[1];
	Node *root = parse_file(realpath(source, NULL));

	Codegen codegen = llvm_codegen();

	Context context = process(internal_root, codegen);
	context.internal_root = internal_root;
	process_node(&context, root);
	codegen.build_fn(context, root, codegen.data);

	return 0;
}
