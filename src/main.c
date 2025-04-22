#include <stdio.h>
#include <stdlib.h>

#include "llvm_codegen.h"
#include "parser.h"
#include "processor.h"

int main(int argc, char **argv) {
	if (argc != 2) {
		printf("Usage: %s [SOURCE]\n", argv[0]);
		return 1;
	}

	char *source = argv[1];
	Node *root = parse_file(realpath(source, NULL));

	Codegen codegen = llvm_codegen();

	Context context = process(root, codegen);
	codegen.build_fn(context, root, codegen.data);

	return 0;
}
