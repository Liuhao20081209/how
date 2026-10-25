#include "syntax.h"
#include <string.h>
#include <ctype.h>

const char *py_ctrl[] = { "if", "elif", "else", "for", "while", "break", "continue", "return", "yield", "try", "except", "finally", "with", "as", NULL };
const char *py_key[]  = { "def", "class", "lambda", "import", "from", "global", "nonlocal", "assert", "del", "pass", "raise", NULL };
const char *py_lib[]  = { "print", "len", "range", "str", "int", "float", "list", "dict", "set", "tuple", "open", "input", "type", "self", "__init__", NULL };
const char *py_val[]  = { "True", "False", "None", "and", "or", "not", "in", "is", NULL };

static int match_word(const char *line, int x, int len, const char *w) {
    int k = (int)strlen(w);
    if (x + k > len) return 0;
    if (strncmp(&line[x], w, k) != 0) return 0;
    if (x + k < len && is_ident_char((unsigned char)line[x + k])) return 0;
    return 1;
}

// in_string 取值：
//   1 = 单引号字符串
//   2 = 双引号字符串
//   3 = 三引号单引号（跨行）
//   4 = 三引号双引号（跨行）
int parse_py_syntax(const char *line, int x, int len, int *in_string, int *in_comment) {
    if (*in_comment) return CP_COMMENT;

    if (*in_string) {
        char q = (*in_string == 1 || *in_string == 3) ? '\'' : '"';
        int triple = (*in_string == 3 || *in_string == 4);
        if (triple) {
            if (x + 2 < len && line[x] == q && line[x + 1] == q && line[x + 2] == q) {
                *in_string = 0;
                return CP_STRING;
            }
        } else {
            if (line[x] == q && (x == 0 || line[x - 1] != '\\')) *in_string = 0;
        }
        return CP_STRING;
    }

    if (line[x] == '#') { *in_comment = 1; return CP_COMMENT; }

    // 三引号优先
    if (x + 2 < len && line[x] == '"' && line[x + 1] == '"' && line[x + 2] == '"') { *in_string = 4; return CP_STRING; }
    if (x + 2 < len && line[x] == '\'' && line[x + 1] == '\'' && line[x + 2] == '\'') { *in_string = 3; return CP_STRING; }
    if (line[x] == '"' && (x == 0 || line[x - 1] != '\\')) { *in_string = 2; return CP_STRING; }
    if (line[x] == '\'' && (x == 0 || line[x - 1] != '\\')) { *in_string = 1; return CP_STRING; }

    if (isdigit((unsigned char)line[x]) && (x == 0 || !is_ident_char((unsigned char)line[x - 1]))) return CP_NUMBER;

    if (line[x] == '(' || line[x] == ')' || line[x] == '{' || line[x] == '}' ||
        line[x] == '[' || line[x] == ']' || line[x] == ':' || line[x] == ',') return CP_OPERATOR;

    if (is_word_start(line, x)) {
        for (int i = 0; py_ctrl[i]; i++) if (match_word(line, x, len, py_ctrl[i])) return CP_WARNING;
        for (int i = 0; py_key[i]; i++)  if (match_word(line, x, len, py_key[i]))  return CP_CONTROL;
        for (int i = 0; py_val[i]; i++)  if (match_word(line, x, len, py_val[i]))  return CP_HTML_ATTR;
        for (int i = 0; py_lib[i]; i++)  if (match_word(line, x, len, py_lib[i]))  return CP_TIME;
    }
    return 0;
}