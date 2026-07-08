#include "lexer.h"

#include "diagnostic.h"

#include <ctype.h>
#include <stdlib.h>

struct lexer {
    const struct source *source;
    struct token_list *tokens;
    size_t offset;
    size_t line;
    size_t column;
};

struct keyword {
    const char *text;
    enum token_kind kind;
};

static const struct keyword keywords[] = {
    {"as", TOKEN_KW_AS}, {"bool", TOKEN_KW_BOOL},
    {"const", TOKEN_KW_CONST}, {"else", TOKEN_KW_ELSE},
    {"extern", TOKEN_KW_EXTERN}, {"false", TOKEN_KW_FALSE},
    {"fn", TOKEN_KW_FN}, {"if", TOKEN_KW_IF}, {"let", TOKEN_KW_LET},
    {"mut", TOKEN_KW_MUT}, {"null", TOKEN_KW_NULL}, {"pub", TOKEN_KW_PUB},
    {"return", TOKEN_KW_RETURN}, {"struct", TOKEN_KW_STRUCT},
    {"true", TOKEN_KW_TRUE}, {"var", TOKEN_KW_VAR},
    {"while", TOKEN_KW_WHILE}, {"void", TOKEN_KW_VOID},
};

static bool at_end(const struct lexer *lexer)
{
    return lexer->offset >= lexer->source->length;
}

static char peek(const struct lexer *lexer, size_t lookahead)
{
    const size_t offset = lexer->offset + lookahead;
    return offset < lexer->source->length ? lexer->source->text[offset] : '\0';
}

static char advance(struct lexer *lexer)
{
    const char character = lexer->source->text[lexer->offset++];
    if (character == '\n') {
        ++lexer->line;
        lexer->column = 1;
    } else {
        ++lexer->column;
    }
    return character;
}

static bool append_token(struct lexer *lexer, enum token_kind kind,
                         size_t start, size_t line, size_t column)
{
    if (lexer->tokens->count == lexer->tokens->capacity) {
        const size_t capacity = lexer->tokens->capacity == 0
            ? 64 : lexer->tokens->capacity * 2;
        struct token *items = realloc(lexer->tokens->items,
                                      capacity * sizeof(*items));
        if (items == NULL) {
            diagnostic_error(lexer->source, start, 1, line, column, "memory",
                             "unable to allocate token buffer");
            return false;
        }
        lexer->tokens->items = items;
        lexer->tokens->capacity = capacity;
    }
    lexer->tokens->items[lexer->tokens->count++] = (struct token) {
        .kind = kind,
        .offset = start,
        .length = lexer->offset - start,
        .line = line,
        .column = column,
    };
    return true;
}

static bool scan_identifier(struct lexer *lexer, size_t start,
                            size_t line, size_t column)
{
    enum token_kind kind = TOKEN_IDENTIFIER;
    while (isalnum((unsigned char)peek(lexer, 0)) || peek(lexer, 0) == '_') {
        advance(lexer);
    }
    for (size_t index = 0; index < sizeof(keywords) / sizeof(keywords[0]); ++index) {
        struct token candidate = {
            .offset = start,
            .length = lexer->offset - start,
        };
        if (token_has_text(lexer->source, candidate, keywords[index].text)) {
            kind = keywords[index].kind;
            break;
        }
    }
    return append_token(lexer, kind, start, line, column);
}

static bool valid_digit(char character, unsigned int base)
{
    if (base == 2) {
        return character == '0' || character == '1';
    }
    if (base == 16) {
        return isxdigit((unsigned char)character) != 0;
    }
    return isdigit((unsigned char)character) != 0;
}

static bool scan_integer(struct lexer *lexer, size_t start,
                         size_t line, size_t column)
{
    unsigned int base = 10;
    if (peek(lexer, 0) == '0' && (peek(lexer, 1) == 'x' || peek(lexer, 1) == 'b')) {
        base = peek(lexer, 1) == 'x' ? 16 : 2;
        advance(lexer);
        advance(lexer);
        if (!valid_digit(peek(lexer, 0), base)) {
            diagnostic_error(lexer->source, start, lexer->offset - start,
                             line, column, "lex", "expected a base-%u digit", base);
            return false;
        }
    }
    while (valid_digit(peek(lexer, 0), base)) {
        advance(lexer);
    }
    if (isalnum((unsigned char)peek(lexer, 0)) || peek(lexer, 0) == '_') {
        diagnostic_error(lexer->source, lexer->offset, 1, lexer->line,
                         lexer->column, "lex", "invalid digit in integer literal");
        return false;
    }
    return append_token(lexer, TOKEN_INTEGER, start, line, column);
}

static bool scan_string(struct lexer *lexer, size_t start,
                        size_t line, size_t column)
{
    advance(lexer); /* opening quote */
    while (!at_end(lexer) && peek(lexer, 0) != '"') {
        if (peek(lexer, 0) == '\n' || peek(lexer, 0) == '\r') {
            diagnostic_error(lexer->source, start, lexer->offset - start,
                             line, column, "lex",
                             "newline in string literal");
            return false;
        }
        if (peek(lexer, 0) == '\\') {
            advance(lexer);
            const char escaped = peek(lexer, 0);
            if (escaped != 'n' && escaped != 'r' && escaped != 't'
                && escaped != '0' && escaped != '\\' && escaped != '"') {
                diagnostic_error(lexer->source, lexer->offset, 1,
                                 lexer->line, lexer->column, "lex",
                                 "unknown string escape");
                return false;
            }
        }
        advance(lexer);
    }
    if (at_end(lexer)) {
        diagnostic_error(lexer->source, start, lexer->offset - start,
                         line, column, "lex", "unterminated string literal");
        return false;
    }
    advance(lexer); /* closing quote */
    return append_token(lexer, TOKEN_STRING, start, line, column);
}

static bool skip_block_comment(struct lexer *lexer, size_t start,
                               size_t line, size_t column)
{
    advance(lexer);
    advance(lexer);
    while (!at_end(lexer)) {
        if (peek(lexer, 0) == '*' && peek(lexer, 1) == '/') {
            advance(lexer);
            advance(lexer);
            return true;
        }
        advance(lexer);
    }
    diagnostic_error(lexer->source, start, 2, line, column, "lex",
                     "unterminated block comment");
    return false;
}

static bool scan_operator(struct lexer *lexer, size_t start,
                          size_t line, size_t column, char character)
{
    enum token_kind kind;
    switch (character) {
    case '(': kind = TOKEN_LEFT_PAREN; break;
    case ')': kind = TOKEN_RIGHT_PAREN; break;
    case '{': kind = TOKEN_LEFT_BRACE; break;
    case '}': kind = TOKEN_RIGHT_BRACE; break;
    case '[': kind = TOKEN_LEFT_BRACKET; break;
    case ']': kind = TOKEN_RIGHT_BRACKET; break;
    case ':': kind = TOKEN_COLON; break;
    case ';': kind = TOKEN_SEMICOLON; break;
    case ',': kind = TOKEN_COMMA; break;
    case '.': kind = TOKEN_DOT; break;
    case '+': kind = TOKEN_PLUS; break;
    case '*': kind = TOKEN_STAR; break;
    case '%': kind = TOKEN_PERCENT; break;
    case '~': kind = TOKEN_TILDE; break;
    case '^': kind = TOKEN_CARET; break;
    case '-':
        if (peek(lexer, 0) == '>') { advance(lexer); kind = TOKEN_ARROW; }
        else { kind = TOKEN_MINUS; }
        break;
    case '/': kind = TOKEN_SLASH; break;
    case '!':
        if (peek(lexer, 0) == '=') { advance(lexer); kind = TOKEN_BANG_EQUAL; }
        else { kind = TOKEN_BANG; }
        break;
    case '=':
        if (peek(lexer, 0) == '=') { advance(lexer); kind = TOKEN_EQUAL_EQUAL; }
        else { kind = TOKEN_EQUAL; }
        break;
    case '<':
        if (peek(lexer, 0) == '=') { advance(lexer); kind = TOKEN_LESS_EQUAL; }
        else if (peek(lexer, 0) == '<') { advance(lexer); kind = TOKEN_SHIFT_LEFT; }
        else { kind = TOKEN_LESS; }
        break;
    case '>':
        if (peek(lexer, 0) == '=') { advance(lexer); kind = TOKEN_GREATER_EQUAL; }
        else if (peek(lexer, 0) == '>') { advance(lexer); kind = TOKEN_SHIFT_RIGHT; }
        else { kind = TOKEN_GREATER; }
        break;
    case '&':
        if (peek(lexer, 0) == '&') { advance(lexer); kind = TOKEN_AMP_AMP; }
        else { kind = TOKEN_AMP; }
        break;
    case '|':
        if (peek(lexer, 0) == '|') { advance(lexer); kind = TOKEN_PIPE_PIPE; }
        else { kind = TOKEN_PIPE; }
        break;
    default:
        diagnostic_error(lexer->source, start, 1, line, column, "lex",
                         "unexpected character '%c'", character);
        return false;
    }
    return append_token(lexer, kind, start, line, column);
}

bool lexer_scan(const struct source *source, struct token_list *tokens)
{
    struct lexer lexer = { source, tokens, 0, 1, 1 };

    while (!at_end(&lexer)) {
        const size_t start = lexer.offset;
        const size_t line = lexer.line;
        const size_t column = lexer.column;
        const char character = peek(&lexer, 0);

        if (isspace((unsigned char)character)) {
            advance(&lexer);
            continue;
        }
        if (character == '/' && peek(&lexer, 1) == '/') {
            while (!at_end(&lexer) && peek(&lexer, 0) != '\n') {
                advance(&lexer);
            }
            continue;
        }
        if (character == '/' && peek(&lexer, 1) == '*') {
            if (!skip_block_comment(&lexer, start, line, column)) {
                return false;
            }
            continue;
        }
        if (isalpha((unsigned char)character) || character == '_') {
            advance(&lexer);
            if (!scan_identifier(&lexer, start, line, column)) {
                return false;
            }
            continue;
        }
        if (isdigit((unsigned char)character)) {
            if (!scan_integer(&lexer, start, line, column)) {
                return false;
            }
            continue;
        }
        if (character == '"') {
            if (!scan_string(&lexer, start, line, column)) return false;
            continue;
        }

        advance(&lexer);
        if (!scan_operator(&lexer, start, line, column, character)) {
            return false;
        }
    }
    return append_token(&lexer, TOKEN_EOF, lexer.offset, lexer.line, lexer.column);
}
