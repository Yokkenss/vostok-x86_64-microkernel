#ifndef ISKRA_HIR_H
#define ISKRA_HIR_H

#include "ast.h"
#include "source.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

enum hir_type_kind {
    HIR_TYPE_ERROR,
    HIR_TYPE_VOID,
    HIR_TYPE_BOOL,
    HIR_TYPE_U8,
    HIR_TYPE_U16,
    HIR_TYPE_U32,
    HIR_TYPE_U64,
    HIR_TYPE_I8,
    HIR_TYPE_I16,
    HIR_TYPE_I32,
    HIR_TYPE_I64,
    HIR_TYPE_USIZE,
    HIR_TYPE_ISIZE,
    HIR_TYPE_LITERAL,
    HIR_TYPE_STRUCT,
    HIR_TYPE_POINTER,
    HIR_TYPE_NULL,
};

enum hir_binding_kind {
    HIR_BINDING_NONE,
    HIR_BINDING_FUNCTION,
    HIR_BINDING_CONSTANT,
    HIR_BINDING_LOCAL,
};

struct hir_node {
    const struct ast_node *ast;
    enum hir_type_kind type;
    enum hir_binding_kind binding_kind;
    uint32_t binding_id;
};

struct hir_program {
    struct hir_node *nodes;
    size_t node_count;
    size_t node_capacity;
};

bool hir_lower(struct hir_program *hir, const struct source *source,
               const struct ast_node *program);
void hir_destroy(struct hir_program *hir);
const struct hir_node *hir_node_for(const struct hir_program *hir,
                                    const struct ast_node *ast);
enum hir_type_kind hir_type_for(const struct hir_program *hir,
                                const struct ast_node *ast);
const char *hir_type_name(enum hir_type_kind type);
void hir_dump(FILE *output, const struct hir_program *hir);

#endif
