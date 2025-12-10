#include "parser.h"

#include "diagnostic.h"

#include <stdbool.h>
#include <stddef.h>

struct parser {
    const struct source *source;
    const struct token_list *tokens;
    size_t current;
    bool failed;
    bool allow_struct_literal;
};

static struct ast_node *parse_expression(struct parser *parser, int minimum);
static struct ast_node *parse_block(struct parser *parser);
static struct ast_node *parse_if(struct parser *parser, struct token keyword);

static const struct token *current(const struct parser *parser)
{
    return &parser->tokens->items[parser->current];
}

static const struct token *previous(const struct parser *parser)
{
    return &parser->tokens->items[parser->current - 1];
}

static bool check(const struct parser *parser, enum token_kind kind)
{
    return current(parser)->kind == kind;
}

static bool match(struct parser *parser, enum token_kind kind)
{
    if (!check(parser, kind)) {
        return false;
    }
    ++parser->current;
    return true;
}

static void parse_error(struct parser *parser, const struct token *token,
                        const char *message)
{
    if (!parser->failed) {
        diagnostic_error(parser->source, token->offset, token->length,
                         token->line, token->column, "parse", "%s", message);
    }
    parser->failed = true;
}

static bool expect(struct parser *parser, enum token_kind kind,
                   const char *message)
{
    if (match(parser, kind)) {
        return true;
    }
    parse_error(parser, current(parser), message);
    return false;
}

static struct ast_node *make_node(struct parser *parser, enum ast_kind kind,
                                  struct token token)
{
    struct ast_node *node = ast_create(kind, token);
    if (node == NULL) {
        parse_error(parser, &token, "out of memory while building AST");
    }
    return node;
}

static bool add_child(struct parser *parser, struct ast_node *parent,
                      struct ast_node *child)
{
    if (child == NULL) {
        return false;
    }
    if (ast_add_child(parent, child)) {
        return true;
    }
    /* Allocation failure is already fatal for this compilation. The caller
       owns unattached nodes; leaving cleanup to process teardown avoids
       guessing which partial subtree has already changed ownership. */
    parse_error(parser, &parent->token, "out of memory while growing AST");
    return false;
}

static bool is_type_name(enum token_kind kind)
{
    return kind == TOKEN_IDENTIFIER || kind == TOKEN_KW_BOOL
        || kind == TOKEN_KW_VOID;
}

static struct ast_node *parse_type(struct parser *parser)
{
    if (match(parser, TOKEN_STAR)) {
        const struct token star = *previous(parser);
        struct ast_node *node = make_node(parser, AST_TYPE_POINTER, star);
        if (node == NULL) {
            return NULL;
        }
        if (match(parser, TOKEN_KW_MUT)) {
            node->flags |= AST_FLAG_MUTABLE;
        } else if (!expect(parser, TOKEN_KW_CONST,
                           "expected 'const' or 'mut' after '*' in pointer type")) {
            ast_destroy(node);
            return NULL;
        }
        if (!add_child(parser, node, parse_type(parser))) {
            ast_destroy(node);
            return NULL;
        }
        return node;
    }

    if (!is_type_name(current(parser)->kind)) {
        parse_error(parser, current(parser), "expected a type");
        return NULL;
    }
    const struct token name = *current(parser);
    ++parser->current;
    return make_node(parser, AST_TYPE_NAME, name);
}

static struct ast_node *parse_struct_literal(struct parser *parser,
                                             struct token name)
{
    struct ast_node *node = make_node(parser, AST_STRUCT_LITERAL, name);
    if (node == NULL) {
        return NULL;
    }
    expect(parser, TOKEN_LEFT_BRACE, "expected '{' in structure initializer");
    while (!parser->failed && !check(parser, TOKEN_RIGHT_BRACE)) {
        if (!expect(parser, TOKEN_IDENTIFIER, "expected a field name")) {
            break;
        }
        const struct token field_name = *previous(parser);
        struct ast_node *field = make_node(parser, AST_FIELD_INITIALIZER,
                                           field_name);
        if (field == NULL
            || !expect(parser, TOKEN_COLON, "expected ':' after field name")
            || !add_child(parser, field, parse_expression(parser, 1))
            || !add_child(parser, node, field)) {
            if (field != NULL && field->child_count == 0) {
                ast_destroy(field);
            }
            break;
        }
        if (!match(parser, TOKEN_COMMA)) {
            if (!check(parser, TOKEN_RIGHT_BRACE)) {
                parse_error(parser, current(parser),
                            "expected ',' or '}' after field initializer");
            }
            break;
        }
    }
    if (!expect(parser, TOKEN_RIGHT_BRACE,
                "expected '}' after structure initializer")) {
        ast_destroy(node);
        return NULL;
    }
    return node;
}

static struct ast_node *parse_primary(struct parser *parser)
{
    struct token token = *current(parser);
    struct ast_node *node;

    if (match(parser, TOKEN_INTEGER)) {
        return make_node(parser, AST_INTEGER, token);
    }
    if (match(parser, TOKEN_KW_TRUE) || match(parser, TOKEN_KW_FALSE)) {
        return make_node(parser, AST_BOOLEAN, token);
    }
    if (match(parser, TOKEN_KW_NULL)) {
        return make_node(parser, AST_NULL, token);
    }
    if (match(parser, TOKEN_IDENTIFIER)) {
        if (parser->allow_struct_literal && check(parser, TOKEN_LEFT_BRACE)) {
            return parse_struct_literal(parser, token);
        }
        node = make_node(parser, AST_NAME, token);
    } else if (match(parser, TOKEN_LEFT_PAREN)) {
        node = parse_expression(parser, 1);
        if (!expect(parser, TOKEN_RIGHT_PAREN, "expected ')' after expression")) {
            ast_destroy(node);
            return NULL;
        }
    } else {
        parse_error(parser, current(parser), "expected an expression");
        return NULL;
    }

    while (!parser->failed) {
        if (match(parser, TOKEN_LEFT_PAREN)) {
            struct ast_node *call = make_node(parser, AST_CALL, *previous(parser));
            if (call == NULL || !add_child(parser, call, node)) {
                ast_destroy(call);
                return NULL;
            }
            if (!check(parser, TOKEN_RIGHT_PAREN)) {
                do {
                    if (!add_child(parser, call, parse_expression(parser, 1))) {
                        ast_destroy(call);
                        return NULL;
                    }
                } while (match(parser, TOKEN_COMMA)
                         && !check(parser, TOKEN_RIGHT_PAREN));
            }
            if (!expect(parser, TOKEN_RIGHT_PAREN,
                        "expected ')' after function arguments")) {
                ast_destroy(call);
                return NULL;
            }
            node = call;
        } else if (match(parser, TOKEN_DOT)) {
            if (!expect(parser, TOKEN_IDENTIFIER,
                        "expected field name after '.'")) {
                ast_destroy(node);
                return NULL;
            }
            struct ast_node *field = make_node(parser, AST_FIELD_ACCESS,
                                               *previous(parser));
            if (field == NULL || !add_child(parser, field, node)) {
                ast_destroy(field);
                return NULL;
            }
            node = field;
        } else {
            break;
        }
    }
    return node;
}

static bool is_unary(enum token_kind kind)
{
    return kind == TOKEN_MINUS || kind == TOKEN_BANG || kind == TOKEN_TILDE
        || kind == TOKEN_STAR || kind == TOKEN_AMP;
}

static struct ast_node *parse_unary(struct parser *parser)
{
    if (is_unary(current(parser)->kind)) {
        const struct token operator = *current(parser);
        ++parser->current;
        struct ast_node *node = make_node(parser, AST_UNARY, operator);
        if (node == NULL) {
            return NULL;
        }
        if (operator.kind == TOKEN_AMP && match(parser, TOKEN_KW_MUT)) {
            node->flags |= AST_FLAG_MUTABLE;
        }
        if (!add_child(parser, node, parse_unary(parser))) {
            ast_destroy(node);
            return NULL;
        }
        return node;
    }

    struct ast_node *node = parse_primary(parser);
    while (node != NULL && match(parser, TOKEN_KW_AS)) {
        struct ast_node *cast = make_node(parser, AST_CAST, *previous(parser));
        if (cast == NULL || !add_child(parser, cast, node)
            || !add_child(parser, cast, parse_type(parser))) {
            ast_destroy(cast);
            return NULL;
        }
        node = cast;
    }
    return node;
}

static int binary_precedence(enum token_kind kind)
{
    switch (kind) {
    case TOKEN_PIPE_PIPE: return 1;
    case TOKEN_AMP_AMP: return 2;
    case TOKEN_PIPE: return 3;
    case TOKEN_CARET: return 4;
    case TOKEN_AMP: return 5;
    case TOKEN_EQUAL_EQUAL:
    case TOKEN_BANG_EQUAL: return 6;
    case TOKEN_LESS:
    case TOKEN_LESS_EQUAL:
    case TOKEN_GREATER:
    case TOKEN_GREATER_EQUAL: return 7;
    case TOKEN_SHIFT_LEFT:
    case TOKEN_SHIFT_RIGHT: return 8;
    case TOKEN_PLUS:
    case TOKEN_MINUS: return 9;
    case TOKEN_STAR:
    case TOKEN_SLASH:
    case TOKEN_PERCENT: return 10;
    default: return 0;
    }
}

static struct ast_node *parse_expression(struct parser *parser, int minimum)
{
    struct ast_node *left = parse_unary(parser);
    while (left != NULL) {
        const int precedence = binary_precedence(current(parser)->kind);
        if (precedence < minimum) {
            break;
        }
        const struct token operator = *current(parser);
        ++parser->current;
        struct ast_node *right = parse_expression(parser, precedence + 1);
        struct ast_node *binary = make_node(parser, AST_BINARY, operator);
        if (right == NULL || binary == NULL
            || !add_child(parser, binary, left)
            || !add_child(parser, binary, right)) {
            ast_destroy(right);
            ast_destroy(binary);
            return NULL;
        }
        left = binary;
    }
    return left;
}

static struct ast_node *parse_local(struct parser *parser, struct token keyword,
                                    bool mutable)
{
    if (!expect(parser, TOKEN_IDENTIFIER, "expected a local variable name")) {
        return NULL;
    }
    struct ast_node *node = make_node(parser,
        mutable ? AST_VAR_DECL : AST_LET_DECL, *previous(parser));
    if (node == NULL) {
        return NULL;
    }
    if (mutable) {
        node->flags |= AST_FLAG_MUTABLE;
    }
    if (match(parser, TOKEN_COLON) && !add_child(parser, node, parse_type(parser))) {
        ast_destroy(node);
        return NULL;
    }
    if (!expect(parser, TOKEN_EQUAL, "expected '=' in local declaration")
        || !add_child(parser, node, parse_expression(parser, 1))
        || !expect(parser, TOKEN_SEMICOLON, "expected ';' after local declaration")) {
        ast_destroy(node);
        return NULL;
    }
    (void)keyword;
    return node;
}

static struct ast_node *parse_if(struct parser *parser, struct token keyword)
{
    struct ast_node *node = make_node(parser, AST_IF, keyword);
    const bool previous_setting = parser->allow_struct_literal;
    parser->allow_struct_literal = false;
    struct ast_node *condition = parse_expression(parser, 1);
    parser->allow_struct_literal = previous_setting;

    if (node == NULL || !add_child(parser, node, condition)
        || !add_child(parser, node, parse_block(parser))) {
        ast_destroy(node);
        return NULL;
    }
    if (match(parser, TOKEN_KW_ELSE)) {
        struct ast_node *alternative;
        if (match(parser, TOKEN_KW_IF)) {
            alternative = parse_if(parser, *previous(parser));
        } else {
            alternative = parse_block(parser);
        }
        if (!add_child(parser, node, alternative)) {
            ast_destroy(node);
            return NULL;
        }
    }
    return node;
}

static struct ast_node *parse_statement(struct parser *parser)
{
    if (match(parser, TOKEN_KW_LET)) {
        return parse_local(parser, *previous(parser), false);
    }
    if (match(parser, TOKEN_KW_VAR)) {
        return parse_local(parser, *previous(parser), true);
    }
    if (match(parser, TOKEN_KW_RETURN)) {
        struct ast_node *node = make_node(parser, AST_RETURN, *previous(parser));
        if (node == NULL) {
            return NULL;
        }
        if (!check(parser, TOKEN_SEMICOLON)
            && !add_child(parser, node, parse_expression(parser, 1))) {
            ast_destroy(node);
            return NULL;
        }
        if (!expect(parser, TOKEN_SEMICOLON, "expected ';' after return")) {
            ast_destroy(node);
            return NULL;
        }
        return node;
    }
    if (match(parser, TOKEN_KW_IF)) {
        return parse_if(parser, *previous(parser));
    }
    if (match(parser, TOKEN_KW_WHILE)) {
        struct ast_node *node = make_node(parser, AST_WHILE, *previous(parser));
        const bool previous_setting = parser->allow_struct_literal;
        parser->allow_struct_literal = false;
        struct ast_node *condition = parse_expression(parser, 1);
        parser->allow_struct_literal = previous_setting;
        if (node == NULL || !add_child(parser, node, condition)
            || !add_child(parser, node, parse_block(parser))) {
            ast_destroy(node);
            return NULL;
        }
        return node;
    }

    struct ast_node *expression = parse_expression(parser, 1);
    if (expression == NULL) {
        return NULL;
    }
    if (match(parser, TOKEN_EQUAL)) {
        struct ast_node *assignment = make_node(parser, AST_ASSIGNMENT,
                                                *previous(parser));
        if (assignment == NULL || !add_child(parser, assignment, expression)
            || !add_child(parser, assignment, parse_expression(parser, 1))) {
            ast_destroy(assignment);
            return NULL;
        }
        expression = assignment;
    } else {
        struct ast_node *statement = make_node(parser, AST_EXPRESSION_STATEMENT,
                                               expression->token);
        if (statement == NULL || !add_child(parser, statement, expression)) {
            ast_destroy(statement);
            return NULL;
        }
        expression = statement;
    }
    if (!expect(parser, TOKEN_SEMICOLON, "expected ';' after statement")) {
        ast_destroy(expression);
        return NULL;
    }
    return expression;
}

static struct ast_node *parse_block(struct parser *parser)
{
    if (!expect(parser, TOKEN_LEFT_BRACE, "expected '{' to start block")) {
        return NULL;
    }
    struct ast_node *node = make_node(parser, AST_BLOCK, *previous(parser));
    if (node == NULL) {
        return NULL;
    }
    while (!parser->failed && !check(parser, TOKEN_RIGHT_BRACE)
           && !check(parser, TOKEN_EOF)) {
        if (!add_child(parser, node, parse_statement(parser))) {
            ast_destroy(node);
            return NULL;
        }
    }
    if (!expect(parser, TOKEN_RIGHT_BRACE, "expected '}' to close block")) {
        ast_destroy(node);
        return NULL;
    }
    return node;
}

static struct ast_node *parse_const(struct parser *parser, struct token keyword)
{
    if (!expect(parser, TOKEN_IDENTIFIER, "expected constant name")) {
        return NULL;
    }
    struct ast_node *node = make_node(parser, AST_CONST_DECL, *previous(parser));
    if (node == NULL
        || !expect(parser, TOKEN_COLON, "expected ':' after constant name")
        || !add_child(parser, node, parse_type(parser))
        || !expect(parser, TOKEN_EQUAL, "expected '=' in constant declaration")
        || !add_child(parser, node, parse_expression(parser, 1))
        || !expect(parser, TOKEN_SEMICOLON, "expected ';' after constant")) {
        ast_destroy(node);
        return NULL;
    }
    (void)keyword;
    return node;
}

static struct ast_node *parse_struct(struct parser *parser, struct token keyword)
{
    if (!expect(parser, TOKEN_IDENTIFIER, "expected structure name")) {
        return NULL;
    }
    struct ast_node *node = make_node(parser, AST_STRUCT_DECL, *previous(parser));
    if (node == NULL
        || !expect(parser, TOKEN_LEFT_BRACE, "expected '{' after structure name")) {
        ast_destroy(node);
        return NULL;
    }
    while (!parser->failed && !check(parser, TOKEN_RIGHT_BRACE)) {
        if (!expect(parser, TOKEN_IDENTIFIER, "expected field name")) {
            break;
        }
        struct ast_node *field = make_node(parser, AST_FIELD_DECL, *previous(parser));
        if (field == NULL
            || !expect(parser, TOKEN_COLON, "expected ':' after field name")
            || !add_child(parser, field, parse_type(parser))
            || !expect(parser, TOKEN_COMMA, "expected ',' after field declaration")
            || !add_child(parser, node, field)) {
            ast_destroy(field);
            break;
        }
    }
    if (!expect(parser, TOKEN_RIGHT_BRACE, "expected '}' after structure")) {
        ast_destroy(node);
        return NULL;
    }
    (void)keyword;
    return node;
}

static struct ast_node *parse_function(struct parser *parser,
                                       bool is_public, bool is_extern)
{
    if (!expect(parser, TOKEN_IDENTIFIER, "expected function name")) {
        return NULL;
    }
    struct ast_node *node = make_node(parser, AST_FUNCTION_DECL, *previous(parser));
    if (node == NULL) {
        return NULL;
    }
    if (is_public) node->flags |= AST_FLAG_PUBLIC;
    if (is_extern) node->flags |= AST_FLAG_EXTERN;

    if (!expect(parser, TOKEN_LEFT_PAREN, "expected '(' after function name")) {
        ast_destroy(node);
        return NULL;
    }
    struct ast_node *parameters = make_node(parser, AST_PARAMETERS, *previous(parser));
    if (parameters == NULL) {
        ast_destroy(node);
        return NULL;
    }
    if (!check(parser, TOKEN_RIGHT_PAREN)) {
        do {
            if (!expect(parser, TOKEN_IDENTIFIER, "expected parameter name")) {
                break;
            }
            struct ast_node *parameter = make_node(parser, AST_PARAMETER,
                                                   *previous(parser));
            if (parameter == NULL
                || !expect(parser, TOKEN_COLON, "expected ':' after parameter name")
                || !add_child(parser, parameter, parse_type(parser))
                || !add_child(parser, parameters, parameter)) {
                ast_destroy(parameter);
                break;
            }
        } while (match(parser, TOKEN_COMMA) && !check(parser, TOKEN_RIGHT_PAREN));
    }
    if (!expect(parser, TOKEN_RIGHT_PAREN, "expected ')' after parameters")
        || !add_child(parser, node, parameters)
        || !expect(parser, TOKEN_ARROW, "expected '->' before return type")
        || !add_child(parser, node, parse_type(parser))) {
        ast_destroy(node);
        return NULL;
    }

    if (is_extern) {
        if (!expect(parser, TOKEN_SEMICOLON,
                    "expected ';' after extern function declaration")) {
            ast_destroy(node);
            return NULL;
        }
    } else if (!add_child(parser, node, parse_block(parser))) {
        ast_destroy(node);
        return NULL;
    }
    return node;
}

static struct ast_node *parse_item(struct parser *parser)
{
    if (match(parser, TOKEN_KW_CONST)) {
        return parse_const(parser, *previous(parser));
    }
    if (match(parser, TOKEN_KW_STRUCT)) {
        return parse_struct(parser, *previous(parser));
    }

    const bool is_public = match(parser, TOKEN_KW_PUB);
    const bool is_extern = match(parser, TOKEN_KW_EXTERN);
    if (!expect(parser, TOKEN_KW_FN, "expected top-level declaration")) {
        return NULL;
    }
    return parse_function(parser, is_public, is_extern);
}

struct ast_node *parser_parse(const struct source *source,
                              const struct token_list *tokens)
{
    struct parser parser = {
        .source = source,
        .tokens = tokens,
        .allow_struct_literal = true,
    };
    struct ast_node *program = make_node(&parser, AST_PROGRAM, tokens->items[0]);
    if (program == NULL) {
        return NULL;
    }

    while (!parser.failed && !check(&parser, TOKEN_EOF)) {
        if (!add_child(&parser, program, parse_item(&parser))) {
            break;
        }
    }
    if (parser.failed) {
        ast_destroy(program);
        return NULL;
    }
    return program;
}
