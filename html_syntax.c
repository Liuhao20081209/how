#include "syntax.h"
#include <string.h>
#include <ctype.h>

const char *html_tags[] = {
    "html", "head", "body", "title", "meta", "link", "style", "script", "div", "span", "p", "a", "img", "ul", "ol", "li",
    "table", "tr", "th", "td", "form", "input", "button", "select", "option", "textarea", "h1", "h2", "h3", "h4", "h5", "h6",
    "header", "footer", "nav", "main", "section", "article", "aside", "canvas", "svg", "iframe", "br", "hr", NULL
};

const char *html_attrs[] = {
    "class", "id", "src", "href", "alt", "type", "value", "name", "placeholder", "onclick",
    "charset", "content", "lang", "rel", "width", "height", "target", NULL
};

const char *css_keywords[] = {
    "color", "background", "background-color", "margin", "padding", "border", "border-radius",
    "display", "flex", "grid", "position", "top", "left", "right", "bottom", "width", "height",
    "font", "font-size", "font-family", "font-weight", "text-align", "line-height", "box-sizing",
    "var", "root", "media", "max-width", "min-width", "auto", "none", "block", "inline", NULL
};

static int match_word(const char *line, int x, int len, const char *w) {
    int k = (int)strlen(w);
    if (x + k > len) return 0;
    if (strncmp(&line[x], w, k) != 0) return 0;
    if (x + k < len && (is_ident_char((unsigned char)line[x + k]) || line[x + k] == '-')) return 0;
    return 1;
}

static int parse_css(const char *line, int x, int len, int *in_string) {
    if (*in_string) {
        char q = (char)(*in_string & 0xFF);
        if (line[x] == q && (x == 0 || line[x - 1] != '\\')) *in_string = 0;
        return CP_STRING;
    }
    if (line[x] == '"' || line[x] == '\'') { *in_string = line[x]; return CP_STRING; }
    if (line[x] == '#') return CP_ERROR;

    if (is_word_start(line, x)) {
        for (int i = 0; css_keywords[i]; i++) {
            if (match_word(line, x, len, css_keywords[i])) return CP_CSS_PROP;
        }
    }
    return 0;
}

// 判断当前位置的 "word" 前面（跳过空白）是否是 '<'，用来区分 <style> 和 </style> 的 style
static int preceded_by_lt(const char *line, int x) {
    int i = x - 1;
    while (i >= 0 && (line[i] == ' ' || line[i] == '\t')) i--;
    return i >= 0 && line[i] == '<';
}

int parse_html_syntax(const char *line, int x, int len, int *in_string, int *in_comment) {
    // bit 0x01 = HTML 注释 <!-- ... -->
    // 10        = CSS 模式
    // 20        = JS  模式
    if ((*in_comment & 0xFF) == 1) {
        if (x + 2 < len && line[x] == '-' && line[x + 1] == '-' && line[x + 2] == '>') { *in_comment = 0; }
        return CP_COMMENT;
    }
    if (x + 3 < len && line[x] == '<' && line[x + 1] == '!' && line[x + 2] == '-' && line[x + 3] == '-') {
        *in_comment = 1; return CP_COMMENT;
    }

    // 中文直接跳过，不干扰标签
    if ((unsigned char)line[x] >= 0x80) return 0;

    if (*in_comment == 10) {
        // CSS 模式：检测 </style
        if (x + 7 <= len && strncmp(&line[x], "</style", 7) == 0) { *in_comment = 0; return CP_HTML_TAG; }
        return parse_css(line, x, len, in_string);
    }
    if (*in_comment == 20) {
        // JS 模式：检测 </script
        if (x + 8 <= len && strncmp(&line[x], "</script", 8) == 0) { *in_comment = 0; return CP_HTML_TAG; }
        int dummy_block = 0;
        return parse_js_syntax(line, x, len, in_string, in_comment, &dummy_block);
    }

    if (*in_string) {
        char q = (char)(*in_string & 0xFF);
        if (line[x] == q && (x == 0 || line[x - 1] != '\\')) { *in_string = 0; }
        return CP_STRING;
    }

    if (line[x] == '<' || line[x] == '>') return CP_HTML_TAG;

    if (line[x] == '"' && (x == 0 || line[x - 1] != '\\')) { *in_string = '"'; return CP_STRING; }
    if (line[x] == '\'' && (x == 0 || line[x - 1] != '\\')) { *in_string = '\''; return CP_STRING; }

    if (is_word_start(line, x)) {
        for (int i = 0; html_tags[i]; i++) {
            if (match_word(line, x, len, html_tags[i])) {
                // 只有 <style> / <script> 才进入子模式，</style> / </script> 不进入
                if (preceded_by_lt(line, x)) {
                    if (strncmp(&line[x], "style", 5) == 0 && !is_ident_char((unsigned char)line[x + 5])) *in_comment = 10;
                    if (strncmp(&line[x], "script", 6) == 0 && !is_ident_char((unsigned char)line[x + 6])) *in_comment = 20;
                }
                return CP_HTML_TAG;
            }
        }
        for (int i = 0; html_attrs[i]; i++) {
            if (match_word(line, x, len, html_attrs[i])) return CP_HTML_ATTR;
        }
    }
    return 0;
}