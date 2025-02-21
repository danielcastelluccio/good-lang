#include "ast.h"
#include "processor.h"

bool type_assignable(Value *type1, Value *type2);
bool value_equal(Value *value1, Value *value2);
Value *evaluate(Context *context, Node *node);
