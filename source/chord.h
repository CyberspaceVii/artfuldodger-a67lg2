#pragma once
#include <linux/input.h>
#include <string.h>

// The caller supplies monotonic time and input/capture callbacks.
#define AD_CHORD_MS 120
#define AD_HELD_MAX 64
typedef struct {
    struct input_event held[AD_HELD_MAX];
    int count, first;
    long since;
    unsigned char swallow[KEY_MAX + 1];
} AdChord;
typedef void (*AdEmit)(void *, const struct input_event *);
typedef int (*AdFire)(void *);
static inline void ad_flush(AdChord *s, AdEmit emit, void *ctx) {
    for (int i = 0; i < s->count; ++i)
        emit(ctx, &s->held[i]);
    s->count = 0;
    s->first = 0;
}
static inline void ad_expire(AdChord *s, long now, AdEmit emit, void *ctx) {
    if (s->first && now - s->since > AD_CHORD_MS) ad_flush(s, emit, ctx);
}
static inline void ad_event(AdChord *s, const struct input_event *ev, long now, int ready,
                            AdEmit emit, AdFire fire, void *ctx) {
    ad_expire(s, now, emit, ctx);
    int key = ev->type == EV_KEY && (ev->code == KEY_POWER || ev->code == KEY_VOLUMEDOWN);
    if (key && s->swallow[ev->code]) {
        if (ev->value == 0) s->swallow[ev->code] = 0;
        return; // Suppress repeats as well as releases of consumed downs.
    }
    if (!ready && s->first) ad_flush(s, emit, ctx);
    if (!s->first && ready && key && ev->value == 1) {
        s->first = ev->code;
        s->since = now;
        s->held[s->count++] = *ev;
        return;
    }
    if (s->first) {
        s->held[s->count++] = *ev;
        if (key && ev->value == 1 && ev->code != s->first && fire(ctx)) {
            // Preserve unrelated keys and packet boundaries buffered during the window.
            for (int i = 0; i < s->count; ++i) {
                const struct input_event *held = &s->held[i];
                if (held->type != EV_KEY ||
                    (held->code != KEY_POWER && held->code != KEY_VOLUMEDOWN))
                    emit(ctx, held);
            }
            s->swallow[KEY_POWER] = s->swallow[KEY_VOLUMEDOWN] = 1;
            s->count = 0;
            s->first = 0;
        } else if (!ready ||
                   (key && (ev->value == 0 || (ev->value == 1 && ev->code != s->first))) ||
                   s->count == AD_HELD_MAX) {
            // Replay buffered events if the chord ends or capture cannot start.
            ad_flush(s, emit, ctx);
        }
        return;
    }
    emit(ctx, ev);
}
