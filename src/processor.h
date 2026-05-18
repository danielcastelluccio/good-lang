#include "common.h"

Value process_module_root(Context *context, Node *root);
Node_Data *process_node(Context *context, Node *node);
Node *find_define(Node *root, String_View identifier);
