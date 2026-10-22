#ifndef _KERNEL_KB_H
#define _KERNEL_KB_H
void keyboard_init(void);
char keyboard_getchar(void);
int keyboard_available(void);

enum {
    KEY_UP = 0x100,
    KEY_DOWN,
    KEY_LEFT,
    KEY_RIGHT,
    KEY_HOME,
    KEY_END,
    KEY_PAGEUP,
    KEY_PAGEDOWN,
    KEY_DELETE,
};
int keyboard_getkey(void); 
int keyboard_key_available(void);
#endif
