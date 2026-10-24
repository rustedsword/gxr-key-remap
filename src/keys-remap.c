#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <linux/input.h>
#include <linux/uinput.h>

static const struct { const char *name; unsigned short btn; } MAP[] = {
    { "pmic_pwrkey", BTN_LEFT   },  /* power button -> left click   */
    { "pmic_resin",  KEY_F13    },  /* volume down  -> F13 key      */
    { "gpio_keys",   BTN_RIGHT  },  /* volume up    -> right click  */
};
enum { NMAP = sizeof(MAP) / sizeof(MAP[0]) };

int main(void) {
    struct pollfd pfd[NMAP];
    unsigned short btn[NMAP];
    int n = 0;

    for (int attempt = 0; attempt < 20 && n < NMAP; attempt++) {
        for (int i = 0; i < n; i++) close(pfd[i].fd);
        n = 0;
        DIR *dir = opendir("/dev/input");
        if (dir) {
            for (struct dirent *e; (e = readdir(dir)) && n < NMAP; ) {
                if (strncmp(e->d_name, "event", 5)) continue;
                char path[300];
                snprintf(path, sizeof path, "/dev/input/%s", e->d_name);
                int fd = open(path, O_RDONLY | O_NONBLOCK);
                if (fd < 0) continue;
                char name[256] = "";
                ioctl(fd, EVIOCGNAME(sizeof name), name);
                int m = -1;
                for (int i = 0; i < NMAP; i++)
                    if (!strcmp(name, MAP[i].name)) { m = i; break; }
                if (m < 0 || ioctl(fd, EVIOCGRAB, 1) < 0) { close(fd); continue; }
                pfd[n].fd = fd; pfd[n].events = POLLIN; btn[n] = MAP[m].btn; n++;
            }
            closedir(dir);
        }
        if (n < NMAP) usleep(500000);
    }
    if (n < NMAP) { fprintf(stderr, "keys-remap: found %d/%d devices\n", n, NMAP); return 1; }

    int ui = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (ui < 0) { perror("open /dev/uinput"); return 1; }
    ioctl(ui, UI_SET_EVBIT, EV_KEY);
    ioctl(ui, UI_SET_EVBIT, EV_REL);
    for (int i = 0; i < NMAP; i++) ioctl(ui, UI_SET_KEYBIT, MAP[i].btn);
    /* declaring relative axes makes udev/libinput classify this as a mouse */
    ioctl(ui, UI_SET_RELBIT, REL_X);
    ioctl(ui, UI_SET_RELBIT, REL_Y);
    struct uinput_setup us;
    memset(&us, 0, sizeof us);
    snprintf(us.name, sizeof us.name, "remapped-buttons");
    us.id.bustype = BUS_VIRTUAL; us.id.vendor = 1; us.id.product = 1;
    if (ioctl(ui, UI_DEV_SETUP, &us) < 0 || ioctl(ui, UI_DEV_CREATE) < 0) {
        perror("uinput create"); return 1;
    }

    for (;;) {
        if (poll(pfd, n, -1) < 0) { if (errno == EINTR) continue; return 1; }
        for (int i = 0; i < n; i++) {
            if (pfd[i].revents & (POLLERR | POLLHUP)) { close(pfd[i].fd); pfd[i].fd = -1; continue; }
            if (!(pfd[i].revents & POLLIN)) continue;
            struct input_event ev;
            while (read(pfd[i].fd, &ev, sizeof ev) == (ssize_t) sizeof ev) {
                if (ev.type != EV_KEY || ev.value == 2) continue;
                struct input_event out[2];
                memset(out, 0, sizeof out);
                out[0].type = EV_KEY; out[0].code = btn[i]; out[0].value = ev.value;
                out[1].type = EV_SYN; out[1].code = SYN_REPORT;
                (void)! write(ui, out, sizeof out);
            }
        }
    }
}
