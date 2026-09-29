#include "kernel/fs.h"
#include "kernel/string.h"
#include "kernel/kmalloc.h"

#define FS_MAX_ENTRIES 64

typedef struct {
    int in_use;
    int is_dir;
    char name[FS_MAX_NAME + 1];
    char *data;
    size_t size;
    size_t cap;
} fs_entry_t;

static fs_entry_t fs_tab[FS_MAX_ENTRIES];

static int strstr_wrap(const char *p);
static int has_substr(const char *s, const char *sub);

void fs_init(void) {
    memset(fs_tab, 0, sizeof(fs_tab));
}

/* Normalize: strip leading '/'s and trailing '/'s (caller tells us if dir).
 * Returns 0 on ok, -1 on bad name. Out always NUL-terminated. */
static int normalize(const char *path, char *out, size_t cap) {
    if (!path || !out || cap == 0)
        return -1;
    while (*path == '/')
        path++;
    size_t n = strlen(path);
    while (n > 0 && path[n - 1] == '/')
        n--;
    if (n == 0 || n > FS_MAX_NAME)
        return -1;
    if (n + 1 > cap)
        return -1;
    memcpy(out, path, n);
    out[n] = 0;
    /* reject empty components ("//"), "." / ".." segments */
    if (strstr_wrap(out))
        return -1;
    return 0;
}

/* Small local helpers (avoid growing string.c for now). */
static int has_substr(const char *s, const char *sub) {
    size_t ls = strlen(s), lb = strlen(sub);
    if (lb == 0 || lb > ls)
        return 0;
    for (size_t i = 0; i + lb <= ls; i++) {
        size_t j = 0;
        while (j < lb && s[i + j] == sub[j])
            j++;
        if (j == lb)
            return 1;
    }
    return 0;
}

/* reject "//" anywhere and any "." / ".." path segment */
static int strstr_wrap(const char *p) {
    if (has_substr(p, "//"))
        return 1;
    /* walk segments */
    const char *s = p;
    while (*s) {
        const char *e = s;
        while (*e && *e != '/')
            e++;
        size_t len = (size_t)(e - s);
        if ((len == 1 && s[0] == '.') || (len == 2 && s[0] == '.' && s[1] == '.'))
            return 1;
        if (*e == '/')
            e++;
        s = e;
    }
    return 0;
}

static int find_slot(const char *norm) {
    for (int i = 0; i < FS_MAX_ENTRIES; i++)
        if (fs_tab[i].in_use && strcmp(fs_tab[i].name, norm) == 0)
            return i;
    return -1;
}

static int free_slot(void) {
    for (int i = 0; i < FS_MAX_ENTRIES; i++)
        if (!fs_tab[i].in_use)
            return i;
    return -1;
}

/* true if `prefix` is a parent dir of `path` ("docs" vs "docs/a.txt") */
static int is_child_of(const char *path, const char *prefix) {
    size_t lp = strlen(prefix);
    if (strncmp(path, prefix, lp) != 0)
        return 0;
    return path[lp] == '/';
}

int fs_create(const char *path) {
    char norm[FS_MAX_NAME + 1];
    if (normalize(path, norm, sizeof(norm)) != 0)
        return -1;
    if (find_slot(norm) >= 0)
        return -1; /* exists (touch on existing file is ok at shell level) */
    int s = free_slot();
    if (s < 0)
        return -1;
    strcpy(fs_tab[s].name, norm);
    fs_tab[s].data = 0;
    fs_tab[s].size = 0;
    fs_tab[s].cap = 0;
    fs_tab[s].is_dir = 0;
    fs_tab[s].in_use = 1;
    return 0;
}

int fs_mkdir(const char *path) {
    char norm[FS_MAX_NAME + 1];
    if (normalize(path, norm, sizeof(norm)) != 0)
        return -1;
    if (find_slot(norm) >= 0)
        return -1;
    int s = free_slot();
    if (s < 0)
        return -1;
    strcpy(fs_tab[s].name, norm);
    fs_tab[s].data = 0;
    fs_tab[s].size = 0;
    fs_tab[s].cap = 0;
    fs_tab[s].is_dir = 1;
    fs_tab[s].in_use = 1;
    return 0;
}

int fs_exists(const char *path) {
    char norm[FS_MAX_NAME + 1];
    if (normalize(path, norm, sizeof(norm)) != 0)
        return 0;
    return find_slot(norm) >= 0;
}

int fs_is_dir(const char *path) {
    char norm[FS_MAX_NAME + 1];
    if (normalize(path, norm, sizeof(norm)) != 0)
        return 0;
    int s = find_slot(norm);
    return s >= 0 && fs_tab[s].is_dir;
}

size_t fs_size(const char *path) {
    char norm[FS_MAX_NAME + 1];
    if (normalize(path, norm, sizeof(norm)) != 0)
        return (size_t)-1;
    int s = find_slot(norm);
    if (s < 0 || fs_tab[s].is_dir)
        return (size_t)-1;
    return fs_tab[s].size;
}

int fs_write(const char *path, const char *data, size_t len, int append) {
    char norm[FS_MAX_NAME + 1];
    if (normalize(path, norm, sizeof(norm)) != 0)
        return -1;
    if (len > FS_MAX_FILE)
        return -1;
    int s = find_slot(norm);
    if (s >= 0 && fs_tab[s].is_dir)
        return -1;
    if (s < 0) {
        if (fs_create(norm) != 0)
            return -1;
        s = find_slot(norm);
        if (s < 0)
            return -1;
    }
    size_t base = append ? fs_tab[s].size : 0;
    if (base + len > FS_MAX_FILE)
        return -1;
    if (base + len == 0) {
        /* truncate to empty: keep buffer, just reset size */
        fs_tab[s].size = 0;
        return 0;
    }
    if (fs_tab[s].cap < base + len) {
        size_t ncap = base + len;
        char *nd = (char *)kmalloc(ncap);
        if (!nd)
            return -1;
        if (append && fs_tab[s].data && fs_tab[s].size)
            memcpy(nd, fs_tab[s].data, fs_tab[s].size);
        if (fs_tab[s].data)
            kfree(fs_tab[s].data);
        fs_tab[s].data = nd;
        fs_tab[s].cap = ncap;
    }
    if (!append) {
        /* truncate: logical size reset, buffer reused */
        fs_tab[s].size = 0;
        base = 0;
    }
    if (len && data)
        memcpy(fs_tab[s].data + base, data, len);
    fs_tab[s].size = base + len;
    return 0;
}

int fs_read(const char *path, char *buf, size_t cap) {
    if (!buf || cap == 0)
        return -1;
    char norm[FS_MAX_NAME + 1];
    if (normalize(path, norm, sizeof(norm)) != 0)
        return -1;
    int s = find_slot(norm);
    if (s < 0 || fs_tab[s].is_dir)
        return -1;
    size_t n = fs_tab[s].size;
    if (n + 1 > cap)
        n = cap - 1; /* truncate, still NUL-terminate */
    if (n && fs_tab[s].data)
        memcpy(buf, fs_tab[s].data, n);
    buf[n] = 0;
    return (int)n;
}

int fs_delete(const char *path) {
    char norm[FS_MAX_NAME + 1];
    if (normalize(path, norm, sizeof(norm)) != 0)
        return -1;
    int s = find_slot(norm);
    if (s < 0)
        return -1;
    if (fs_tab[s].is_dir) {
        for (int i = 0; i < FS_MAX_ENTRIES; i++) {
            if (fs_tab[i].in_use && is_child_of(fs_tab[i].name, norm))
                return -1; /* not empty */
        }
    }
    if (fs_tab[s].data)
        kfree(fs_tab[s].data);
    fs_tab[s].data = 0;
    fs_tab[s].size = fs_tab[s].cap = 0;
    fs_tab[s].in_use = 0;
    fs_tab[s].is_dir = 0;
    fs_tab[s].name[0] = 0;
    return 0;
}

int fs_num_entries(void) {
    int n = 0;
    for (int i = 0; i < FS_MAX_ENTRIES; i++)
        if (fs_tab[i].in_use)
            n++;
    return n;
}

int fs_get_entry(int idx, char *name_out, size_t name_cap,
                 int *is_dir_out, size_t *size_out) {
    int cur = 0;
    for (int i = 0; i < FS_MAX_ENTRIES; i++) {
        if (!fs_tab[i].in_use)
            continue;
        if (cur == idx) {
            if (name_out && name_cap) {
                strncpy(name_out, fs_tab[i].name, name_cap - 1);
                name_out[name_cap - 1] = 0;
            }
            if (is_dir_out)
                *is_dir_out = fs_tab[i].is_dir;
            if (size_out)
                *size_out = fs_tab[i].size;
            return 0;
        }
        cur++;
    }
    return -1;
}

/* Glob: '*' matches any (possibly empty) run, '?' matches exactly one char. */
int fs_match(const char *pattern, const char *str) {
    const char *p = pattern, *s = str;
    const char *star = 0, *ss = 0;
    while (*s) {
        if (*p == '?' || *p == *s) {
            p++;
            s++;
        } else if (*p == '*') {
            star = p++;
            ss = s;
        } else if (star) {
            p = star + 1;
            s = ++ss;
        } else {
            return 0;
        }
    }
    while (*p == '*')
        p++;
    return *p == 0;
}
