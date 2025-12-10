#ifndef ISKRA_PARSER_H
#define ISKRA_PARSER_H

#include "ast.h"
#include "source.h"
#include "token.h"

struct ast_node *parser_parse(const struct source *source,
                              const struct token_list *tokens);

#endif
