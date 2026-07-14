#include "ast.h"

#include <stdlib.h>

static const char *ast_kind_name(enum ast_kind kind)
{
    static const char *const names[] = {
        "program", "const", "struct", "field", "fn", "parameters",
        "parameter", "type", "pointer-type", "array-type", "block", "let", "var",
        "assign", "return", "if", "while", "expression-statement", "call",
        "field-access", "index", "array-literal", "struct-literal",
        "field-initializer", "name", "integer", "string", "boolean",
        "null", "unary", "cast", "binary",
    };
    return names[kind];
}

static bool prints_token(enum ast_kind kind)
{
    switch (kind) {
    case AST_CONST_DECL:
    case AST_STRUCT_DECL:
    case AST_FIELD_DECL:
    case AST_FUNCTION_DECL:
    case AST_PARAMETER:
    case AST_TYPE_NAME:
    case AST_TYPE_ARRAY:
    case AST_LET_DECL:
    case AST_VAR_DECL:
    case AST_FIELD_ACCESS:
    case AST_INDEX:
    case AST_STRUCT_LITERAL:
    case AST_FIELD_INITIALIZER:
    case AST_NAME:
    case AST_INTEGER:
    case AST_STRING:
    case AST_BOOLEAN:
    case AST_UNARY:
    case AST_BINARY:
        return true;
    default:
        return false;
    }
}

struct ast_node *ast_create(enum ast_kind kind, struct token token)
{
    struct ast_node *node = calloc(1, sizeof(*node));
    if (node != NULL) {
        node->kind = kind;
        node->token = token;
    }
    return node;
}

bool ast_add_child(struct ast_node *parent, struct ast_node *child)
{
    if (child == NULL) {
        return false;
    }
    if (parent->child_count == parent->child_capacity) {
        const size_t capacity = parent->child_capacity == 0
            ? 4 : parent->child_capacity * 2;
        struct ast_node **children = realloc(parent->children,
                                             capacity * sizeof(*children));
        if (children == NULL) {
            return false;
        }
        parent->children = children;
        parent->child_capacity = capacity;
    }
    parent->children[parent->child_count++] = child;
    return true;
}

void ast_destroy(struct ast_node *node)
{
    if (node == NULL) {
        return;
    }
    for (size_t index = 0; index < node->child_count; ++index) {
        ast_destroy(node->children[index]);
    }
    free(node->children);
    free(node);
}

static void dump_node(FILE *output, const struct source *source,
                      const struct ast_node *node, unsigned int depth)
{
    for (unsigned int index = 0; index < depth; ++index) {
        fputs("  ", output);
    }
    fprintf(output, "(%s", ast_kind_name(node->kind));
    if (prints_token(node->kind)) {
        fputc(' ', output);
        fprintf(output, "%.*s", (int)node->token.length,
                source->text + node->token.offset);
    }
    if ((node->flags & AST_FLAG_PUBLIC) != 0) {
        fputs(" public", output);
    }
    if ((node->flags & AST_FLAG_EXTERN) != 0) {
        fputs(" extern", output);
    }
    if ((node->flags & AST_FLAG_MUTABLE) != 0) {
        fputs(" mutable", output);
    }
    if (node->child_count == 0) {
        fputs(")\n", output);
        return;
    }
    fputc('\n', output);
    for (size_t index = 0; index < node->child_count; ++index) {
        dump_node(output, source, node->children[index], depth + 1);
    }
    for (unsigned int index = 0; index < depth; ++index) {
        fputs("  ", output);
    }
    fputs(")\n", output);
}

void ast_dump(FILE *output, const struct source *source,
              const struct ast_node *node)
{
    dump_node(output, source, node, 0);
}
