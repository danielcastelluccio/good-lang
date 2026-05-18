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

	Context context = { .codegen = codegen, .data = &data, .static_id = 1 };
	arrsetcap(context.scopes, 32);

	process_module_root(&context, internal_root);

	Scope internal_scope = {
		.node = internal_root,
		.identifiers = NULL
	};

	context.internal_root = internal_root;
	context.internal_scope = internal_scope;
	context.context_type = get_data(&context, find_define(context.internal_root, cstr_to_sv("Context")))->define.typed_value.value;

	process_module_root(&context, root);

	codegen.build_fn(context, root, codegen.data);

	return 0;
}
