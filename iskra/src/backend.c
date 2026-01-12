#include "backend.h"

#include "diagnostic.h"
#include "token.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

enum {
    BACKEND_FUNCTION_LIMIT = 128,
    BACKEND_CONSTANT_LIMIT = 128,
    BACKEND_LOCAL_LIMIT = 256,
};

enum value_type {
    VALUE_VOID,
    VALUE_BOOL,
    VALUE_U8,
    VALUE_U16,
    VALUE_U32,
    VALUE_U64,
    VALUE_I8,
    VALUE_I16,
    VALUE_I32,
    VALUE_I64,
    VALUE_USIZE,
    VALUE_ISIZE,
    VALUE_LITERAL,
    VALUE_STRUCT,
    VALUE_POINTER,
    VALUE_NULL,
};

struct backend_function {
    struct token name;
    enum value_type result;
    bool external;
};

struct backend_constant {
    struct token name;
    enum value_type type;
    uint64_t value;
};

struct local_slot {
    const struct ast_node *declaration;
    struct token name;
    enum value_type type;
    const struct ast_node *type_node;
    size_t offset;
    size_t size;
};

struct emitter {
    FILE *output;
    const struct source *source;
    const struct hir_program *hir;
    struct backend_function functions[BACKEND_FUNCTION_LIMIT];
    size_t function_count;
    struct backend_constant constants[BACKEND_CONSTANT_LIMIT];
    size_t constant_count;
    const struct ast_node *structures[BACKEND_CONSTANT_LIMIT];
    size_t structure_count;
    struct local_slot slots[BACKEND_LOCAL_LIMIT];
    size_t slot_count;
    size_t frame_bytes;
    const struct local_slot *active[BACKEND_LOCAL_LIMIT];
    size_t active_count;
    unsigned int next_label;
    unsigned int return_label;
    size_t temporary_pushes;
    enum value_type current_return_type;
    bool failed;
};

static bool token_equal(const struct emitter *emitter, struct token left,
                        struct token right)
{
    if (left.length != right.length) {
        return false;
    }
    for (size_t index = 0; index < left.length; ++index) {
        if (emitter->source->text[left.offset + index]
            != emitter->source->text[right.offset + index]) {
            return false;
        }
    }
    return true;
}

static void emit_error(struct emitter *emitter, struct token token,
                       const char *message)
{
    diagnostic_error(emitter->source, token.offset, token.length, token.line,
                     token.column, "codegen", "%s", message);
    emitter->failed = true;
}

static enum value_type parse_type(const struct emitter *emitter,
                                  const struct ast_node *node)
{
    if (node->kind == AST_TYPE_POINTER) return VALUE_POINTER;
    static const struct {
        const char *name;
        enum value_type type;
    } names[] = {
        { "void", VALUE_VOID }, { "bool", VALUE_BOOL },
        { "u8", VALUE_U8 }, { "u16", VALUE_U16 },
        { "u32", VALUE_U32 }, { "u64", VALUE_U64 },
        { "i8", VALUE_I8 }, { "i16", VALUE_I16 },
        { "i32", VALUE_I32 }, { "i64", VALUE_I64 },
        { "usize", VALUE_USIZE }, { "isize", VALUE_ISIZE },
    };
    for (size_t index = 0; index < sizeof(names) / sizeof(names[0]); ++index) {
        if (token_has_text(emitter->source, node->token, names[index].name)) {
            return names[index].type;
        }
    }
    return VALUE_STRUCT;
}

static bool is_signed(enum value_type type)
{
    return type == VALUE_I8 || type == VALUE_I16 || type == VALUE_I32
        || type == VALUE_I64 || type == VALUE_ISIZE
        || type == VALUE_LITERAL;
}

static unsigned int type_width(enum value_type type)
{
    switch (type) {
    case VALUE_BOOL:
    case VALUE_U8:
    case VALUE_I8: return 8;
    case VALUE_U16:
    case VALUE_I16: return 16;
    case VALUE_U32:
    case VALUE_I32: return 32;
    default: return 64;
    }
}

static void emit_normalize(struct emitter *emitter, enum value_type type)
{
    switch (type) {
    case VALUE_BOOL:
    case VALUE_U8: fprintf(emitter->output, "    movzbq %%al, %%rax\n"); break;
    case VALUE_U16: fprintf(emitter->output, "    movzwq %%ax, %%rax\n"); break;
    case VALUE_U32: fprintf(emitter->output, "    movl %%eax, %%eax\n"); break;
    case VALUE_I8: fprintf(emitter->output, "    movsbq %%al, %%rax\n"); break;
    case VALUE_I16: fprintf(emitter->output, "    movswq %%ax, %%rax\n"); break;
    case VALUE_I32: fprintf(emitter->output, "    movslq %%eax, %%rax\n"); break;
    default: break;
    }
}

static uint64_t parse_integer(const struct emitter *emitter, struct token token,
                              bool *valid)
{
    char buffer[96];
    if (token.length == 0 || token.length >= sizeof(buffer)) {
        *valid = false;
        return 0;
    }
    for (size_t index = 0; index < token.length; ++index) {
        buffer[index] = emitter->source->text[token.offset + index];
    }
    buffer[token.length] = '\0';
    int base = 10;
    const char *digits = buffer;
    if (token.length > 2 && buffer[0] == '0'
        && (buffer[1] == 'x' || buffer[1] == 'X')) {
        base = 16;
        digits += 2;
    } else if (token.length > 2 && buffer[0] == '0'
               && (buffer[1] == 'b' || buffer[1] == 'B')) {
        base = 2;
        digits += 2;
    }
    errno = 0;
    char *end;
    const unsigned long long value = strtoull(digits, &end, base);
    *valid = errno == 0 && *digits != '\0' && *end == '\0';
    return (uint64_t)value;
}

static const struct backend_function *find_function(
    const struct emitter *emitter, struct token name)
{
    for (size_t index = 0; index < emitter->function_count; ++index) {
        if (token_equal(emitter, emitter->functions[index].name, name)) {
            return &emitter->functions[index];
        }
    }
    return NULL;
}

static const struct backend_constant *find_constant(
    const struct emitter *emitter, struct token name)
{
    for (size_t index = 0; index < emitter->constant_count; ++index) {
        if (token_equal(emitter, emitter->constants[index].name, name)) {
            return &emitter->constants[index];
        }
    }
    return NULL;
}

static const struct ast_node *find_structure(const struct emitter *emitter,
                                              struct token name)
{
    for (size_t index = 0; index < emitter->structure_count; ++index) {
        if (token_equal(emitter, emitter->structures[index]->token, name))
            return emitter->structures[index];
    }
    return NULL;
}

static size_t align_up(size_t value, size_t alignment)
{ return (value + alignment - 1) & ~(alignment - 1); }

static size_t scalar_size(enum value_type type)
{
    switch (type) {
    case VALUE_BOOL: case VALUE_U8: case VALUE_I8: return 1;
    case VALUE_U16: case VALUE_I16: return 2;
    case VALUE_U32: case VALUE_I32: return 4;
    default: return 8;
    }
}

static size_t type_size(const struct emitter *emitter, enum value_type type,
                        const struct ast_node *type_node);

static size_t type_alignment(const struct emitter *emitter,
                             enum value_type type,
                             const struct ast_node *type_node)
{
    if (type != VALUE_STRUCT) return scalar_size(type);
    const struct ast_node *structure = type_node->kind == AST_STRUCT_DECL
        ? type_node : find_structure(emitter, type_node->token);
    size_t alignment = 1;
    for (size_t index = 0; structure != NULL
         && index < structure->child_count; ++index) {
        const struct ast_node *field_type = structure->children[index]->children[0];
        const size_t field_alignment = type_alignment(
            emitter, parse_type(emitter, field_type), field_type);
        if (field_alignment > alignment) alignment = field_alignment;
    }
    return alignment;
}

static size_t type_size(const struct emitter *emitter, enum value_type type,
                        const struct ast_node *type_node)
{
    if (type != VALUE_STRUCT) return scalar_size(type);
    const struct ast_node *structure = type_node->kind == AST_STRUCT_DECL
        ? type_node : find_structure(emitter, type_node->token);
    size_t size = 0;
    for (size_t index = 0; structure != NULL
         && index < structure->child_count; ++index) {
        const struct ast_node *field_type = structure->children[index]->children[0];
        const enum value_type field_kind = parse_type(emitter, field_type);
        size = align_up(size, type_alignment(emitter, field_kind, field_type));
        size += type_size(emitter, field_kind, field_type);
    }
    return align_up(size, type_alignment(emitter, VALUE_STRUCT, structure));
}

static const struct ast_node *find_field(const struct emitter *emitter,
                                         const struct ast_node *structure,
                                         struct token name, size_t *offset)
{
    size_t current = 0;
    for (size_t index = 0; index < structure->child_count; ++index) {
        const struct ast_node *field_type = structure->children[index]->children[0];
        const enum value_type field_kind = parse_type(emitter, field_type);
        current = align_up(current,
                           type_alignment(emitter, field_kind, field_type));
        if (token_equal(emitter, structure->children[index]->token, name)) {
            if (offset != NULL) *offset = current;
            return structure->children[index];
        }
        current += type_size(emitter, field_kind, field_type);
    }
    return NULL;
}

static size_t storage_size(const struct emitter *emitter,
                           enum value_type type,
                           const struct ast_node *type_node)
{
    if (type != VALUE_STRUCT) return 8;
    const struct ast_node *structure = type_node->kind == AST_STRUCT_DECL
                                     ? type_node
                                     : find_structure(emitter, type_node->token);
    return structure == NULL ? 8
                             : type_size(emitter, VALUE_STRUCT, structure);
}

static const struct local_slot *find_active(const struct emitter *emitter,
                                            struct token name)
{
    for (size_t index = emitter->active_count; index > 0; --index) {
        if (token_equal(emitter, emitter->active[index - 1]->name, name)) {
            return emitter->active[index - 1];
        }
    }
    return NULL;
}

static const struct local_slot *find_declaration_slot(
    const struct emitter *emitter, const struct ast_node *declaration)
{
    for (size_t index = 0; index < emitter->slot_count; ++index) {
        if (emitter->slots[index].declaration == declaration) {
            return &emitter->slots[index];
        }
    }
    return NULL;
}

static enum value_type expression_type(const struct emitter *emitter,
                                       const struct ast_node *node)
{
    const enum hir_type_kind hir_type = hir_type_for(emitter->hir, node);
    if (hir_type != HIR_TYPE_ERROR) {
        static const enum value_type mapping[] = {
            VALUE_VOID, VALUE_VOID, VALUE_BOOL, VALUE_U8, VALUE_U16,
            VALUE_U32, VALUE_U64, VALUE_I8, VALUE_I16, VALUE_I32,
            VALUE_I64, VALUE_USIZE, VALUE_ISIZE, VALUE_LITERAL,
            VALUE_STRUCT, VALUE_POINTER, VALUE_NULL,
        };
        return mapping[hir_type];
    }
    switch (node->kind) {
    case AST_INTEGER: return VALUE_LITERAL;
    case AST_BOOLEAN: return VALUE_BOOL;
    case AST_NULL: return VALUE_NULL;
    case AST_STRUCT_LITERAL: return VALUE_STRUCT;
    case AST_FIELD_ACCESS: {
        const struct ast_node *base = node->children[0];
        const struct ast_node *structure = NULL;
        if (base->kind == AST_NAME) {
            const struct local_slot *slot = find_active(emitter, base->token);
            if (slot != NULL && slot->type == VALUE_STRUCT)
                structure = find_structure(emitter, slot->type_node->token);
        } else if (base->kind == AST_UNARY && base->token.kind == TOKEN_STAR
                   && base->children[0]->kind == AST_NAME) {
            const struct local_slot *slot = find_active(
                emitter, base->children[0]->token);
            if (slot != NULL && slot->type_node->kind == AST_TYPE_POINTER)
                structure = find_structure(
                    emitter, slot->type_node->children[0]->token);
        }
        const struct ast_node *field = structure == NULL ? NULL
            : find_field(emitter, structure, node->token, NULL);
        return field == NULL ? VALUE_VOID
                             : parse_type(emitter, field->children[0]);
    }
    case AST_NAME: {
        const struct local_slot *local = find_active(emitter, node->token);
        if (local != NULL) return local->type;
        const struct backend_constant *constant =
            find_constant(emitter, node->token);
        return constant == NULL ? VALUE_VOID : constant->type;
    }
    case AST_CALL: {
        const struct backend_function *function =
            find_function(emitter, node->children[0]->token);
        return function == NULL ? VALUE_VOID : function->result;
    }
    case AST_CAST: return parse_type(emitter, node->children[1]);
    case AST_UNARY:
        if (node->token.kind == TOKEN_BANG) return VALUE_BOOL;
        if (node->token.kind == TOKEN_AMP) return VALUE_POINTER;
        if (node->token.kind == TOKEN_STAR
            && node->children[0]->kind == AST_NAME) {
            const struct local_slot *slot = find_active(
                emitter, node->children[0]->token);
            if (slot != NULL && slot->type_node->kind == AST_TYPE_POINTER)
                return parse_type(emitter, slot->type_node->children[0]);
        }
        return expression_type(emitter, node->children[0]);
    case AST_BINARY:
        if (node->token.kind == TOKEN_EQUAL_EQUAL
            || node->token.kind == TOKEN_BANG_EQUAL
            || node->token.kind == TOKEN_LESS
            || node->token.kind == TOKEN_LESS_EQUAL
            || node->token.kind == TOKEN_GREATER
            || node->token.kind == TOKEN_GREATER_EQUAL
            || node->token.kind == TOKEN_AMP_AMP
            || node->token.kind == TOKEN_PIPE_PIPE) {
            return VALUE_BOOL;
        }
        if (expression_type(emitter, node->children[0]) == VALUE_LITERAL) {
            return expression_type(emitter, node->children[1]);
        }
        return expression_type(emitter, node->children[0]);
    default: return VALUE_VOID;
    }
}

static void emit_symbol(FILE *output, const struct source *source,
                        const struct backend_function *function)
{
    if (!function->external) {
        fprintf(output, "_I%zu", function->name.length);
    }
    fprintf(output, "%.*s", (int)function->name.length,
            source->text + function->name.offset);
}

static unsigned int new_label(struct emitter *emitter)
{
    return emitter->next_label++;
}

static void emit_expression(struct emitter *emitter,
                            const struct ast_node *node);

static const struct ast_node *structure_for_expression(
    const struct emitter *emitter, const struct ast_node *node)
{
    if (node->kind == AST_STRUCT_LITERAL)
        return find_structure(emitter, node->token);
    if (node->kind == AST_NAME) {
        const struct local_slot *slot = find_active(emitter, node->token);
        if (slot != NULL && slot->type == VALUE_STRUCT)
            return find_structure(emitter, slot->type_node->token);
    }
    if (node->kind == AST_UNARY && node->token.kind == TOKEN_STAR
        && node->children[0]->kind == AST_NAME) {
        const struct local_slot *slot = find_active(
            emitter, node->children[0]->token);
        if (slot != NULL && slot->type_node->kind == AST_TYPE_POINTER)
            return find_structure(emitter,
                                  slot->type_node->children[0]->token);
    }
    return NULL;
}

static void emit_address(struct emitter *emitter, const struct ast_node *node)
{
    if (node->kind == AST_NAME) {
        const struct local_slot *slot = find_active(emitter, node->token);
        if (slot == NULL) {
            emit_error(emitter, node->token, "name is not addressable");
            return;
        }
        fprintf(emitter->output, "    leaq -%zu(%%rbp), %%rax\n",
                slot->offset);
        return;
    }
    if (node->kind == AST_UNARY && node->token.kind == TOKEN_STAR) {
        emit_expression(emitter, node->children[0]);
        return;
    }
    if (node->kind == AST_FIELD_ACCESS) {
        const struct ast_node *structure = structure_for_expression(
            emitter, node->children[0]);
        size_t offset = 0;
        if (structure == NULL
            || find_field(emitter, structure, node->token, &offset) == NULL) {
            emit_error(emitter, node->token, "cannot resolve structure field");
            return;
        }
        emit_address(emitter, node->children[0]);
        if (offset != 0)
            fprintf(emitter->output, "    addq $%zu, %%rax\n", offset);
        return;
    }
    emit_error(emitter, node->token, "expression is not addressable");
}

static void emit_load_at_rax(struct emitter *emitter, enum value_type type)
{
    switch (type) {
    case VALUE_BOOL: case VALUE_U8:
        fprintf(emitter->output, "    movzbq (%%rax), %%rax\n"); break;
    case VALUE_I8:
        fprintf(emitter->output, "    movsbq (%%rax), %%rax\n"); break;
    case VALUE_U16:
        fprintf(emitter->output, "    movzwq (%%rax), %%rax\n"); break;
    case VALUE_I16:
        fprintf(emitter->output, "    movswq (%%rax), %%rax\n"); break;
    case VALUE_U32:
        fprintf(emitter->output, "    movl (%%rax), %%eax\n"); break;
    case VALUE_I32:
        fprintf(emitter->output, "    movslq (%%rax), %%rax\n"); break;
    default: fprintf(emitter->output, "    movq (%%rax), %%rax\n"); break;
    }
}

static void emit_store_rax(struct emitter *emitter, enum value_type type,
                           const char *destination)
{
    const char *instruction = "movq";
    const char *source = "%rax";
    switch (type) {
    case VALUE_BOOL: case VALUE_U8: case VALUE_I8:
        instruction = "movb"; source = "%al"; break;
    case VALUE_U16: case VALUE_I16:
        instruction = "movw"; source = "%ax"; break;
    case VALUE_U32: case VALUE_I32:
        instruction = "movl"; source = "%eax"; break;
    default: break;
    }
    fprintf(emitter->output, "    %s %s, %s\n", instruction, source,
            destination);
}

static void emit_call(struct emitter *emitter, const struct ast_node *node)
{
    static const char *const argument_registers[] = {
        "%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9",
    };
    for (size_t index = node->child_count; index > 1; --index) {
        emit_expression(emitter, node->children[index - 1]);
        fprintf(emitter->output, "    pushq %%rax\n");
        ++emitter->temporary_pushes;
    }
    for (size_t index = 1; index < node->child_count; ++index) {
        fprintf(emitter->output, "    popq %s\n",
                argument_registers[index - 1]);
        --emitter->temporary_pushes;
    }
    const struct backend_function *function =
        find_function(emitter, node->children[0]->token);
    const bool pad_stack = (emitter->temporary_pushes & 1) != 0;
    if (pad_stack) fprintf(emitter->output, "    subq $8, %%rsp\n");
    fprintf(emitter->output, "    call ");
    emit_symbol(emitter->output, emitter->source, function);
    fputc('\n', emitter->output);
    if (pad_stack) fprintf(emitter->output, "    addq $8, %%rsp\n");
    emit_normalize(emitter, function->result);
}

static void emit_comparison(struct emitter *emitter, enum token_kind operation,
                            enum value_type operand_type)
{
    const char *condition = NULL;
    switch (operation) {
    case TOKEN_EQUAL_EQUAL: condition = "e"; break;
    case TOKEN_BANG_EQUAL: condition = "ne"; break;
    case TOKEN_LESS: condition = is_signed(operand_type) ? "l" : "b"; break;
    case TOKEN_LESS_EQUAL: condition = is_signed(operand_type) ? "le" : "be"; break;
    case TOKEN_GREATER: condition = is_signed(operand_type) ? "g" : "a"; break;
    case TOKEN_GREATER_EQUAL: condition = is_signed(operand_type) ? "ge" : "ae"; break;
    default: break;
    }
    fprintf(emitter->output,
            "    cmpq %%rcx, %%rax\n    set%s %%al\n    movzbq %%al, %%rax\n",
            condition);
}

static void emit_binary(struct emitter *emitter, const struct ast_node *node)
{
    if (node->token.kind == TOKEN_AMP_AMP
        || node->token.kind == TOKEN_PIPE_PIPE) {
        const unsigned int skip = new_label(emitter);
        const unsigned int done = new_label(emitter);
        emit_expression(emitter, node->children[0]);
        fprintf(emitter->output, "    testq %%rax, %%rax\n    j%s .L%u\n",
                node->token.kind == TOKEN_AMP_AMP ? "z" : "nz", skip);
        emit_expression(emitter, node->children[1]);
        fprintf(emitter->output,
                "    testq %%rax, %%rax\n    setne %%al\n    movzbq %%al, %%rax\n"
                "    jmp .L%u\n.L%u:\n    movq $%u, %%rax\n.L%u:\n",
                done, skip,
                node->token.kind == TOKEN_AMP_AMP ? 0u : 1u, done);
        return;
    }

    const enum value_type operand_type =
        expression_type(emitter, node->children[0]) == VALUE_LITERAL
        ? expression_type(emitter, node->children[1])
        : expression_type(emitter, node->children[0]);
    emit_expression(emitter, node->children[0]);
    fprintf(emitter->output, "    pushq %%rax\n");
    ++emitter->temporary_pushes;
    emit_expression(emitter, node->children[1]);
    fprintf(emitter->output, "    movq %%rax, %%rcx\n    popq %%rax\n");
    --emitter->temporary_pushes;

    switch (node->token.kind) {
    case TOKEN_PLUS: fprintf(emitter->output, "    addq %%rcx, %%rax\n"); break;
    case TOKEN_MINUS: fprintf(emitter->output, "    subq %%rcx, %%rax\n"); break;
    case TOKEN_STAR: fprintf(emitter->output, "    imulq %%rcx, %%rax\n"); break;
    case TOKEN_SLASH:
    case TOKEN_PERCENT:
        if (is_signed(operand_type)) {
            fprintf(emitter->output, "    cqto\n    idivq %%rcx\n");
        } else {
            fprintf(emitter->output, "    xorq %%rdx, %%rdx\n    divq %%rcx\n");
        }
        if (node->token.kind == TOKEN_PERCENT) {
            fprintf(emitter->output, "    movq %%rdx, %%rax\n");
        }
        break;
    case TOKEN_AMP: fprintf(emitter->output, "    andq %%rcx, %%rax\n"); break;
    case TOKEN_PIPE: fprintf(emitter->output, "    orq %%rcx, %%rax\n"); break;
    case TOKEN_CARET: fprintf(emitter->output, "    xorq %%rcx, %%rax\n"); break;
    case TOKEN_SHIFT_LEFT: {
        const unsigned int valid = new_label(emitter);
        fprintf(emitter->output,
                "    cmpq $%u, %%rcx\n    jb .L%u\n    ud2\n.L%u:\n"
                "    shlq %%cl, %%rax\n",
                type_width(operand_type), valid, valid);
        break;
    }
    case TOKEN_SHIFT_RIGHT:
    {
        const unsigned int valid = new_label(emitter);
        fprintf(emitter->output,
                "    cmpq $%u, %%rcx\n    jb .L%u\n    ud2\n.L%u:\n",
                type_width(operand_type), valid, valid);
        fprintf(emitter->output, is_signed(operand_type)
               ? "    sarq %%cl, %%rax\n" : "    shrq %%cl, %%rax\n");
        break;
    }
    case TOKEN_EQUAL_EQUAL:
    case TOKEN_BANG_EQUAL:
    case TOKEN_LESS:
    case TOKEN_LESS_EQUAL:
    case TOKEN_GREATER:
    case TOKEN_GREATER_EQUAL:
        emit_comparison(emitter, node->token.kind, operand_type);
        return;
    default:
        emit_error(emitter, node->token, "unsupported binary operator");
        return;
    }
    emit_normalize(emitter, operand_type);
}

static void emit_expression(struct emitter *emitter,
                            const struct ast_node *node)
{
    switch (node->kind) {
    case AST_INTEGER: {
        bool valid;
        const uint64_t value = parse_integer(emitter, node->token, &valid);
        if (!valid) {
            emit_error(emitter, node->token, "integer literal is out of range");
        }
        fprintf(emitter->output, "    movabsq $0x%llx, %%rax\n",
                (unsigned long long)value);
        break;
    }
    case AST_BOOLEAN:
        fprintf(emitter->output, "    movq $%u, %%rax\n",
                token_has_text(emitter->source, node->token, "true") ? 1u : 0u);
        break;
    case AST_NULL:
        fprintf(emitter->output, "    xorq %%rax, %%rax\n");
        break;
    case AST_STRUCT_LITERAL:
        emit_error(emitter, node->token,
                   "structure literal requires a destination");
        break;
    case AST_FIELD_ACCESS:
        emit_address(emitter, node);
        emit_load_at_rax(emitter, expression_type(emitter, node));
        break;
    case AST_NAME: {
        const struct local_slot *local = find_active(emitter, node->token);
        if (local != NULL) {
            fprintf(emitter->output, "    movq -%zu(%%rbp), %%rax\n",
                    local->offset);
        } else {
            const struct backend_constant *constant =
                find_constant(emitter, node->token);
            fprintf(emitter->output, "    movabsq $0x%llx, %%rax\n",
                    (unsigned long long)constant->value);
        }
        break;
    }
    case AST_CALL: emit_call(emitter, node); break;
    case AST_CAST:
        emit_expression(emitter, node->children[0]);
        emit_normalize(emitter, parse_type(emitter, node->children[1]));
        break;
    case AST_UNARY:
        if (node->token.kind == TOKEN_AMP) {
            emit_address(emitter, node->children[0]);
            break;
        }
        emit_expression(emitter, node->children[0]);
        if (node->token.kind == TOKEN_STAR) {
            if (expression_type(emitter, node) != VALUE_STRUCT)
                emit_load_at_rax(emitter, expression_type(emitter, node));
        } else if (node->token.kind == TOKEN_MINUS) {
            fprintf(emitter->output, "    negq %%rax\n");
        } else if (node->token.kind == TOKEN_TILDE) {
            fprintf(emitter->output, "    notq %%rax\n");
        } else if (node->token.kind == TOKEN_BANG) {
            fprintf(emitter->output,
                    "    testq %%rax, %%rax\n    sete %%al\n"
                    "    movzbq %%al, %%rax\n");
        }
        emit_normalize(emitter, expression_type(emitter, node));
        break;
    case AST_BINARY: emit_binary(emitter, node); break;
    default:
        emit_error(emitter, node->token, "unsupported expression in backend");
        break;
    }
}

static void emit_block(struct emitter *emitter, const struct ast_node *block);

static void emit_statement(struct emitter *emitter,
                           const struct ast_node *statement)
{
    switch (statement->kind) {
    case AST_LET_DECL:
    case AST_VAR_DECL: {
        const struct local_slot *slot = find_declaration_slot(emitter, statement);
        const bool explicit_type = statement->child_count == 2;
        const struct ast_node *initializer =
            statement->children[explicit_type ? 1 : 0];
        if (slot->type == VALUE_STRUCT
            && initializer->kind == AST_STRUCT_LITERAL) {
            const struct ast_node *structure = find_structure(
                emitter, initializer->token);
            for (size_t index = 0; index < initializer->child_count; ++index) {
                const struct ast_node *field = initializer->children[index];
                size_t field_offset = 0;
                (void)find_field(emitter, structure, field->token,
                                 &field_offset);
                emit_expression(emitter, field->children[0]);
                char destination[64];
                (void)snprintf(destination, sizeof(destination),
                               "%zd(%%rbp)",
                               -(ptrdiff_t)slot->offset
                               + (ptrdiff_t)field_offset);
                emit_store_rax(emitter,
                    parse_type(emitter,
                               find_field(emitter, structure, field->token,
                                          NULL)->children[0]),
                    destination);
            }
        } else {
            emit_expression(emitter, initializer);
            emit_normalize(emitter, slot->type);
            fprintf(emitter->output, "    movq %%rax, -%zu(%%rbp)\n",
                    slot->offset);
        }
        emitter->active[emitter->active_count++] = slot;
        break;
    }
    case AST_ASSIGNMENT: {
        const struct ast_node *target = statement->children[0];
        const struct local_slot *slot = target->kind == AST_NAME
            ? find_active(emitter, target->token) : NULL;
        if (slot == NULL) {
            emit_address(emitter, target);
            fprintf(emitter->output, "    pushq %%rax\n");
            ++emitter->temporary_pushes;
        }
        emit_expression(emitter, statement->children[1]);
        emit_normalize(emitter, slot == NULL
            ? expression_type(emitter, target) : slot->type);
        if (slot != NULL) {
            fprintf(emitter->output, "    movq %%rax, -%zu(%%rbp)\n",
                    slot->offset);
        } else {
            fprintf(emitter->output, "    popq %%rcx\n");
            emit_store_rax(emitter, expression_type(emitter, target),
                           "(%rcx)");
            --emitter->temporary_pushes;
        }
        break;
    }
    case AST_RETURN:
        if (statement->child_count == 0) {
            fprintf(emitter->output, "    xorq %%rax, %%rax\n");
        } else {
            emit_expression(emitter, statement->children[0]);
            emit_normalize(emitter, emitter->current_return_type);
        }
        fprintf(emitter->output, "    jmp .Lreturn%u\n", emitter->return_label);
        break;
    case AST_IF: {
        const unsigned int alternative = new_label(emitter);
        const unsigned int done = new_label(emitter);
        emit_expression(emitter, statement->children[0]);
        fprintf(emitter->output,
                "    testq %%rax, %%rax\n    jz .L%u\n", alternative);
        emit_block(emitter, statement->children[1]);
        fprintf(emitter->output, "    jmp .L%u\n.L%u:\n", done, alternative);
        if (statement->child_count == 3) {
            if (statement->children[2]->kind == AST_BLOCK) {
                emit_block(emitter, statement->children[2]);
            } else {
                emit_statement(emitter, statement->children[2]);
            }
        }
        fprintf(emitter->output, ".L%u:\n", done);
        break;
    }
    case AST_WHILE: {
        const unsigned int condition = new_label(emitter);
        const unsigned int done = new_label(emitter);
        fprintf(emitter->output, ".L%u:\n", condition);
        emit_expression(emitter, statement->children[0]);
        fprintf(emitter->output, "    testq %%rax, %%rax\n    jz .L%u\n", done);
        emit_block(emitter, statement->children[1]);
        fprintf(emitter->output, "    jmp .L%u\n.L%u:\n", condition, done);
        break;
    }
    case AST_EXPRESSION_STATEMENT:
        emit_expression(emitter, statement->children[0]);
        break;
    default:
        emit_error(emitter, statement->token,
                   "unsupported statement in backend");
        break;
    }
}

static void emit_block(struct emitter *emitter, const struct ast_node *block)
{
    const size_t active_count = emitter->active_count;
    for (size_t index = 0; index < block->child_count; ++index) {
        emit_statement(emitter, block->children[index]);
    }
    emitter->active_count = active_count;
}

static void collect_local_declarations(struct emitter *emitter,
                                       const struct ast_node *node)
{
    if (node->kind == AST_LET_DECL || node->kind == AST_VAR_DECL) {
        const bool explicit_type = node->child_count == 2;
        enum value_type type = explicit_type
                             ? parse_type(emitter, node->children[0])
                             : expression_type(emitter,
                                               node->children[explicit_type ? 1 : 0]);
        if (type == VALUE_LITERAL) type = VALUE_I64;
        const struct ast_node *type_node = explicit_type
                                         ? node->children[0]
                                         : node->children[0];
        const size_t size = storage_size(emitter, type, type_node);
        emitter->frame_bytes += size;
        emitter->slots[emitter->slot_count] = (struct local_slot) {
            .declaration = node,
            .name = node->token,
            .type = type,
            .type_node = type_node,
            .offset = emitter->frame_bytes,
            .size = size,
        };
        ++emitter->slot_count;
    }
    for (size_t index = 0; index < node->child_count; ++index) {
        collect_local_declarations(emitter, node->children[index]);
    }
}

static void emit_function(struct emitter *emitter, const struct ast_node *node)
{
    static const char *const parameter_registers[] = {
        "%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9",
    };
    if ((node->flags & AST_FLAG_EXTERN) != 0) return;

    emitter->slot_count = 0;
    emitter->frame_bytes = 0;
    emitter->active_count = 0;
    emitter->temporary_pushes = 0;
    const struct ast_node *parameters = node->children[0];
    for (size_t index = 0; index < parameters->child_count; ++index) {
        const struct ast_node *type_node = parameters->children[index]->children[0];
        const enum value_type type = parse_type(emitter, type_node);
        const size_t size = storage_size(emitter, type, type_node);
        emitter->frame_bytes += size;
        emitter->slots[emitter->slot_count] = (struct local_slot) {
            .declaration = parameters->children[index],
            .name = parameters->children[index]->token,
            .type = type,
            .type_node = type_node,
            .offset = emitter->frame_bytes,
            .size = size,
        };
        emitter->active[emitter->active_count++] =
            &emitter->slots[emitter->slot_count++];
    }
    collect_local_declarations(emitter, node->children[2]);
    const size_t frame_size = (emitter->frame_bytes + 15) & ~(size_t)15;
    const struct backend_function *function = find_function(emitter,
                                                            node->token);
    fprintf(emitter->output, ".global ");
    emit_symbol(emitter->output, emitter->source, function);
    fprintf(emitter->output, "\n.type ");
    emit_symbol(emitter->output, emitter->source, function);
    fprintf(emitter->output, ", @function\n");
    emit_symbol(emitter->output, emitter->source, function);
    fprintf(emitter->output,
            ":\n    pushq %%rbp\n    movq %%rsp, %%rbp\n"
            "    subq $%zu, %%rsp\n", frame_size);
    for (size_t index = 0; index < parameters->child_count; ++index) {
        fprintf(emitter->output, "    movq %s, -%zu(%%rbp)\n",
                parameter_registers[index], emitter->slots[index].offset);
    }
    emitter->return_label = new_label(emitter);
    emitter->current_return_type = function->result;
    emit_block(emitter, node->children[2]);
    if (function->result == VALUE_VOID) {
        fprintf(emitter->output, "    xorq %%rax, %%rax\n");
    }
    fprintf(emitter->output,
            ".Lreturn%u:\n    leave\n    ret\n.size ",
            emitter->return_label);
    emit_symbol(emitter->output, emitter->source, function);
    fprintf(emitter->output, ", . - ");
    emit_symbol(emitter->output, emitter->source, function);
    fputc('\n', emitter->output);
}

static void collect_globals(struct emitter *emitter,
                            const struct ast_node *program)
{
    for (size_t index = 0; index < program->child_count; ++index) {
        const struct ast_node *item = program->children[index];
        if (item->kind == AST_STRUCT_DECL) {
            emitter->structures[emitter->structure_count++] = item;
        } else if (item->kind == AST_FUNCTION_DECL) {
            emitter->functions[emitter->function_count++] =
                (struct backend_function) {
                    .name = item->token,
                    .result = parse_type(emitter, item->children[1]),
                    .external = (item->flags & AST_FLAG_EXTERN) != 0,
                };
        } else if (item->kind == AST_CONST_DECL) {
            bool valid = false;
            uint64_t value = 0;
            const struct ast_node *expression = item->children[1];
            if (expression->kind == AST_INTEGER) {
                value = parse_integer(emitter, expression->token, &valid);
            } else if (expression->kind == AST_BOOLEAN) {
                value = token_has_text(emitter->source, expression->token,
                                       "true");
                valid = true;
            }
            if (!valid) {
                emit_error(emitter, expression->token,
                           "backend constants must currently be literals");
            }
            emitter->constants[emitter->constant_count++] =
                (struct backend_constant) {
                    .name = item->token,
                    .type = parse_type(emitter, item->children[0]),
                    .value = value,
                };
        }
    }
}

bool backend_emit_x86_64(FILE *output, const struct source *source,
                         const struct ast_node *program,
                         const struct hir_program *hir)
{
    struct emitter emitter = {
        .output = output, .source = source, .hir = hir,
    };
    collect_globals(&emitter, program);
    fprintf(output, ".section .text\n");
    for (size_t index = 0; index < program->child_count; ++index) {
        if (program->children[index]->kind == AST_FUNCTION_DECL) {
            emit_function(&emitter, program->children[index]);
        }
    }
    fprintf(output, ".section .note.GNU-stack,\"\",@progbits\n");
    return !emitter.failed && !ferror(output);
}
