#include "kernel/shell.h"
#include "kernel/tty.h"
#include "kernel/kprintf.h"
#include "kernel/string.h"
#include "kernel/keyboard.h"
#include "kernel/serial.h"
#include "kernel/timer.h"
#include "kernel/sysinfo.h"
#include "kernel/port.h"
#include "kernel/fs.h"
#include "kernel/editor.h"

#define MAX_ARGS 32
#define MAX_TOK 64
#define CAP_SIZE 4096
#define INPUT_SIZE 4097


static int is_space(char c) { return c == ' ' || c == '\t'; }

static int has_glob(const char *s) {
    for (; *s; s++)
        if (*s == '*' || *s == '?')
            return 1;
    return 0;
}

static int has_slash(const char *s) {
    for (; *s; s++)
        if (*s == '/')
            return 1;
    return 0;
}

static int has_inner_slash(const char *s) { return has_slash(s); }

static const char *base_name(const char *p) {
    const char *last = 0;
    for (const char *q = p; *q; q++)
        if (*q == '/')
            last = q;
    return last ? last + 1 : p;
}

static int child_remainder(const char *path, const char *dir, const char **rem_out) {
    size_t ld = strlen(dir);
    while (ld > 0 && dir[ld - 1] == '/')
        ld--;
    if (strncmp(path, dir, ld) != 0)
        return 0;
    if (path[ld] != '/')
        return 0;
    if (rem_out)
        *rem_out = path + ld + 1;
    return 1;
}


static void read_line(char *buf, size_t cap) {
    size_t n = 0;
    for (;;) {
        char c;
        if (serial_available())
            c = serial_getc();
        else if (keyboard_available())
            c = keyboard_getchar();
        else {
            __asm__ volatile("hlt");
            continue;
        }
        if (c == '\r')
            c = '\n';
        if (c == '\n') {
            tty_putchar('\n');
            buf[n] = 0;
            return;
        } else if (c == '\b') {
            if (n > 0) {
                n--;
                tty_putchar('\b');
            }
        } else if (n + 1 < cap) {
            buf[n++] = c;
            tty_putchar(c);
        }
    }
}


static int tokenize(char *line, char bufs[MAX_ARGS][MAX_TOK], char *argv[MAX_ARGS], int quoted[MAX_ARGS]) {
    int argc = 0;
    char *p = line;
    while (*p) {
        while (is_space(*p))
            p++;
        if (!*p)
            break;
        if (argc >= MAX_ARGS)
            return -2;
        if (*p == '>' || *p == '<') {
            char *d = bufs[argc];
            int dl = 0;
            if (*p == '>' && p[1] == '>') {
                d[dl++] = '>';
                d[dl++] = '>';
                p += 2;
            } else {
                d[dl++] = *p++;
            }
            d[dl] = 0;
            argv[argc] = d;
            quoted[argc] = 0;
            argc++;
            continue;
        }
        char *d = bufs[argc];
        size_t dl = 0;
        int closed = 1;
        int q = 0; 
        while (*p && !is_space(*p) && *p != '<' && *p != '>') {
            if (*p == '"' || *p == '\'') {
                char qq = *p++;
                q = 1;
                closed = 0;
                while (*p && *p != qq) {
                    if (*p == '\\' && p[1]) {
                        p++;
                        if (dl + 1 < MAX_TOK)
                            d[dl++] = *p++;
                        else
                            p++;
                    } else {
                        if (dl + 1 < MAX_TOK)
                            d[dl++] = *p++;
                        else
                            p++;
                    }
                }
                if (!*p)
                    return -1; 
                p++; 
                closed = 1;
            } else if (*p == '\\' && p[1]) {
                p++;
                q = 1;
                if (dl + 1 < MAX_TOK)
                    d[dl++] = *p++;
                else
                    p++;
            } else {
                if (dl + 1 < MAX_TOK)
                    d[dl++] = *p++;
                else
                    p++;
            }
        }
        (void)closed;
        d[dl] = 0;
        argv[argc] = d;
        quoted[argc] = q;
        argc++;
    }
    return argc;
}


static int parse_redir(int argc, char **argv, int *quoted, char *argv2[MAX_ARGS],
                       int *quoted2, char *in_file, char *out_file, int *append_out) {
    int n = 0;
    in_file[0] = 0;
    out_file[0] = 0;
    *append_out = 0;
    for (int i = 0; i < argc; i++) {
        if (!quoted[i] && !strcmp(argv[i], "<")) {
            if (i + 1 >= argc)
                return -1;
            strncpy(in_file, argv[++i], MAX_TOK - 1);
            in_file[MAX_TOK - 1] = 0;
        } else if (!quoted[i] && !strcmp(argv[i], ">")) {
            if (i + 1 >= argc)
                return -1;
            strncpy(out_file, argv[++i], MAX_TOK - 1);
            out_file[MAX_TOK - 1] = 0;
            *append_out = 0;
        } else if (!quoted[i] && !strcmp(argv[i], ">>")) {
            if (i + 1 >= argc)
                return -1;
            strncpy(out_file, argv[++i], MAX_TOK - 1);
            out_file[MAX_TOK - 1] = 0;
            *append_out = 1;
        } else {
            if (n >= MAX_ARGS)
                return -2;
            argv2[n] = argv[i];
            quoted2[n] = quoted[i];
            n++;
        }
    }
    return n;
}


static int glob_expand(int argc, char **argv, int *quoted, char bufs[MAX_ARGS][MAX_TOK], char *out[MAX_ARGS]) {
    int n = 0;
    char name[FS_MAX_NAME + 1];
    for (int i = 0; i < argc; i++) {
        if (i > 0 && !quoted[i] && has_glob(argv[i])) {
            int matched = 0;
            int total = fs_num_entries();
            for (int e = 0; e < total && n < MAX_ARGS; e++) {
                int is_dir = 0;
                size_t sz = 0;
                if (fs_get_entry(e, name, sizeof(name), &is_dir, &sz) != 0)
                    continue;
                int ok;
                if (has_slash(argv[i])) {
                    ok = fs_match(argv[i], name);
                } else {
                    if (has_inner_slash(name))
                        continue; 
                    ok = fs_match(argv[i], name);
                }
                if (ok) {
                    strncpy(bufs[n], name, MAX_TOK - 1);
                    bufs[n][MAX_TOK - 1] = 0;
                    out[n] = bufs[n];
                    n++;
                    matched = 1;
                }
            }
            if (!matched) {
                if (n < MAX_ARGS)
                    out[n++] = argv[i];
            }
        } else {
            if (n < MAX_ARGS)
                out[n++] = argv[i];
        }
    }
    return n;
}


static void print_with_nl(const char *s) {
    kprintf("%s", s);
    size_t n = strlen(s);
    if (n == 0 || s[n - 1] != '\n')
        kprintf("\n");
}

static int parse_lineno(const char *s, int *out) {
    if (!s || !*s)
        return 0;
    int v = 0;
    for (const char *p = s; *p; p++) {
        if (*p < '0' || *p > '9')
            return 0;
        v = v * 10 + (*p - '0');
        if (v > 1000000)
            return 0;
    }
    if (v < 1)
        return 0;
    *out = v;
    return 1;
}

static int count_lines(const char *buf, int n) {
    if (n <= 0)
        return 0;
    int nl = 0;
    for (int i = 0; i < n; i++)
        if (buf[i] == '\n')
            nl++;
    if (buf[n - 1] != '\n')
        nl++;
    return nl;
}

static int print_lines_range(const char *buf, int n, int start, int end, int *total_out) {
    int total = count_lines(buf, n);
    if (total_out)
        *total_out = total;
    if (n <= 0 || start > total)
        return 0;
    if (end == 0 || end > total)
        end = total;
    int line = 1;
    int i = 0;
    int printed = 0;
    while (i < n) {
        int ls = i;
        while (i < n && buf[i] != '\n')
            i++;
        int has_nl = (i < n && buf[i] == '\n');
        int le = i + (has_nl ? 1 : 0); 
        if (line >= start && line <= end) {
            for (int j = ls; j < le; j++)
                tty_putchar(buf[j]);
            printed++;
        }
        if (line >= end)
            break;
        if (has_nl)
            i++;
        line++;
    }
    return printed;
}

static void cmd_ls_list_top(void) {
    int total = fs_num_entries();
    int printn = 0;
    char name[FS_MAX_NAME + 1];
    static char lbuf[INPUT_SIZE];
    for (int e = 0; e < total; e++) {
        int is_dir = 0;
        size_t sz = 0;
        if (fs_get_entry(e, name, sizeof(name), &is_dir, &sz) != 0)
            continue;
        if (has_inner_slash(name))
            continue;
        if (is_dir)
            kprintf("%s/\n", name);
        else {
            int n = fs_read(name, lbuf, sizeof(lbuf));
            int lines = (n > 0) ? count_lines(lbuf, n) : 0;
            kprintf("%s (%d lines, %u chars)\n", name, lines, (unsigned)sz);
        }
        printn = 1;
    }
    if (!printn)
        kprintf("(empty)\n");
}

static void cmd_ls_list_dir(const char *dir) {
    char stripped[MAX_TOK];
    strncpy(stripped, dir, sizeof(stripped) - 1);
    stripped[sizeof(stripped) - 1] = 0;
    size_t L = strlen(stripped);
    while (L > 0 && stripped[L - 1] == '/')
        stripped[--L] = 0;
    int total = fs_num_entries();
    int printn = 0;
    char name[FS_MAX_NAME + 1];
    static char lbuf[INPUT_SIZE];
    for (int e = 0; e < total; e++) {
        int is_dir = 0;
        size_t sz = 0;
        if (fs_get_entry(e, name, sizeof(name), &is_dir, &sz) != 0)
            continue;
        const char *rem = 0;
        if (!child_remainder(name, stripped, &rem))
            continue;
        if (has_inner_slash(rem))
            continue; 
        if (is_dir)
            kprintf("%s/\n", rem);
        else {
            int n = fs_read(name, lbuf, sizeof(lbuf));
            int lines = (n > 0) ? count_lines(lbuf, n) : 0;
            kprintf("%s (%d lines, %u chars)\n", rem, lines, (unsigned)sz);
        }
        printn = 1;
    }
    if (!printn)
        kprintf("(empty)\n");
}

static void cmd_ls(int argc, char **argv) {
    if (argc == 1) {
        cmd_ls_list_top();
        return;
    }
    for (int i = 1; i < argc; i++) {
        if (has_glob(argv[i])) {
            kprintf("list: no matches: '%s'\n", argv[i]);
            continue;
        }
        if (fs_is_dir(argv[i])) {
            if (argc > 2)
                kprintf("%s:\n", argv[i]);
            cmd_ls_list_dir(argv[i]);
        } else if (fs_exists(argv[i])) {
            size_t sz = fs_size(argv[i]);
            static char lbuf[INPUT_SIZE];
            int n = fs_read(argv[i], lbuf, sizeof(lbuf));
            int lines = (n > 0) ? count_lines(lbuf, n) : 0;
            kprintf("%s (%d lines, %u chars)\n", argv[i], lines, (unsigned)sz);
        } else {
            kprintf("list: '%s': no such file or directory\n", argv[i]);
        }
    }
}

static void cmd_cat(int argc, char **argv, const char *in_data, int in_present) {
    static char buf[INPUT_SIZE];
    int start = 1, end = 0, have_range = 0;
    int nargs = argc;
    if (nargs > 1) {
        int a = 0, b = 0;
        if (nargs >= 4 && parse_lineno(argv[nargs - 2], &a) &&
            parse_lineno(argv[nargs - 1], &b)) {
            if (nargs > 3 || in_present) {
                start = a;
                end = b;
                nargs -= 2;
                have_range = 1;
            }
        } else if (nargs >= 3 && parse_lineno(argv[nargs - 1], &a)) {
            if (nargs > 2 || in_present) {
                start = a;
                end = a; 
                nargs -= 1;
                have_range = 1;
            }
        }
        if (have_range && end != 0 && end < start) {
            kprintf("print: invalid range %d-%d (end < start)\n", start, end);
            return;
        }
    }
    if (nargs == 1) {
        if (in_present) {
            if (have_range) {
                int total = 0;
                int got = print_lines_range(in_data, strlen(in_data), start, end, &total);
                if (got == 0) {
                    if (end == start)
                        kprintf("print: no line %d (input has %d lines)\n", start, total);
                    else
                        kprintf("print: no lines %d-%d (input has %d lines)\n", start, end, total);
                }
            } else
                print_with_nl(in_data);
        } else {
            kprintf("usage: print <file...> [start [end]]\n");
            kprintf("  e.g. print f.txt 5 8  (lines 5-8)\n");
        }
        return;
    }
    for (int i = 1; i < nargs; i++) {
        if (has_glob(argv[i])) {
            kprintf("print: no matches: '%s'\n", argv[i]);
            continue;
        }
        if (fs_is_dir(argv[i])) {
            kprintf("print: '%s': is a directory\n", argv[i]);
            continue;
        }
        int n = fs_read(argv[i], buf, sizeof(buf));
        if (n < 0) {
            kprintf("print: '%s': no such file\n", argv[i]);
            continue;
        }
        if (have_range) {
            int total = 0;
            int got = print_lines_range(buf, n, start, end, &total);
            if (got == 0) {
                if (end == start)
                    kprintf("print: '%s': no line %d (file has %d lines)\n", argv[i], start, total);
                else
                    kprintf("print: '%s': no lines %d-%d (file has %d lines)\n", argv[i], start, end, total);
            }
        } else {
            kprintf("%s", buf);
            if (n > 0 && buf[n - 1] != '\n')
                kprintf("\n");
        }
    }
}


static void cmd_type_one(const char *label, const char *buf, int n, const char *path_for_ext);

typedef struct {
    unsigned char magic[8];
    int magic_len;
    const char *desc;
} type_magic_t;

static const type_magic_t type_magics[] = {
    { { 0x7F, 'E', 'L', 'F' }, 4, "ELF executable" },
    { { 'M', 'Z' }, 2, "DOS/PE executable" },
    { { '#', '!' }, 2, "script" },
    { { '%', 'P', 'D', 'F' }, 4, "PDF document" },
    { { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A }, 8, "PNG image" },
    { { 0xFF, 0xD8, 0xFF }, 3, "JPEG image" },
    { { 'G', 'I', 'F', '8', '7', 'a' }, 6, "GIF image" },
    { { 'G', 'I', 'F', '8', '9', 'a' }, 6, "GIF image" },
    { { 'B', 'M' }, 2, "BMP image" },
    { { 'P', 'K', 0x03, 0x04 }, 4, "ZIP archive" },
    { { 0x1F, 0x8B }, 2, "gzip compressed" },
};
#define TYPE_NUM_MAGICS (sizeof(type_magics) / sizeof(type_magics[0]))

typedef struct {
    const char *ext;
    const char *desc;
} type_ext_t;

static const type_ext_t type_exts[] = {
    { "txt", "ASCII text (by extension)" },
    { "log", "ASCII text (by extension)" },
    { "md", "ASCII text (by extension)" },
    { "cfg", "ASCII text (by extension)" },
    { "c", "ASCII text (by extension)" },
    { "h", "ASCII text (by extension)" },
    { "s", "ASCII text (by extension)" },
    { "asm", "ASCII text (by extension)" },
    { "sh", "ASCII text (by extension)" },
    { "lst", "ASCII text (by extension)" },
    { "png", "PNG image (by extension)" },
    { "jpg", "JPEG image (by extension)" },
    { "jpeg", "JPEG image (by extension)" },
    { "gif", "GIF image (by extension)" },
    { "bmp", "BMP image (by extension)" },
    { "pdf", "PDF document (by extension)" },
    { "zip", "ZIP archive (by extension)" },
    { "gz", "gzip compressed (by extension)" },
    { "tgz", "gzip compressed (by extension)" },
    { "elf", "data (by extension)" },
    { "bin", "data (by extension)" },
    { "img", "data (by extension)" },
    { "iso", "data (by extension)" },
    { "o", "data (by extension)" },
};
#define TYPE_NUM_EXTS (sizeof(type_exts) / sizeof(type_exts[0]))

static const char *type_ext_fallback(const char *path) {
    if (!path)
        return 0;
    const char *dot = 0;
    for (const char *p = path; *p; p++)
        if (*p == '.')
            dot = p;
    if (!dot || !dot[1])
        return 0;
    const char *slash = 0;
    for (const char *p = path; *p; p++)
        if (*p == '/')
            slash = p;
    if (slash && dot < slash)
        return 0;
    dot++;
    for (size_t i = 0; i < TYPE_NUM_EXTS; i++)
        if (!strcmp(dot, type_exts[i].ext))
            return type_exts[i].desc;
    return 0;
}

static int type_is_text(const char *buf, int n) {
    for (int i = 0; i < n; i++) {
        unsigned char c = (unsigned char)buf[i];
        if (c == '\n' || c == '\r' || c == '\t' || c == '\f' || c == '\b')
            continue;
        if (c >= 32 && c < 127)
            continue;
        return 0;
    }
    return 1;
}

static const char *type_by_header(const char *buf, int n) {
    for (size_t i = 0; i < TYPE_NUM_MAGICS; i++) {
        int len = type_magics[i].magic_len;
        if (n < len)
            continue;
        int match = 1;
        for (int j = 0; j < len; j++) {
            if ((unsigned char)buf[j] != type_magics[i].magic[j]) {
                match = 0;
                break;
            }
        }
        if (match)
            return type_magics[i].desc;
    }
    return 0;
}

static void cmd_type_one(const char *label, const char *buf, int n, const char *path_for_ext) {
    const char *t = 0;
    if (n == 0)
        t = "empty";
    else
        t = type_by_header(buf, n);
    if (!t) {
        if (n > 0 && type_is_text(buf, n))
            t = "ASCII text";
        else if (n > 0) {
            const char *ext = type_ext_fallback(path_for_ext);
            t = ext ? ext : "data";
        }
    }
    kprintf("%s: %s\n", label, t ? t : "data");
}

static void cmd_type(int argc, char **argv, const char *in_data, int in_present) {
    static char buf[INPUT_SIZE];
    if (argc == 1) {
        if (in_present) {
            cmd_type_one("<stdin>", in_data, (int)strlen(in_data), 0);
        } else {
            kprintf("usage: type <file...> | type < file\n");
        }
        return;
    }
    for (int i = 1; i < argc; i++) {
        if (has_glob(argv[i])) {
            kprintf("type: no matches: '%s'\n", argv[i]);
            continue;
        }
        if (fs_is_dir(argv[i])) {
            kprintf("%s: directory\n", argv[i]);
            continue;
        }
        int n = fs_read(argv[i], buf, sizeof(buf));
        if (n < 0) {
            kprintf("type: '%s': no such file\n", argv[i]);
            continue;
        }
        cmd_type_one(argv[i], buf, n, argv[i]);
    }
}

static void cmd_help(void) {
    kprintf("commands:\n");
    kprintf(" General: help sysinfo man echo clear ticks beep\n\n");
    kprintf(" Files: create mkdir list print remove edit type\n\n");
    kprintf(" Redirection: echo hello > f.txt | echo more >> f.txt | print < f.txt\n\n");
    kprintf(" Globbing: * matches any run, ? matches one char\n\n");
    kprintf(" Chaining: cmd1 ; cmd2 ; cmd3\n\n");
}

static void man_overview(void) {
    kprintf("MAN - view manual (usage: man <command>)\n\n");
    kprintf("Commands: help sysinfo man echo clear ticks beep\n");
    kprintf("          create mkdir list print remove edit type\n");
    kprintf("Topics: redirection globbing chaining\n");
    kprintf("Try: man echo ; man list ; man redirection\n");
}

static void man_for(const char *topic) {
    if (!strcmp(topic, "help")) {
        kprintf("HELP - short command summary\n\n");
        kprintf("SYNOPSIS: help\n");
        kprintf("DESC: Lists commands, redir, glob, chain hints.\n");
        kprintf("EXAMPLE: help\n");
    } else if (!strcmp(topic, "sysinfo")) {
        kprintf("SYSINFO - system information\n\n");
        kprintf("SYNOPSIS: sysinfo\n");
        kprintf("DESC: Prints OS, CPU, memory, uptime (like fastfetch).\n");
        kprintf("EXAMPLE: sysinfo | sysinfo > info.txt\n");
    } else if (!strcmp(topic, "man")) {
        kprintf("MAN - read manuals\n\n");
        kprintf("SYNOPSIS: man [command|topic]\n");
        kprintf("DESC: No args lists topics. With arg shows that page.\n");
        kprintf("EXAMPLES: man | man echo | man redirection\n");
    } else if (!strcmp(topic, "echo")) {
        kprintf("ECHO - print text\n\n");
        kprintf("SYNOPSIS: echo [text...] | echo < file\n");
        kprintf("DESC: Prints args joined by spaces. Bare echo prints\n");
        kprintf("  blank line; with '< file' prints file (stdin redir).\n");
        kprintf("EXAMPLES: echo hi | echo hi > a.txt | echo \"*\"\n");
    } else if (!strcmp(topic, "clear")) {
        kprintf("CLEAR - clear screen\n\n");
        kprintf("SYNOPSIS: clear\n");
        kprintf("DESC: Clears the 80x25 VGA text screen.\n");
        kprintf("EXAMPLE: clear\n");
    } else if (!strcmp(topic, "ticks")) {
        kprintf("TICKS - uptime in ticks\n\n");
        kprintf("SYNOPSIS: ticks\n");
        kprintf("DESC: Prints PIT ticks since boot (100 ticks = 1 sec).\n");
        kprintf("EXAMPLE: ticks\n");
    } else if (!strcmp(topic, "beep")) {
        kprintf("BEEP - PC speaker beep\n\n");
        kprintf("SYNOPSIS: beep\n");
        kprintf("DESC: Plays a short beep, then prints 'beep!'.\n");
        kprintf("EXAMPLE: beep\n");
    } else if (!strcmp(topic, "create")) {
        kprintf("CREATE - create files\n");
        kprintf("SYNOPSIS: create <file...>\n");
        kprintf("DESC: Creates empty file(s). Existing files kept.\n");
        kprintf("  Dirs error if path is a directory.\n");
        kprintf("EXAMPLE: create hi.txt ; echo hi > hi.txt\n");
    } else if (!strcmp(topic, "mkdir")) {
        kprintf("MKDIR - make directories\n");
        kprintf("SYNOPSIS: mkdir <dir...>\n");
        kprintf("DESC: Creates dir(s). Use paths like docs/ or a/b.\n");
        kprintf("EXAMPLE: mkdir docs ; list\n");
    } else if (!strcmp(topic, "list") || !strcmp(topic, "ls")) {
        kprintf("LIST - list files\n");
        kprintf("SYNOPSIS: list [path...]  (bare list = top level)\n");
        kprintf("DESC: Lists files or dir contents. Globs expand before\n");
        kprintf("  lookup; unmatched glob prints 'no matches'.\n");
        kprintf("EXAMPLES: list | list docs | list *.txt\n");
    } else if (!strcmp(topic, "print") || !strcmp(topic, "cat")) {
        kprintf("PRINT - print files\n");
        kprintf("SYNOPSIS: print <file...> [start [end]] | print < file\n");
        kprintf("DESC: Prints file(s). Bare print shows usage; dirs\n");
        kprintf("  error; missing files error per file.\n");
        kprintf("  start/end are 1-based line numbers: 'print a.txt\n");
        kprintf("  5 8' prints lines 5-8, 'print a.txt 5' prints\n");
        kprintf("  line 5 only.\n");
        kprintf("EXAMPLES: print a.txt | print *.txt | print < a.txt\n");
        kprintf("  print a.txt 5 8 | print a.txt 5\n");
    } else if (!strcmp(topic, "remove") || !strcmp(topic, "rm")) {
        kprintf("REMOVE - delete files\n");
        kprintf("SYNOPSIS: remove <path...>\n");
        kprintf("DESC: Deletes files/dirs. Dirs must be empty.\n");
        kprintf("EXAMPLES: remove a.txt | remove *.log\n");
    } else if (!strcmp(topic, "edit")) {
        kprintf("EDIT - nano-like text editor\n\n");
        kprintf("SYNOPSIS: edit <file>\n");
        kprintf("DESC: Full-screen editor. Creates file if missing.\n");
        kprintf("  Arrows move, Home/End line, PgUp/PgDn scroll.\n");
        kprintf("  ^O save, ^X (or ESC) exit, ^K cut, ^U paste.\n");
        kprintf("  Scrolls; status bar shows line/col.\n");
        kprintf("EXAMPLE: edit notes.txt\n");
    } else if (!strcmp(topic, "type")) {
        kprintf("TYPE - identify file type\n\n");
        kprintf("SYNOPSIS: type <file...> | type < file\n");
        kprintf("DESC: Inspects file header (ELF, PNG, JPEG, GIF,\n");
        kprintf("  BMP, ZIP, gzip, PDF, MZ, #! script). Text without\n");
        kprintf("  header prints ASCII text; binary without header\n");
        kprintf("  prints data or uses the file extension.\n");
        kprintf("EXAMPLES: type a.txt | type *.bin | type < a.txt\n");
    } else if (!strcmp(topic, "redirection") || !strcmp(topic, "redir") || !strcmp(topic, ">") || !strcmp(topic, "redirect")) {
        kprintf("REDIRECTION - file redirect (< > >>)\n");
        kprintf("SYNOPSIS: cmd > f | cmd >> f | cmd < f\n");
        kprintf("DESC: > creates/overwrites, >> appends, < feeds file\n");
        kprintf("  as stdin. Any command output can redirect.\n");
        kprintf("EXAMPLES: echo hi > a.txt | sysinfo > i.txt\n");
        kprintf("  echo more >> a.txt | print < a.txt\n");
    } else if (!strcmp(topic, "globbing") || !strcmp(topic, "glob") || !strcmp(topic, "*") || !strcmp(topic, "wildcard")) {
        kprintf("GLOBBING - wildcards (* ?)\n");
        kprintf("SYNOPSIS: list *.txt | print docs/*.txt\n");
        kprintf("DESC: * matches any run, ? matches one char.\n");
        kprintf("  Only after command word. Quote to suppress.\n");
        kprintf("EXAMPLES: list *.txt | echo \"*\"\n");
    } else if (!strcmp(topic, "chaining") || !strcmp(topic, "chain") || !strcmp(topic, ";")) {
        kprintf("CHAINING - run commands in order (;)\n");
        kprintf("SYNOPSIS: cmd1 ; cmd2 ; cmd3\n");
        kprintf("DESC: ';' splits commands. ';' in quotes or as \\;\n");
        kprintf("  stays literal. Blank parts skipped.\n");
        kprintf("EXAMPLE: create a.txt ; echo hi > a.txt ; print a.txt\n");
    } else {
        kprintf("No manual entry for '%s'\n", topic);
        kprintf("Try 'man' without args to list topics.\n");
    }
}

static void cmd_man(int argc, char **argv) {
    if (argc == 1) {
        man_overview();
        return;
    }
    for (int i = 1; i < argc; i++)
        man_for(argv[i]);
}


static void shell_exec(int argc, char **argv, const char *in_data, size_t in_len, int in_present) {
    (void)in_len;
    if (argc <= 0)
        return;
    const char *cmd = argv[0];

    if (!strcmp(cmd, "help")) {
        cmd_help();
    } else if (!strcmp(cmd, "man")) {
        cmd_man(argc, argv);
    } else if (!strcmp(cmd, "sysinfo")) {
        sysinfo_show();
    } else if (!strcmp(cmd, "echo")) {
        if (argc == 1) {
            if (in_present)
                print_with_nl(in_data);
            else
                kprintf("\n");
        } else {
            for (int i = 1; i < argc; i++) {
                if (i > 1)
                    kprintf(" ");
                kprintf("%s", argv[i]);
            }
            kprintf("\n");
        }
    } else if (!strcmp(cmd, "clear")) {
        tty_clear();
    } else if (!strcmp(cmd, "ticks")) {
        kprintf("%d\n", timer_ticks());
    } else if (!strcmp(cmd, "beep")) {
        outb(0x43, 0xB6);
        outb(0x42, 0x28);
        outb(0x42, 0x0A);
        outb(0x61, inb(0x61) | 3);
        timer_sleep(20);
        outb(0x61, inb(0x61) & ~3);
        kprintf("beep!\n");
    } else if (!strcmp(cmd, "create")) {
        if (argc < 2) {
            kprintf("usage: create <file...>\n");
        } else {
            for (int i = 1; i < argc; i++) {
                if (fs_exists(argv[i])) {
                    if (fs_is_dir(argv[i]))
                        kprintf("create: '%s': is a directory\n", argv[i]);
                } else if (fs_create(argv[i]) != 0) {
                    kprintf("create: cannot create '%s' (bad name? disk full?)\n", argv[i]);
                }
            }
        }
    } else if (!strcmp(cmd, "mkdir")) {
        if (argc < 2) {
            kprintf("usage: mkdir <dir...>\n");
        } else {
            for (int i = 1; i < argc; i++) {
                if (fs_exists(argv[i]))
                    kprintf("mkdir: '%s': already exists\n", argv[i]);
                else if (fs_mkdir(argv[i]) != 0)
                    kprintf("mkdir: cannot create '%s' (bad name? disk full?)\n", argv[i]);
            }
        }
    } else if (!strcmp(cmd, "list")) {
        cmd_ls(argc, argv);
    } else if (!strcmp(cmd, "print")) {
        cmd_cat(argc, argv, in_data, in_present);
    } else if (!strcmp(cmd, "type")) {
        cmd_type(argc, argv, in_data, in_present);
    } else if (!strcmp(cmd, "remove")) {
        if (argc < 2) {
            kprintf("usage: remove <file...>\n");
        } else {
            for (int i = 1; i < argc; i++) {
                if (has_glob(argv[i])) {
                    kprintf("remove: no matches: '%s'\n", argv[i]);
                } else if (!fs_exists(argv[i])) {
                    kprintf("remove: '%s': no such file\n", argv[i]);
                } else if (fs_delete(argv[i]) != 0) {
                    kprintf("remove: cannot remove '%s' (dir not empty?)\n", argv[i]);
                }
            }
        }
    } else if (!strcmp(cmd, "edit")) {
        if (argc < 2) {
            kprintf("usage: edit <file>\n");
        } else if (argc > 2) {
            kprintf("edit: too many arguments (usage: edit <file>)\n");
        } else {
            editor_open(argv[1]);
        }
    } else {
        kprintf("unknown command: '%s' (try help)\n", cmd);
    }
}


#define MAX_CMDS 32

static int split_semis(char *line, char *segs[], int max) {
    int n = 0;
    char *start = line;
    char quote = 0;
    char *p = line;
    for (;;) {
        char c = *p;
        if (c == 0) {
            char *end = p;
            char *s = start;
            while (s < end && is_space(*s))
                s++;
            char *e = end;
            while (e > s && is_space(*(e - 1)))
                e--;
            if (s < e && n < max)
                segs[n++] = s;
            break;
        }
        if (quote) {
            if (c == '\\' && p[1]) {
                p += 2;
                continue;
            }
            if (c == quote)
                quote = 0;
            p++;
        } else {
            if (c == '\\' && p[1]) {
                p += 2;
                continue;
            }
            if (c == '"' || c == '\'') {
                quote = c;
                p++;
            } else if (c == ';') {
                char *end = p;
                char *s = start;
                while (s < end && is_space(*s))
                    s++;
                char *e = end;
                while (e > s && is_space(*(e - 1)))
                    e--;
                if (s < e) {
                    if (n < max)
                        segs[n++] = s;
                    else
                        break;
                }
                *p = 0;
                start = p + 1;
                p++;
            } else {
                p++;
            }
        }
    }
    return n;
}

void shell_run(void) {
    static char line[256];
    static char bufs[MAX_ARGS][MAX_TOK];
    static char *argv[MAX_ARGS];
    static int qflags[MAX_ARGS];
    static char *argv2[MAX_ARGS];
    static int qflags2[MAX_ARGS];
    static char exp_bufs[MAX_ARGS][MAX_TOK];
    static char *eargv[MAX_ARGS];
    static char in_file[MAX_TOK], out_file[MAX_TOK];
    static char cap[CAP_SIZE];
    static char input[INPUT_SIZE];

    tty_setcolor(0x0A, 0x00);
    tty_write("Welcome to idkOS! Type 'help' for list of commands.\n");
    tty_setcolor(0x07, 0x00);
    for (;;) {
        tty_setcolor(0x0E, 0x00);
        tty_write("idk> ");
        tty_setcolor(0x07, 0x00);
        read_line(line, sizeof(line));

        static char *segs[MAX_CMDS];
        int ncmds = split_semis(line, segs, MAX_CMDS);
        for (int ci = 0; ci < ncmds; ci++) {
            char *cmdline = segs[ci];

            int tr = tokenize(cmdline, bufs, argv, qflags);
            if (tr == -1) {
                kprintf("syntax error: unclosed quote\n");
                continue;
            }
            if (tr == -2) {
                kprintf("syntax error: too many arguments (max %d)\n", MAX_ARGS);
                continue;
            }
            int argc = tr;
            if (argc == 0)
                continue;

            int append = 0;
            int pr = parse_redir(argc, argv, qflags, argv2, qflags2, in_file, out_file, &append);
            if (pr < 0) {
                kprintf("syntax error: redirection needs a filename (e.g. echo hi > f.txt)\n");
                continue;
            }
            int fargc = pr;
            int eargc = glob_expand(fargc, argv2, qflags2, exp_bufs, eargv);
            int have_in = in_file[0] != 0;
            int have_out = out_file[0] != 0;

            if (eargc == 0) {
                if (have_out) {
                    if (fs_write(out_file, "", 0, append) != 0)
                        kprintf("redirection failed: cannot write '%s'\n", out_file);
                } else if (have_in) {
                    kprintf("syntax error: no command for '< %s'\n", in_file);
                }
                continue;
            }

            const char *in_data = "";
            int in_present = 0;
            if (have_in) {
                int n = fs_read(in_file, input, sizeof(input));
                if (n < 0) {
                    kprintf("redirection failed: '%s': no such file\n", in_file);
                    continue;
                }
                in_data = input;
                in_present = 1;
                (void)n;
            }

            if (have_out)
                kprintf_begin_capture(cap, sizeof(cap));

            (void)base_name;

            shell_exec(eargc, eargv, in_data, in_present ? strlen(in_data) : 0, in_present);

            if (have_out) {
                size_t n = kprintf_end_capture();
                cap[n] = 0;
                if (fs_write(out_file, cap, n, append) != 0)
                    kprintf("redirection failed: cannot write '%s' (too big? bad name?)\n", out_file);
                else if (n + 1 >= sizeof(cap))
                    kprintf("warning: output truncated to %d bytes\n", CAP_SIZE - 1);
            }
        }
    }
}

