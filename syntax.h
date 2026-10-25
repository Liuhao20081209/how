#ifndef SYNTAX_H
#define SYNTAX_H

// 扩充的 14 色高级配色宏
#define CP_TEXT         1
#define CP_KEYWORD      2
#define CP_STRING       3
#define CP_COMMENT      4
#define CP_ERROR        5
#define CP_WARNING      6
#define CP_TIME         7
#define CP_CONTROL      8
#define CP_NUMBER       9
#define CP_HTML_TAG     10
#define CP_HTML_ATTR    11
#define CP_CSS_PROP     12
#define CP_OPERATOR     13
#define CP_PREPROC      14

// 统一标识符字符判断：字母、数字、下划线
// 所有解析器必须用这个，避免 my_int 里的 int 被误识别
static inline int is_ident_char(int c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

// 判断 x 是否是“词首”（前一个字符不是标识符字符）
static inline int is_word_start(const char *line, int x) {
    if (x <= 0) return 1;
    return !is_ident_char((unsigned char)line[x - 1]);
}

// 获取一个 UTF-8 字符的字节长度（1~4），非法首字节返回 1
static inline int utf8_seq_len(unsigned char c) {
    if (c < 0x80) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

// 获取一个 UTF-8 字符在终端上的显示宽度（1 或 2）
static inline int utf8_display_width(unsigned char c) {
    return (c >= 0xE0) ? 2 : 1;
}

int parse_c_syntax(const char *line, int x, int len, int *in_string, int *in_comment, int *in_block);
int parse_py_syntax(const char *line, int x, int len, int *in_string, int *in_comment);
int parse_html_syntax(const char *line, int x, int len, int *in_string, int *in_comment);
int parse_js_syntax(const char *line, int x, int len, int *in_string, int *in_comment, int *in_block);
int parse_php_syntax(const char *line, int x, int len, int *in_string, int *in_comment, int *in_block);
int parse_sh_syntax(const char *line, int x, int len, int *in_string, int *in_comment);
int parse_log_syntax(const char *line, int x, int len);

#endif