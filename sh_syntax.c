#include "syntax.h"
#include <string.h>
#include <ctype.h>

const char *sh_ctrl[] = { "if", "then", "else", "elif", "fi", "for", "while", "in", "do", "done", "case", "esac", "until", "break", "continue", "return", "exit", NULL };
const char *sh_lib[]  = { "echo", "cd", "pwd", "ls", "cp", "mv", "rm", "mkdir", "rmdir", "touch", "cat", "grep", "awk", "sed", "find", "chmod", "chown", "ps", "kill", "top", "df", "du", "tar", "zip", "unzip", "curl", "wget", "ssh", "scp", "ping", "sudo", "apt", "pkg", "yum", "dnf", "export", "source", "alias", "unset", NULL };

static int match_word(const char *line, int x, int len, const char *w) {
    int k = (int)strlen(w);
    if (x + k > len) return 0;
    if (strncmp(&line[x], w, k) != 0) return 0;
    if (x + k < len && is_ident_char((unsigned char)line[x + k])) return 0;
    return 1;
}

int parse_sh_syntax(const char *line, int x, int len, int *in_string, int *in_comment) {
    if (*in_string) {
        char q = (char)(*in_string & 0xFF);
        if (line[x] == q && (x == 0 || line[x - 1] != '\\')) *in_string = 0;
        return CP_STRING;
    }
    if (*in_comment) return CP_COMMENT;

    if (line[x] == '#' && (x == 0 || isspace((unsigned char)line[x - 1]) || line[x - 1] == ';')) {
        *in_comment = 1; return CP_COMMENT;
    }
    if (line[x] == '"' && (x == 0 || line[x - 1] != '\\')) { *in_string = '"'; return CP_STRING; }
    if (line[x] == '\'' && (x == 0 || line[x - 1] != '\\')) { *in_string = '\''; return CP_STRING; }
    if (line[x] == '$') return CP_HTML_ATTR;

    if (isdigit((unsigned char)line[x]) && (x == 0 || !is_ident_char((unsigned char)line[x - 1]))) return CP_NUMBER;

    if (line[x] == '(' || line[x] == ')' || line[x] == '{' || line[x] == '}' ||
        line[x] == '[' || line[x] == ']' || line[x] == ';' || line[x] == ',') return CP_OPERATOR;

    if (is_word_start(line, x)) {
        for (int i = 0; sh_ctrl[i]; i++) if (match_word(line, x, len, sh_ctrl[i])) return CP_WARNING;
        for (int i = 0; sh_lib[i]; i++)  if (match_word(line, x, len, sh_lib[i]))  return CP_TIME;
    }
    return 0;
}

int parse_log_syntax(const char *line, int x, int len) {
    // 时间戳形如 HH:MM:SS 或 HH-MM-SS
    if (x + 2 < len && isdigit((unsigned char)line[x]) && (line[x + 2] == ':' || line[x + 2] == '-')) return CP_NUMBER;

    if (is_word_start(line, x)) {
        static const char *errs[]  = { "ERROR", "Error", "error", "E/", "FATAL", "Fatal", NULL };
        static const char *warns[] = { "WARN", "Warn", "warning", "W/", "WARNING", NULL };
        static const char *infos[] = { "INFO", "Info", "info", "I/", "DEBUG", "Debug", NULL };
        for (int i = 0; errs[i]; i++) {
            int k = (int)strlen(errs[i]);
            if (x + k <= len && strncmp(&line[x], errs[i], k) == 0) return CP_ERROR;
        }
        for (int i = 0; warns[i]; i++) {
            int k = (int)strlen(warns[i]);
            if (x + k <= len && strncmp(&line[x], warns[i], k) == 0) return CP_WARNING;
        }
        for (int i = 0; infos[i]; i++) {
            int k = (int)strlen(infos[i]);
            if (x + k <= len && strncmp(&line[x], infos[i], k) == 0) return CP_TIME;
        }
    }
    return 0;
}