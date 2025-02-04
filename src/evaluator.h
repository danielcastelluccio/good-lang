#include "ast.h"
#include "processor.h"

bool value_equal(Value *value1, Value *value2);
Value *evaluate(Context *context, Node *node);
