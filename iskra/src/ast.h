#ifndef ISKRA_AST_H
#define ISKRA_AST_H

#include "source.h"
#include "token.h"

#include <stdbool.h>
#include <stdio.h>

enum ast_kind {
    AST_PROGRAM,
    AST_CONST_DECL,
    AST_STRUCT_DECL,
    AST_FIELD_DECL,
    AST_FUNCTION_DECL,
    AST_PARAMETERS,
    AST_PARAMETER,
    AST_TYPE_NAME,
    AST_TYPE_POINTER,
    AST_BLOCK,
    AST_LET_DECL,
    AST_VAR_DECL,
    AST_ASSIGNMENT,
    AST_RETURN,
    AST_IF,
    AST_WHILE,
    AST_EXPRESSION_STATEMENT,
    AST_CALL,
    AST_FIELD_ACCESS,
    AST_STRUCT_LITERAL,
    AST_FIELD_INITIALIZER,
    AST_NAME,
    AST_INTEGER,
    AST_BOOLEAN,
    AST_NULL,
    AST_UNARY,
    AST_CAST,
    AST_BINARY,
};

enum ast_flags {
    AST_FLAG_PUBLIC = 1 << 0,
    AST_FLAG_EXTERN = 1 << 1,
    AST_FLAG_MUTABLE = 1 << 2,
};

struct ast_node {
    enum ast_kind kind;
    struct token token;
    unsigned int flags;
    struct ast_node **children;
    size_t child_count;
    size_t child_capacity;
};

struct ast_node *ast_create(enum ast_kind kind, struct token token);
bool ast_add_child(struct ast_node *parent, struct ast_node *child);
void ast_destroy(struct ast_node *node);
void ast_dump(FILE *output, const struct source *source,
              const struct ast_node *node);

#endif
