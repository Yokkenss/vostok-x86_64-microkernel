#ifndef ISKRA_TOKEN_H
#define ISKRA_TOKEN_H

#include "source.h"

#include <stdbool.h>
#include <stddef.h>

enum token_kind {
    TOKEN_EOF,
    TOKEN_IDENTIFIER,
    TOKEN_INTEGER,
    TOKEN_KW_AS,
    TOKEN_KW_BOOL,
    TOKEN_KW_CONST,
    TOKEN_KW_ELSE,
    TOKEN_KW_EXTERN,
    TOKEN_KW_FALSE,
    TOKEN_KW_FN,
    TOKEN_KW_IF,
    TOKEN_KW_LET,
    TOKEN_KW_MUT,
    TOKEN_KW_NULL,
    TOKEN_KW_PUB,
    TOKEN_KW_RETURN,
    TOKEN_KW_STRUCT,
    TOKEN_KW_TRUE,
    TOKEN_KW_VAR,
    TOKEN_KW_WHILE,
    TOKEN_KW_VOID,
    TOKEN_LEFT_PAREN,
    TOKEN_RIGHT_PAREN,
    TOKEN_LEFT_BRACE,
    TOKEN_RIGHT_BRACE,
    TOKEN_COLON,
    TOKEN_SEMICOLON,
    TOKEN_COMMA,
    TOKEN_DOT,
    TOKEN_ARROW,
    TOKEN_PLUS,
    TOKEN_MINUS,
    TOKEN_STAR,
    TOKEN_SLASH,
    TOKEN_PERCENT,
    TOKEN_BANG,
    TOKEN_TILDE,
    TOKEN_AMP,
    TOKEN_PIPE,
    TOKEN_CARET,
    TOKEN_EQUAL,
    TOKEN_EQUAL_EQUAL,
    TOKEN_BANG_EQUAL,
    TOKEN_LESS,
    TOKEN_LESS_EQUAL,
    TOKEN_GREATER,
    TOKEN_GREATER_EQUAL,
    TOKEN_SHIFT_LEFT,
    TOKEN_SHIFT_RIGHT,
    TOKEN_AMP_AMP,
    TOKEN_PIPE_PIPE,
};

struct token {
    enum token_kind kind;
    size_t offset;
    size_t length;
    size_t line;
    size_t column;
};

struct token_list {
    struct token *items;
    size_t count;
    size_t capacity;
};

const char *token_kind_name(enum token_kind kind);
bool token_has_text(const struct source *source, struct token token,
                    const char *text);
void token_print_text(const struct source *source, struct token token);
void token_list_free(struct token_list *tokens);

#endif
