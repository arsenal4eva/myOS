#include "kernel/keyboard.h"
#include "kernel/idt.h"
#include "kernel/port.h"

#define KB_BUF 256
static volatile int buf[KB_BUF];
static volatile int head = 0, tail = 0;
static int shift = 0;
static int ctrl = 0;
static int ext = 0; /* 0xE0 prefix pending */

static const char layout[128] = {
    0,27,'1','2','3','4','5','6','7','8','9','0','-','=','\b','\t',
    'q','w','e','r','t','y','u','i','o','p','[',']','\n',0,'a','s',
    'd','f','g','h','j','k','l',';','\'','`',0,'\\','z','x','c','v',
    'b','n','m',',','.','/',0,'*',0,' ',0,0,0,0,0,0,0,0,0,0,0,0,'7','8','9','-','4','5','6','+','1','2','3','0','.',
};
static const char layout_shift[128] = {
    0,27,'!','@','#','$','%','^','&','*','(',')','_','+','\b','\t',
    'Q','W','E','R','T','Y','U','I','O','P','{','}','\n',0,'A','S',
    'D','F','G','H','J','K','L',':','"','~',0,'|','Z','X','C','V',
    'B','N','M','<','>','?',0,'*',0,' ',0,0,0,0,0,0,0,0,0,0,0,0,'7','8','9','-','4','5','6','+','1','2','3','0','.',
};

static void push_key(int k) {
    int n = (head + 1) % KB_BUF;
    if (n != tail) { buf[head] = k; head = n; }
}

static void on_key(regs_t *r) {
    (void)r;
    uint8_t sc = inb(0x60);
    if (sc == 0xE0) { ext = 1; return; }
    /* Ctrl press/release */
    if (sc == 0x1D) { ctrl = 1; ext = 0; return; }
    if (sc == 0x9D) { ctrl = 0; ext = 0; return; }
    if (sc == 0x2A || sc == 0x36) { shift = 1; ext = 0; return; }
    if (sc == 0xAA || sc == 0xB6) { shift = 0; ext = 0; return; }
    if (sc & 0x80) { ext = 0; return; }
    if (ext) {
        ext = 0;
        switch (sc) {
        case 0x48: push_key(KEY_UP); break;
        case 0x50: push_key(KEY_DOWN); break;
        case 0x4B: push_key(KEY_LEFT); break;
        case 0x4D: push_key(KEY_RIGHT); break;
        case 0x47: push_key(KEY_HOME); break;
        case 0x4F: push_key(KEY_END); break;
        case 0x49: push_key(KEY_PAGEUP); break;
        case 0x51: push_key(KEY_PAGEDOWN); break;
        case 0x53: push_key(KEY_DELETE); break;
        default: break;
        }
        return;
    }
    if (sc >= 128) return;
    char c = shift ? layout_shift[sc] : layout[sc];
    if (!c) return;
    if (ctrl && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) {
        char up = (c >= 'a') ? (char)(c - 'a' + 'A') : c;
        push_key(up - 'A' + 1); /* Ctrl+A..Z -> 1..26 */
        return;
    }
    push_key((int)c);
}

void keyboard_init(void) { irq_install_handler(1, on_key); }
int keyboard_available(void) { return head != tail; }
int keyboard_key_available(void) { return head != tail; }
char keyboard_getchar(void) {
    while (head == tail) __asm__ volatile("hlt");
    int k = buf[tail]; tail = (tail + 1) % KB_BUF;
    if (k >= 0x100) return 0; /* special: legacy callers ignore */
    return (char)k;
}
int keyboard_getkey(void) {
    while (head == tail) __asm__ volatile("hlt");
    int k = buf[tail]; tail = (tail + 1) % KB_BUF;
    return k;
}
