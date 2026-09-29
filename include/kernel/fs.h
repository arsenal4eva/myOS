#ifndef _KERNEL_FS_H
#define _KERNEL_FS_H
#include <stddef.h>
#include <stdint.h>

/* Tiny in-memory (RAM) filesystem for idkOS shell.
 * Flat table of entries; names may contain '/' to simulate
 * directories (e.g. "docs/notes.txt"). No persistence. */

#define FS_MAX_NAME 56
#define FS_MAX_FILE 4096

void fs_init(void);

/* Create empty file / directory. 0 = ok, -1 = error (exists, bad name, full). */
int fs_create(const char *path);
int fs_mkdir(const char *path);

/* Queries */
int fs_exists(const char *path);
int fs_is_dir(const char *path);
size_t fs_size(const char *path); /* (size_t)-1 if missing/dir */

/* Write (creates file if missing). append=0 truncates, !=0 appends.
 * 0 = ok, -1 = error (dir, too big, bad name, OOM). */
int fs_write(const char *path, const char *data, size_t len, int append);

/* Read whole file into buf (NUL-terminates if space). Returns bytes read, -1 on error. */
int fs_read(const char *path, char *buf, size_t cap);

/* Delete file or *empty* dir. 0 = ok, -1 = error. */
int fs_delete(const char *path);

/* Iteration over all entries (files + dirs). */
int fs_num_entries(void);
int fs_get_entry(int idx, char *name_out, size_t name_cap,
                 int *is_dir_out, size_t *size_out);

/* Glob match: pattern supports '*' (any run) and '?' (any one char). 1 = match. */
int fs_match(const char *pattern, const char *str);

#endif
