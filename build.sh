xxd -i src/internal.lang > src/internal_source.h

gcc src/main.c src/lexer.c src/ast.c src/parser.c src/processor.c src/evaluator.c src/common.c src/value.c src/llvm_codegen.c src/util.c src/stb/ds.c -lLLVM -o lang -g -Wall -Wextra -Werror
