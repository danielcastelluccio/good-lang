#include <stdio.h>

#include "llvm_codegen.h"
#include "parser.h"
#include "processor.h"

int main(int argc, char **argv) {
	if (argc != 2) {
		printf("Usage: %s [SOURCE]\n", argv[0]);
		return 1;
	}

	char *source = argv[1];
	Node *root = parse_file(source);
	Context context = process(root);
	build_llvm(context, root);

	return 0;
}
