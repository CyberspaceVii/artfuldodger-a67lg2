// The Artful Dodger input daemon: grab the hardware keys and pass everything through a
// uinput clone except the screenshot chord, which we hand to the capture module.
// Stock never sees the chord, so nothing gets blanked. Dies clean on any signal -
// the grab releases and the clone goes away, so the keys go back to normal.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/system_properties.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <android/log.h>

#define TRIGPROP   "debug.cyberspacevii.artfuldodger"    // capture module fires on any change
#define CHORD_WINDOW_MS 120              // both keys must go down within this
#define HELD_MAX 64
#define LG(...) __android_log_print(ANDROID_LOG_INFO, "ArtfulDodger", __VA_ARGS__)

static volatile sig_atomic_t g_run = 1;
static int g_evfd = -1, g_uifd = -1;

static long now_ms(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}
static int test_bit(const unsigned char *b, int i) { return (b[i / 8] >> (i % 8)) & 1; }

static void cleanup(void) {
    if (g_evfd >= 0) { ioctl(g_evfd, EVIOCGRAB, 0); close(g_evfd); g_evfd = -1; }
    if (g_uifd >= 0) { ioctl(g_uifd, UI_DEV_DESTROY); close(g_uifd); g_uifd = -1; }
}
static void on_sig(int s) { (void) s; g_run = 0; }

// The real gpio-keys node. Our clone shares the name, so also require a phys and
// a POWER key - and no hardcoded event number.
static int open_gpio_keys(void) {
    for (int i = 0; i < 64; i++) {
        char path[32]; snprintf(path, sizeof(path), "/dev/input/event%d", i);
        int fd = open(path, O_RDONLY | O_NONBLOCK);   // non-blocking: idle waits in poll(), not read()
        if (fd < 0) continue;
        char name[80] = {0}, phys[80] = {0};
        unsigned char kb[(KEY_MAX + 7) / 8] = {0};
        ioctl(fd, EVIOCGNAME(sizeof(name)), name);
        ioctl(fd, EVIOCGPHYS(sizeof(phys)), phys);
        ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(kb)), kb);
        if (strcmp(name, "gpio-keys") == 0 && phys[0] && test_bit(kb, KEY_POWER)) return fd;
        close(fd);
    }
    return -1;
}

// Clone into uinput with the same name/id/keys, so Android loads the same
// keylayout (gpio-keys.kl -> POWER keeps WAKE) and the keys behave identically.
static int make_clone(int src) {
    int ui = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (ui < 0) { LG("open uinput: %s", strerror(errno)); return -1; }

    unsigned char evbits[(EV_MAX + 7) / 8] = {0};
    if (ioctl(src, EVIOCGBIT(0, sizeof(evbits)), evbits) < 0) { LG("EVIOCGBIT(0): %s", strerror(errno)); close(ui); return -1; }
    for (int ev = 0; ev <= EV_MAX; ev++) {
        if (ev == EV_REP || !test_bit(evbits, ev)) continue;
        ioctl(ui, UI_SET_EVBIT, ev);
    }
    if (test_bit(evbits, EV_KEY)) {
        unsigned char keybits[(KEY_MAX + 7) / 8] = {0};
        ioctl(src, EVIOCGBIT(EV_KEY, sizeof(keybits)), keybits);
        for (int k = 0; k <= KEY_MAX; k++)
            if (test_bit(keybits, k)) ioctl(ui, UI_SET_KEYBIT, k);
    }

    struct uinput_setup us; memset(&us, 0, sizeof(us));
    struct input_id id;
    if (ioctl(src, EVIOCGID, &id) == 0) us.id = id;
    if (ioctl(src, EVIOCGNAME(sizeof(us.name)), us.name) < 0 || us.name[0] == 0)
        strncpy(us.name, "gpio-keys", sizeof(us.name) - 1);

    if (ioctl(ui, UI_DEV_SETUP, &us) < 0) {
        struct uinput_user_dev ud; memset(&ud, 0, sizeof(ud));
        strncpy(ud.name, us.name, sizeof(ud.name) - 1);
        ud.id = us.id;
        if (write(ui, &ud, sizeof(ud)) != (ssize_t) sizeof(ud)) { LG("legacy setup: %s", strerror(errno)); close(ui); return -1; }
    }
    if (ioctl(ui, UI_DEV_CREATE) < 0) { LG("UI_DEV_CREATE: %s", strerror(errno)); close(ui); return -1; }
    return ui;
}

static void emit(int ui, const struct input_event *ev) {
    struct input_event e = *ev;
    if (write(ui, &e, sizeof(e)) != (ssize_t) sizeof(e)) return;
}

static void fire_capture(void) {
    static unsigned long n = 0;
    char v[PROP_VALUE_MAX];
    snprintf(v, sizeof(v), "c%lu", ++n);   // never starts with '0'
    __system_property_set(TRIGPROP, v);
}

int main(void) {
    freopen("/dev/null", "r", stdin);
    struct sigaction sa; memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_sig; sa.sa_flags = 0;   // no SA_RESTART so poll() wakes on SIGTERM
    sigaction(SIGTERM, &sa, NULL); sigaction(SIGINT, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);

    g_evfd = open_gpio_keys();
    if (g_evfd < 0) { LG("no gpio-keys device found"); return 1; }
    g_uifd = make_clone(g_evfd);
    if (g_uifd < 0) { close(g_evfd); return 1; }
    atexit(cleanup);

    usleep(400 * 1000);   // let InputReader enumerate the clone before we grab
    if (ioctl(g_evfd, EVIOCGRAB, 1) < 0) { LG("EVIOCGRAB: %s", strerror(errno)); return 1; }
    __system_property_set(TRIGPROP, "0");   // baseline so the module can take a serial before any chord
    LG("running");

    int rawdown[KEY_MAX + 1]; memset(rawdown, 0, sizeof(rawdown));
    int swallow_up[KEY_MAX + 1]; memset(swallow_up, 0, sizeof(swallow_up));
    struct input_event held[HELD_MAX];
    int nheld = 0, pending = 0;
    long pending_since = 0;

    struct pollfd pfd = { g_evfd, POLLIN, 0 };
    while (g_run) {
        pfd.revents = 0;
        int pr = poll(&pfd, 1, pending ? 20 : 1000);
        if (pr < 0) { if (errno == EINTR) continue; LG("poll: %s", strerror(errno)); break; }

        // window elapsed with nothing new -> a single key, not the chord: flush it
        if (pending && now_ms() - pending_since > CHORD_WINDOW_MS) {
            for (int i = 0; i < nheld; i++) emit(g_uifd, &held[i]);
            nheld = 0; pending = 0;
        }
        if (pr == 0) continue;

        struct input_event ev;
        while (read(g_evfd, &ev, sizeof(ev)) == (ssize_t) sizeof(ev)) {
            int chordkey = (ev.type == EV_KEY && (ev.code == KEY_POWER || ev.code == KEY_VOLUMEDOWN));
            if (ev.type == EV_KEY && ev.code >= 0 && ev.code <= KEY_MAX)
                rawdown[ev.code] = (ev.value != 0);   // value 2 (repeat) counts as down

            if (!pending && chordkey && ev.value == 1) {   // first chord-key down -> start buffering
                pending = 1; pending_since = now_ms(); nheld = 0;
                held[nheld++] = ev;
                continue;
            }

            if (pending) {
                if (nheld < HELD_MAX) held[nheld++] = ev;
                if (rawdown[KEY_POWER] && rawdown[KEY_VOLUMEDOWN]) {
                    // real chord: swallow the downs we buffered (and their later
                    // ups); a key already passed through is left alone
                    for (int i = 0; i < nheld; i++)
                        if (held[i].type == EV_KEY && held[i].value == 1 &&
                            (held[i].code == KEY_POWER || held[i].code == KEY_VOLUMEDOWN))
                            swallow_up[held[i].code] = 1;
                    nheld = 0; pending = 0;
                    fire_capture();
                    continue;
                }
                if ((ev.type == EV_KEY && ev.value == 0 && chordkey) || nheld >= HELD_MAX) {
                    for (int i = 0; i < nheld; i++) emit(g_uifd, &held[i]);
                    nheld = 0; pending = 0;
                }
                continue;
            }

            if (ev.type == EV_KEY && ev.value == 0 && ev.code >= 0 && ev.code <= KEY_MAX && swallow_up[ev.code]) {
                swallow_up[ev.code] = 0;   // swallow the up of a swallowed chord-key down
                continue;
            }
            emit(g_uifd, &ev);
        }
    }
    cleanup();
    return 0;
}
