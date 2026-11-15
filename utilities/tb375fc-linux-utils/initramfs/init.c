/*
 * TB375FC / MT6897 Interactive C init with BusyBox shell
 */

#ifndef BOOT_PARTITION
#define BOOT_PARTITION "/dev/null"
#endif

#define AT_FDCWD -100
#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR   2
#define O_CREAT  0x40
#define O_TRUNC  0x200
#define O_NONBLOCK 0x800

#define MS_RDONLY      1
#define MS_NOSUID      2
#define MS_NODEV       4
#define MS_NOEXEC      8
#define MS_REMOUNT     32
#define MS_MOVE        8192

#define SIGCHLD        17
#define TIOCSCTTY      0x540E

struct timespec {
    long tv_sec;
    long tv_nsec;
};

struct dirent64 {
    unsigned long long d_ino;
    long long d_off;
    unsigned short d_reclen;
    unsigned char d_type;
    char d_name[];
};

void *memcpy(void *dest, const void *src, unsigned long n)
{
    char *d = (char *)dest;
    const char *s = (const char *)src;
    while (n--)
        *d++ = *s++;
    return dest;
}

void *memset(void *s, int c, unsigned long n)
{
    unsigned char *p = (unsigned char *)s;
    while (n--)
        *p++ = (unsigned char)c;
    return s;
}

static long ksys(long n, long a, long b, long c, long d, long e)
{
    register long x0 asm("x0") = a;
    register long x1 asm("x1") = b;
    register long x2 asm("x2") = c;
    register long x3 asm("x3") = d;
    register long x4 asm("x4") = e;
    register long x8 asm("x8") = n;
    asm volatile("svc 0"
                 : "+r"(x0)
                 : "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x8)
                 : "memory");
    return x0;
}

static long mount_(const char *src, const char *tgt, const char *fstype,
                   unsigned long flags, const void *data)
{
    return ksys(40, (long)src, (long)tgt, (long)fstype, flags, (long)data);
}

static long openat(long dirfd, const char *path, long flags, long mode)
{
    return ksys(56, dirfd, (long)path, flags, mode, 0);
}

static long close_(long fd)
{
    return ksys(57, fd, 0, 0, 0, 0);
}

static long read_(long fd, void *buf, unsigned long len)
{
    return ksys(63, fd, (long)buf, len, 0, 0);
}

static long write_(long fd, const void *buf, unsigned long len)
{
    return ksys(64, fd, (long)buf, len, 0, 0);
}

static long mkdirat(long dirfd, const char *path, long mode)
{
    return ksys(34, dirfd, (long)path, mode, 0, 0);
}

static long nanosleep(const struct timespec *req, struct timespec *rem)
{
    return ksys(101, (long)req, (long)rem, 0, 0, 0);
}

static long getdents64(long fd, void *buf, unsigned long len)
{
    return ksys(217, fd, (long)buf, len, 0, 0);
}

static long execve_(const char *path, const char **argv, const char **envp)
{
    return ksys(221, (long)path, (long)argv, (long)envp, 0, 0);
}

static long sync_(void)
{
    return ksys(81, 0, 0, 0, 0, 0);
}

static long symlinkat(const char *oldpath, long newdirfd, const char *newpath)
{
    return ksys(36, (long)oldpath, newdirfd, (long)newpath, 0, 0);
}

static long strlen_(const char *s)
{
    long n = 0;
    while (s[n])
        n++;
    return n;
}

static void sleep_ms(unsigned ms)
{
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, 0);
}

static long kmsg_fd = -1;
static long tty_fd = -1;

static void klog(const char *s)
{
    long len = strlen_(s);
    if (kmsg_fd >= 0)
        write_(kmsg_fd, s, len);
    if (tty_fd >= 0)
        write_(tty_fd, s, len);
}

static void dump_file(const char *path, const char *prefix)
{
    long fd = openat(AT_FDCWD, path, O_RDONLY, 0);
    if (fd < 0) {
        klog(prefix);
        klog(": not available\n");
        return;
    }
    klog(prefix);
    klog(":\n");
    char buf[512];
    long n;
    while ((n = read_(fd, buf, sizeof(buf) - 1)) > 0) {
        buf[n] = '\0';
        klog(buf);
    }
    close_(fd);
}

void _start(void)
{
    mkdirat(AT_FDCWD, "/dev", 0755);
    mkdirat(AT_FDCWD, "/proc", 0755);
    mkdirat(AT_FDCWD, "/sys", 0755);
    mkdirat(AT_FDCWD, "/root", 0750);
    mkdirat(AT_FDCWD, "/tmp", 0777);

    mount_("devtmpfs", "/dev", "devtmpfs", 0, 0);
    mount_("proc", "/proc", "proc", 0, 0);
    mount_("sysfs", "/sys", "sysfs", 0, 0);

    mkdirat(AT_FDCWD, "/dev/pts", 0755);
    mount_("devpts", "/dev/pts", "devpts", 0, 0);

    kmsg_fd = openat(AT_FDCWD, "/dev/kmsg", O_WRONLY, 0);
    tty_fd = openat(AT_FDCWD, "/dev/tty0", O_WRONLY, 0);

    klog("\n\n");
    klog("===============================================================\n");
    klog("  Lenovo Xiaoxin Pad Pro 12.7 (TB375FC / MT8792)\n");
    klog("  Mainline Linux 7.2 PREEMPT - Interactive Userspace\n");
    klog("===============================================================\n\n");

    dump_file("/proc/version", "Kernel Version");

    /* Create busybox applet symlinks */
    static const char * const applets[] = {
        "ls", "cat", "echo", "sh", "ps", "top", "dmesg", "mount", "umount",
        "reboot", "poweroff", "clear", "vi", "mkdir", "rm", "cp", "mv",
        "uname", "df", "free", "kill", "grep", "find", "wc", "head", "tail",
        "sleep", "sync", 0
    };
    for (int i = 0; applets[i]; i++) {
        char target[64];
        long p = 0;
        const char *prefix = "/bin/";
        while (prefix[p]) { target[p] = prefix[p]; p++; }
        long q = 0;
        while (applets[i][q]) { target[p++] = applets[i][q++]; }
        target[p] = '\0';
        symlinkat("/bin/busybox", AT_FDCWD, target);
    }

    klog("\n[OK] Interactive shell starting on /dev/tty0 (display).\n");
    klog("[INFO] Attach USB keyboard via OTG to type commands directly.\n\n");

    /* Infinite shell spawn loop */
    for (;;) {
        long pid = ksys(220, SIGCHLD, 0, 0, 0, 0); // clone(SIGCHLD, 0)
        if (pid == 0) {
            /* Child process */
            ksys(157, 0, 0, 0, 0, 0); // setsid
            long tfd = openat(AT_FDCWD, "/dev/tty0", O_RDWR, 0);
            if (tfd >= 0) {
                ksys(29, tfd, TIOCSCTTY, 0, 0, 0); // TIOCSCTTY
                ksys(24, tfd, 0, 0, 0, 0); // dup3 -> 0
                ksys(24, tfd, 1, 0, 0, 0); // dup3 -> 1
                ksys(24, tfd, 2, 0, 0, 0); // dup3 -> 2
                if (tfd > 2) close_(tfd);
            }
            const char *sh_argv[3];
            const char *sh_envp[4];
            sh_argv[0] = "/bin/sh";
            sh_argv[1] = "-i";
            sh_argv[2] = 0;
            sh_envp[0] = "HOME=/root";
            sh_envp[1] = "TERM=linux";
            sh_envp[2] = "PATH=/bin:/sbin:/usr/bin:/usr/sbin";
            sh_envp[3] = 0;

            execve_("/bin/sh", sh_argv, sh_envp);
            sh_argv[0] = "/bin/busybox";
            sh_argv[1] = "sh";
            execve_("/bin/busybox", sh_argv, sh_envp);

            /* If execve fails */
            klog("[ERROR] execve /bin/sh failed!\n");
            for (;;) sleep_ms(2000);
        }

        /* Parent: wait4 */
        int status = 0;
        ksys(260, pid, (long)&status, 0, 0, 0);
        klog("[INIT] Shell session closed. Restarting interactive shell in 1s...\n");
        sleep_ms(1000);
    }
}
