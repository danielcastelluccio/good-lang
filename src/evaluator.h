#include "ast.h"
#include "common.h"
#include "processor.h"

bool type_assignable(Value_Data *type1, Value_Data *type2);
bool value_equal(Value_Data *value1, Value_Data *value2);
Value evaluate(Context *context, Node *node);
