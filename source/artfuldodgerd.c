// Replay hardware key events through uinput; consume capture chords when ready.

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
#include <pthread.h>
#include <stdatomic.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/time.h>
#include "protocol.h"
#include "chord.h"
#include <linux/input.h>
#include <linux/uinput.h>
#include <android/log.h>

#define LG(...) __android_log_print(ANDROID_LOG_INFO, "ArtfulDodger", __VA_ARGS__)

static volatile sig_atomic_t g_run = 1;
static int g_evfd = -1, g_uifd = -1;

static long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}
static int test_bit(const unsigned char *b, int i) {
    return (b[i / 8] >> (i % 8)) & 1;
}

static void cleanup(void) {
    if (g_evfd >= 0) {
        ioctl(g_evfd, EVIOCGRAB, 0);
        close(g_evfd);
        g_evfd = -1;
    }
    if (g_uifd >= 0) {
        ioctl(g_uifd, UI_DEV_DESTROY);
        close(g_uifd);
        g_uifd = -1;
    }
}
static void on_sig(int s) {
    (void)s;
    g_run = 0;
}

// Distinguish the hardware node from our gpio-keys clone by its physical path.
static int open_gpio_keys(void) {
    for (int i = 0; i < 64; i++) {
        char path[32];
        snprintf(path, sizeof(path), "/dev/input/event%d", i);
        int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
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

// Keep the hardware identity so InputReader uses gpio-keys.kl, including Power wake.
static int make_clone(int src) {
    int ui = open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (ui < 0) {
        LG("open uinput: %s", strerror(errno));
        return -1;
    }

    unsigned char evbits[(EV_MAX + 7) / 8] = {0};
    if (ioctl(src, EVIOCGBIT(0, sizeof(evbits)), evbits) < 0) {
        LG("EVIOCGBIT(0): %s", strerror(errno));
        close(ui);
        return -1;
    }
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

    struct uinput_setup us;
    memset(&us, 0, sizeof(us));
    struct input_id id;
    if (ioctl(src, EVIOCGID, &id) == 0) us.id = id;
    if (ioctl(src, EVIOCGNAME(sizeof(us.name)), us.name) < 0 || us.name[0] == 0)
        strncpy(us.name, "gpio-keys", sizeof(us.name) - 1);

    if (ioctl(ui, UI_DEV_SETUP, &us) < 0) {
        struct uinput_user_dev ud;
        memset(&ud, 0, sizeof(ud));
        strncpy(ud.name, us.name, sizeof(ud.name) - 1);
        ud.id = us.id;
        if (write(ui, &ud, sizeof(ud)) != (ssize_t)sizeof(ud)) {
            LG("legacy setup: %s", strerror(errno));
            close(ui);
            return -1;
        }
    }
    if (ioctl(ui, UI_DEV_CREATE) < 0) {
        LG("UI_DEV_CREATE: %s", strerror(errno));
        close(ui);
        return -1;
    }
    return ui;
}

static int event_error;
static void emit(void *ctx, const struct input_event *ev) {
    (void)ctx;
    ssize_t n;
    do {
        n = write(g_uifd, ev, sizeof(*ev));
    } while (n < 0 && errno == EINTR);
    if (n != sizeof(*ev)) event_error = 1;
}
// -1 unavailable; 0 busy (ordinary keys pass through); 1 ready.
static atomic_int capture_state = -1;
static atomic_int input_grabbed = 0;
static unsigned long long requests, saved_count, failed_count;
static unsigned last_error;
static long capture_ms, save_ms;
static char boot_id[80];
static void status(const char *phase) {
    static char previous[32];
    static int previous_grab = -1;
    static unsigned long long previous_requests = ~0ULL, previous_saved, previous_failed;
    static unsigned previous_error;
    int grab = atomic_load(&input_grabbed);
    if (!strcmp(previous, phase) && previous_grab == grab && previous_requests == requests &&
        previous_saved == saved_count && previous_failed == failed_count &&
        previous_error == last_error)
        return;
    const char *dir = "/data/adb/artfuldodger";
    char tmp[160];
    snprintf(tmp, sizeof(tmp), "%s/runtime.%d.tmp", dir, getpid());
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) return;
    dprintf(fd,
            "pid=%d\nboot_id=%s\nstate=%s\ninput_grabbed=%d\nrequests=%llu\nsaved=%llu\nfailed=%"
            "llu\nlast_error=%u\nlast_result=%s\ncapture_ms=%ld\nsave_ms=%ld\n",
            getpid(), boot_id, phase, atomic_load(&input_grabbed), requests, saved_count,
            failed_count, last_error, ad_error_text(last_error), capture_ms, save_ms);
    close(fd);
    if (rename(tmp, "/data/adb/artfuldodger/runtime"))
        unlink(tmp);
    else {
        snprintf(previous, sizeof(previous), "%s", phase);
        previous_grab = grab;
        previous_requests = requests;
        previous_saved = saved_count;
        previous_failed = failed_count;
        previous_error = last_error;
    }
}
static void diagnostic(void) {
    const char *path = "/data/adb/artfuldodger/events.log";
    struct stat st;
    if (!stat(path, &st) && st.st_size > 32768)
        rename(path, "/data/adb/artfuldodger/events.previous.log");
    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) return;
    dprintf(fd, "pid=%d request=%llu result=%u capture_ms=%ld save_ms=%ld\n", getpid(), requests,
            last_error, capture_ms, save_ms);
    close(fd);
}
static int wake_pipe[2];
static char publisher[4096];
static int publish(const char *name) {
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (!pid) {
        if (setsid() < 0 || prctl(PR_SET_PDEATHSIG, SIGKILL) < 0 || getppid() == 1) _exit(127);
        execl("/system/bin/sh", "sh", publisher, name, (char *)NULL);
        _exit(127);
    }
    int status;
    for (int i = 0; i < 120; ++i) {
        pid_t result = waitpid(pid, &status, WNOHANG);
        if (result == pid) return WIFEXITED(status) ? WEXITSTATUS(status) : AD_FILESYSTEM;
        if (result < 0 && errno != EINTR) return -1;
        usleep(100000);
    }
    kill(-pid, SIGKILL);
    kill(pid, SIGKILL);
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
    return AD_TIMEOUT;
}
static void *capture_worker(void *unused) {
    (void)unused;
    status("waiting");
    int fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    struct sockaddr_un address;
    socklen_t len = ad_address(&address);
    struct timeval timeout = {15, 0};
    if (fd < 0) return NULL;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    if (connect(fd, (struct sockaddr *)&address, len) || !ad_peer_is(fd, 1000)) goto done;
    AdMessage reply;
    if (ad_receive(fd, &reply) || reply.kind != AD_READY || reply.id) goto done;
    atomic_store(&capture_state, 1);
    uint64_t id = 0;
    while (g_run) {
        struct pollfd pollfds[2] = {{wake_pipe[0], POLLIN, 0}, {fd, POLLIN, 0}};
        int n = poll(pollfds, 2, 1000);
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
        status(atomic_load(&input_grabbed) ? "ready" : "starting");
        // An idle connection should be quiet; unexpected traffic invalidates readiness.
        if (pollfds[1].revents) break;
        if (!(pollfds[0].revents & POLLIN)) continue;
        char command;
        if (read(wake_pipe[0], &command, 1) != 1) break;
        ++requests;
        last_error = 0;
        capture_ms = save_ms = 0;
        status("busy");
        long started = now_ms();
        if (ad_send(fd, AD_CAPTURE, ++id, NULL) || ad_receive(fd, &reply) || reply.id != id) {
            last_error = AD_UNAVAILABLE;
            break;
        }
        capture_ms = now_ms() - started;
        if (reply.kind == AD_FAILED) {
            last_error = reply.error;
            ++failed_count;
            diagnostic();
            atomic_store(&capture_state, 1);
            continue;
        }
        if (reply.kind != AD_CAPTURED || !ad_name_valid(reply.name)) {
            last_error = AD_INVALID;
            break;
        }
        started = now_ms();
        int error = publish(reply.name);
        save_ms = now_ms() - started;
        last_error = error < 0 ? AD_FILESYSTEM : (unsigned)error;
        if (!last_error)
            ++saved_count;
        else
            ++failed_count;
        diagnostic();
        if (ad_send_result(fd, last_error ? AD_FAILED : AD_SAVED, id, NULL, last_error) ||
            ad_receive(fd, &reply) || reply.kind != AD_READY || reply.id != id) {
            last_error = AD_UNAVAILABLE;
            break;
        }
        atomic_store(&capture_state, 1);
    }
done:
    atomic_store(&capture_state, -1);
    if (!last_error) last_error = AD_UNAVAILABLE;
    status("unavailable");
    close(fd);
    return NULL;
}
static int fire_capture(void *ctx) {
    (void)ctx;
    int expected = 1;
    if (!atomic_compare_exchange_strong(&capture_state, &expected, 0)) return 0;
    if (write(wake_pipe[1], "c", 1) != 1) {
        atomic_store(&capture_state, -1);
        return 0;
    }
    return 1;
}

int main(int argc, char **argv) {
    if (argc != 2 || strlen(argv[1]) >= sizeof(publisher) || argv[1][0] != '/') return 1;
    strcpy(publisher, argv[1]);
    if (access(publisher, R_OK) != 0) return 1;
    // Only one daemon may own the input path.
    int singleton =
        open("/data/adb/artfuldodger/daemon.lock", O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (singleton < 0 || flock(singleton, LOCK_EX | LOCK_NB) != 0) return 1;
    FILE *boot = fopen("/proc/sys/kernel/random/boot_id", "r");
    if (boot) {
        if (!fgets(boot_id, sizeof(boot_id), boot)) boot_id[0] = 0;
        fclose(boot);
    }
    boot_id[strcspn(boot_id, "\r\n")] = 0;
    freopen("/dev/null", "r", stdin);
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_sig;
    sa.sa_flags = 0; // no SA_RESTART so poll() wakes on SIGTERM
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);

    if (pipe2(wake_pipe, O_CLOEXEC | O_NONBLOCK)) return 1;
    pthread_t worker;
    if (pthread_create(&worker, NULL, capture_worker, NULL)) return 1;
    pthread_detach(worker);
    for (int i = 0; i < 160 && atomic_load(&capture_state) < 0; ++i)
        usleep(100000);
    if (atomic_load(&capture_state) != 1) {
        LG("capture service not ready; keys untouched");
        return 1;
    }
    g_evfd = open_gpio_keys();
    if (g_evfd < 0) {
        LG("no gpio-keys device found");
        return 1;
    }
    g_uifd = make_clone(g_evfd);
    if (g_uifd < 0) {
        close(g_evfd);
        return 1;
    }
    atexit(cleanup);

    usleep(400 * 1000); // let InputReader enumerate the clone before we grab
    if (ioctl(g_evfd, EVIOCGRAB, 1) < 0) {
        LG("EVIOCGRAB: %s", strerror(errno));
        return 1;
    }
    unsigned char initial[(KEY_MAX + 7) / 8] = {0};
    if (ioctl(g_evfd, EVIOCGKEY(sizeof(initial)), initial) < 0) return 1;
    for (int key = 0; key <= KEY_MAX; ++key)
        if (test_bit(initial, key)) {
            LG("keys held during activation; retry after release");
            return 1;
        }
    atomic_store(&input_grabbed, 1);
    LG("running");

    AdChord chord = {0};
    struct pollfd pfd = {g_evfd, POLLIN, 0};
    while (g_run && !event_error && atomic_load(&capture_state) >= 0) {
        int pr = poll(&pfd, 1, chord.first ? 20 : 250);
        if (pr < 0) {
            if (errno == EINTR) continue;
            break;
        }
        ad_expire(&chord, now_ms(), emit, NULL);
        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) break;
        if (!(pfd.revents & POLLIN)) continue;
        struct input_event ev;
        ssize_t bytes;
        while ((bytes = read(g_evfd, &ev, sizeof(ev))) == sizeof(ev)) {
            // Release the grab if the kernel reports lost events.
            if (ev.type == EV_SYN && ev.code == SYN_DROPPED) {
                event_error = 1;
                break;
            }
            ad_event(&chord, &ev, now_ms(), atomic_load(&capture_state) == 1, emit, fire_capture,
                     NULL);
            if (event_error) break;
        }
        if (bytes == 0 || (bytes < 0 && errno != EAGAIN && errno != EINTR)) break;
        if (bytes > 0 && bytes != sizeof(ev)) break;
    }
    g_run = 0;
    atomic_store(&input_grabbed, 0);
    ad_flush(&chord, emit, NULL);
    cleanup();
    return event_error ? 1 : 0;
}
