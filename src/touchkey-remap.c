#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <poll.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <linux/input.h>
#include <linux/uinput.h>

#ifndef SOURCE_NAME
#define SOURCE_NAME "sec_touchkey"
#endif
#ifndef OUT_NAME
#define OUT_NAME "remapped-touchkey"
#endif

/*
 * Deliberately simple implementation:
 *
 *     BTN_TOUCH down -> BTN_LEFT down
 *     BTN_TOUCH up   -> BTN_LEFT up
 */

static volatile sig_atomic_t stop_requested;

static void handle_signal(int signal_number)
{
    (void)signal_number;
    stop_requested = 1;
}

/*
 * Return 1 for a new press, 0 for a new release, and -1 when the logical
 * button state did not change.
 */
static int translate_touch_state(int source_value, int *left_held)
{
    int pressed = source_value != 0;

    if (pressed == *left_held)
        return -1;

    *left_held = pressed;
    return pressed;
}

static int emit_event(int ui, unsigned short type, unsigned short code,
                      int value)
{
    struct input_event event;
    ssize_t written;

    memset(&event, 0, sizeof(event));
    event.type = type;
    event.code = code;
    event.value = value;

    do {
        written = write(ui, &event, sizeof(event));
    } while (written < 0 && errno == EINTR);

    if (written == (ssize_t)sizeof(event))
        return 0;

    if (written < 0)
        perror("touchkey-remap: write /dev/uinput");
    else
        fprintf(stderr, "touchkey-remap: short write to /dev/uinput\n");
    return -1;
}

static int emit_left(int ui, int pressed)
{
    if (emit_event(ui, EV_KEY, BTN_LEFT, pressed) < 0)
        return -1;
    return emit_event(ui, EV_SYN, SYN_REPORT, 0);
}

static int open_source(void)
{
    int attempt;

    for (attempt = 0; attempt < 20; attempt++) {
        DIR *dir = opendir("/dev/input");

        if (dir) {
            struct dirent *entry;

            while ((entry = readdir(dir))) {
                char path[300];
                char name[256] = "";
                int fd;

                if (strncmp(entry->d_name, "event", 5) != 0)
                    continue;

                snprintf(path, sizeof(path), "/dev/input/%s", entry->d_name);
                fd = open(path, O_RDONLY | O_NONBLOCK);
                if (fd < 0)
                    continue;

                (void)ioctl(fd, EVIOCGNAME(sizeof(name)), name);
                if (strcmp(name, SOURCE_NAME) == 0 &&
                    ioctl(fd, EVIOCGRAB, 1) == 0) {
                    closedir(dir);
                    return fd;
                }
                close(fd);
            }
            closedir(dir);
        }
        usleep(500000);
    }

    return -1;
}

static int create_uinput(void)
{
    struct uinput_setup setup;
    int ui = open("/dev/uinput", O_WRONLY);

    if (ui < 0) {
        perror("touchkey-remap: open /dev/uinput");
        return -1;
    }

    if (ioctl(ui, UI_SET_EVBIT, EV_KEY) < 0 ||
        ioctl(ui, UI_SET_EVBIT, EV_REL) < 0 ||
        ioctl(ui, UI_SET_KEYBIT, BTN_LEFT) < 0 ||
        ioctl(ui, UI_SET_RELBIT, REL_X) < 0 ||
        ioctl(ui, UI_SET_RELBIT, REL_Y) < 0) {
        perror("touchkey-remap: configure /dev/uinput");
        close(ui);
        return -1;
    }

    memset(&setup, 0, sizeof(setup));
    snprintf(setup.name, sizeof(setup.name), OUT_NAME);
    setup.id.bustype = BUS_VIRTUAL;
    setup.id.vendor = 1;
    setup.id.product = 2;

    if (ioctl(ui, UI_DEV_SETUP, &setup) < 0 ||
        ioctl(ui, UI_DEV_CREATE) < 0) {
        perror("touchkey-remap: create uinput device");
        close(ui);
        return -1;
    }

    return ui;
}

static int run_service(void)
{
    struct sigaction action;
    struct pollfd pfd;
    int exit_status = 0;
    int left_held = 0;
    int src;
    int ui;

    src = open_source();
    if (src < 0) {
        fprintf(stderr, "touchkey-remap: %s not found\n", SOURCE_NAME);
        return 1;
    }

    ui = create_uinput();
    if (ui < 0) {
        (void)ioctl(src, EVIOCGRAB, 0);
        close(src);
        return 1;
    }

    memset(&action, 0, sizeof(action));
    action.sa_handler = handle_signal;
    sigemptyset(&action.sa_mask);
    (void)sigaction(SIGINT, &action, NULL);
    (void)sigaction(SIGTERM, &action, NULL);

    pfd.fd = src;
    pfd.events = POLLIN;

    while (!stop_requested) {
        int poll_result = poll(&pfd, 1, -1);

        if (poll_result < 0) {
            if (errno == EINTR)
                continue;
            perror("touchkey-remap: poll");
            exit_status = 1;
            break;
        }

        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            fprintf(stderr, "touchkey-remap: source device disconnected\n");
            exit_status = 1;
            break;
        }

        if (pfd.revents & POLLIN) {
            for (;;) {
                struct input_event event;
                ssize_t count = read(src, &event, sizeof(event));

                if (count == (ssize_t)sizeof(event)) {
                    int translated;

                    if (event.type != EV_KEY ||
                        event.code != BTN_TOUCH)
                        continue;

                    translated =
                        translate_touch_state(event.value, &left_held);
                    if (translated >= 0 &&
                        emit_left(ui, translated) < 0) {
                        exit_status = 1;
                        stop_requested = 1;
                        break;
                    }
                    continue;
                }

                if (count < 0 &&
                    (errno == EAGAIN || errno == EWOULDBLOCK))
                    break;
                if (count < 0 && errno == EINTR)
                    continue;

                if (count < 0)
                    perror("touchkey-remap: read source device");
                else
                    fprintf(stderr,
                            "touchkey-remap: short read from source device\n");
                exit_status = 1;
                stop_requested = 1;
                break;
            }
        }
    }

    if (left_held)
        (void)emit_left(ui, 0);
    (void)ioctl(ui, UI_DEV_DESTROY);
    close(ui);
    (void)ioctl(src, EVIOCGRAB, 0);
    close(src);
    return exit_status;
}

static int run_self_test(void)
{
    static const int source_values[] = { 1, 1, 2, 0, 0, 1, 0 };
    static const int expected[] = { 1, -1, -1, 0, -1, 1, 0 };
    int left_held = 0;
    size_t i;

    for (i = 0; i < sizeof(source_values) / sizeof(source_values[0]); i++) {
        int actual = translate_touch_state(source_values[i], &left_held);

        if (actual != expected[i]) {
            fprintf(stderr,
                    "touchkey-remap self-test failed at event %zu: "
                    "got %d, expected %d\n",
                    i, actual, expected[i]);
            return 1;
        }
    }

    if (left_held != 0) {
        fprintf(stderr,
                "touchkey-remap self-test failed: left button remains held\n");
        return 1;
    }

    printf("touchkey-remap self-test: press/release forwarding passed\n");
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--self-test") == 0)
        return run_self_test();

    if (argc != 1) {
        fprintf(stderr, "usage: %s [--self-test]\n", argv[0]);
        return 2;
    }

    return run_service();
}
