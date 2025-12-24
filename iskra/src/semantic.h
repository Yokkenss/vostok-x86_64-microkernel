#ifndef ISKRA_SEMANTIC_H
#define ISKRA_SEMANTIC_H

#include "ast.h"
#include "source.h"

#include <stdbool.h>

bool semantic_check(const struct source *source, const struct ast_node *program);

#endif
