#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "llvm_codegen.h"
#include "parser.h"
#include "processor.h"

#include "internal_source.h"

#include "stb/ds.h"

int main(int argc, char **argv) {
	if (argc != 2) {
		printf("Usage: %s [SOURCE]\n", argv[0]);
		return 1;
	}

	Data data = {};

	char *source_file = argv[1];
	Node *root = parse_file(&data, realpath(source_file, NULL));

	Node *internal_root = parse_source(&data, (char *) src_internal_lang, src_internal_lang_len, "internal");

	Codegen codegen = llvm_codegen();

	Context context = process(&data, internal_root, codegen);
	Scope internal_scope = {
		.node = internal_root,
		.identifiers = NULL
	};
	for (int i = 0; i < arrlen(internal_root->block.statements); i++) {
		if (internal_root->block.statements[i]->kind == DEFINE_NODE) {
			Scope_Identifier scope_identifier = {
				.tag = SCOPE_DEFINE,
				.define = internal_root->block.statements[i]
			};
			hmput(internal_scope.identifiers, sv_hash(internal_root->block.statements[i]->define.identifier), scope_identifier);
		}
	}
	context.internal_root = internal_root;
	context.internal_scope = internal_scope;

	process_node(&context, root);

	codegen.build_fn(context, root, codegen.data);

	return 0;
}
