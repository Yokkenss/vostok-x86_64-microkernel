#ifndef ISKRA_BACKEND_H
#define ISKRA_BACKEND_H

#include "ast.h"
#include "hir.h"
#include "source.h"

#include <stdbool.h>
#include <stdio.h>

bool backend_emit_x86_64(FILE *output, const struct source *source,
                         const struct ast_node *program,
                         const struct hir_program *hir);

#endif
