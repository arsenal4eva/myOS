#include "kernel/tty.h"
#include "kernel/port.h"

#define VGA_BUF ((volatile uint16_t *)0xB8000)
#define VGA_W 80
#define VGA_H 25

static size_t row, col;
static uint8_t color = 0x07;

static void update_cursor(void) {
    uint16_t pos = (uint16_t)(row * VGA_W + col);
    outb(0x3D4, 0x0F); outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E); outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

static void scroll(void) {
    for (size_t y = 1; y < VGA_H; y++)
        for (size_t x = 0; x < VGA_W; x++)
            VGA_BUF[(y-1)*VGA_W+x] = VGA_BUF[y*VGA_W+x];
    for (size_t x = 0; x < VGA_W; x++)
        VGA_BUF[(VGA_H-1)*VGA_W+x] = (uint16_t)(' ' | (color << 8));
    row = VGA_H - 1;
}

void tty_init(void) { row = col = 0; tty_clear(); }
void tty_clear(void) {
    for (size_t y = 0; y < VGA_H; y++)
        for (size_t x = 0; x < VGA_W; x++)
            VGA_BUF[y*VGA_W+x] = (uint16_t)(' ' | (color << 8));
    row = col = 0;
    update_cursor();
}
void tty_setcolor(uint8_t fg, uint8_t bg) { color = (uint8_t)((bg << 4) | (fg & 0x0F)); }

void tty_putchar(char c) {
    if (c == '\n') { col = 0; if (++row >= VGA_H) scroll(); }
    else if (c == '\r') { col = 0; }
    else if (c == '\t') { for (int i = 0; i < 4; i++) tty_putchar(' '); return; }
    else if (c == '\b') {
        if (col > 0) { col--; VGA_BUF[row*VGA_W+col] = (uint16_t)(' ' | (color << 8)); }
        update_cursor(); return;
    } else {
        VGA_BUF[row*VGA_W+col] = (uint16_t)((uint8_t)c | (color << 8));
        if (++col >= VGA_W) { col = 0; if (++row >= VGA_H) scroll(); }
    }
    update_cursor();
}
void tty_write(const char *s) { while (*s) tty_putchar(*s++); }
void tty_writen(const char *s, size_t n) { for (size_t i = 0; i < n; i++) tty_putchar(s[i]); }
void tty_getcursor(size_t *row_out, size_t *col_out) {
    if (row_out) *row_out = row;
    if (col_out) *col_out = col;
}
void tty_setcursor(size_t r, size_t c) {
    if (r >= VGA_H) r = VGA_H - 1;
    if (c >= VGA_W) c = VGA_W - 1;
    row = r; col = c; update_cursor();
}
void tty_hidecursor(void) {
    outb(0x3D4, 0x0A); outb(0x3D5, 0x20);
}
void tty_showcursor(void) {
    outb(0x3D4, 0x0A); outb(0x3D5, 0x0E);
    outb(0x3D4, 0x0B); outb(0x3D5, 0x0F);
    update_cursor();
}
void tty_clear_row(size_t r) {
    if (r >= VGA_H) return;
    for (size_t x = 0; x < VGA_W; x++)
        VGA_BUF[r*VGA_W+x] = (uint16_t)(' ' | (color << 8));
}
void tty_write_at(size_t r, size_t c, const char *s, size_t n) {
    if (r >= VGA_H) return;
    for (size_t i = 0; i < n && c + i < VGA_W; i++)
        VGA_BUF[r*VGA_W+c+i] = (uint16_t)((uint8_t)s[i] | (color << 8));
}
