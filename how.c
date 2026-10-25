#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ncurses.h>
#include <locale.h>
#include <ctype.h>
#include <signal.h>
#include <time.h>
#include "syntax.h"

#define CTRL(k) ((k) & 0x1f)
#define MAX_LEN 4096
#define UNDO_MAX 100
#define STATUS_TIMEOUT 2   
#define CP_ERROR_BAR 15    /* 红底白字提示栏 */

/* ---------- 工具函数 ---------- */

int get_screen_width(const char *str) {
    if (!str) return 0;
    int w = 0;
    for (int i = 0; str[i]; ) {
        int n = utf8_seq_len((unsigned char)str[i]);
        if (n <= 0) n = 1;
        w += utf8_display_width((unsigned char)str[i]);
        i += n;
    }
    return w;
}

/* ---------- 数据结构 ---------- */

typedef enum { LANG_TEXT, LANG_C, LANG_PY, LANG_HTML, LANG_JS, LANG_PHP, LANG_SH, LANG_LOG } LangType;

typedef struct UndoNode {
    char **lines;
    int num_lines;
    int cy, cx;
    struct UndoNode *prev;
} UndoNode;

typedef struct {
    char **lines;
    int num_lines;
    int capacity;
    int cy, cx;
    int row_offset;
    char filename[256];
    int is_readonly;
    int dirty;
    char status_msg[128];
    time_t status_time;
    int status_is_error;   
    int sel_mode;
    int sel_start_y;
    int sel_start_x;
    char *clipboard;
    UndoNode *undo_top;
    UndoNode *redo_top;
    int undo_count;
    int last_find_y;
    int last_find_x;
    char last_find_str[128];
    LangType lang;
} Editor;

Editor E;

/* ---------- 状态消息辅助 ---------- */

static void set_status_msg(const char *s) {
    snprintf(E.status_msg, sizeof(E.status_msg), "%s", s ? s : "");
    E.status_time = time(NULL);
    E.status_is_error = 0; 
}

static void set_status_fmt(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(E.status_msg, sizeof(E.status_msg), fmt, ap);
    va_end(ap);
    E.status_time = time(NULL);
    E.status_is_error = 0;
}

static void set_status_error(const char *s) {
    snprintf(E.status_msg, sizeof(E.status_msg), "%s", s ? s : "");
    E.status_time = time(NULL);
    E.status_is_error = 1; 
}

/* ---------- 跨行高亮状态缓存 ---------- */

static int  state_cached_offset = -1;
static int  state_in_string     = 0;
static int  state_in_comment    = 0;
static int  state_in_block      = 0;

static void invalidate_state_cache(void) { state_cached_offset = -1; }

static void advance_state_line(const char *line, int *in_string, int *in_comment, int *in_block) {
    int len = (int)strlen(line);
    for (int x = 0; x < len; x++) {
        switch (E.lang) {
            case LANG_C:    parse_c_syntax   (line, x, len, in_string, in_comment, in_block); break;
            case LANG_PY:   parse_py_syntax  (line, x, len, in_string, in_comment);            break;
            case LANG_HTML: parse_html_syntax(line, x, len, in_string, in_comment);            break;
            case LANG_JS:   parse_js_syntax  (line, x, len, in_string, in_comment, in_block);  break;
            case LANG_PHP:  parse_php_syntax (line, x, len, in_string, in_comment, in_block);  break;
            case LANG_SH:   parse_sh_syntax  (line, x, len, in_string, in_comment);            break;
            default: break;
        }
    }
    if (E.lang == LANG_C || E.lang == LANG_JS || E.lang == LANG_PHP ||
        E.lang == LANG_PY || E.lang == LANG_SH) {
        *in_comment = 0;
    }
}

static void restore_state_before(int offset) {
    if (state_cached_offset == offset) return;
    state_in_string = 0;
    state_in_comment = 0;
    state_in_block = 0;
    int limit = offset < E.num_lines ? offset : E.num_lines;
    for (int i = 0; i < limit; i++) {
        advance_state_line(E.lines[i], &state_in_string, &state_in_comment, &state_in_block);
    }
    state_cached_offset = offset;
}

/* ---------- 编辑器核心 ---------- */

void init_editor() {
    E.lines = malloc(sizeof(char*) * 100);
    E.capacity = 100;
    for (int i = 0; i < E.capacity; i++) E.lines[i] = NULL;
    E.num_lines = 1;
    E.lines[0] = strdup("");
    E.cy = 0; E.cx = 0;
    E.row_offset = 0;
    E.is_readonly = 0;
    E.dirty = 0;
    strcpy(E.filename, "[Untitled]");
    E.status_msg[0] = '\0';
    E.status_time = 0;
    E.status_is_error = 0;
    E.sel_mode = 0; E.sel_start_y = 0; E.sel_start_x = 0; E.clipboard = NULL;
    E.undo_top = NULL; E.redo_top = NULL; E.undo_count = 0;
    E.last_find_y = -1; E.last_find_x = -1; E.last_find_str[0] = '\0';
    E.lang = LANG_TEXT;
    invalidate_state_cache();
}

void detect_lang() {
    E.lang = LANG_TEXT;
    char *ext = strrchr(E.filename, '.');
    if (ext) {
        if (strcmp(ext, ".c") == 0 || strcmp(ext, ".cpp") == 0 || strcmp(ext, ".h") == 0) E.lang = LANG_C;
        else if (strcmp(ext, ".py") == 0) E.lang = LANG_PY;
        else if (strcmp(ext, ".html") == 0 || strcmp(ext, ".htm") == 0) E.lang = LANG_HTML;
        else if (strcmp(ext, ".js") == 0) E.lang = LANG_JS;
        else if (strcmp(ext, ".php") == 0) E.lang = LANG_PHP;
        else if (strcmp(ext, ".sh") == 0 || strcmp(ext, ".bash") == 0) E.lang = LANG_SH;
        else if (strcmp(ext, ".log") == 0) E.lang = LANG_LOG;
    }
    invalidate_state_cache();
}

void insert_line(int at, const char *text) {
    if (E.num_lines >= E.capacity) {
        int old_cap = E.capacity; E.capacity *= 2;
        E.lines = realloc(E.lines, sizeof(char*) * E.capacity);
        for (int i = old_cap; i < E.capacity; i++) E.lines[i] = NULL;
    }
    memmove(&E.lines[at + 1], &E.lines[at], sizeof(char*) * (E.num_lines - at));
    E.lines[at] = strdup(text);
    E.num_lines++;
}

void delete_line(int at) {
    if (at < 0 || at >= E.num_lines || E.num_lines == 1) return;
    free(E.lines[at]);
    memmove(&E.lines[at], &E.lines[at + 1], sizeof(char*) * (E.num_lines - at - 1));
    E.lines[E.num_lines - 1] = NULL; E.num_lines--;
}

void free_lines(char **lines, int num) {
    if (!lines) return;
    for (int i = 0; i < num; i++) free(lines[i]);
    free(lines);
}

char** clone_lines(char **src, int num, int *out_num) {
    char **dst = malloc(sizeof(char*) * (num + 10));
    for (int i = 0; i < num; i++) dst[i] = strdup(src[i]);
    *out_num = num; return dst;
}

void free_undo_node(UndoNode *n) {
    if (!n) return;
    free_lines(n->lines, n->num_lines); free(n);
}

void free_undo_stack(UndoNode **top) {
    UndoNode *p = *top;
    while (p) { UndoNode *prev = p->prev; free_undo_node(p); p = prev; }
    *top = NULL;
}

void trim_undo_stack() {
    if (E.undo_count <= UNDO_MAX) return;
    UndoNode *oldest = E.undo_top;
    while (oldest && oldest->prev) oldest = oldest->prev;
    if (!oldest || oldest == E.undo_top) return;
    UndoNode *p = E.undo_top;
    while (p && p->prev != oldest) p = p->prev;
    if (p) p->prev = NULL;
    free_undo_node(oldest); E.undo_count--;
}

void save_undo_state() {
    if (E.is_readonly) return;
    UndoNode *node = malloc(sizeof(UndoNode));
    int n; node->lines = clone_lines(E.lines, E.num_lines, &n);
    node->num_lines = n; node->cy = E.cy; node->cx = E.cx;
    node->prev = E.undo_top; E.undo_top = node; E.undo_count++;
    trim_undo_stack(); free_undo_stack(&E.redo_top);
}

void load_file(const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        free_lines(E.lines, E.num_lines);
        E.capacity = 100; E.lines = malloc(sizeof(char*) * E.capacity);
        for (int i = 0; i < E.capacity; i++) E.lines[i] = NULL;
        E.num_lines = 1; E.lines[0] = strdup("");
        E.cy = 0; E.cx = 0; E.row_offset = 0; E.dirty = 0;
        strncpy(E.filename, filename, sizeof(E.filename) - 1);
        E.filename[sizeof(E.filename) - 1] = '\0';
        detect_lang();
        set_status_fmt("New File: %s", filename);
        return;
    }
    free_lines(E.lines, E.num_lines);
    E.capacity = 100; E.lines = malloc(sizeof(char*) * E.capacity);
    for (int i = 0; i < E.capacity; i++) E.lines[i] = NULL;
    E.num_lines = 0;
    char buffer[MAX_LEN]; char *long_line = NULL; size_t long_len = 0;
    while (fgets(buffer, sizeof(buffer), fp)) {
        size_t blen = strlen(buffer);
        int has_nl = (blen > 0 && buffer[blen - 1] == '\n');
        if (has_nl) buffer[blen - 1] = '\0';
        if (long_line) {
            size_t add = strlen(buffer);
            long_line = realloc(long_line, long_len + add + 1);
            memcpy(long_line + long_len, buffer, add + 1); long_len += add;
            if (has_nl) { insert_line(E.num_lines, long_line); free(long_line); long_line = NULL; long_len = 0; }
        } else if (!has_nl && blen == sizeof(buffer) - 1) {
            long_line = strdup(buffer); long_len = blen;
        } else { insert_line(E.num_lines, buffer); }
    }
    if (long_line) { insert_line(E.num_lines, long_line); free(long_line); }
    if (E.num_lines == 0) insert_line(0, "");
    fclose(fp);
    strncpy(E.filename, filename, sizeof(E.filename) - 1);
    E.filename[sizeof(E.filename) - 1] = '\0';
    E.cy = 0; E.cx = 0; E.row_offset = 0; E.dirty = 0;
    E.last_find_y = -1; E.last_find_x = -1; E.last_find_str[0] = '\0';
    detect_lang();
    invalidate_state_cache();
}

void save_file(const char *fname) {
    if (E.is_readonly) { set_status_error("Error: Read-only"); return; }
    FILE *fp = fopen(fname, "w");
    if (!fp) { set_status_error("Error: Cannot write"); return; }
    for (int i = 0; i < E.num_lines; i++) {
        if (fputs(E.lines[i], fp) == EOF || fputc('\n', fp) == EOF) {
            fclose(fp); set_status_error("Error: Write failed"); return;
        }
    }
    fclose(fp);
    strncpy(E.filename, fname, sizeof(E.filename) - 1);
    E.filename[sizeof(E.filename) - 1] = '\0';
    E.dirty = 0;
    set_status_fmt("Saved %d lines", E.num_lines);
    detect_lang();
}

void copy_selection() {
    if (!E.sel_mode) { set_status_error("Press Ctrl+P to select first"); return; }
    int sy = E.sel_start_y, sx = E.sel_start_x, ey = E.cy, ex = E.cx;
    if (sy > ey || (sy == ey && sx > ex)) { int ty = sy; sy = ey; ey = ty; int tx = sx; sx = ex; ex = tx; }
    int len = 0;
    for (int i = sy; i <= ey; i++) {
        if (i == sy && i == ey) len += (ex - sx);
        else if (i == sy) len += ((int)strlen(E.lines[i]) - sx);
        else if (i == ey) len += ex;
        else len += (int)strlen(E.lines[i]);
        if (i != ey) len += 1;
    }
    free(E.clipboard); E.clipboard = malloc(len + 1);
    char *ptr = E.clipboard;
    for (int i = sy; i <= ey; i++) {
        int start = (i == sy) ? sx : 0;
        int end   = (i == ey) ? ex : (int)strlen(E.lines[i]);
        int copy_len = end - start;
        if (copy_len > 0) { memcpy(ptr, &E.lines[i][start], copy_len); ptr += copy_len; }
        if (i != ey) { *ptr = '\n'; ptr++; }
    }
    *ptr = '\0'; E.sel_mode = 0;
    set_status_fmt("Copied %d bytes", len);
}

void delete_selection() {
    if (!E.sel_mode || E.is_readonly) return;
    save_undo_state();
    int sy = E.sel_start_y, sx = E.sel_start_x, ey = E.cy, ex = E.cx;
    if (sy > ey || (sy == ey && sx > ex)) { int ty = sy; sy = ey; ey = ty; int tx = sx; sx = ex; ex = tx; }
    if (sy == ey) {
        memmove(&E.lines[sy][sx], &E.lines[sy][ex], strlen(E.lines[sy]) - ex + 1);
    } else {
        char *end_part = strdup(&E.lines[ey][ex]);
        int prefix_len = sx + (int)strlen(end_part);
        char *new_line = malloc(prefix_len + 1);
        memcpy(new_line, E.lines[sy], sx);
        strcpy(new_line + sx, end_part);
        free(end_part); free(E.lines[sy]); E.lines[sy] = new_line;
        for (int i = ey; i > sy; i--) delete_line(i);
    }
    E.cy = sy; E.cx = sx; E.sel_mode = 0; E.dirty = 1;
    set_status_msg("Deleted selection");
}

void paste_clipboard() {
    if (!E.clipboard || strlen(E.clipboard) == 0) { set_status_error("Clipboard empty"); return; }
    if (E.is_readonly) return;
    save_undo_state();
    char *p = E.clipboard;
    while (*p) {
        if (*p == '\n') {
            char *left = strdup(E.lines[E.cy]);
            char *right = strdup(E.lines[E.cy] + E.cx);
            left[E.cx] = '\0'; free(E.lines[E.cy]); E.lines[E.cy] = left;
            insert_line(E.cy + 1, right); free(right); E.cy++; E.cx = 0;
        } else {
            int len = (int)strlen(E.lines[E.cy]);
            E.lines[E.cy] = realloc(E.lines[E.cy], len + 2);
            memmove(&E.lines[E.cy][E.cx + 1], &E.lines[E.cy][E.cx], len - E.cx + 1);
            E.lines[E.cy][E.cx] = *p; E.cx++;
        }
        p++;
    }
    E.dirty = 1; set_status_msg("Pasted");
}

void do_undo() {
    if (!E.undo_top) { set_status_error("Nothing to Undo"); return; }
    UndoNode *r = malloc(sizeof(UndoNode)); int n;
    r->lines = clone_lines(E.lines, E.num_lines, &n);
    r->num_lines = n; r->cy = E.cy; r->cx = E.cx; r->prev = E.redo_top; E.redo_top = r;
    free_lines(E.lines, E.num_lines);
    E.lines = E.undo_top->lines; E.num_lines = E.undo_top->num_lines;
    E.cy = E.undo_top->cy; E.cx = E.undo_top->cx;
    UndoNode *tmp = E.undo_top->prev; free(E.undo_top); E.undo_top = tmp;
    E.undo_count--; E.dirty = 1;
    set_status_msg("Undone");
    invalidate_state_cache();
}

void do_redo() {
    if (!E.redo_top) { set_status_error("Nothing to Redo"); return; }
    UndoNode *u = malloc(sizeof(UndoNode)); int n;
    u->lines = clone_lines(E.lines, E.num_lines, &n);
    u->num_lines = n; u->cy = E.cy; u->cx = E.cx; u->prev = E.undo_top; E.undo_top = u; E.undo_count++;
    free_lines(E.lines, E.num_lines);
    E.lines = E.redo_top->lines; E.num_lines = E.redo_top->num_lines;
    E.cy = E.redo_top->cy; E.cx = E.redo_top->cx;
    UndoNode *tmp = E.redo_top->prev; free(E.redo_top); E.redo_top = tmp;
    E.dirty = 1;
    set_status_msg("Redone");
    invalidate_state_cache();
}

int prompt_input(const char *prompt, char *out_buf, int max_len, const char *initial) {
    int max_y, max_x; getmaxyx(stdscr, max_y, max_x);
    move(max_y - 2, 0); clrtoeol(); mvprintw(max_y - 2, 0, "%s", prompt); refresh();
    curs_set(1); int idx = 0; out_buf[0] = '\0';
    int prompt_w = get_screen_width(prompt);

    if (initial && initial[0]) {
        strncpy(out_buf, initial, max_len - 1);
        out_buf[max_len - 1] = '\0';
        idx = (int)strlen(out_buf);
        move(max_y - 2, 0); clrtoeol();
        mvprintw(max_y - 2, 0, "%s%s", prompt, out_buf);
        int w = prompt_w + get_screen_width(out_buf);
        move(max_y - 2, w < max_x ? w : max_x - 1);
        refresh();
    }

    while (1) {
        int c = getch();
        if (c == '\n' || c == KEY_ENTER || c == 13) return 1;
        else if (c == 27) { out_buf[0] = '\0'; return 0; }
        else if (c == KEY_RESIZE) { getmaxyx(stdscr, max_y, max_x); continue; }
        else if (c == KEY_BACKSPACE || c == 127 || c == 8) {
            if (idx > 0) {
                idx--; while (idx > 0 && (out_buf[idx] & 0xC0) == 0x80) idx--;
                out_buf[idx] = '\0';
                move(max_y - 2, 0); clrtoeol();
                mvprintw(max_y - 2, 0, "%s%s", prompt, out_buf);
                int w = prompt_w + get_screen_width(out_buf);
                move(max_y - 2, w < max_x ? w : max_x - 1);
                refresh();
            }
        } else if (c >= 32 && c != 127 && (isprint(c) || (unsigned char)c >= 0x80)) {
            if (idx + 1 < max_len) {
                out_buf[idx++] = c; out_buf[idx] = '\0';
                move(max_y - 2, 0); clrtoeol();
                mvprintw(max_y - 2, 0, "%s%s", prompt, out_buf);
                int w = prompt_w + get_screen_width(out_buf);
                move(max_y - 2, w < max_x ? w : max_x - 1);
                refresh();
            }
        }
    }
}

void show_help() {
    int max_y, max_x; getmaxyx(stdscr, max_y, max_x);
    int win_w = max_x - 4; if (win_w > 68) win_w = 68; if (win_w < 20) win_w = max_x;
    int win_h = max_y - 4; if (win_h > 16) win_h = 16; if (win_h < 8) win_h = max_y;
    if (win_w < 20 || win_h < 8) { set_status_error("Screen too small"); return; }
    WINDOW *win = newwin(win_h, win_w, (max_y - win_h) / 2, (max_x - win_w) / 2);
    if (!win) return;
    box(win, 0, 0);
    
    wattron(win, A_BOLD | A_REVERSE); mvwprintw(win, 0, 2, " How Help (Press any key) "); wattroff(win, A_BOLD | A_REVERSE);
    const char *h[] = {
        "File: Ctrl+E New | Ctrl+O Open | Ctrl+S Save | Ctrl+A Save As",
        "Edit: Ctrl+R Undo| Ctrl+T Redo | Ctrl+P Select",
        "      Ctrl+Y Copy| Ctrl+U Paste| Ctrl+D Delete",
        "Tool: Ctrl+F Find| Ctrl+N Next | Ctrl+I Replace| Ctrl+B GoTo",
        "Exit: Ctrl+X",
        "Mobile: If key missing, use CTRL on virtual keyboard."
    };
    for (int i = 0; i < 6 && i + 2 < win_h - 1; i++) 
        mvwprintw(win, i + 2, 2, "%.*s", win_w - 4, h[i]);
    
    wrefresh(win); getch(); delwin(win);
}

int get_screen_lines(const char *str, int max_width) {
    if (!str || !*str) return 1;
    int bytes = (int)strlen(str);
    int screen_w = 0; int lines = 1;
    for (int i = 0; i < bytes; ) {
        int n = utf8_seq_len((unsigned char)str[i]);
        int cw = utf8_display_width((unsigned char)str[i]);
        if (screen_w + cw > max_width) { lines++; screen_w = cw; }
        else screen_w += cw;
        i += n;
    }
    return lines;
}

static int in_selection(int y, int x) {
    if (!E.sel_mode) return 0;
    int sy = E.sel_start_y, sx = E.sel_start_x;
    int ey = E.cy,          ex = E.cx;
    if (sy > ey || (sy == ey && sx > ex)) {
        int t = sy; sy = ey; ey = t;
        t = sx; sx = ex; ex = t;
    }
    if (y < sy || y > ey) return 0;
    if (y == sy && y == ey) return x >= sx && x < ex;
    if (y == sy) return x >= sx;
    if (y == ey) return x < ex;
    return 1;
}

void draw_ui() {
    erase(); 
    int max_y, max_x; getmaxyx(stdscr, max_y, max_x);
    if (E.cy < 0) E.cy = 0;
    if (E.cy >= E.num_lines) E.cy = E.num_lines - 1;
    if (E.cx < 0) E.cx = 0;
    if (E.cx > (int)strlen(E.lines[E.cy])) E.cx = (int)strlen(E.lines[E.cy]);
    if (E.cx > 0 && (E.lines[E.cy][E.cx] & 0xC0) == 0x80) {
        while (E.cx > 0 && (E.lines[E.cy][E.cx] & 0xC0) == 0x80) E.cx--;
    }

    attron(A_REVERSE); 
    mvhline(0, 0, ' ', max_x);
    char left_info[256];
    snprintf(left_info, sizeof(left_info), " How v1.0 | Ln:%d Col:%d", E.cy + 1, E.cx + 1);
    mvprintw(0, 0, "%s", left_info);
    char name_display[280];
    if (E.dirty) snprintf(name_display, sizeof(name_display), "*%s ", E.filename);
    else snprintf(name_display, sizeof(name_display), "%s ", E.filename);
    if (max_x > (int)strlen(left_info) + (int)strlen(name_display) + 2)
        mvprintw(0, max_x - (int)strlen(name_display), "%s", name_display);
    attroff(A_REVERSE);

    int line_num_width = 3;
    { int digits = 1, tmp_n = E.num_lines;
      while (tmp_n >= 10) { digits++; tmp_n /= 10; }
      if (digits > line_num_width) line_num_width = digits; }
    int text_start = line_num_width + 1;
    if (text_start >= max_x) text_start = (max_x > 2) ? 2 : 0;
    if (text_start < 1) text_start = 1;

    int text_area_height = max_y - 3;
    int max_text_width = max_x - text_start;
    if (max_text_width <= 0) max_text_width = 1;

    if (E.status_msg[0] && E.status_time > 0 && time(NULL) - E.status_time >= STATUS_TIMEOUT) {
        E.status_msg[0] = '\0';
    }

    restore_state_before(E.row_offset);
    int in_string  = state_in_string;
    int in_comment = state_in_comment;
    int in_block   = state_in_block;

    int screen_row = 1;
    int cursor_screen_y = -1, cursor_screen_x = text_start;

    for (int i = E.row_offset; i < E.num_lines && screen_row < text_area_height + 1; i++) {
        char *line_str = E.lines[i];
        int byte_len = (int)strlen(line_str);
        char num_buf[16]; snprintf(num_buf, sizeof(num_buf), "%d", i + 1);
        int num_len = (int)strlen(num_buf);
        int padding = line_num_width - num_len;
        if (padding < 0) padding = 0;

        if (i == E.cy) {
            attron(A_BOLD); mvprintw(screen_row, 0, "%*s", padding, "");
            attron(A_REVERSE); mvprintw(screen_row, padding, "%s", num_buf); attroff(A_REVERSE | A_BOLD);
            mvaddch(screen_row, padding + num_len, ' ');
        } else {
            attron(A_DIM); mvprintw(screen_row, 0, "%*s", padding, "");
            mvprintw(screen_row, padding, "%s ", num_buf); attroff(A_DIM);
        }

        if (i == E.cy) { 
            attron(A_DIM); 
            mvhline(screen_row, text_start, ' ', max_x - text_start); 
            attroff(A_DIM); 
        }

        int screen_x = text_start;
        int word_color = CP_TEXT;
        int active_color = 0;

        for (int x = 0; x < byte_len; ) {
            int clen = utf8_seq_len((unsigned char)line_str[x]);
            if (clen < 1) clen = 1;
            if (x + clen > byte_len) clen = byte_len - x;
            int cw = utf8_display_width((unsigned char)line_str[x]);

            if (screen_x + cw > text_start + max_text_width) {
                screen_row++; screen_x = text_start;
                if (screen_row >= text_area_height + 1) break;
            }
            if (i == E.cy && x == E.cx) { cursor_screen_y = screen_row; cursor_screen_x = screen_x; }

            int current_color = 0;
            if (is_word_start(line_str, x) || in_string || in_comment || in_block) {
                switch (E.lang) {
                    case LANG_C:    current_color = parse_c_syntax   (line_str, x, byte_len, &in_string, &in_comment, &in_block); break;
                    case LANG_PY:   current_color = parse_py_syntax  (line_str, x, byte_len, &in_string, &in_comment);            break;
                    case LANG_HTML: current_color = parse_html_syntax(line_str, x, byte_len, &in_string, &in_comment);            break;
                    case LANG_JS:   current_color = parse_js_syntax  (line_str, x, byte_len, &in_string, &in_comment, &in_block); break;
                    case LANG_PHP:  current_color = parse_php_syntax (line_str, x, byte_len, &in_string, &in_comment, &in_block); break;
                    case LANG_SH:   current_color = parse_sh_syntax  (line_str, x, byte_len, &in_string, &in_comment);            break;
                    case LANG_LOG:  current_color = parse_log_syntax (line_str, x, byte_len);                                    break;
                    default: break;
                }
                if (current_color > 0) word_color = current_color;
                else if (is_word_start(line_str, x)) word_color = CP_TEXT;
            }
            if (in_string) word_color = CP_STRING;
            else if (in_comment || in_block) word_color = CP_COMMENT;

            if (line_str[x] == '{' || line_str[x] == '}' || line_str[x] == ';' ||
                line_str[x] == '(' || line_str[x] == ')' || line_str[x] == '[' || line_str[x] == ']') {
                if (word_color != CP_TEXT) word_color = current_color > 0 ? current_color : word_color;
            }

            char mb[8] = {0};
            memcpy(mb, &line_str[x], clen);

            if (has_colors()) {
                if (word_color != active_color) {
                    if (active_color > 0) attroff(COLOR_PAIR(active_color));
                    if (word_color > 0)   attron(COLOR_PAIR(word_color));
                    active_color = word_color;
                }
            }
            
            int sel = in_selection(i, x);
            if (sel) attron(A_REVERSE);
            mvaddstr(screen_row, screen_x, mb);
            if (sel) attroff(A_REVERSE);

            screen_x += cw;
            x += clen;
        }
        if (active_color > 0 && has_colors()) attroff(COLOR_PAIR(active_color));

        if (i == E.cy && E.cx == byte_len) { cursor_screen_y = screen_row; cursor_screen_x = screen_x; }

        if (E.lang == LANG_C || E.lang == LANG_JS || E.lang == LANG_PHP ||
            E.lang == LANG_PY || E.lang == LANG_SH) {
            in_comment = 0;
        }

        screen_row++;
    }

    attron(A_DIM);
    while (screen_row < max_y - 2) { mvaddch(screen_row, 2, '~'); screen_row++; }
    attroff(A_DIM);

    // 状态栏（倒数第二行）
    if (E.status_msg[0] != '\0') {
        if (E.status_is_error && has_colors()) attron(COLOR_PAIR(CP_ERROR_BAR) | A_BOLD);
        else if (E.status_is_error) attron(A_REVERSE | A_BOLD);

        if (E.status_is_error) mvhline(max_y - 2, 0, ' ', max_x); // 只有错误才铺满背景
        else move(max_y - 2, 0); 
        
        mvprintw(max_y - 2, 2, "%s", E.status_msg); 

        if (E.status_is_error && has_colors()) attroff(COLOR_PAIR(CP_ERROR_BAR) | A_BOLD);
        else if (E.status_is_error) attroff(A_REVERSE | A_BOLD);
    } else {
        move(max_y - 2, 0);
        clrtoeol();
    }
    
    // 底部快捷键栏（倒数第一行）
    attron(A_REVERSE); 
    mvhline(max_y - 1, 0, ' ', max_x);
    int key_width = 14; int cols = max_x / key_width; if (cols > 7) cols = 7; if (cols < 1) cols = 1;
    const char *keys_en[7] = {"^G Help", "^O Open", "^F Find", "^R Undo", "^S Save", "^K Lang", "^X Exit"};
    for (int i = 0; i < cols; i++) {
        mvprintw(max_y - 1, i * key_width, "%s", keys_en[i]);
    }
    attroff(A_REVERSE);

    if (E.row_offset > E.cy) E.row_offset = E.cy;
    while (E.row_offset < E.cy) {
        int h = 0;
        for (int i = E.row_offset; i <= E.cy && h <= text_area_height; i++) {
            h += get_screen_lines(E.lines[i], max_text_width);
        }
        if (h <= text_area_height) break;
        E.row_offset++;
    }

    if (cursor_screen_y != -1) {
        if (cursor_screen_y > max_y - 3) cursor_screen_y = max_y - 3;
        move(cursor_screen_y, cursor_screen_x);
    } else {
        move(E.cy - E.row_offset + 1, text_start);
    }
    refresh();
}

void find_next(const char *needle) {
    if (strlen(needle) == 0) return;

    int start_y, start_x;
    int had_prev = (E.last_find_str[0] &&
                    strcmp(E.last_find_str, needle) == 0 &&
                    E.last_find_y >= 0);

    if (had_prev) {
        start_y = E.last_find_y;
        start_x = E.last_find_x + 1;
    } else {
        start_y = E.cy;
        start_x = E.cx + 1;
    }

    int total = E.num_lines;
    if (total <= 0) { set_status_error("File is empty"); return; }

    for (int step = 0; step <= total; step++) {
        int i = (start_y + step) % total;
        int from = (step == 0) ? start_x : 0;
        if (step == total) from = 0;
        if (from < 0) from = 0;
        if (from > (int)strlen(E.lines[i])) from = 0;

        int is_wrapped = (start_y + step) >= total;

        char *pos = strstr(E.lines[i] + from, needle);
        if (pos) {
            int new_cx = (int)(pos - E.lines[i]);

            if (is_wrapped && had_prev &&
                i == E.last_find_y && new_cx == E.last_find_x) {
                set_status_error("No more matches");
                return;
            }

            E.cy = i;
            E.cx = new_cx;
            E.last_find_y = E.cy;
            E.last_find_x = E.cx;
            strncpy(E.last_find_str, needle, sizeof(E.last_find_str) - 1);
            E.last_find_str[sizeof(E.last_find_str) - 1] = '\0';

            if (is_wrapped) {
                set_status_fmt("Wrapped to line %d", E.cy + 1);
            } else {
                set_status_fmt("Found at line %d", E.cy + 1);
            }
            return;
        }
    }

    set_status_error("[Not Found!]");
    E.last_find_y = -1;
    E.last_find_x = -1;
}

void do_replace() {
    char old_str[128] = "", new_str[128] = "";
    if (!prompt_input("Replace find: ", old_str, 127, "")) return;
    if (strlen(old_str) == 0) return;
    if (!prompt_input("Replace with: ", new_str, 127, "")) return;
    save_undo_state();
    int saved_cy = E.cy; int saved_cx = E.cx; int count = 0; int ask_all = 0;
    for (int i = 0; i < E.num_lines; i++) {
        char *p = E.lines[i];
        while ((p = strstr(p, old_str)) != NULL) {
            int idx = (int)(p - E.lines[i]);
            int old_len = (int)strlen(old_str);
            int new_len = (int)strlen(new_str);
            if (!ask_all) {
                E.cy = i; E.cx = idx;
                while (E.cx > 0 && (E.lines[i][E.cx] & 0xC0) == 0x80) E.cx--;
                draw_ui(); int max_y, max_x; getmaxyx(stdscr, max_y, max_x);
                
                if (has_colors()) attron(COLOR_PAIR(CP_ERROR_BAR) | A_BOLD);
                else attron(A_REVERSE | A_BOLD);
                mvhline(max_y - 2, 0, ' ', max_x);
                mvprintw(max_y - 2, 2, "Replace this? (y/n/a=all/q=quit)");
                if (has_colors()) attroff(COLOR_PAIR(CP_ERROR_BAR) | A_BOLD);
                else attroff(A_REVERSE | A_BOLD);
                refresh();
                
                int c = getch();
                if (c == 'q' || c == 'Q') {
                    E.cy = saved_cy; E.cx = saved_cx;
                    if (count > 0) { E.dirty = 1; invalidate_state_cache(); }
                    set_status_fmt("Replaced %d matches", count);
                    return;
                }
                if (c == 'a' || c == 'A') ask_all = 1;
                if (c != 'y' && c != 'Y' && !ask_all) { p = E.lines[i] + idx + old_len; continue; }
            }
            E.lines[i] = realloc(E.lines[i], strlen(E.lines[i]) - old_len + new_len + 1);
            memmove(&E.lines[i][idx + new_len], &E.lines[i][idx + old_len],
                    strlen(E.lines[i]) - idx - old_len + 1);
            memcpy(&E.lines[i][idx], new_str, new_len);
            p = &E.lines[i][idx + new_len]; count++;
        }
    }
    E.cy = saved_cy; E.cx = saved_cx;
    if (count > 0) { E.dirty = 1; invalidate_state_cache(); }
    set_status_fmt("Replaced %d matches", count);
}

void gen_timestamp_name(char *buf, int max_len) {
    time_t t = time(NULL); struct tm *tm = localtime(&t);
    strftime(buf, max_len, "%Y%m%d%H%M%S.txt", tm);
}

void draw_splash_screen() {
    clear(); 
    int max_y, max_x; getmaxyx(stdscr, max_y, max_x);
    
    if (has_colors()) attron(COLOR_PAIR(CP_COMMENT) | A_DIM);
    else attron(A_DIM);
    for (int i = 0; i < max_y - 1; i++) mvaddch(i, 0, '~');
    if (has_colors()) attroff(COLOR_PAIR(CP_COMMENT) | A_DIM);
    else attroff(A_DIM);

    int color_logo = CP_TEXT;         
    int color_info = CP_COMMENT;      
    int color_link = CP_CONTROL;      

    const char *logo[] = {
        "██╗  ██╗ ██████╗ ██╗    ██╗",
        "██║  ██║██╔═══██╗██║    ██║",
        "███████║██║   ██║██║ █╗ ██║",
        "██╔══██║██║   ██║██║███╗██║",
        "██║  ██║╚██████╔╝╚███╔███╔╝",
        "╚═╝  ╚═╝ ╚═════╝  ╚══╝╚══╝ "
    };
    
    const char *info_main[] = {
        "Welcome to How!",
        "Lightweight text editor for Linux (Termux) & Windows Server"
    };
    
    const char *info_detail[] = {
        "Detail: https://github.com/Liuhao20081209/how",
        "Email : 3693257606@qq.com"
    };

    int logo_lines = 6;
    int total_height = logo_lines + 1 + 2 + 1 + 2; 
    int start_y = (max_y - total_height) / 2;
    if (start_y < 1) start_y = 1;

    int max_width = get_screen_width(info_main[1]); 
    int base_start_x = (max_x - max_width) / 2;
    if (base_start_x < 2) base_start_x = 2;
  
    int logo_width = 32; 
    int start_logo_x = base_start_x + (max_width - logo_width) / 2;
    if (start_logo_x < 2) start_logo_x = 2;

    if (has_colors()) attron(COLOR_PAIR(color_logo));
    for (int i = 0; i < logo_lines; i++) {
        mvprintw(start_y + i, start_logo_x, "%s", logo[i]);
    }
    if (has_colors()) attroff(COLOR_PAIR(color_logo));

    int start_info_y = start_y + logo_lines + 1; 
    for (int i = 0; i < 2; i++) {
        int info_x = base_start_x + (max_width - get_screen_width(info_main[i])) / 2;
        if (has_colors()) attron(COLOR_PAIR(color_info));
        mvprintw(start_info_y + i, info_x, "%s", info_main[i]);
        if (has_colors()) attroff(COLOR_PAIR(color_info));
    }

    int start_detail_y = start_info_y + 2 + 1; 
    for (int i = 0; i < 2; i++) {
        int detail_x = base_start_x + (max_width - get_screen_width("Detail: https://github.com/Liuhao20081209/how")) / 2;
        if (detail_x < 2) detail_x = 2;
        
        if (has_colors()) attron(COLOR_PAIR(color_link));
        mvprintw(start_detail_y + i, detail_x, "%s", info_detail[i]);
        if (has_colors()) attroff(COLOR_PAIR(color_link));
    }

    int shortcut_y = start_detail_y + 2 + 3; 
    if (shortcut_y > max_y - 4) shortcut_y = max_y - 4;

    const char *shortcuts[] = {"^O Open", "^S Save", "^X Exit", "^G Help"};
    int total_sc_len = 42; 
    int start_x = (max_x - total_sc_len) / 2;
    if (start_x < 2) start_x = 2;

    if (has_colors()) attron(COLOR_PAIR(CP_OPERATOR));
    for (int i = 0; i < 4; i++) {
        mvprintw(shortcut_y, start_x, "%s", shortcuts[i]);
        start_x += 11; 
    }
    if (has_colors()) attroff(COLOR_PAIR(CP_OPERATOR));

    int press_key_y = shortcut_y + 2;
    if (press_key_y >= max_y) press_key_y = max_y - 1;

    if (press_key_y > 0) {
        const char *press_key = "Press any key to continue...";
        int pk_x = (max_x - get_screen_width(press_key)) / 2;
        if (pk_x < 2) pk_x = 2;
        
        if (has_colors()) attron(COLOR_PAIR(CP_TEXT));
        mvprintw(press_key_y, pk_x, "%s", press_key);
        if (has_colors()) attroff(COLOR_PAIR(CP_TEXT));
    }

    refresh(); 
    getch(); 
    clear();
}

int main(int argc, char *argv[]) {
    setlocale(LC_ALL, "");

    char filename[256] = "";
    if (argc > 1) {
        strncpy(filename, argv[1], sizeof(filename) - 1);
        filename[sizeof(filename) - 1] = '\0';
    }
    signal(SIGINT, SIG_IGN); signal(SIGTSTP, SIG_IGN);
    initscr(); raw(); keypad(stdscr, TRUE); noecho(); curs_set(1);

    if (has_colors()) {
        start_color(); use_default_colors();
        if (COLORS >= 256) {
            init_pair(CP_TEXT,        253, -1);
            init_pair(CP_KEYWORD,      75, -1);
            init_pair(CP_STRING,      173, -1);
            init_pair(CP_COMMENT,     243, -1);
            init_pair(CP_ERROR,       167, -1);
            init_pair(CP_WARNING,     179, -1);
            init_pair(CP_CONTROL,     109, -1);
            init_pair(CP_TIME,         80, -1);
            init_pair(CP_NUMBER,      175, -1);
            init_pair(CP_OPERATOR,    245, -1);
            init_pair(CP_PREPROC,     139, -1);
            init_pair(CP_HTML_TAG,     68, -1);
            init_pair(CP_HTML_ATTR,   132, -1);
            init_pair(CP_CSS_PROP,    168, -1);
            
            init_pair(CP_ERROR_BAR,   COLOR_WHITE, 160); // 亮白字，深红底
        } else {
            init_pair(CP_TEXT,        COLOR_WHITE,   -1);
            init_pair(CP_KEYWORD,     COLOR_GREEN,   -1);
            init_pair(CP_STRING,      COLOR_YELLOW,  -1);
            init_pair(CP_COMMENT,     COLOR_BLUE,    -1);
            init_pair(CP_ERROR,       COLOR_RED,     -1);
            init_pair(CP_WARNING,     COLOR_MAGENTA, -1);
            init_pair(CP_CONTROL,     COLOR_MAGENTA, -1);
            init_pair(CP_TIME,        COLOR_CYAN,    -1);
            init_pair(CP_NUMBER,      COLOR_CYAN,    -1);
            init_pair(CP_OPERATOR,    COLOR_WHITE,   -1);
            init_pair(CP_PREPROC,     COLOR_YELLOW,  -1);
            init_pair(CP_HTML_TAG,    COLOR_GREEN,   -1);
            init_pair(CP_HTML_ATTR,   COLOR_RED,     -1);
            init_pair(CP_CSS_PROP,    COLOR_MAGENTA, -1);
            
            init_pair(CP_ERROR_BAR,   COLOR_WHITE, COLOR_RED); // 标准 8/16 色白字红底
        }
    }

    init_editor();
    if (strlen(filename) > 0) load_file(filename); else draw_splash_screen();

    int ch;
    char utf8_buffer[8] = {0};
    int utf8_count = 0;
    int utf8_expect = 0;

    while (1) {
        draw_ui();
        ch = getch();

        if (ch == ERR || ch == KEY_RESIZE) continue;
        if (ch == 27) { utf8_count = 0; utf8_expect = 0; continue; }
        if (ch == KEY_BTAB || ch == 0x1A) continue;

        if (ch >= 128 && ch <= 255) {
            if (E.is_readonly) continue;
            if (utf8_count == 0) {
                int need = utf8_seq_len((unsigned char)ch);
                if (need <= 1) continue;
                utf8_expect = need;
            }
            utf8_buffer[utf8_count++] = (char)ch;
            if (utf8_count == utf8_expect) {
                utf8_buffer[utf8_count] = '\0';
                save_undo_state();
                int len = (int)strlen(E.lines[E.cy]);
                char *tmp = realloc(E.lines[E.cy], len + utf8_count + 1);
                if (tmp) {
                    E.lines[E.cy] = tmp;
                    memmove(&E.lines[E.cy][E.cx + utf8_count], &E.lines[E.cy][E.cx], len - E.cx + 1);
                    memcpy(&E.lines[E.cy][E.cx], utf8_buffer, utf8_count);
                    E.cx += utf8_count; E.dirty = 1;
                }
                utf8_count = 0; utf8_expect = 0;
            }
            continue;
        } else {
            utf8_count = 0; utf8_expect = 0;
        }

        if (ch == CTRL('G')) { show_help(); }
        else if (ch == CTRL('P')) {
            E.sel_mode = !E.sel_mode;
            if (E.sel_mode) { E.sel_start_y = E.cy; E.sel_start_x = E.cx; set_status_msg("Select Mode (Arrows to move)"); }
            else set_status_msg("Selection Cancelled");
        }
        else if (ch == CTRL('Y')) { copy_selection(); }
        else if (ch == CTRL('U')) { paste_clipboard(); }
        else if (ch == CTRL('D')) { delete_selection(); }
        else if (ch == CTRL('R')) { do_undo(); }
        else if (ch == CTRL('T')) { do_redo(); }
        else if (ch == CTRL('F')) {
            char buf[128] = "";
            if (prompt_input("Find: ", buf, 127, E.last_find_str)) {
                if (strlen(buf) > 0) {
                    if (strcmp(buf, E.last_find_str) == 0 && E.last_find_y >= 0) {
                        find_next(buf);
                    } else {
                        strncpy(E.last_find_str, buf, sizeof(E.last_find_str) - 1);
                        E.last_find_str[sizeof(E.last_find_str) - 1] = '\0';
                        E.last_find_y = -1; E.last_find_x = -1;
                        find_next(buf);
                    }
                }
            }
        }
        else if (ch == CTRL('N')) {
            if (E.last_find_str[0]) find_next(E.last_find_str);
            else set_status_error("No previous search");
        }
        else if (ch == CTRL('I')) { do_replace(); }
        else if (ch == CTRL('B')) {
            char buf[16] = "";
            if (prompt_input("Go to line: ", buf, 9, "")) {
                int target = atoi(buf) - 1;
                if (target >= 0 && target < E.num_lines) { E.cy = target; E.cx = 0; E.status_msg[0] = '\0'; }
                else set_status_error("Invalid line");
            }
        }
        else if (ch == CTRL('S')) {
            if (E.is_readonly) { set_status_error("Error: Read-only"); }
            else {
                if (strcmp(E.filename, "[Untitled]") == 0) {
                    char buf[256] = "";
                    if (prompt_input("Save as: ", buf, 255, "")) {
                        if (strlen(buf) == 0) gen_timestamp_name(buf, sizeof(buf));
                        save_file(buf);
                    } else set_status_error("Save cancelled");
                } else save_file(E.filename);
            }
        }
        else if (ch == CTRL('E')) {
            save_undo_state();
            E.num_lines = 0; insert_line(0, "");
            strcpy(E.filename, "[Untitled]");
            E.dirty = 0; E.status_msg[0] = '\0';
            E.last_find_y = -1; E.last_find_x = -1; E.last_find_str[0] = '\0';
            E.lang = LANG_TEXT; E.cy = 0; E.cx = 0; E.row_offset = 0;
            invalidate_state_cache();
        }
        else if (ch == CTRL('O')) {
            char buf[256] = "";
            if (prompt_input("Open file: ", buf, 255, "")) {
                if (strlen(buf) > 0) load_file(buf);
                else set_status_error("Open cancelled");
            }
        }
        else if (ch == CTRL('A')) {
            char buf[256] = "";
            if (prompt_input("Save as: ", buf, 255, "")) {
                if (strlen(buf) == 0) gen_timestamp_name(buf, sizeof(buf));
                save_file(buf);
            }
        }
        else if (ch == CTRL('X')) {
            if (E.dirty) {
                if (strcmp(E.filename, "[Untitled]") == 0) {
                    char buf[256] = "";
                    set_status_error("Unsaved file, please name it");
                    draw_ui();
                    if (prompt_input("Save as: ", buf, 255, "")) {
                        if (strlen(buf) == 0) gen_timestamp_name(buf, sizeof(buf));
                        save_file(buf);
                        break;
                    } else continue;
                }
                
                int max_y, max_x; getmaxyx(stdscr, max_y, max_x);
                
                // 倒数第二行：提示语 (红底)
                if (has_colors()) attron(COLOR_PAIR(CP_ERROR_BAR) | A_BOLD);
                else attron(A_REVERSE | A_BOLD);
                mvhline(max_y - 2, 0, ' ', max_x);
                mvprintw(max_y - 2, 2, "Save modified buffer?");
                if (has_colors()) attroff(COLOR_PAIR(CP_ERROR_BAR) | A_BOLD);
                else attroff(A_REVERSE | A_BOLD);
                
                // 倒数第一行：选项 (常规底栏反色)
                attron(A_REVERSE);
                mvhline(max_y - 1, 0, ' ', max_x);
                mvprintw(max_y - 1, 2, "Y Yes   N No   ^C Cancel");
                attroff(A_REVERSE);
                refresh();

                int c2 = getch();
                if (c2 == 'y' || c2 == 'Y') { save_file(E.filename); break; }
                else if (c2 == 'n' || c2 == 'N') { break; }
                else if (c2 == CTRL('C') || c2 == 27) { set_status_msg("Exit cancelled"); continue; }
                else { set_status_error("Invalid key, cancelled"); continue; }
            }
            break;
        }
        else if (ch == KEY_UP) { if (E.cy > 0) E.cy--; }
        else if (ch == KEY_DOWN) { if (E.cy < E.num_lines - 1) E.cy++; }
        else if (ch == KEY_LEFT) {
            if (E.cx > 0) { E.cx--; while (E.cx > 0 && (E.lines[E.cy][E.cx] & 0xC0) == 0x80) E.cx--; }
            else if (E.cy > 0) { E.cy--; E.cx = (int)strlen(E.lines[E.cy]); }
        }
        else if (ch == KEY_RIGHT) {
            if (E.cx < (int)strlen(E.lines[E.cy])) {
                E.cx++;
                while (E.cx < (int)strlen(E.lines[E.cy]) && (E.lines[E.cy][E.cx] & 0xC0) == 0x80) E.cx++;
            } else if (E.cy < E.num_lines - 1) { E.cy++; E.cx = 0; }
        }
        else if (ch == KEY_HOME) { E.cx = 0; }
        else if (ch == KEY_END)  { E.cx = (int)strlen(E.lines[E.cy]); }
        else if (E.sel_mode) continue;
        else if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
            if (E.is_readonly) continue;
            save_undo_state();
            if (E.cx > 0) {
                int prev_x = E.cx - 1;
                while (prev_x > 0 && (E.lines[E.cy][prev_x] & 0xC0) == 0x80) prev_x--;
                memmove(&E.lines[E.cy][prev_x], &E.lines[E.cy][E.cx], strlen(E.lines[E.cy]) - E.cx + 1);
                E.cx = prev_x; E.dirty = 1;
            } else if (E.cy > 0) {
                int prev_len = (int)strlen(E.lines[E.cy - 1]);
                E.lines[E.cy - 1] = realloc(E.lines[E.cy - 1], prev_len + strlen(E.lines[E.cy]) + 1);
                strcat(E.lines[E.cy - 1], E.lines[E.cy]);
                delete_line(E.cy);
                E.cy--; E.cx = prev_len; E.dirty = 1;
            }
        }
        else if (ch == '\n' || ch == KEY_ENTER || ch == 13) {
            if (E.is_readonly) continue;
            save_undo_state();
            char *left = strdup(E.lines[E.cy]);
            char *right = strdup(E.lines[E.cy] + E.cx);
            left[E.cx] = '\0';
            free(E.lines[E.cy]); E.lines[E.cy] = left;
            insert_line(E.cy + 1, right); free(right);
            E.cy++; E.cx = 0; E.dirty = 1;
        }
        else if (ch == 9) {
            if (E.is_readonly) continue;
            save_undo_state();
            int len = (int)strlen(E.lines[E.cy]);
            char *tmp = realloc(E.lines[E.cy], len + 5);
            if (tmp) {
                E.lines[E.cy] = tmp;
                memmove(&E.lines[E.cy][E.cx + 4], &E.lines[E.cy][E.cx], len - E.cx + 1);
                memcpy(&E.lines[E.cy][E.cx], "    ", 4);
                E.cx += 4; E.dirty = 1;
            }
        }
        else if (ch >= 32 && ch < 128) {
            if (E.is_readonly) continue;
            save_undo_state();
            int len = (int)strlen(E.lines[E.cy]);
            char *tmp = realloc(E.lines[E.cy], len + 2);
            if (!tmp) continue;
            E.lines[E.cy] = tmp;
            memmove(&E.lines[E.cy][E.cx + 1], &E.lines[E.cy][E.cx], len - E.cx + 1);
            E.lines[E.cy][E.cx] = (char)ch;
            E.cx++; E.dirty = 1;
        }
    }

    free_lines(E.lines, E.num_lines);
    free(E.clipboard);
    free_undo_stack(&E.undo_top);
    free_undo_stack(&E.redo_top);
    endwin();
    return 0;
}
