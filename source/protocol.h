#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <errno.h>

#define AD_SOCKET "cyberspacevii.artfuldodger.v4"
#define AD_SPOOL "/data/system/artfuldodger_spool"
#define AD_MAGIC 0x41445634U
enum { AD_READY = 1, AD_CAPTURE, AD_CAPTURED, AD_SAVED, AD_FAILED };
enum {
    AD_OK = 0,
    AD_INVALID = 20,
    AD_USER = 21,
    AD_STORAGE = 22,
    AD_SPOOL_FULL = 23,
    AD_CAPTURE_ERROR = 24,
    AD_ENCODE = 25,
    AD_FILESYSTEM = 26,
    AD_INDEX = 27,
    AD_CONFLICT = 28,
    AD_TIMEOUT = 29,
    AD_UNAVAILABLE = 30
};
static inline const char *ad_error_text(unsigned error) {
    switch (error) {
    case AD_OK:
        return "No error";
    case AD_USER:
        return "Unsupported user: screenshots support user 0";
    case AD_STORAGE:
        return "Unlock storage before taking a screenshot";
    case AD_SPOOL_FULL:
        return "Pending screenshots full: run control.sh retry";
    case AD_CAPTURE_ERROR:
        return "Screen capture failed";
    case AD_ENCODE:
        return "Could not encode screenshot";
    case AD_FILESYSTEM:
        return "Could not write screenshot: check free space";
    case AD_INDEX:
        return "Image retained: gallery indexing failed";
    case AD_CONFLICT:
        return "Image retained: destination already exists";
    case AD_TIMEOUT:
        return "Screenshot operation timed out";
    case AD_UNAVAILABLE:
        return "Capture service unavailable";
    case AD_INVALID:
        return "Invalid pending screenshot";
    default:
        return "Screenshot failed";
    }
}
typedef struct {
    uint32_t magic;
    uint32_t kind;
    uint32_t error;
    uint32_t reserved;
    uint64_t id;
    char name[96];
} AdMessage;

static inline socklen_t ad_address(struct sockaddr_un *addr) {
    memset(addr, 0, sizeof(*addr));
    addr->sun_family = AF_UNIX;
    memcpy(addr->sun_path + 1, AD_SOCKET, sizeof(AD_SOCKET) - 1);
    return (socklen_t)(offsetof(struct sockaddr_un, sun_path) + sizeof(AD_SOCKET));
}
static inline int ad_send_result(int fd, uint32_t kind, uint64_t id, const char *name,
                                 uint32_t error) {
    AdMessage m;
    memset(&m, 0, sizeof(m));
    m.magic = AD_MAGIC;
    m.kind = kind;
    m.id = id;
    m.error = error;
    if (name) {
        if (strlen(name) >= sizeof(m.name)) return -1;
        memcpy(m.name, name, strlen(name));
    }
    return send(fd, &m, sizeof(m), MSG_NOSIGNAL) == (ssize_t)sizeof(m) ? 0 : -1;
}
static inline int ad_send(int fd, uint32_t kind, uint64_t id, const char *name) {
    return ad_send_result(fd, kind, id, name, AD_OK);
}
static inline int ad_receive(int fd, AdMessage *m) {
    // MSG_TRUNC distinguishes an oversized packet from a valid prefix.
    ssize_t n = recv(fd, m, sizeof(*m), MSG_TRUNC);
    if (n < 0) return -1;
    if (n == (ssize_t)sizeof(*m) && m->magic == AD_MAGIC && memchr(m->name, 0, sizeof(m->name)))
        return 0;
    errno = n ? EPROTO : ECONNRESET;
    return -1;
}
static inline int ad_peer_is(int fd, uid_t uid) {
    struct ucred peer;
    socklen_t len = sizeof(peer);
    return getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &peer, &len) == 0 && len == sizeof(peer) &&
           peer.uid == uid;
}
static inline int ad_name_valid(const char *name) {
    size_t n = strlen(name);
    if (n < 9 || n >= 96 || strncmp(name, "cap_", 4) || strcmp(name + n - 4, ".png")) return 0;
    for (size_t i = 4; i < n - 4; ++i)
        if ((name[i] < '0' || name[i] > '9') && name[i] != '_') return 0;
    return 1;
}
