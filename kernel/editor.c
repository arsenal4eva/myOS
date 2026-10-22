#include "kernel/editor.h"
#include "kernel/tty.h"
#include "kernel/keyboard.h"
#include "kernel/serial.h"
#include "kernel/fs.h"
#include "kernel/string.h"
#include "kernel/port.h"

#define ED_MAX FS_MAX_FILE
#define ED_VIEW_H 23  
#define ED_COLS 80

static char ed_buf[ED_MAX + 1];
static char ed_cut[ED_MAX + 1];
static int ed_cut_len = 0;
static char ed_msg[81];

static int ed_len;
static int ed_cursor;   
static int ed_top;      
static int ed_dirty;

static int ed_line_of(int off) {
    int l = 0;
    for (int i = 0; i < off; i++) if (ed_buf[i] == '\n') l++;
    return l;
}
static int ed_col_of(int off) {
    int c = 0;
    for (int i = off - 1; i >= 0 && ed_buf[i] != '\n'; i--) c++;
    return c;
}
static int ed_line_start(int line) {
    int l = 0, i = 0;
    while (i < ed_len && l < line) { if (ed_buf[i++] == '\n') l++; }
    return (l == line) ? i : ed_len;
}
static int ed_line_len_at(int start) {
    int n = 0;
    while (start + n < ed_len && ed_buf[start + n] != '\n') n++;
    return n;
}
static int ed_total_lines(void) {
    if (ed_len == 0) return 1;
    int n = 1;
    for (int i = 0; i < ed_len; i++) if (ed_buf[i] == '\n') n++;
    return n;
}
static void ed_goto_line_col(int line, int col) {
    int s = ed_line_start(line);
    int ll = ed_line_len_at(s);
    if (col > ll) col = ll;
    ed_cursor = s + col;
}

static int ed_getkey(void) {
    for (;;) {
        if (keyboard_key_available()) return keyboard_getkey();
        if (serial_available()) {
            char c = serial_getc();
            if (c == '\r') c = '\n';
            if (c == 27) return 24; 
            return (int)c;
        }
        __asm__ volatile("hlt");
    }
}

static void ed_draw(const char *path) {
    tty_clear();
    for (int r = 0; r < ED_VIEW_H; r++) {
        int line = ed_top + r;
        tty_setcursor((size_t)r, 0);
        if (line < ed_total_lines()) {
            int s = ed_line_start(line);
            int ll = ed_line_len_at(s);
            if (ll > ED_COLS) ll = ED_COLS;
            tty_setcolor(0x07, 0x00);
            tty_writen(ed_buf + s, (size_t)ll);
        } else {
            tty_setcolor(0x08, 0x00);
            tty_write("~");
            tty_setcolor(0x07, 0x00);
        }
    }
    tty_setcolor(0x0E, 0x00);
    tty_clear_row(23);
    tty_write_at(23, 0, ed_msg, strlen(ed_msg));
    ed_msg[0] = 0;
    tty_setcolor(0x00, 0x07);
    tty_clear_row(24);
    char bar[81];
    int li = ed_line_of(ed_cursor) + 1, co = ed_col_of(ed_cursor) + 1;
    int n = 0;
    const char *nm = path;
    char tmp[16];
    bar[0] = 0;
    strcpy(bar, " edit ");
    strncpy(bar + strlen(bar), nm, 20);
    strcat(bar, "  L");
    tmp[0] = 0; int tl = 0, v = li;
    char rev[8]; int ri = 0;
    if (v == 0) rev[ri++] = '0';
    while (v > 0) { rev[ri++] = (char)('0' + v % 10); v /= 10; }
    while (ri > 0) tmp[tl++] = rev[--ri];
    tmp[tl] = 0;
    strcat(bar, tmp);
    strcat(bar, ",C");
    tl = 0; v = co; ri = 0;
    if (v == 0) rev[ri++] = '0';
    while (v > 0) { rev[ri++] = (char)('0' + v % 10); v /= 10; }
    while (ri > 0) tmp[tl++] = rev[--ri];
    tmp[tl] = 0;
    strcat(bar, tmp);
    if (ed_dirty) strcat(bar, " *");
    strcat(bar, "  ^O save ^X exit ^K cut ^U uncut");
    n = (int)strlen(bar);
    if (n > 80) n = 80;
    tty_write_at(24, 0, bar, (size_t)n);
    tty_setcolor(0x07, 0x00);
    int cl = ed_line_of(ed_cursor) - ed_top;
    int cc = ed_col_of(ed_cursor);
    if (cc > 79) cc = 79;
    if (cl < 0) cl = 0;
    if (cl >= ED_VIEW_H) cl = ED_VIEW_H - 1;
    tty_setcursor((size_t)cl, (size_t)cc);
    outb(0x3D4, 0x0A); outb(0x3D5, 0x0E);
    outb(0x3D4, 0x0B); outb(0x3D5, 0x0F);
}

static void ed_ensure_visible(void) {
    int cl = ed_line_of(ed_cursor);
    if (cl < ed_top) ed_top = cl;
    if (cl >= ed_top + ED_VIEW_H) ed_top = cl - ED_VIEW_H + 1;
    if (ed_top < 0) ed_top = 0;
}

static void ed_insert(char c) {
    if (ed_len >= ED_MAX) { strcpy(ed_msg, "buffer full"); return; }
    memmove(ed_buf + ed_cursor + 1, ed_buf + ed_cursor, (size_t)(ed_len - ed_cursor));
    ed_buf[ed_cursor] = c;
    ed_len++; ed_cursor++;
    ed_dirty = 1;
}
static void ed_backspace(void) {
    if (ed_cursor <= 0) return;
    memmove(ed_buf + ed_cursor - 1, ed_buf + ed_cursor, (size_t)(ed_len - ed_cursor));
    ed_len--; ed_cursor--;
    ed_dirty = 1;
}
static void ed_delete_at(void) {
    if (ed_cursor >= ed_len) return;
    memmove(ed_buf + ed_cursor, ed_buf + ed_cursor + 1, (size_t)(ed_len - ed_cursor - 1));
    ed_len--;
    ed_dirty = 1;
}
static void ed_cut_line(void) {
    int line = ed_line_of(ed_cursor);
    int s = ed_line_start(line);
    int ll = ed_line_len_at(s);
    int take = ll + ((s + ll < ed_len && ed_buf[s + ll] == '\n') ? 1 : 0);
    if (take <= 0) return;
    if (take > ED_MAX) take = ED_MAX;
    memcpy(ed_cut, ed_buf + s, (size_t)take);
    ed_cut_len = take;
    memmove(ed_buf + s, ed_buf + s + take, (size_t)(ed_len - s - take));
    ed_len -= take;
    ed_cursor = s;
    if (ed_cursor > ed_len) ed_cursor = ed_len;
    ed_dirty = 1;
    strcpy(ed_msg, "line cut (^U to paste)");
}
static void ed_uncut(void) {
    if (ed_cut_len <= 0) { strcpy(ed_msg, "nothing to paste"); return; }
    if (ed_len + ed_cut_len > ED_MAX) { strcpy(ed_msg, "no room to paste"); return; }
    memmove(ed_buf + ed_cursor + ed_cut_len, ed_buf + ed_cursor, (size_t)(ed_len - ed_cursor));
    memcpy(ed_buf + ed_cursor, ed_cut, (size_t)ed_cut_len);
    ed_len += ed_cut_len; ed_cursor += ed_cut_len;
    ed_dirty = 1;
}

static void ed_move_left(void)  { if (ed_cursor > 0) ed_cursor--; }
static void ed_move_right(void) { if (ed_cursor < ed_len) ed_cursor++; }
static void ed_move_up(void) {
    int l = ed_line_of(ed_cursor), c = ed_col_of(ed_cursor);
    if (l > 0) ed_goto_line_col(l - 1, c);
}
static void ed_move_down(void) {
    int l = ed_line_of(ed_cursor), c = ed_col_of(ed_cursor);
    if (l + 1 < ed_total_lines()) ed_goto_line_col(l + 1, c);
}

void editor_open(const char *path) {
    ed_len = 0; ed_cursor = 0; ed_top = 0; ed_dirty = 0; ed_cut_len = 0;
    ed_msg[0] = 0;
    ed_buf[0] = 0;
    if (!path || !*path) { tty_write("edit: usage: edit <file>\n"); return; }
    if (fs_is_dir(path)) { tty_write("edit: is a directory\n"); return; }
    if (fs_exists(path)) {
        static char tmp[FS_MAX_FILE + 1];
        int n = fs_read(path, tmp, sizeof(tmp));
        if (n < 0) n = 0;
        if (n > ED_MAX) n = ED_MAX;
        memcpy(ed_buf, tmp, (size_t)n);
        ed_len = n; ed_cursor = ed_len;
    }
    ed_buf[ed_len] = 0;

    ed_ensure_visible();

    for (;;) {
        ed_ensure_visible();
        ed_draw(path);
        int k = ed_getkey();
        if (k == 24 || k == 27) { 
            if (ed_dirty) {
                tty_setcursor(23, 0);
                tty_setcolor(0x0E, 0x00);
                tty_write("Modified! Press Y to save+exit, N exit w/o save, C cancel ");
                tty_setcolor(0x07, 0x00);
                int a = ed_getkey();
                if (a == 'y' || a == 'Y' || a == '\n') {
                    if (fs_write(path, ed_buf, (size_t)ed_len, 0) != 0) {
                        strcpy(ed_msg, "save FAILED (too big?)");
                        ed_dirty = 1;
                        continue;
                    }
                    break;
                } else if (a == 'n' || a == 'N') {
                    break;
                } else {
                    continue;
                }
            } else break;
        } else if (k == 15) { 
            if (fs_write(path, ed_buf, (size_t)ed_len, 0) != 0)
                strcpy(ed_msg, "save FAILED (too big?)");
            else { strcpy(ed_msg, "saved"); ed_dirty = 0; }
        } else if (k == 11) { ed_cut_line(); }        
        else if (k == 21) { ed_uncut(); }             
        else if (k == KEY_UP) ed_move_up();
        else if (k == KEY_DOWN) ed_move_down();
        else if (k == KEY_LEFT) ed_move_left();
        else if (k == KEY_RIGHT) ed_move_right();
        else if (k == KEY_HOME) { int l = ed_line_of(ed_cursor); ed_goto_line_col(l, 0); }
        else if (k == KEY_END) { int l = ed_line_of(ed_cursor); int s = ed_line_start(l); ed_goto_line_col(l, ed_line_len_at(s)); }
        else if (k == KEY_PAGEUP) { int l = ed_line_of(ed_cursor); int c = ed_col_of(ed_cursor); l -= ED_VIEW_H; if (l < 0) l = 0; ed_goto_line_col(l, c); }
        else if (k == KEY_PAGEDOWN) { int l = ed_line_of(ed_cursor); int c = ed_col_of(ed_cursor); l += ED_VIEW_H; if (l >= ed_total_lines()) l = ed_total_lines() - 1; ed_goto_line_col(l, c); }
        else if (k == KEY_DELETE || k == 127) ed_delete_at();
        else if (k == '\b' || k == 8) ed_backspace();
        else if (k == '\n' || k == '\r') ed_insert('\n');
        else if (k >= 32 && k < 127) ed_insert((char)k);
        else if (k == '\t') { ed_insert(' '); ed_insert(' '); ed_insert(' '); ed_insert(' '); }
    }
    tty_clear();
    tty_setcolor(0x07, 0x00);
}

