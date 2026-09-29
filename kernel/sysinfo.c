#include "kernel/sysinfo.h"
#include "kernel/tty.h"
#include "kernel/kprintf.h"
#include "kernel/string.h"
#include "kernel/timer.h"
#include "kernel/pmm.h"
#include "kernel/port.h"
#include "kernel/paging.h"
#include "kernel/kmalloc.h"

#define SI_LOGO_C  0x0B 
#define SI_LABEL_C 0x0E 
#define SI_VAL_C   0x07 
#define SI_HDR_C   0x0A 
#define SI_DIM_C   0x08 
#define SI_LOGO_W  26

static char *s_put(char *o, const char *s) {
    while (*s) *o++ = *s++;
    *o = 0;
    return o;
}
static char *s_putu(char *o, uint32_t v) {
    char t[10];
    int i = 0;
    if (!v) { *o++ = '0'; *o = 0; return o; }
    while (v) { t[i++] = (char)('0' + v % 10); v /= 10; }
    while (i--) *o++ = t[i];
    *o = 0;
    return o;
}
static void s_fit(char *s, size_t max) {
    if (strlen(s) > max) s[max] = 0;
    size_t n = strlen(s);
    while (n > 0 && s[n - 1] == ' ') s[--n] = 0;
}

static void cpuid32(uint32_t leaf, uint32_t sub, uint32_t *a,
                    uint32_t *b, uint32_t *c, uint32_t *d) {
    __asm__ volatile("cpuid"
                     : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
                     : "a"(leaf), "c"(sub));
}
static int cpu_has_cpuid(void) {
    uint32_t f1, f2;
    __asm__ volatile("pushfl; popl %0; movl %0, %1; xorl $0x200000, %1;"
                     "pushl %1; popfl; pushfl; popl %1"
                     : "=r"(f1), "=r"(f2) :: "cc");
    return ((f1 ^ f2) & 0x200000u) != 0;
}
static void cpu_rdtsc(uint32_t *lo, uint32_t *hi) {
    __asm__ volatile("rdtsc" : "=a"(*lo), "=d"(*hi));
}
static void cpu_vendor(char v[13]) {
    uint32_t a, b, c, d;
    cpuid32(0, 0, &a, &b, &c, &d);
    memcpy(v + 0, &b, 4);
    memcpy(v + 4, &d, 4);
    memcpy(v + 8, &c, 4);
    v[12] = 0;
}
static int cpu_brand(char b[49]) {
    uint32_t a, cc, d, e;
    cpuid32(0x80000000u, 0, &a, &cc, &d, &e);
    if (a < 0x80000004u) { b[0] = 0; return 0; }
    for (int i = 0; i < 3; i++) {
        cpuid32(0x80000002u + (uint32_t)i, 0, &a, &cc, &d, &e);
        memcpy(b + i * 16 + 0, &a, 4);
        memcpy(b + i * 16 + 4, &cc, 4);
        memcpy(b + i * 16 + 8, &d, 4);
        memcpy(b + i * 16 + 12, &e, 4);
    }
    b[48] = 0;
    size_t s = 0;
    while (b[s] == ' ') s++;
    if (s) memmove(b, b + s, 48 - s + 1);
    s_fit(b, 48);
    size_t w = 0; 
    for (size_t r = 0; b[r]; r++) {
        if (b[r] == ' ' && w > 0 && b[w - 1] == ' ') continue;
        b[w++] = b[r];
    }
    b[w] = 0;
    return b[0] != 0;
}
static uint32_t cpu_max_basic(void) {
    uint32_t a, b, c, d;
    cpuid32(0, 0, &a, &b, &c, &d);
    return a;
}
static void cpu_topo(uint32_t *cores, uint32_t *threads) {
    uint32_t a, b, c, d;
    *cores = 1; *threads = 1;
    if (cpu_max_basic() >= 0xBu) {
        uint32_t smt = 0, pkg = 0;
        for (uint32_t sub = 0; sub < 5; sub++) {
            cpuid32(0xBu, sub, &a, &b, &c, &d);
            uint32_t type = (c >> 8) & 0xFFu;
            if (!type) break;
            if (type == 1) smt = b & 0xFFFFu;
            else if (type == 2) pkg = b & 0xFFFFu;
        }
        if (pkg) {
            *threads = pkg;
            *cores = smt ? pkg / smt : pkg;
            if (!*cores) *cores = 1;
            return;
        }
    }
    cpuid32(1, 0, &a, &b, &c, &d);
    if (d & (1u << 28)) {
        uint32_t t = (b >> 16) & 0xFFu;
        if (t) *threads = t;
    }
    char ven[13];
    cpu_vendor(ven);
    if (!strcmp(ven, "GenuineIntel") && cpu_max_basic() >= 4) {
        cpuid32(4, 0, &a, &b, &c, &d);
        uint32_t co = (a >> 26) + 1;
        if (co && co < 1024) *cores = co;
    } else if (!strcmp(ven, "AuthenticAMD")) {
        cpuid32(0x80000000u, 0, &a, &b, &c, &d);
        if (a >= 0x80000008u) {
            cpuid32(0x80000008u, 0, &a, &b, &c, &d);
            uint32_t co = (c & 0xFFu) + 1;
            if (co < 1024) *cores = co;
        }
    }
    if (*cores > *threads) *threads = *cores;
}
static uint32_t cpu_mhz(void) {
    uint32_t a, b, c, d;
    cpuid32(1, 0, &a, &b, &c, &d);
    if (!(d & (1u << 4))) return 0;
    uint32_t t0 = timer_ticks();
    while (timer_ticks() == t0) {}
    uint32_t lo0, hi0, lo1, hi1;
    cpu_rdtsc(&lo0, &hi0);
    uint32_t start = timer_ticks();
    while (timer_ticks() - start < 10) {}
    cpu_rdtsc(&lo1, &hi1);
    uint32_t lo = lo1 - lo0;
    uint32_t hi = hi1 - hi0 - (lo1 < lo0 ? 1u : 0u);
    if (hi) return 0;
    return lo / 100000u;
}

#define SI_WIN 0xE0000000u
static void phys_copy(void *dst, uint32_t src, uint32_t len) {
    uint8_t *d = (uint8_t *)dst;
    while (len) {
        uint32_t pg = src & ~0xFFFu;
        uint32_t off = src & 0xFFFu;
        uint32_t n = 0x1000u - off;
        if (n > len) n = len;
        if (pg < 0x400000u) {
            memcpy(d, (const void *)src, n);
        } else {
            paging_map(SI_WIN, pg, 1, 0);
            memcpy(d, (const void *)(SI_WIN + off), n);
        }
        d += n; src += n; len -= n;
    }
}
static void phys_done(void) { paging_unmap(SI_WIN); }
static int sum8_eq0(const uint8_t *p, uint32_t len) {
    uint8_t s = 0;
    for (uint32_t i = 0; i < len; i++) s = (uint8_t)(s + p[i]);
    return s == 0;
}

typedef struct { uint16_t v, d; uint8_t cl, sub; } pcidev_t;
static pcidev_t si_gpus[4]; static int si_ngpu;
static pcidev_t si_nets[4]; static int si_nnet;
static pcidev_t si_auds[4]; static int si_naud;
static pcidev_t si_host; static int si_has_host;
static int si_pci_total;

static uint32_t pci_cfg(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off) {
    outl(0xCF8, 0x80000000u | ((uint32_t)bus << 16) |
                 ((uint32_t)dev << 11) | ((uint32_t)fn << 8) | (off & 0xFCu));
    return inl(0xCFC);
}
static void pci_add(pcidev_t *arr, int *n, int cap, pcidev_t *d) {
    if (*n < cap) arr[(*n)++] = *d;
}
static void pci_scan(void) {
    si_ngpu = si_nnet = si_naud = 0;
    si_has_host = 0; si_pci_total = 0;
    for (uint32_t bus = 0; bus < 256; bus++) {
        for (uint32_t dev = 0; dev < 32; dev++) {
            uint32_t w0 = pci_cfg((uint8_t)bus, (uint8_t)dev, 0, 0);
            if ((w0 & 0xFFFFu) == 0xFFFFu) continue;
            uint32_t hdr = pci_cfg((uint8_t)bus, (uint8_t)dev, 0, 0x0C);
            int nfs = (hdr & 0x00800000u) ? 8 : 1;
            for (int fn = 0; fn < nfs; fn++) {
                uint32_t r0 = pci_cfg((uint8_t)bus, (uint8_t)dev, (uint8_t)fn, 0);
                if ((r0 & 0xFFFFu) == 0xFFFFu) continue;
                uint32_t r2 = pci_cfg((uint8_t)bus, (uint8_t)dev, (uint8_t)fn, 0x08);
                pcidev_t d;
                d.v = (uint16_t)(r0 & 0xFFFFu);
                d.d = (uint16_t)((r0 >> 16) & 0xFFFFu);
                d.cl = (uint8_t)((r2 >> 24) & 0xFFu);
                d.sub = (uint8_t)((r2 >> 16) & 0xFFu);
                si_pci_total++;
                if (d.cl == 0x03) pci_add(si_gpus, &si_ngpu, 4, &d);
                else if (d.cl == 0x02) pci_add(si_nets, &si_nnet, 4, &d);
                else if (d.cl == 0x04) pci_add(si_auds, &si_naud, 4, &d);
                else if (d.cl == 0x06 && d.sub == 0x00 && !si_has_host) {
                    si_host = d; si_has_host = 1;
                }
            }
        }
    }
}
static const struct { uint16_t v, d; const char *n; } pci_names[] = {
    { 0x1234, 0x1111, "QEMU Standard VGA" },
    { 0x1013, 0x00B8, "Cirrus GD 5446" },
    { 0x1AF4, 0x1050, "VirtIO GPU" },
    { 0x15AD, 0x0405, "VMware SVGA" },
    { 0x80EE, 0xBEEF, "VirtualBox Graphics" },
    { 0x8086, 0x1237, "Intel 440FX Host" },
    { 0x8086, 0x7000, "Intel PIIX3 ISA" },
    { 0x8086, 0x7010, "Intel PIIX3 IDE" },
    { 0x8086, 0x100E, "Intel 82540EM Gigabit Ethernet" },
    { 0x8086, 0x10D3, "Intel 82574L Gigabit Ethernet" },
    { 0x8086, 0x2415, "Intel 82801AA AC'97 Audio" },
    { 0x8086, 0x2668, "Intel HD Audio" },
    { 0x10EC, 0x8139, "Realtek RTL-8139" },
    { 0x1AF4, 0x1000, "VirtIO Network" },
    { 0x1AF4, 0x1005, "VirtIO Sound" },
    { 0x1022, 0x2000, "AMD PCnet" },
    { 0x1B36, 0x0001, "QEMU PCI-PCI Bridge" },
    { 0, 0, 0 },
};
static const char *pci_ven_name(uint16_t v) {
    switch (v) {
    case 0x8086: return "Intel";
    case 0x10DE: return "NVIDIA";
    case 0x1002: return "AMD";
    case 0x1022: return "AMD";
    case 0x1234: return "QEMU";
    case 0x1013: return "Cirrus Logic";
    case 0x1AF4: return "VirtIO";
    case 0x1B36: return "Red Hat";
    case 0x10EC: return "Realtek";
    case 0x15AD: return "VMware";
    case 0x80EE: return "VirtualBox";
    case 0x1039: return "SiS";
    case 0x1106: return "VIA";
    default: return 0;
    }
}
static const char *pci_cls_name(uint8_t c, uint8_t s) {
    if (c == 0x01) {
        if (s == 0x01) return "IDE";
        if (s == 0x06) return "SATA";
        if (s == 0x08) return "NVMe";
        if (s == 0x00) return "SCSI";
        return "Storage";
    }
    if (c == 0x02) return "Ethernet";
    if (c == 0x03) return (s == 0x02) ? "3D" : "VGA";
    if (c == 0x04) return "Audio";
    if (c == 0x06) {
        if (s == 0x00) return "Host bridge";
        if (s == 0x01) return "ISA bridge";
        return "Bridge";
    }
    if (c == 0x0C && s == 0x03) return "USB";
    if (c == 0x07) return "Serial";
    return "Device";
}
static void pci_dev_name(char *o, pcidev_t *p) {
    for (size_t i = 0; pci_names[i].n; i++) {
        if (pci_names[i].v == p->v && pci_names[i].d == p->d) {
            s_put(o, pci_names[i].n);
            return;
        }
    }
    const char *vn = pci_ven_name(p->v);
    char *e = o;
    if (vn) { e = s_put(e, vn); e = s_put(e, " "); }
    s_put(e, pci_cls_name(p->cl, p->sub));
    s_fit(o, 30);
}
static void pci_hex4(char *o, uint32_t v) {
    static const char *h = "0123456789ABCDEF";
    o[0] = h[(v >> 12) & 15]; o[1] = h[(v >> 8) & 15];
    o[2] = h[(v >> 4) & 15]; o[3] = h[v & 15]; o[4] = 0;
}

static void sm_str(char *dst, size_t cap, const uint8_t *st, const uint8_t *end, uint8_t idx) {
    dst[0] = 0;
    if (!idx || !cap) return;
    const uint8_t *p = st;
    for (uint8_t i = 1;; i++) {
        if (p >= end || !*p) return;
        if (i == idx) {
            size_t j = 0;
            while (p + j < end && p[j] && j + 1 < cap) { dst[j] = (char)p[j]; j++; }
            dst[j] = 0;
            return;
        }
        while (p < end && *p) p++;
        p++;
    }
}
static int smbios_get(char sys[44], char bios[44]) {
    sys[0] = 0; bios[0] = 0;
    int got = 0;
    for (uint32_t a = 0xF0000; a < 0x100000; a += 16) {
        const uint8_t *e = (const uint8_t *)a;
        if (e[0] != '_' || e[1] != 'S' || e[2] != 'M' || e[3] != '_') continue;
        uint8_t len = e[5];
        if (len < 0x1F) continue;
        uint8_t hdr[0x20];
        memcpy(hdr, e, 0x1F);
        if (!sum8_eq0(hdr, 0x1F)) continue;
        uint32_t tlen = (uint32_t)hdr[0x16] | ((uint32_t)hdr[0x17] << 8);
        uint32_t taddr = (uint32_t)hdr[0x18] | ((uint32_t)hdr[0x19] << 8) |
                         ((uint32_t)hdr[0x1A] << 16) | ((uint32_t)hdr[0x1B] << 24);
        uint32_t count = (uint32_t)hdr[0x1C] | ((uint32_t)hdr[0x1D] << 8);
        if (!tlen || tlen > 256 * 1024 || !count || count > 4096) continue;
        uint8_t *tab = (uint8_t *)kmalloc(tlen);
        if (!tab) { phys_done(); return got; }
        phys_copy(tab, taddr, tlen);
        const uint8_t *p = tab, *end = tab + tlen;
        char maker[44] = { 0 }, prod[44] = { 0 };
        char bven[44] = { 0 }, bver[44] = { 0 }, bdate[20] = { 0 };
        int w0 = 1, w1 = 1;
        uint32_t n = 0;
        while (p + 4 <= end && n < count && n < 1024) {
            uint8_t type = p[0], sl = p[1];
            if (sl < 4 || p + sl > end) break;
            const uint8_t *str = p + sl;
            if (type == 0 && w0) {
                sm_str(bven, sizeof(bven), str, end, p[4]);
                sm_str(bver, sizeof(bver), str, end, p[5]);
                sm_str(bdate, sizeof(bdate), str, end, p[8]);
                w0 = 0;
            } else if (type == 1 && w1) {
                sm_str(maker, sizeof(maker), str, end, p[4]);
                sm_str(prod, sizeof(prod), str, end, p[5]);
                w1 = 0;
            }
            const uint8_t *q = str;
            while (q + 1 < end && (q[0] || q[1])) q++;
            p = q + 2;
            n++;
        }
        if (maker[0] || prod[0]) {
            char t[96];
            char *o = s_put(t, maker);
            if (maker[0] && prod[0]) o = s_put(o, " ");
            s_put(o, prod);
            s_fit(t, 43);
            strcpy(sys, t);
            got |= 1;
        }
        if (bven[0] || bver[0]) {
            char t[96];
            char *o = s_put(t, bven);
            if (bver[0]) { o = s_put(o, " "); o = s_put(o, bver); }
            if (bdate[0] && strlen(t) + 3 + strlen(bdate) <= 43) {
                o = s_put(o, " ("); o = s_put(o, bdate); s_put(o, ")");
            }
            s_fit(t, 43);
            strcpy(bios, t);
            got |= 2;
        }
        kfree(tab);
        break;
    }
    phys_done();
    return got;
}

enum { BAT_NO_ACPI = 0, BAT_NONE, BAT_PRESENT };
static int acpi_battery(int *rev_out, char oem[7]) {
    if (rev_out) *rev_out = 0;
    if (oem) oem[0] = 0;
    for (uint32_t a = 0xE0000; a < 0x100000; a += 16) {
        const uint8_t *e = (const uint8_t *)a;
        if (memcmp(e, "RSD PTR ", 8) != 0) continue;
        uint8_t rsdp[36];
        memcpy(rsdp, e, 36);
        if (!sum8_eq0(rsdp, 20)) continue;
        int rev = rsdp[15];
        if (rev >= 1 && !sum8_eq0(rsdp, 36)) continue;
        uint32_t rsdt = (uint32_t)rsdp[16] | ((uint32_t)rsdp[17] << 8) |
                        ((uint32_t)rsdp[18] << 16) | ((uint32_t)rsdp[19] << 24);
        uint32_t xsdt_lo = (uint32_t)rsdp[24] | ((uint32_t)rsdp[25] << 8) |
                           ((uint32_t)rsdp[26] << 16) | ((uint32_t)rsdp[27] << 24);
        uint32_t xsdt_hi = (uint32_t)rsdp[28] | ((uint32_t)rsdp[29] << 8) |
                           ((uint32_t)rsdp[30] << 16) | ((uint32_t)rsdp[31] << 24);
        uint32_t root = (rev >= 1 && xsdt_hi == 0 && xsdt_lo) ? xsdt_lo : rsdt;
        int is_xsdt = (root == xsdt_lo && xsdt_lo != 0);
        if (!root) { phys_done(); return BAT_NO_ACPI; }
        if (rev_out) *rev_out = rev;
        if (oem) { memcpy(oem, rsdp + 9, 6); oem[6] = 0; }
        uint8_t hdr[36];
        phys_copy(hdr, root, 36);
        if (memcmp(hdr, "RSDT", 4) != 0 && memcmp(hdr, "XSDT", 4) != 0) {
            phys_done();
            return BAT_NONE;
        }
        uint32_t hlen = (uint32_t)hdr[4] | ((uint32_t)hdr[5] << 8) |
                        ((uint32_t)hdr[6] << 16) | ((uint32_t)hdr[7] << 24);
        if (hlen < 36 || hlen > 4096) {
            phys_done();
            return BAT_NONE;
        }
        {
            uint8_t *full = (uint8_t *)kmalloc(hlen);
            if (full) {
                phys_copy(full, root, hlen);
                int ok = sum8_eq0(full, hlen);
                kfree(full);
                if (!ok) { phys_done(); return BAT_NONE; }
            }
        }
        uint32_t esz = is_xsdt ? 8u : 4u;
        uint32_t nent = (hlen >= 36 && hlen <= 4096) ? (hlen - 36) / esz : 0;
        if (nent > 128) nent = 128;
        uint32_t facp = 0;
        for (uint32_t i = 0; i < nent; i++) {
            uint8_t ent[8];
            phys_copy(ent, root + 36 + i * esz, esz);
            uint32_t t = (uint32_t)ent[0] | ((uint32_t)ent[1] << 8) |
                         ((uint32_t)ent[2] << 16) | ((uint32_t)ent[3] << 24);
            if (!t) continue;
            uint8_t sig[8];
            phys_copy(sig, t, 8);
            if (memcmp(sig, "FACP", 4) == 0) { facp = t; break; }
        }
        if (!facp) { phys_done(); return BAT_NONE; }
        uint8_t f[48];
        phys_copy(f, facp, 48);
        uint32_t dsdt = (uint32_t)f[40] | ((uint32_t)f[41] << 8) |
                        ((uint32_t)f[42] << 16) | ((uint32_t)f[43] << 24);
        if (!dsdt) { phys_done(); return BAT_NONE; }
        uint8_t dh[36];
        phys_copy(dh, dsdt, 36);
        if (memcmp(dh, "DSDT", 4) != 0) { phys_done(); return BAT_NONE; }
        uint32_t dlen = (uint32_t)dh[4] | ((uint32_t)dh[5] << 8) |
                        ((uint32_t)dh[6] << 16) | ((uint32_t)dh[7] << 24);
        if (dlen < 36 || dlen > 512 * 1024) { phys_done(); return BAT_NONE; }
        /* chunked AML scan for the battery _HID */
        uint8_t chunk[4096 + 8];
        uint32_t off = 36;
        int found = 0;
        while (off < dlen && !found) {
            uint32_t n = dlen - off;
            if (n > 4096) n = 4096;
            phys_copy(chunk, dsdt + off, n + 8 > dlen - off ? dlen - off : n + 8);
            uint32_t lim = (n + 8 > dlen - off ? dlen - off : n + 8);
            for (uint32_t i = 0; i + 7 < lim; i++) {
                if (memcmp(chunk + i, "PNP0C0A", 7) == 0) { found = 1; break; }
            }
            off += n;
        }
        phys_done();
        return found ? BAT_PRESENT : BAT_NONE;
    }
    phys_done();
    return BAT_NO_ACPI;
}

static int ata_stat(uint16_t st, uint32_t budget) {
    uint32_t start = timer_ticks();
    for (;;) {
        uint8_t s = inb(st);
        if (!(s & 0x80)) return s;
        if (timer_ticks() - start >= budget) return -1;
    }
}
static int ata_identify(uint16_t base, int slave, char model[41],
                        uint64_t *sectors, int *is_atapi) {
    uint16_t dt = base, st = base + 7, dev = base + 6, cmd = base + 7;
    uint16_t c1 = base + 4, c2 = base + 5;
    *sectors = 0; *is_atapi = 0; model[0] = 0;
    if (inb(st) == 0xFF) return 0; 
    outb(dev, (uint8_t)(0xA0 | (slave << 4)));
    for (int i = 0; i < 4; i++) io_wait();
    outb(base + 2, 0); outb(base + 3, 0);
    outb(c1, 0); outb(c2, 0);
    outb(cmd, 0xEC);
    uint8_t s = inb(st);
    if (!s) return 0;
    uint8_t lo = inb(c1), hi = inb(c2);
    if (lo == 0x14 && hi == 0xEB) {
        outb(cmd, 0xA1);
        *is_atapi = 1;
        s = inb(st);
        if (!s) return 0;
    }
    int w = ata_stat(st, 100);
    if (w < 0 || (w & 0x01)) return 0;
    if (!(w & 0x08)) return 0;
    uint16_t buf[256];
    for (int i = 0; i < 256; i++) buf[i] = inw(dt);
    for (int i = 0; i < 20; i++) {
        model[i * 2] = (char)(buf[27 + i] >> 8);
        model[i * 2 + 1] = (char)(buf[27 + i] & 0xFF);
    }
    model[40] = 0;
    s_fit(model, 28);
    if (!model[0]) s_put(model, "Unknown device");
    uint64_t sec;
    if (buf[83] & 0x0400) {
        sec = (uint64_t)buf[100] | ((uint64_t)buf[101] << 16) |
              ((uint64_t)buf[102] << 32) | ((uint64_t)buf[103] << 48);
    } else {
        sec = (uint64_t)buf[60] | ((uint64_t)buf[61] << 16);
    }
    *sectors = sec;
    return 1;
}

static void si_logo(const char *s) {
    char b[SI_LOGO_W + 1];
    size_t n = strlen(s);
    if (n > SI_LOGO_W) n = SI_LOGO_W;
    memcpy(b, s, n);
    for (size_t i = n; i < SI_LOGO_W; i++) b[i] = ' ';
    b[SI_LOGO_W] = 0;
    tty_setcolor(SI_LOGO_C, 0x00);
    kprintf("%s", b);
}
static void si_label(const char *s) {
    tty_setcolor(SI_LABEL_C, 0x00);
    kprintf("%s", s);
}
static void si_val(const char *s) {
    tty_setcolor(SI_VAL_C, 0x00);
    kprintf("%s", s);
}
static void si_nl(void) { kprintf("\n"); }
static void si_row(const char *logo, const char *label, const char *val) {
    si_logo(logo);
    si_label(label);
    si_val(val);
    si_nl();
}

void sysinfo_show(void) {
    static const char *art[] = {
        " ____ ____             ",
        "  ||  |   \\  |   /     ",
        "  ||  |    | |__/      ",
        "  ||  |    | |  \\     ",
        " _||_ |___/  |   \\    ",
    };
    const char *logo[9];
    for (int i = 0; i < 9; i++) logo[i] = "";
    for (int i = 0; i < 5; i++) logo[i + 2] = art[i];
    int r = 0;
    char val[96];

    si_logo(logo[r++]);
    tty_setcolor(SI_HDR_C, 0x00); kprintf("%s", "guest");
    si_val("@");
    tty_setcolor(SI_HDR_C, 0x00); kprintf("%s", "idkOS");
    si_nl();
    si_logo(logo[r++]);
    tty_setcolor(SI_DIM_C, 0x00); kprintf("%s", "-----------");
    si_nl();

    si_row(logo[r++], "OS: ", "idkOS 0.1.0 i686");

    pci_scan();
    char sysname[44] = { 0 }, biosinfo[44] = { 0 };
    int sm = smbios_get(sysname, biosinfo);
    int acpi_rev = 0;
    char acpi_oem[7] = { 0 };
    int bat = acpi_battery(&acpi_rev, acpi_oem);

    if (!(sm & 1)) {
        if (si_has_host) pci_dev_name(sysname, &si_host);
        else s_put(sysname, "Generic PC");
    }
    si_row(logo[r++], "Host: ", sysname);

    if (!(sm & 2)) {
        if (acpi_rev) {
            char *o = s_put(biosinfo, "ACPI ");
            o = s_putu(o, (uint32_t)acpi_rev + 1);
            o = s_put(o, ".0 (");
            o = s_put(o, acpi_oem);
            s_put(o, ")");
        } else {
            s_put(biosinfo, "unknown");
        }
    }
    si_row(logo[r++], "BIOS: ", biosinfo);

    {
        char *o = s_put(val, "idk 0.1.0 i686 multiboot1 (");
        o = s_put(o, __DATE__);
        s_put(o, ")");
        s_fit(val, 44);
        si_row(logo[r++], "Kernel: ", val);
    }

    si_row(logo[r++], "Shell: ", "idk> (built-in)");
    si_row(logo[r++], "Display: ", "80x25 VGA text + serial");

    if (si_ngpu) {
        char nm[36];
        pci_dev_name(nm, &si_gpus[0]);
        char *o = s_put(val, nm);
        if (si_ngpu > 1) { o = s_put(o, " +"); o = s_putu(o, (uint32_t)(si_ngpu - 1)); }
        char h1[5], h2[5];
        pci_hex4(h1, si_gpus[0].v); pci_hex4(h2, si_gpus[0].d);
        o = s_put(o, " ["); o = s_put(o, h1); o = s_put(o, ":");
        o = s_put(o, h2); s_put(o, "]");
        s_fit(val, 44);
        si_row(logo[r++], "GPU: ", val);
    } else {
        si_row(logo[r++], "GPU: ", "none detected");
    }

    if (cpu_has_cpuid()) {
        char vendor[13], brand[49];
        cpu_vendor(vendor);
        int hb = cpu_brand(brand);
        uint32_t cores, threads;
        cpu_topo(&cores, &threads);
        uint32_t mhz = cpu_mhz();
        char *o;
        if (hb) {
            s_fit(brand, 32);
            o = s_put(val, brand);
        } else {
            o = s_put(val, vendor);
        }
        o = s_put(o, " ("); o = s_put(o, vendor); s_put(o, ")");
        s_fit(val, 44);
        si_row("", "CPU: ", val);
        o = s_put(val, "~");
        o = mhz ? s_putu(o, mhz) : s_put(o, "???");
        o = s_put(o, " MHz, ");
        o = s_putu(o, cores); o = s_put(o, "C/");
        o = s_putu(o, threads); s_put(o, "T");
        si_row("", "Clock: ", val);
    } else {
        si_row("", "CPU: ", "unknown (no CPUID)");
    }

    {
        uint32_t total_f = pmm_total_count();
        uint32_t free_f = pmm_free_count();
        uint32_t used_f = total_f > free_f ? total_f - free_f : 0;
        uint32_t pct = total_f ? (used_f * 100u) / total_f : 0;
        char *o = s_putu(val, (used_f * 4) / 1024);
        o = s_put(o, "MiB / ");
        o = s_putu(o, (total_f * 4) / 1024);
        o = s_put(o, "MiB (");
        o = s_putu(o, pct);
        o = s_put(o, "%) [");
        uint32_t filled = (pct * 10) / 100;
        for (uint32_t i = 0; i < 10; i++) *o++ = (i < filled) ? '#' : '-';
        *o++ = ']'; *o = 0;
        si_row("", "Memory: ", val);
    }

    {
        char model[41] = { 0 };
        uint64_t sec = 0;
        int atapi = 0, found = 0;
        const uint16_t bases[2] = { 0x1F0, 0x170 };
        for (int ch = 0; ch < 2 && !found; ch++)
            for (int sl = 0; sl < 2 && !found; sl++)
                if (ata_identify(bases[ch], sl, model, &sec, &atapi) && !atapi)
                    found = 1;
        if (!found)
            for (int ch = 0; ch < 2 && !found; ch++)
                for (int sl = 0; sl < 2 && !found; sl++)
                    if (ata_identify(bases[ch], sl, model, &sec, &atapi))
                        found = 1;
        if (found) {
            char *o = s_put(val, atapi ? "ATAPI " : "");
            o = s_put(o, model);
            uint32_t gib = (uint32_t)(sec >> 21);
            if (gib) { o = s_put(o, " ("); o = s_putu(o, gib); s_put(o, " GiB)"); }
            else if (sec) {
                o = s_put(o, " (");
                o = s_putu(o, (uint32_t)(sec >> 11));
                s_put(o, " MiB)");
            }
            s_fit(val, 44);
        } else {
            s_put(val, "none detected");
        }
        si_row("", "Disk: ", val);
    }

    if (si_nnet) {
        char nm[36];
        pci_dev_name(nm, &si_nets[0]);
        char *o = s_put(val, nm);
        if (si_nnet > 1) { o = s_put(o, " +"); o = s_putu(o, (uint32_t)(si_nnet - 1)); }
        char h1[5], h2[5];
        pci_hex4(h1, si_nets[0].v); pci_hex4(h2, si_nets[0].d);
        o = s_put(o, " ["); o = s_put(o, h1); o = s_put(o, ":");
        o = s_put(o, h2); s_put(o, "]");
        s_fit(val, 44);
        si_row("", "Network: ", val);
    } else {
        si_row("", "Network: ", "none detected");
    }
    si_label("Audio: ");
    if (si_naud) {
        char nm[36];
        pci_dev_name(nm, &si_auds[0]);
        char *o = s_put(val, nm);
        if (si_naud > 1) { o = s_put(o, " +"); o = s_putu(o, (uint32_t)(si_naud - 1)); }
        char h1[5], h2[5];
        pci_hex4(h1, si_auds[0].v); pci_hex4(h2, si_auds[0].d);
        o = s_put(o, " ["); o = s_put(o, h1); o = s_put(o, ":");
        o = s_put(o, h2); s_put(o, "]");
        s_fit(val, 44);
        si_val(val);
    } else {
        si_val("none detected");
    }
    si_nl();

    si_label("Battery: ");
    if (bat == BAT_PRESENT)
        si_val("ACPI device present (charge stats need EC driver)");
    else if (bat == BAT_NONE)
        si_val("none detected (AC power)");
    else
        si_val("unknown (no ACPI tables)");
    si_nl();

    {
        uint32_t ticks = timer_ticks();
        uint32_t secs = ticks / 100;
        uint32_t m = secs / 60; secs %= 60;
        uint32_t h = m / 60; m %= 60;
        uint32_t d = h / 24; h %= 24;
        char *o = s_put(val, "");
        if (d) { o = s_putu(o, d); o = s_put(o, "d "); }
        if (h || d) { o = s_putu(o, h); o = s_put(o, "h "); }
        if (m || h || d) { o = s_putu(o, m); o = s_put(o, "m "); }
        o = s_putu(o, secs); o = s_put(o, "s (");
        o = s_putu(o, ticks); s_put(o, " ticks)");
        si_label("Uptime: ");
        si_val(val);
        si_nl();
    }

    for (int i = 0; i < 8; i++) {
        tty_setcolor(0x00, (uint8_t)i);
        kprintf("%s", "   ");
    }
    tty_setcolor(SI_VAL_C, 0x00);
    kprintf("%s", " ");
    for (int i = 8; i < 16; i++) {
        tty_setcolor(0x00, (uint8_t)i);
        kprintf("%s", "   ");
    }
    tty_setcolor(SI_VAL_C, 0x00);
    si_nl();
}
