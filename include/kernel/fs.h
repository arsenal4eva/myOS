#ifndef _KERNEL_FS_H
#define _KERNEL_FS_H
#include <stddef.h>
#include <stdint.h>

#define FS_MAX_NAME 56
#define FS_MAX_FILE 4096

void fs_init(void);

int fs_create(const char *path);
int fs_mkdir(const char *path);

int fs_exists(const char *path);
int fs_is_dir(const char *path);
size_t fs_size(const char *path); 

int fs_write(const char *path, const char *data, size_t len, int append);

int fs_read(const char *path, char *buf, size_t cap);

int fs_delete(const char *path);

int fs_num_entries(void);
int fs_get_entry(int idx, char *name_out, size_t name_cap,
                 int *is_dir_out, size_t *size_out);

int fs_match(const char *pattern, const char *str);

#endif
