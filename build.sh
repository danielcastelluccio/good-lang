xxd -i src/internal.lang > src/internal_source.h

FLAGS="-lLLVM -o lang -Wall -Wextra -Werror -fshort-enums"
if [[ "$1" == "release" ]]; then
	FLAGS+=" -DNDEBUG -O3 -flto -fprofile-use -fprofile-correction"
else
	FLAGS+=" -g"
fi

gcc $FLAGS src/main.c src/lexer.c src/ast.c src/parser.c src/processor.c src/evaluator.c src/common.c src/value.c src/llvm_codegen.c src/util.c src/string_view.c src/stb/ds.c
