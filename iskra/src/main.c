#include "ast.h"
#include "backend.h"
#include "lexer.h"
#include "hir.h"
#include "parser.h"
#include "semantic.h"
#include "source.h"
#include "token.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool read_source(const char *path, struct source *source, char **storage)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "iskrac: cannot open %s: %s\n", path, strerror(errno));
        return false;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fprintf(stderr, "iskrac: cannot seek %s\n", path);
        fclose(file);
        return false;
    }
    const long length = ftell(file);
    if (length < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fprintf(stderr, "iskrac: cannot determine size of %s\n", path);
        fclose(file);
        return false;
    }

    char *text = malloc((size_t)length + 1);
    if (text == NULL) {
        fprintf(stderr, "iskrac: out of memory reading %s\n", path);
        fclose(file);
        return false;
    }
    if (fread(text, 1, (size_t)length, file) != (size_t)length) {
        fprintf(stderr, "iskrac: cannot read %s\n", path);
        free(text);
        fclose(file);
        return false;
    }
    fclose(file);
    text[length] = '\0';
    *storage = text;
    *source = (struct source) { path, text, (size_t)length };
    return true;
}

static void dump_tokens(const struct source *source,
                        const struct token_list *tokens)
{
    for (size_t index = 0; index < tokens->count; ++index) {
        const struct token token = tokens->items[index];
        printf("%zu:%zu %-12s", token.line, token.column,
               token_kind_name(token.kind));
        if (token.kind != TOKEN_EOF) {
            fputc(' ', stdout);
            token_print_text(source, token);
        }
        fputc('\n', stdout);
    }
}

static void usage(FILE *output)
{
    fputs("usage: iskrac (--dump-tokens | --dump-ast | --check | --emit-hir | --emit-asm) FILE\n",
          output);
}

int main(int argument_count, char **arguments)
{
    if (argument_count != 3
        || (strcmp(arguments[1], "--dump-tokens") != 0
            && strcmp(arguments[1], "--dump-ast") != 0
            && strcmp(arguments[1], "--check") != 0
            && strcmp(arguments[1], "--emit-hir") != 0
            && strcmp(arguments[1], "--emit-asm") != 0)) {
        usage(stderr);
        return 2;
    }

    struct source source;
    struct token_list tokens = {0};
    struct ast_node *program = NULL;
    struct hir_program hir = {0};
    char *storage = NULL;
    int status = 1;

    if (!read_source(arguments[2], &source, &storage)
        || !lexer_scan(&source, &tokens)) {
        goto done;
    }
    if (strcmp(arguments[1], "--dump-tokens") == 0) {
        dump_tokens(&source, &tokens);
        status = 0;
        goto done;
    }

    program = parser_parse(&source, &tokens);
    if (program != NULL) {
        if (strcmp(arguments[1], "--dump-ast") == 0) {
            ast_dump(stdout, &source, program);
            status = 0;
        } else if (semantic_check(&source, program)) {
            if (hir_lower(&hir, &source, program)) {
                if (strcmp(arguments[1], "--emit-hir") == 0) {
                    hir_dump(stdout, &hir);
                }
                status = strcmp(arguments[1], "--emit-asm") == 0
                       && !backend_emit_x86_64(stdout, &source, program, &hir);
            }
        }
    }

done:
    hir_destroy(&hir);
    ast_destroy(program);
    token_list_free(&tokens);
    free(storage);
    return status;
}
