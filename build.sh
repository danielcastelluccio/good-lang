gcc src/main.c src/lexer.c src/ast.c src/parser.c src/processor.c src/evaluator.c src/common.c src/llvm_codegen.c src/stb/ds.c -lLLVM -o lang -g -Wall -Wextra -Werror
