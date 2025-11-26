#ifndef ISKRA_LEXER_H
#define ISKRA_LEXER_H

#include "source.h"
#include "token.h"

#include <stdbool.h>

bool lexer_scan(const struct source *source, struct token_list *tokens);

#endif
