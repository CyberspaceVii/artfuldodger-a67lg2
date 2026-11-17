#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include "chord.h"
#include "protocol.h"
typedef struct {
    struct input_event events[256];
    int count, shots, accept;
} Harness;
static void emit(void *ctx, const struct input_event *ev) {
    Harness *h = ctx;
    assert(h->count < 256);
    h->events[h->count++] = *ev;
}
static int fire(void *ctx) {
    Harness *h = ctx;
    if (h->accept) ++h->shots;
    return h->accept;
}
static void event(AdChord *s, Harness *h, int code, int value, long now, int ready) {
    struct input_event ev = {0};
    ev.type = EV_KEY;
    ev.code = code;
    ev.value = value;
    ad_event(s, &ev, now, ready, emit, fire, h);
}
static void chords(void) {
    AdChord s = {0};
    Harness h = {.accept = 1};
    event(&s, &h, KEY_POWER, 1, 0, 1);
    assert(h.count == 0);
    ad_expire(&s, 121, emit, &h);
    event(&s, &h, KEY_POWER, 0, 130, 1);
    assert(h.count == 2 && h.shots == 0);
    memset(&s, 0, sizeof(s));
    memset(&h, 0, sizeof(h));
    h.accept = 1;
    event(&s, &h, KEY_POWER, 1, 0, 1);
    event(&s, &h, KEY_VOLUMEDOWN, 1, 50, 1);
    event(&s, &h, KEY_POWER, 2, 60, 0);
    event(&s, &h, KEY_VOLUMEDOWN, 2, 70, 0);
    event(&s, &h, KEY_POWER, 0, 80, 0);
    event(&s, &h, KEY_VOLUMEDOWN, 0, 90, 0);
    assert(h.count == 0 && h.shots == 1);
    // A subsequent independent press is restored after swallowed releases.
    event(&s, &h, KEY_POWER, 1, 100, 0);
    event(&s, &h, KEY_POWER, 0, 110, 0);
    assert(h.count == 2);
    memset(&s, 0, sizeof(s));
    memset(&h, 0, sizeof(h));
    h.accept = 1;
    event(&s, &h, KEY_POWER, 1, 0, 1);
    event(&s, &h, KEY_VOLUMEUP, 1, 5, 1);
    event(&s, &h, KEY_VOLUMEDOWN, 1, 10, 1);
    assert(h.shots == 1 && h.count == 1 && h.events[0].code == KEY_VOLUMEUP);
    memset(&s, 0, sizeof(s));
    memset(&h, 0, sizeof(h));
    event(&s, &h, KEY_VOLUMEDOWN, 1, 0, 1);
    event(&s, &h, KEY_POWER, 1, 20, 1);
    assert(h.count == 2 && h.shots == 0); // Request refused: full chord is replayed.
    memset(&s, 0, sizeof(s));
    memset(&h, 0, sizeof(h));
    h.accept = 1;
    event(&s, &h, KEY_POWER, 1, 0, 1);
    event(&s, &h, KEY_VOLUMEDOWN, 1, 500, 1);
    event(&s, &h, KEY_VOLUMEDOWN, 0, 510, 1);
    event(&s, &h, KEY_POWER, 0, 520, 1);
    assert(h.count == 4 && h.shots == 0); // Long-held first key is not a fresh chord.
    memset(&s, 0, sizeof(s));
    memset(&h, 0, sizeof(h));
    h.accept = 1;
    event(&s, &h, KEY_POWER, 1, 0, 1);
    event(&s, &h, KEY_VOLUMEDOWN, 1, 20, 0);
    assert(h.count == 2 && h.shots == 0); // Readiness lost mid-chord.
    memset(&s, 0, sizeof(s));
    memset(&h, 0, sizeof(h));
    event(&s, &h, KEY_VOLUMEUP, 1, 0, 1);
    event(&s, &h, KEY_VOLUMEUP, 0, 5, 1);
    assert(h.count == 2);
    // Exhaust every key order and press gap around the chord boundary.
    for (int reverse = 0; reverse < 2; ++reverse)
        for (int gap = 0; gap <= 150; ++gap) {
            memset(&s, 0, sizeof(s));
            memset(&h, 0, sizeof(h));
            h.accept = 1;
            int first = reverse ? KEY_POWER : KEY_VOLUMEDOWN,
                second = reverse ? KEY_VOLUMEDOWN : KEY_POWER;
            event(&s, &h, first, 1, 0, 1);
            event(&s, &h, second, 1, gap, 1);
            event(&s, &h, first, 0, gap + 1, 1);
            event(&s, &h, second, 0, gap + 2, 1);
            assert(h.shots == (gap <= AD_CHORD_MS));
            assert(h.count == (gap <= AD_CHORD_MS ? 0 : 4));
        }
}
static void protocol(void) {
    assert(ad_name_valid("cap_123_456_1.png"));
    assert(!ad_name_valid("cap_../../file.png"));
    assert(!ad_name_valid("cap_123.png/sh"));
    assert(!ad_name_valid("cap_.png"));
    int fds[2];
    assert(socketpair(AF_UNIX, SOCK_SEQPACKET, 0, fds) == 0);
    assert(ad_peer_is(fds[0], getuid()));
    assert(!ad_peer_is(fds[0], getuid() + 1));
    AdMessage m;
    assert(ad_send(fds[0], AD_CAPTURED, 42, "cap_123_1.png") == 0);
    assert(ad_receive(fds[1], &m) == 0 && m.kind == AD_CAPTURED && m.id == 42);
    char bad[sizeof(m) + 1] = {0};
    memcpy(bad, &m, sizeof(m));
    assert(send(fds[0], bad, sizeof(bad), 0) == sizeof(bad));
    assert(ad_receive(fds[1], &m) < 0 && errno == EPROTO);
    m.magic = AD_MAGIC;
    memset(m.name, 'x', sizeof(m.name));
    assert(send(fds[0], &m, sizeof(m), 0) == sizeof(m));
    assert(ad_receive(fds[1], &m) < 0 && errno == EPROTO);
    close(fds[0]);
    assert(ad_receive(fds[1], &m) < 0 && errno == ECONNRESET);
    close(fds[1]);
}
int main(void) {
    chords();
    protocol();
    puts("PASS: chord boundaries/replay/repeats/readiness and IPC framing/credentials/path "
         "validation");
}
