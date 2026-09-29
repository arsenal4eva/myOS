#ifndef _KERNEL_SERIAL_H
#define _KERNEL_SERIAL_H
void serial_init(void);
void serial_putchar(char c);
void serial_write(const char *s);
int serial_available(void);
char serial_getc(void);
#endif
