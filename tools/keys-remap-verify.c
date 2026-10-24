#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <linux/input.h>

#define LBITS (8 * sizeof(long))

static int open_by_name(const char *want, int flags) {
    DIR *d = opendir("/dev/input"); if (!d) return -1;
    struct dirent *e; char path[300]; int fd = -1;
    while ((e = readdir(d))) {
        if (strncmp(e->d_name, "event", 5)) continue;
        snprintf(path, sizeof path, "/dev/input/%s", e->d_name);
        int f = open(path, flags | O_NONBLOCK); if (f < 0) continue;
        char nm[256] = ""; ioctl(f, EVIOCGNAME(sizeof nm), nm);
        if (!strcmp(nm, want)) { fd = f; break; }
        close(f);
    }
    closedir(d); return fd;
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <source_name>\n", argv[0]); return 2; }
    const char *src = argv[1];

    int vfd = -1;
    for (int t = 0; t < 30 && vfd < 0; t++) { vfd = open_by_name("remapped-buttons", O_RDONLY); if (vfd < 0) usleep(100000); }
    if (vfd < 0) { fprintf(stderr, "virtual 'remapped-buttons' not found (daemon up?)\n"); return 1; }
    if (ioctl(vfd, EVIOCGRAB, 1) < 0) perror("grab virtual");   /* shield the compositor from test clicks */
    { struct input_event ev; while (read(vfd, &ev, sizeof ev) == sizeof ev) {} }

    int gfd = open_by_name(src, O_RDONLY);
    if (gfd >= 0) {
        if (ioctl(gfd, EVIOCGRAB, 1) < 0)
            printf("[%s] grab-check: EBUSY -> GRABBED by daemon (good)\n", src);
        else { printf("[%s] grab-check: NOT grabbed (!!)\n", src); ioctl(gfd, EVIOCGRAB, 0); }
        close(gfd);
    }

    int sfd = open_by_name(src, O_RDWR);
    if (sfd < 0) { fprintf(stderr, "source '%s' not found\n", src); return 1; }
    unsigned long keyb[KEY_MAX / LBITS + 1]; memset(keyb, 0, sizeof keyb);
    ioctl(sfd, EVIOCGBIT(EV_KEY, sizeof keyb), keyb);
    int code = -1;
    for (int c = 0; c <= KEY_MAX; c++) if (keyb[c / LBITS] >> (c % LBITS) & 1) { code = c; break; }
    if (code < 0) { fprintf(stderr, "no key on source\n"); return 1; }
    printf("[%s] injecting source key code %d (press+release)\n", src, code);

    struct input_event inj[4]; memset(inj, 0, sizeof inj);
    inj[0].type = EV_KEY; inj[0].code = code; inj[0].value = 1;
    inj[1].type = EV_SYN; inj[1].code = SYN_REPORT;
    inj[2].type = EV_KEY; inj[2].code = code; inj[2].value = 0;
    inj[3].type = EV_SYN; inj[3].code = SYN_REPORT;
    if (write(sfd, inj, sizeof inj) != (ssize_t) sizeof inj) perror("inject");

    struct pollfd p = { .fd = vfd, .events = POLLIN };
    int seen = 0;
    for (int t = 0; t < 8; t++) {
        if (poll(&p, 1, 100) <= 0) continue;
        struct input_event ev;
        while (read(vfd, &ev, sizeof ev) == sizeof ev)
            if (ev.type == EV_KEY) { printf("   -> virtual mouse emitted code %u value %d\n", ev.code, ev.value); seen++; }
    }
    if (!seen) printf("   -> NO output observed (!!)\n");
    ioctl(vfd, EVIOCGRAB, 0);
    return 0;
}
