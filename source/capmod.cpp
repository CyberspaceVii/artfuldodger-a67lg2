// Capture secure layers from a worker in system_server.
#include <jni.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <initializer_list>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <android/log.h>
#include "zygisk.hpp"
#include "toastdex.h"
#include "protocol.h"

#define WARN(...) __android_log_print(ANDROID_LOG_WARN, "ArtfulDodger", __VA_ARGS__)
static JavaVM *vm;
static bool exception(JNIEnv *e) {
    if (!e->ExceptionCheck()) return false;
    e->ExceptionClear();
    return true;
}
struct Frame {
    JNIEnv *e;
    bool valid;
    explicit Frame(JNIEnv *env) : e(env), valid(e->PushLocalFrame(96) == 0) {}
    ~Frame() {
        if (valid)
            e->PopLocalFrame(nullptr);
        else
            exception(e);
    }
};
struct Api {
    jclass dc{}, builder{}, capture{}, listener{}, buffer{}, bitmap{}, format{}, file{}, activity{},
        user{};
    jobject png{}, context{};
    jclass toast{};
    jmethodID ids{}, token{}, ctor{}, secure{}, build{}, sync{}, cap{}, getBuffer{}, asBitmap{},
        hardware{}, closeBuffer{}, recycle{}, compress{}, fileCtor{}, close{}, currentUser{},
        unlocked{}, toastMethod{};
    ~Api() = default;
    void clear(JNIEnv *e) {
        for (jobject ref : {(jobject)dc, (jobject)builder, (jobject)capture, (jobject)listener,
                            (jobject)buffer, (jobject)bitmap, (jobject)format, (jobject)file,
                            (jobject)activity, (jobject)user, png, context, (jobject)toast})
            if (ref) e->DeleteGlobalRef(ref);
    }
};
static jclass globalClass(JNIEnv *e, const char *name) {
    if (exception(e)) return nullptr;
    jclass c = e->FindClass(name);
    if (exception(e) || !c) return nullptr;
    jclass result = (jclass)e->NewGlobalRef(c);
    return exception(e) ? nullptr : result;
}
static bool initialize(JNIEnv *e, Api &a) {
    Frame frame(e);
    if (!frame.valid) return false;
    jclass sm = e->FindClass("android/os/ServiceManager");
    if (exception(e) || !sm) return false;
    auto service =
        e->GetStaticMethodID(sm, "getService", "(Ljava/lang/String;)Landroid/os/IBinder;");
    jclass cc = e->FindClass("java/lang/Class");
    if (exception(e) || !service || !cc) return false;
    auto getLoader = e->GetMethodID(cc, "getClassLoader", "()Ljava/lang/ClassLoader;");
    if (exception(e) || !getLoader) return false;
    jobject binder = e->CallStaticObjectMethod(sm, service, e->NewStringUTF("activity"));
    if (exception(e) || !binder) return false;
    jobject loader = e->CallObjectMethod(e->GetObjectClass(binder), getLoader);
    if (exception(e) || !loader) return false;
    jclass cl = e->FindClass("java/lang/ClassLoader");
    if (exception(e) || !cl) return false;
    auto load = e->GetMethodID(cl, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    if (exception(e) || !load) return false;
    jobject dc = e->CallObjectMethod(loader, load,
                                     e->NewStringUTF("com.android.server.display.DisplayControl"));
    if (exception(e) || !dc) return false;
    a.dc = (jclass)e->NewGlobalRef(dc);
    a.builder = globalClass(e, "android/window/ScreenCapture$DisplayCaptureArgs$Builder");
    a.capture = globalClass(e, "android/window/ScreenCapture");
    a.listener = globalClass(e, "android/window/ScreenCapture$SynchronousScreenCaptureListener");
    a.buffer = globalClass(e, "android/window/ScreenCapture$ScreenshotHardwareBuffer");
    a.bitmap = globalClass(e, "android/graphics/Bitmap");
    a.format = globalClass(e, "android/graphics/Bitmap$CompressFormat");
    a.file = globalClass(e, "java/io/FileOutputStream");
    a.activity = globalClass(e, "android/app/ActivityManager");
    a.user = globalClass(e, "android/os/UserManager");
    if (!a.dc || !a.builder || !a.capture || !a.listener || !a.buffer || !a.bitmap || !a.format ||
        !a.file || !a.activity || !a.user || exception(e))
        return false;
#define METHOD(dst, cls, name, sig)                                                                \
    a.dst = e->GetMethodID(a.cls, name, sig);                                                      \
    if (exception(e) || !a.dst) {                                                                  \
        WARN("Missing %s", name);                                                                  \
        return false;                                                                              \
    }
#define STATIC(dst, cls, name, sig)                                                                \
    a.dst = e->GetStaticMethodID(a.cls, name, sig);                                                \
    if (exception(e) || !a.dst) {                                                                  \
        WARN("Missing %s", name);                                                                  \
        return false;                                                                              \
    }
    STATIC(ids, dc, "getPhysicalDisplayIds", "()[J")
    STATIC(token, dc, "getPhysicalDisplayToken", "(J)Landroid/os/IBinder;")
    METHOD(ctor, builder, "<init>", "(Landroid/os/IBinder;)V")
    a.secure = e->GetMethodID(a.builder, "setCaptureSecureLayers",
                              "(Z)Landroid/window/ScreenCapture$CaptureArgs$Builder;");
    if (exception(e) || !a.secure)
        a.secure = e->GetMethodID(a.builder, "setCaptureSecureLayers",
                                  "(Z)Landroid/window/ScreenCapture$DisplayCaptureArgs$Builder;");
    if (exception(e) || !a.secure) return false;
    METHOD(build, builder, "build", "()Landroid/window/ScreenCapture$DisplayCaptureArgs;")
    STATIC(sync, capture, "createSyncCaptureListener",
           "()Landroid/window/ScreenCapture$SynchronousScreenCaptureListener;")
    STATIC(cap, capture, "captureDisplay",
           "(Landroid/window/ScreenCapture$DisplayCaptureArgs;Landroid/window/"
           "ScreenCapture$ScreenCaptureListener;)I")
    METHOD(getBuffer, listener, "getBuffer",
           "()Landroid/window/ScreenCapture$ScreenshotHardwareBuffer;")
    METHOD(asBitmap, buffer, "asBitmap", "()Landroid/graphics/Bitmap;")
    METHOD(hardware, buffer, "getHardwareBuffer", "()Landroid/hardware/HardwareBuffer;")
    jclass hb = e->FindClass("android/hardware/HardwareBuffer");
    if (exception(e) || !hb) return false;
    a.closeBuffer = e->GetMethodID(hb, "close", "()V");
    if (exception(e) || !a.closeBuffer) return false;
    METHOD(recycle, bitmap, "recycle", "()V")
    METHOD(compress, bitmap, "compress",
           "(Landroid/graphics/Bitmap$CompressFormat;ILjava/io/OutputStream;)Z")
    METHOD(fileCtor, file, "<init>", "(Ljava/lang/String;)V")
    METHOD(close, file, "close", "()V")
    STATIC(currentUser, activity, "getCurrentUser", "()I")
    METHOD(unlocked, user, "isUserUnlocked", "()Z")
#undef STATIC
#undef METHOD
    auto png = e->GetStaticFieldID(a.format, "PNG", "Landroid/graphics/Bitmap$CompressFormat;");
    if (exception(e) || !png) return false;
    a.png = e->NewGlobalRef(e->GetStaticObjectField(a.format, png));
    if (exception(e) || !a.png) return false;
    jclass at = e->FindClass("android/app/ActivityThread");
    if (exception(e) || !at) return false;
    auto current =
        e->GetStaticMethodID(at, "currentActivityThread", "()Landroid/app/ActivityThread;");
    if (exception(e) || !current) return false;
    auto system = e->GetMethodID(at, "getSystemContext", "()Landroid/app/ContextImpl;");
    if (exception(e) || !system) return false;
    jobject thread = e->CallStaticObjectMethod(at, current);
    if (exception(e) || !thread) return false;
    jobject ctx = e->CallObjectMethod(thread, system);
    if (exception(e) || !ctx) return false;
    a.context = e->NewGlobalRef(ctx);
    // Resolve the toast helper before accepting capture requests.
    jobject bytes = e->NewDirectByteBuffer(kToastDex, kToastDex_len);
    jclass im = e->FindClass("dalvik/system/InMemoryDexClassLoader");
    if (exception(e) || !im || !bytes) return false;
    auto imCtor = e->GetMethodID(im, "<init>", "(Ljava/nio/ByteBuffer;Ljava/lang/ClassLoader;)V");
    if (exception(e) || !imCtor) return false;
    jobject tl = e->NewObject(im, imCtor, bytes, loader);
    if (exception(e) || !tl) return false;
    jobject toast = e->CallObjectMethod(tl, load, e->NewStringUTF("com.wl.T"));
    if (exception(e) || !toast) return false;
    a.toast = (jclass)e->NewGlobalRef(toast);
    a.toastMethod =
        e->GetStaticMethodID(a.toast, "toast", "(Landroid/content/Context;Ljava/lang/String;)V");
    return !exception(e) && a.context && a.toastMethod;
}
static void toast(JNIEnv *e, Api &a, const char *text) {
    Frame frame(e);
    if (!frame.valid) return;
    e->CallStaticVoidMethod(a.toast, a.toastMethod, a.context, e->NewStringUTF(text));
    exception(e);
}
static unsigned userError(JNIEnv *e, Api &a) {
    jint user = e->CallStaticIntMethod(a.activity, a.currentUser);
    if (exception(e)) return AD_CAPTURE_ERROR;
    if (user != 0) return AD_USER;
    jclass ctx = e->GetObjectClass(a.context);
    auto getService =
        e->GetMethodID(ctx, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
    if (exception(e) || !getService) return AD_CAPTURE_ERROR;
    jobject manager = e->CallObjectMethod(a.context, getService, e->NewStringUTF("user"));
    if (exception(e) || !manager) return AD_STORAGE;
    bool unlocked = e->CallBooleanMethod(manager, a.unlocked);
    return !exception(e) && unlocked ? AD_OK : AD_STORAGE;
}
static unsigned capture(JNIEnv *e, Api &a, char name[96]) {
    Frame frame(e);
    if (!frame.valid) return AD_CAPTURE_ERROR;
    unsigned error = userError(e, a);
    if (error) return error;
    error = AD_CAPTURE_ERROR;
    jobject bitmap = nullptr, hardware = nullptr, stream = nullptr;
    char path[256] = {}, temp[256] = {};
    bool ok = false;
    do {
        auto ids = (jlongArray)e->CallStaticObjectMethod(a.dc, a.ids);
        if (exception(e) || !ids || e->GetArrayLength(ids) < 1) break;
        jlong id;
        e->GetLongArrayRegion(ids, 0, 1, &id);
        if (exception(e)) break;
        jobject token = e->CallStaticObjectMethod(a.dc, a.token, id);
        if (exception(e) || !token) break;
        jobject builder = e->NewObject(a.builder, a.ctor, token);
        if (exception(e) || !builder) break;
        e->CallObjectMethod(builder, a.secure, JNI_TRUE);
        if (exception(e)) break;
        jobject args = e->CallObjectMethod(builder, a.build);
        if (exception(e) || !args) break;
        jobject listener = e->CallStaticObjectMethod(a.capture, a.sync);
        if (exception(e) || !listener) break;
        jint status = e->CallStaticIntMethod(a.capture, a.cap, args, listener);
        if (exception(e) || status != 0) break;
        jobject buffer = e->CallObjectMethod(listener, a.getBuffer);
        if (exception(e) || !buffer) break;
        hardware = e->CallObjectMethod(buffer, a.hardware);
        if (exception(e) || !hardware) break;
        bitmap = e->CallObjectMethod(buffer, a.asBitmap);
        if (exception(e) || !bitmap) break;
        error = AD_FILESYSTEM;
        if (mkdir(AD_SPOOL, 0700) < 0 && errno != EEXIST) break;
        struct stat st;
        if (lstat(AD_SPOOL, &st) || !S_ISDIR(st.st_mode) || st.st_uid != getuid() ||
            (st.st_mode & 077))
            break;
        DIR *directory = opendir(AD_SPOOL);
        if (!directory) break;
        unsigned count = 0;
        struct dirent *entry;
        while ((entry = readdir(directory)))
            if (ad_name_valid(entry->d_name)) ++count;
        closedir(directory);
        if (count >= 32) {
            WARN("Spool full; preserve failed captures for recovery");
            error = AD_SPOOL_FULL;
            break;
        }
        // Write to a private temp file, then publish the completed PNG.
        snprintf(temp, sizeof(temp), AD_SPOOL "/cap_XXXXXX");
        int fd = mkstemp(temp);
        if (fd < 0) break;
        close(fd);
        // The wire format accepts only numeric final names.
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        static unsigned long serial = 0;
        snprintf(name, 96, "cap_%ld_%ld_%lu.png", (long)ts.tv_sec, ts.tv_nsec, ++serial);
        snprintf(path, sizeof(path), AD_SPOOL "/%s", name);
        stream = e->NewObject(a.file, a.fileCtor, e->NewStringUTF(temp));
        if (exception(e) || !stream) break;
        error = AD_ENCODE;
        bool compressed = e->CallBooleanMethod(bitmap, a.compress, a.png, 100, stream);
        if (exception(e) || !compressed) break;
        e->CallVoidMethod(stream, a.close);
        stream = nullptr;
        if (exception(e)) break;
        error = userError(e, a);
        if (error) break;
        error = AD_FILESYSTEM;
        // Fail on a name collision instead of replacing an existing capture.
        if (link(temp, path) != 0) break;
        ok = true;
    } while (false);
    if (stream) {
        e->CallVoidMethod(stream, a.close);
        exception(e);
    }
    if (bitmap) {
        e->CallVoidMethod(bitmap, a.recycle);
        exception(e);
    }
    if (hardware) {
        e->CallVoidMethod(hardware, a.closeBuffer);
        exception(e);
    }
    if (*temp) unlink(temp);
    return ok ? AD_OK : error;
}
static void *serve(void *) {
    JNIEnv *e = nullptr;
    if (vm->AttachCurrentThread(&e, nullptr) != JNI_OK) return nullptr;
    Api api;
    bool ready = false;
    for (int i = 0; i < 60 && !ready; ++i) {
        ready = initialize(e, api);
        if (!ready) {
            api.clear(e);
            api = Api{};
            sleep(2);
        }
    }
    if (!ready) {
        WARN("Capture API unavailable; interceptor stays off");
        vm->DetachCurrentThread();
        return nullptr;
    }
    int server = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    struct sockaddr_un address;
    socklen_t len = ad_address(&address);
    if (server < 0 || bind(server, (sockaddr *)&address, len) || listen(server, 1)) {
        WARN("Capture socket unavailable: %s", strerror(errno));
        if (server >= 0) close(server);
        api.clear(e);
        vm->DetachCurrentThread();
        return nullptr;
    }
    for (;;) {
        int client = accept4(server, nullptr, nullptr, SOCK_CLOEXEC);
        if (client < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (!ad_peer_is(client, 0)) {
            close(client);
            continue;
        }
        struct timeval timeout = {20, 0};
        setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        if (ad_send(client, AD_READY, 0, nullptr)) {
            close(client);
            continue;
        }
        for (;;) {
            AdMessage request;
            if (ad_receive(client, &request)) {
                if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) continue;
                break;
            }
            if (request.kind != AD_CAPTURE || !request.id) break;
            char name[96] = {};
            unsigned error = capture(e, api, name);
            if (error) {
                WARN("Capture request failed: %u", error);
                toast(e, api, ad_error_text(error));
                if (ad_send_result(client, AD_FAILED, request.id, nullptr, error)) break;
                continue;
            }
            if (ad_send(client, AD_CAPTURED, request.id, name)) break;
            AdMessage ack;
            bool received = ad_receive(client, &ack) == 0;
            bool saved = received && ack.id == request.id && ack.kind == AD_SAVED;
            toast(e, api,
                  saved ? "Screenshot saved" : ad_error_text(received ? ack.error : AD_TIMEOUT));
            if (!received || ack.id != request.id ||
                (ack.kind != AD_SAVED && ack.kind != AD_FAILED))
                break;
            if (ad_send(client, AD_READY, request.id, nullptr)) break;
        }
        close(client);
    }
    close(server);
    api.clear(e);
    vm->DetachCurrentThread();
    return nullptr;
}
class ArtfulDodger : public zygisk::ModuleBase {
    zygisk::Api *api_ = nullptr;

  public:
    void onLoad(zygisk::Api *api, JNIEnv *env) override {
        api_ = api;
        env->GetJavaVM(&vm);
    }
    void preAppSpecialize(zygisk::AppSpecializeArgs *) override {
        api_->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
    }
    void postServerSpecialize(const zygisk::ServerSpecializeArgs *) override {
        pthread_t thread;
        if (!pthread_create(&thread, nullptr, serve, nullptr)) pthread_detach(thread);
    }
};
REGISTER_ZYGISK_MODULE(ArtfulDodger)
