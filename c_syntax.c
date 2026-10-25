#include "syntax.h"
#include <string.h>
#include <ctype.h>

const char *c_preproc[] = { "include", "define", "ifdef", "ifndef", "endif", "undef", "pragma", "error", NULL };
const char *c_ctrl[] = { "if", "else", "switch", "case", "default", "for", "while", "do", "break", "continue", "goto", "return", NULL };
const char *c_type[] = { "int", "char", "float", "double", "void", "short", "long", "signed", "unsigned", "struct", "union", "enum", "_Bool", "bool", NULL };
const char *c_mod[]  = { "const", "static", "extern", "volatile", "register", "inline", "restrict", "typedef", "sizeof", NULL };
const char *c_lib[]  = { "printf", "scanf", "malloc", "free", "fopen", "fclose", "fgets", "fputs", "FILE", "stdin", "stdout", "stderr", NULL };

static int match_word(const char *line, int x, int len, const char *w) {
    int k = (int)strlen(w);
    if (x + k > len) return 0;
    if (strncmp(&line[x], w, k) != 0) return 0;
    // 后缀必须是标识符边界
    if (x + k < len && is_ident_char((unsigned char)line[x + k])) return 0;
    return 1;
}

int parse_c_syntax(const char *line, int x, int len, int *in_string, int *in_comment, int *in_block) {
    // 跨行块注释
    if (*in_block) {
        if (x + 1 < len && line[x] == '*' && line[x + 1] == '/') { *in_block = 0; }
        return CP_COMMENT;
    }
    // 跨行字符串（C 中 '\' 续行的情况在状态机里也要考虑）
    if (*in_string) {
        if (*in_string == 1 && line[x] == '"' && (x == 0 || line[x - 1] != '\\')) *in_string = 0;
        if (*in_string == 2 && line[x] == '\'' && (x == 0 || line[x - 1] != '\\')) *in_string = 0;
        return CP_STRING;
    }
    if (*in_comment) return CP_COMMENT;

    if (x + 1 < len && line[x] == '/' && line[x + 1] == '/') { *in_comment = 1; return CP_COMMENT; }
    if (x + 1 < len && line[x] == '/' && line[x + 1] == '*') { *in_block = 1; return CP_COMMENT; }

    if (line[x] == '"' && (x == 0 || line[x - 1] != '\\')) { *in_string = 1; return CP_STRING; }
    if (line[x] == '\'' && (x == 0 || line[x - 1] != '\\')) { *in_string = 2; return CP_STRING; }

    if (line[x] == '#') return CP_ERROR;

    if (isdigit((unsigned char)line[x]) && (x == 0 || !is_ident_char((unsigned char)line[x - 1]))) return CP_NUMBER;

    if (line[x] == '(' || line[x] == ')' || line[x] == '{' || line[x] == '}' ||
        line[x] == '[' || line[x] == ']' || line[x] == ';' || line[x] == ',' ||
        line[x] == '+' || line[x] == '-' || line[x] == '*' || line[x] == '/' ||
        line[x] == '=' || line[x] == '<' || line[x] == '>' || line[x] == '!' ||
        line[x] == '&' || line[x] == '|' || line[x] == '^' || line[x] == '~' || line[x] == '%') {
        return CP_OPERATOR;
    }

    if (is_word_start(line, x)) {
        for (int i = 0; c_preproc[i]; i++) if (match_word(line, x, len, c_preproc[i])) return CP_PREPROC;
        for (int i = 0; c_ctrl[i]; i++)   if (match_word(line, x, len, c_ctrl[i]))   return CP_WARNING;
        for (int i = 0; c_mod[i]; i++)    if (match_word(line, x, len, c_mod[i]))    return CP_CONTROL;
        for (int i = 0; c_type[i]; i++)   if (match_word(line, x, len, c_type[i]))   return CP_KEYWORD;
        for (int i = 0; c_lib[i]; i++)    if (match_word(line, x, len, c_lib[i]))    return CP_TIME;
    }
    return 0;
}