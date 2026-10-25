#include "syntax.h"
#include <string.h>
#include <ctype.h>

const char *php_keywords[] = {
    "if", "else", "elseif", "endif", "while", "endwhile", "for", "endfor", "foreach", "endforeach",
    "switch", "case", "default", "break", "continue", "return", "function", "class", "public", "private", "protected", "static",
    "abstract", "final", "interface", "extends", "implements", "new", "clone", "try", "catch", "finally", "throw",
    "namespace", "use", "require", "include", "require_once", "include_once", "isset", "unset", "empty", "die", "exit",
    "true", "false", "null", NULL
};

const char *php_lib[] = { "echo", "print", NULL };

static int match_word(const char *line, int x, int len, const char *w) {
    int k = (int)strlen(w);
    if (x + k > len) return 0;
    if (strncmp(&line[x], w, k) != 0) return 0;
    if (x + k < len && is_ident_char((unsigned char)line[x + k])) return 0;
    return 1;
}

// in_comment 高位标记：
//   bit 0 = 单行注释
//   bit 1 = 块注释（通过 *in_block 状态机处理）
// 这里保持原样，只修词边界
int parse_php_syntax(const char *line, int x, int len, int *in_string, int *in_comment, int *in_block) {
    if (*in_block) {
        if (x + 1 < len && line[x] == '*' && line[x + 1] == '/') { *in_block = 0; return CP_COMMENT; }
        return CP_COMMENT;
    }
    if (*in_string) {
        char q = (char)(*in_string & 0xFF);
        if (line[x] == q && (x == 0 || line[x - 1] != '\\')) *in_string = 0;
        return CP_STRING;
    }
    if (*in_comment) return CP_COMMENT;

    if (x + 1 < len && line[x] == '/' && line[x + 1] == '/') { *in_comment = 1; return CP_COMMENT; }
    if (x + 1 < len && line[x] == '/' && line[x + 1] == '*') { *in_block = 1; return CP_COMMENT; }
    if (line[x] == '#') { *in_comment = 1; return CP_COMMENT; }
    if (line[x] == '"' && (x == 0 || line[x - 1] != '\\')) { *in_string = '"'; return CP_STRING; }
    if (line[x] == '\'' && (x == 0 || line[x - 1] != '\\')) { *in_string = '\''; return CP_STRING; }

    if (x + 5 <= len && strncmp(&line[x], "<?php", 5) == 0) return CP_ERROR;
    if (x + 2 <= len && strncmp(&line[x], "?>", 2) == 0) return CP_ERROR;

    if (isdigit((unsigned char)line[x]) && (x == 0 || !is_ident_char((unsigned char)line[x - 1]))) return CP_NUMBER;

    if (is_word_start(line, x)) {
        static const char *ctrl[] = { "if", "else", "elseif", "for", "foreach", "while", "switch", "return", NULL };
        for (int i = 0; ctrl[i]; i++) if (match_word(line, x, len, ctrl[i])) return CP_WARNING;
        for (int i = 0; php_keywords[i]; i++) if (match_word(line, x, len, php_keywords[i])) return CP_KEYWORD;
        for (int i = 0; php_lib[i]; i++) if (match_word(line, x, len, php_lib[i])) return CP_TIME;
    }
    return 0;
}