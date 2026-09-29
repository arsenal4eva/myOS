#include "kernel/serial.h"
#include "kernel/port.h"
#define COM1 0x3F8
void serial_init(void) {
    outb(COM1+1, 0x00);
    outb(COM1+3, 0x80);
    outb(COM1+0, 0x03); outb(COM1+1, 0x00);
    outb(COM1+3, 0x03);
    outb(COM1+2, 0xC7);
    outb(COM1+4, 0x0B);
}
static int tx_empty(void) { return inb(COM1+5) & 0x20; }
void serial_putchar(char c) {
    if (c == '\n') serial_putchar('\r');
    while (!tx_empty());
    outb(COM1, (uint8_t)c);
}
void serial_write(const char *s) { while (*s) serial_putchar(*s++); }
int serial_available(void) { return inb(COM1+5) & 0x01; }
char serial_getc(void) { while (!serial_available()); return (char)inb(COM1); }
