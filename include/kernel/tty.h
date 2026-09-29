#ifndef _KERNEL_TTY_H
#define _KERNEL_TTY_H
#include <stddef.h>
#include <stdint.h>
void tty_init(void);
void tty_clear(void);
void tty_putchar(char c);
void tty_write(const char *s);
void tty_writen(const char *s, size_t n);
void tty_setcolor(uint8_t fg, uint8_t bg);
void tty_getcursor(size_t *row_out, size_t *col_out);
void tty_setcursor(size_t row, size_t col);
void tty_hidecursor(void);
void tty_showcursor(void);
#endif
