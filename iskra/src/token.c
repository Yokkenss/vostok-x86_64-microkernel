#include "token.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *token_kind_name(enum token_kind kind)
{
    static const char *const names[] = {
        "eof", "identifier", "integer", "string", "as", "bool", "const", "else",
        "extern", "false", "fn", "if", "let", "mut", "null", "pub",
        "return", "struct", "true", "var", "while", "void", "(", ")",
        "{", "}", "[", "]", ":", ";", ",", ".", "->", "+", "-", "*", "/",
        "%", "!", "~", "&", "|", "^", "=", "==", "!=", "<", "<=",
        ">", ">=", "<<", ">>", "&&", "||",
    };
    return names[kind];
}

bool token_has_text(const struct source *source, struct token token,
                    const char *text)
{
    const size_t length = strlen(text);
    return token.length == length
        && memcmp(source->text + token.offset, text, length) == 0;
}

void token_print_text(const struct source *source, struct token token)
{
    printf("%.*s", (int)token.length, source->text + token.offset);
}

void token_list_free(struct token_list *tokens)
{
    free(tokens->items);
    tokens->items = NULL;
    tokens->count = 0;
    tokens->capacity = 0;
}
