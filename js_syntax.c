#include "syntax.h"
#include <string.h>
#include <ctype.h>

const char *js_ctrl[] = { "if", "else", "for", "while", "switch", "case", "default", "return", "break", "continue", "try", "catch", "finally", NULL };
const char *js_key[]  = { "function", "class", "extends", "super", "import", "export", "from", "as", "async", "await", "yield", "new", "delete", "typeof", "instanceof", "in", "void", NULL };
const char *js_lib[]  = { "console", "log", "document", "window", "Math", "JSON", "Array", "Object", "String", "Number", "Boolean", "Promise", "setTimeout", NULL };
const char *js_val[]  = { "true", "false", "null", "undefined", "NaN", "this", "let", "var", "const", NULL };

static int match_word(const char *line, int x, int len, const char *w) {
    int k = (int)strlen(w);
    if (x + k > len) return 0;
    if (strncmp(&line[x], w, k) != 0) return 0;
    if (x + k < len && is_ident_char((unsigned char)line[x + k])) return 0;
    return 1;
}

int parse_js_syntax(const char *line, int x, int len, int *in_string, int *in_comment, int *in_block) {
    if (*in_block) {
        if (x + 1 < len && line[x] == '*' && line[x + 1] == '/') { *in_block = 0; }
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

    if (line[x] == '"' && (x == 0 || line[x - 1] != '\\')) { *in_string = '"'; return CP_STRING; }
    if (line[x] == '\'' && (x == 0 || line[x - 1] != '\\')) { *in_string = '\''; return CP_STRING; }
    if (line[x] == '`' && (x == 0 || line[x - 1] != '\\')) { *in_string = '`'; return CP_STRING; }

    if (isdigit((unsigned char)line[x]) && (x == 0 || !is_ident_char((unsigned char)line[x - 1]))) return CP_NUMBER;

    if (line[x] == '(' || line[x] == ')' || line[x] == '{' || line[x] == '}' ||
        line[x] == '[' || line[x] == ']' || line[x] == ';' || line[x] == ',') return CP_OPERATOR;

    if (is_word_start(line, x)) {
        for (int i = 0; js_ctrl[i]; i++) if (match_word(line, x, len, js_ctrl[i])) return CP_WARNING;
        for (int i = 0; js_key[i]; i++)  if (match_word(line, x, len, js_key[i]))  return CP_CONTROL;
        for (int i = 0; js_val[i]; i++)  if (match_word(line, x, len, js_val[i]))  return CP_HTML_ATTR;
        for (int i = 0; js_lib[i]; i++)  if (match_word(line, x, len, js_lib[i]))  return CP_TIME;
    }
    return 0;
}