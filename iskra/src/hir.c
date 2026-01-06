#include "hir.h"

#include "token.h"

#include <stdlib.h>

enum { SYMBOL_LIMIT = 256, LOCAL_LIMIT = 256 };

struct global_symbol {
    struct token name;
    enum hir_binding_kind kind;
    enum hir_type_kind type;
    uint32_t id;
    const struct ast_node *type_node;
};

struct local_symbol {
    struct token name;
    enum hir_type_kind type;
    uint32_t id;
    size_t depth;
    const struct ast_node *type_node;
};

struct lowerer {
    struct hir_program *hir;
    const struct source *source;
    struct global_symbol globals[SYMBOL_LIMIT];
    size_t global_count;
    struct local_symbol locals[LOCAL_LIMIT];
    size_t local_count;
    size_t depth;
    uint32_t next_local_id;
    const struct ast_node *structures[SYMBOL_LIMIT];
    size_t structure_count;
    bool failed;
};

static bool token_equal(const struct lowerer *lowerer, struct token left,
                        struct token right)
{
    if (left.length != right.length) return false;
    for (size_t index = 0; index < left.length; ++index) {
        if (lowerer->source->text[left.offset + index]
            != lowerer->source->text[right.offset + index]) return false;
    }
    return true;
}

static enum hir_type_kind type_from_ast(const struct lowerer *lowerer,
                                        const struct ast_node *node)
{
    if (node->kind == AST_TYPE_POINTER) return HIR_TYPE_POINTER;
    static const struct {
        const char *name;
        enum hir_type_kind type;
    } names[] = {
        { "void", HIR_TYPE_VOID }, { "bool", HIR_TYPE_BOOL },
        { "u8", HIR_TYPE_U8 }, { "u16", HIR_TYPE_U16 },
        { "u32", HIR_TYPE_U32 }, { "u64", HIR_TYPE_U64 },
        { "i8", HIR_TYPE_I8 }, { "i16", HIR_TYPE_I16 },
        { "i32", HIR_TYPE_I32 }, { "i64", HIR_TYPE_I64 },
        { "usize", HIR_TYPE_USIZE }, { "isize", HIR_TYPE_ISIZE },
    };
    for (size_t index = 0; index < sizeof(names) / sizeof(names[0]); ++index) {
        if (token_has_text(lowerer->source, node->token, names[index].name)) {
            return names[index].type;
        }
    }
    return node->kind == AST_TYPE_NAME ? HIR_TYPE_STRUCT : HIR_TYPE_ERROR;
}

static bool add_node(struct lowerer *lowerer, const struct ast_node *ast,
                     enum hir_type_kind type,
                     enum hir_binding_kind binding_kind, uint32_t binding_id)
{
    if (lowerer->hir->node_count == lowerer->hir->node_capacity) {
        const size_t capacity = lowerer->hir->node_capacity == 0
                              ? 128 : lowerer->hir->node_capacity * 2;
        struct hir_node *nodes = realloc(lowerer->hir->nodes,
                                         capacity * sizeof(*nodes));
        if (nodes == NULL) {
            lowerer->failed = true;
            return false;
        }
        lowerer->hir->nodes = nodes;
        lowerer->hir->node_capacity = capacity;
    }
    lowerer->hir->nodes[lowerer->hir->node_count++] = (struct hir_node) {
        .ast = ast,
        .type = type,
        .binding_kind = binding_kind,
        .binding_id = binding_id,
    };
    return true;
}

static const struct global_symbol *find_global(const struct lowerer *lowerer,
                                                struct token name)
{
    for (size_t index = 0; index < lowerer->global_count; ++index) {
        if (token_equal(lowerer, lowerer->globals[index].name, name)) {
            return &lowerer->globals[index];
        }
    }
    return NULL;
}

static const struct local_symbol *find_local(const struct lowerer *lowerer,
                                              struct token name)
{
    for (size_t index = lowerer->local_count; index > 0; --index) {
        if (token_equal(lowerer, lowerer->locals[index - 1].name, name)) {
            return &lowerer->locals[index - 1];
        }
    }
    return NULL;
}

static const struct ast_node *find_structure(const struct lowerer *lowerer,
                                              struct token name)
{
    for (size_t index = 0; index < lowerer->structure_count; ++index) {
        if (token_equal(lowerer, lowerer->structures[index]->token, name))
            return lowerer->structures[index];
    }
    return NULL;
}

static const struct ast_node *structure_for_expression(
    const struct lowerer *lowerer, const struct ast_node *node)
{
    if (node->kind == AST_STRUCT_LITERAL)
        return find_structure(lowerer, node->token);
    if (node->kind == AST_NAME) {
        const struct local_symbol *local = find_local(lowerer, node->token);
        if (local != NULL && local->type == HIR_TYPE_STRUCT)
            return find_structure(lowerer, local->type_node->token);
    }
    if (node->kind == AST_UNARY && node->token.kind == TOKEN_STAR
        && node->children[0]->kind == AST_NAME) {
        const struct local_symbol *local = find_local(
            lowerer, node->children[0]->token);
        if (local != NULL && local->type_node->kind == AST_TYPE_POINTER)
            return find_structure(lowerer,
                                  local->type_node->children[0]->token);
    }
    return NULL;
}

static enum hir_type_kind lower_expression(struct lowerer *lowerer,
                                           const struct ast_node *node);

static enum hir_type_kind lower_expression(struct lowerer *lowerer,
                                           const struct ast_node *node)
{
    enum hir_type_kind type = HIR_TYPE_ERROR;
    enum hir_binding_kind binding = HIR_BINDING_NONE;
    uint32_t binding_id = 0;
    switch (node->kind) {
    case AST_INTEGER: type = HIR_TYPE_LITERAL; break;
    case AST_BOOLEAN: type = HIR_TYPE_BOOL; break;
    case AST_NULL: type = HIR_TYPE_NULL; break;
    case AST_STRUCT_LITERAL:
        for (size_t index = 0; index < node->child_count; ++index) {
            (void)lower_expression(lowerer, node->children[index]->children[0]);
        }
        type = HIR_TYPE_STRUCT;
        break;
    case AST_FIELD_ACCESS:
        (void)lower_expression(lowerer, node->children[0]);
        {
            const struct ast_node *structure = structure_for_expression(
                lowerer, node->children[0]);
            if (structure != NULL) {
                for (size_t index = 0; index < structure->child_count;
                     ++index) {
                    if (token_equal(lowerer,
                                    structure->children[index]->token,
                                    node->token)) {
                        type = type_from_ast(
                            lowerer,
                            structure->children[index]->children[0]);
                        break;
                    }
                }
            }
        }
        break;
    case AST_NAME: {
        const struct local_symbol *local = find_local(lowerer, node->token);
        if (local != NULL) {
            type = local->type;
            binding = HIR_BINDING_LOCAL;
            binding_id = local->id;
        } else {
            const struct global_symbol *global = find_global(lowerer,
                                                              node->token);
            if (global != NULL) {
                type = global->type;
                binding = global->kind;
                binding_id = global->id;
            }
        }
        break;
    }
    case AST_CALL: {
        (void)lower_expression(lowerer, node->children[0]);
        for (size_t index = 1; index < node->child_count; ++index) {
            (void)lower_expression(lowerer, node->children[index]);
        }
        const struct global_symbol *function =
            find_global(lowerer, node->children[0]->token);
        type = function->type;
        binding = HIR_BINDING_FUNCTION;
        binding_id = function->id;
        break;
    }
    case AST_CAST:
        (void)lower_expression(lowerer, node->children[0]);
        type = type_from_ast(lowerer, node->children[1]);
        (void)add_node(lowerer, node->children[1], type, HIR_BINDING_NONE, 0);
        break;
    case AST_UNARY:
        type = lower_expression(lowerer, node->children[0]);
        if (node->token.kind == TOKEN_BANG) type = HIR_TYPE_BOOL;
        if (node->token.kind == TOKEN_AMP) type = HIR_TYPE_POINTER;
        if (node->token.kind == TOKEN_STAR
            && node->children[0]->kind == AST_NAME) {
            const struct local_symbol *local = find_local(
                lowerer, node->children[0]->token);
            if (local != NULL && local->type_node->kind == AST_TYPE_POINTER)
                type = type_from_ast(lowerer,
                                     local->type_node->children[0]);
        }
        break;
    case AST_BINARY: {
        enum hir_type_kind left = lower_expression(lowerer, node->children[0]);
        const enum hir_type_kind right = lower_expression(lowerer,
                                                          node->children[1]);
        if (left == HIR_TYPE_LITERAL) left = right;
        type = left;
        if (node->token.kind == TOKEN_EQUAL_EQUAL
            || node->token.kind == TOKEN_BANG_EQUAL
            || node->token.kind == TOKEN_LESS
            || node->token.kind == TOKEN_LESS_EQUAL
            || node->token.kind == TOKEN_GREATER
            || node->token.kind == TOKEN_GREATER_EQUAL
            || node->token.kind == TOKEN_AMP_AMP
            || node->token.kind == TOKEN_PIPE_PIPE) type = HIR_TYPE_BOOL;
        break;
    }
    default: break;
    }
    (void)add_node(lowerer, node, type, binding, binding_id);
    return type;
}

static void lower_block(struct lowerer *lowerer, const struct ast_node *block);

static void lower_statement(struct lowerer *lowerer,
                            const struct ast_node *statement)
{
    switch (statement->kind) {
    case AST_LET_DECL:
    case AST_VAR_DECL: {
        const bool explicit_type = statement->child_count == 2;
        const enum hir_type_kind value = lower_expression(
            lowerer, statement->children[explicit_type ? 1 : 0]);
        enum hir_type_kind type = explicit_type
            ? type_from_ast(lowerer, statement->children[0]) : value;
        if (type == HIR_TYPE_LITERAL) type = HIR_TYPE_I64;
        if (explicit_type) {
            (void)add_node(lowerer, statement->children[0], type,
                           HIR_BINDING_NONE, 0);
        }
        const uint32_t id = lowerer->next_local_id++;
        (void)add_node(lowerer, statement, type, HIR_BINDING_LOCAL, id);
        lowerer->locals[lowerer->local_count++] = (struct local_symbol) {
            .name = statement->token, .type = type, .id = id,
            .depth = lowerer->depth,
            .type_node = explicit_type ? statement->children[0]
                                       : statement->children[0],
        };
        break;
    }
    case AST_ASSIGNMENT:
        (void)lower_expression(lowerer, statement->children[0]);
        (void)lower_expression(lowerer, statement->children[1]);
        break;
    case AST_RETURN:
        if (statement->child_count != 0) {
            (void)lower_expression(lowerer, statement->children[0]);
        }
        break;
    case AST_IF:
        (void)lower_expression(lowerer, statement->children[0]);
        lower_block(lowerer, statement->children[1]);
        if (statement->child_count == 3) {
            if (statement->children[2]->kind == AST_BLOCK) {
                lower_block(lowerer, statement->children[2]);
            } else lower_statement(lowerer, statement->children[2]);
        }
        break;
    case AST_WHILE:
        (void)lower_expression(lowerer, statement->children[0]);
        lower_block(lowerer, statement->children[1]);
        break;
    case AST_EXPRESSION_STATEMENT:
        (void)lower_expression(lowerer, statement->children[0]);
        break;
    default: break;
    }
}

static void lower_block(struct lowerer *lowerer, const struct ast_node *block)
{
    const size_t locals = lowerer->local_count;
    ++lowerer->depth;
    for (size_t index = 0; index < block->child_count; ++index) {
        lower_statement(lowerer, block->children[index]);
    }
    --lowerer->depth;
    lowerer->local_count = locals;
}

static void collect_globals(struct lowerer *lowerer,
                            const struct ast_node *program)
{
    for (size_t index = 0; index < program->child_count; ++index) {
        if (program->children[index]->kind == AST_STRUCT_DECL)
            lowerer->structures[lowerer->structure_count++] =
                program->children[index];
    }
    for (size_t index = 0; index < program->child_count; ++index) {
        const struct ast_node *item = program->children[index];
        if (item->kind != AST_FUNCTION_DECL
            && item->kind != AST_CONST_DECL) continue;
        const enum hir_binding_kind kind = item->kind == AST_FUNCTION_DECL
                                         ? HIR_BINDING_FUNCTION
                                         : HIR_BINDING_CONSTANT;
        const enum hir_type_kind type = type_from_ast(
            lowerer, item->children[item->kind == AST_FUNCTION_DECL ? 1 : 0]);
        lowerer->globals[lowerer->global_count] = (struct global_symbol) {
            .name = item->token, .kind = kind, .type = type,
            .id = (uint32_t)lowerer->global_count,
            .type_node = item->children[
                item->kind == AST_FUNCTION_DECL ? 1 : 0],
        };
        (void)add_node(lowerer, item, type, kind,
                       (uint32_t)lowerer->global_count);
        ++lowerer->global_count;
    }
}

bool hir_lower(struct hir_program *hir, const struct source *source,
               const struct ast_node *program)
{
    struct lowerer lowerer = { .hir = hir, .source = source };
    collect_globals(&lowerer, program);
    for (size_t index = 0; index < program->child_count; ++index) {
        const struct ast_node *function = program->children[index];
        if (function->kind != AST_FUNCTION_DECL
            || (function->flags & AST_FLAG_EXTERN) != 0) continue;
        lowerer.local_count = 0;
        lowerer.depth = 0;
        const struct ast_node *parameters = function->children[0];
        for (size_t parameter = 0; parameter < parameters->child_count;
             ++parameter) {
            const struct ast_node *node = parameters->children[parameter];
            const enum hir_type_kind type = type_from_ast(&lowerer,
                                                          node->children[0]);
            const uint32_t id = lowerer.next_local_id++;
            (void)add_node(&lowerer, node, type, HIR_BINDING_LOCAL, id);
            (void)add_node(&lowerer, node->children[0], type,
                           HIR_BINDING_NONE, 0);
            lowerer.locals[lowerer.local_count++] = (struct local_symbol) {
                .name = node->token, .type = type, .id = id, .depth = 0,
                .type_node = node->children[0],
            };
        }
        lower_block(&lowerer, function->children[2]);
    }
    return !lowerer.failed;
}

void hir_destroy(struct hir_program *hir)
{
    free(hir->nodes);
    *hir = (struct hir_program) {0};
}

const struct hir_node *hir_node_for(const struct hir_program *hir,
                                    const struct ast_node *ast)
{
    for (size_t index = hir->node_count; index > 0; --index) {
        if (hir->nodes[index - 1].ast == ast) return &hir->nodes[index - 1];
    }
    return NULL;
}

enum hir_type_kind hir_type_for(const struct hir_program *hir,
                                const struct ast_node *ast)
{
    const struct hir_node *node = hir_node_for(hir, ast);
    return node == NULL ? HIR_TYPE_ERROR : node->type;
}

const char *hir_type_name(enum hir_type_kind type)
{
    static const char *const names[] = {
        "error", "void", "bool", "u8", "u16", "u32", "u64",
        "i8", "i16", "i32", "i64", "usize", "isize", "literal",
        "struct", "pointer", "null",
    };
    return names[type];
}

void hir_dump(FILE *output, const struct hir_program *hir)
{
    static const char *const bindings[] = {
        "none", "function", "constant", "local",
    };
    for (size_t index = 0; index < hir->node_count; ++index) {
        const struct hir_node *node = &hir->nodes[index];
        fprintf(output, "hir %zu:%zu type=%s binding=%s:%u\n",
                node->ast->token.line, node->ast->token.column,
                hir_type_name(node->type), bindings[node->binding_kind],
                node->binding_id);
    }
}
