#ifndef _KERNEL_KB_H
#define _KERNEL_KB_H
void keyboard_init(void);
/* Blocking read of one char (handles shift, backspace done by shell). Returns ASCII. */
char keyboard_getchar(void);
int keyboard_available(void);

/* Extended key API for fullscreen apps.
 * Values 0x00-0xFF are ASCII; values >= 0x100 are special keys. */
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
int keyboard_getkey(void); /* blocking, returns ASCII or KEY_*; Ctrl+A-Z -> 1-26 */
int keyboard_key_available(void);
#endif
