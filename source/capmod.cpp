// The Artful Dodger capture half: runs inside system_server via Zygisk, waits on the
// daemon's trigger property, and grabs the screen WITH secure layers so
// FLAG_SECURE windows image instead of blanking. This only works from
// system_server (DisplayControl's natives live here, and it holds
// CAPTURE_BLACKOUT_CONTENT). PNG goes to a spool for service.sh to file; then a toast.

#include <jni.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/system_properties.h>
#include <android/log.h>
#include "zygisk.hpp"
#include "toastdex.h"

#define TRIG     "debug.cyberspacevii.artfuldodger"
#define SPOOL    "/data/system/artfuldodger_spool"
#define TOASTMSG "Screenshot captured"
#define WARN(...) __android_log_print(ANDROID_LOG_WARN, "ArtfulDodger", __VA_ARGS__)

static JavaVM *gVm = nullptr;
static jclass gToastCls = nullptr;
static jmethodID gToastMid = nullptr;

static bool exc(JNIEnv *e) { if (e->ExceptionCheck()) { e->ExceptionClear(); return true; } return false; }

// system_server's own classloader (has DisplayControl's registered natives)
static jobject serverClassLoader(JNIEnv *e) {
    jclass sm = e->FindClass("android/os/ServiceManager");
    if (!sm) { e->ExceptionClear(); return nullptr; }
    jmethodID gs = e->GetStaticMethodID(sm, "getService", "(Ljava/lang/String;)Landroid/os/IBinder;");
    if (!gs) { e->ExceptionClear(); return nullptr; }
    const char *svcs[] = {"activity", "package", "window", "display", nullptr};
    jclass classCls = e->FindClass("java/lang/Class");
    jmethodID gccl = e->GetMethodID(classCls, "getClassLoader", "()Ljava/lang/ClassLoader;");
    for (int i = 0; svcs[i]; i++) {
        jstring n = e->NewStringUTF(svcs[i]);
        jobject b = e->CallStaticObjectMethod(sm, gs, n);
        e->DeleteLocalRef(n);
        if (exc(e) || !b) continue;
        jobject cl = e->CallObjectMethod(e->GetObjectClass(b), gccl);
        if (exc(e)) continue;
        if (cl) return cl;
    }
    return nullptr;
}

static jclass loadVia(JNIEnv *e, jobject loader, const char *dotName) {
    jclass clc = e->FindClass("java/lang/ClassLoader");
    jmethodID lm = e->GetMethodID(clc, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    jstring n = e->NewStringUTF(dotName);
    jobject c = e->CallObjectMethod(loader, lm, n);
    e->DeleteLocalRef(n);
    if (exc(e)) return nullptr;
    return (jclass) c;
}

static void showToast(JNIEnv *e, jobject scl) {
    if (!gToastCls) {
        jobject bb = e->NewDirectByteBuffer((void *) kToastDex, kToastDex_len);
        jclass imdcl = e->FindClass("dalvik/system/InMemoryDexClassLoader");
        jmethodID imc = e->GetMethodID(imdcl, "<init>", "(Ljava/nio/ByteBuffer;Ljava/lang/ClassLoader;)V");
        jobject loader = e->NewObject(imdcl, imc, bb, scl);
        if (exc(e)) return;
        jclass t = loadVia(e, loader, "com.wl.T");
        if (!t) return;
        gToastMid = e->GetStaticMethodID(t, "toast", "(Landroid/content/Context;Ljava/lang/String;)V");
        if (!gToastMid) { e->ExceptionClear(); return; }
        gToastCls = (jclass) e->NewGlobalRef(t);
    }
    // system Context via ActivityThread (JNI, so no hidden-api gate)
    jclass at = e->FindClass("android/app/ActivityThread");
    jmethodID cur = e->GetStaticMethodID(at, "currentActivityThread", "()Landroid/app/ActivityThread;");
    jobject ath = e->CallStaticObjectMethod(at, cur);
    if (exc(e) || !ath) return;
    jmethodID gsc = e->GetMethodID(at, "getSystemContext", "()Landroid/app/ContextImpl;");
    jobject ctx = e->CallObjectMethod(ath, gsc);
    if (exc(e) || !ctx) return;
    jstring msg = e->NewStringUTF(TOASTMSG);
    e->CallStaticVoidMethod(gToastCls, gToastMid, ctx, msg);
    exc(e);
}

static void doCapture(JNIEnv *e) {
    jobject scl = serverClassLoader(e);
    if (!scl) { WARN("no server classloader"); return; }

    jclass dc = loadVia(e, scl, "com.android.server.display.DisplayControl");
    if (!dc) { WARN("DisplayControl not loadable"); return; }

    jmethodID mIds = e->GetStaticMethodID(dc, "getPhysicalDisplayIds", "()[J");
    if (!mIds) { e->ExceptionClear(); return; }
    jlongArray ids = (jlongArray) e->CallStaticObjectMethod(dc, mIds);
    if (exc(e) || !ids || e->GetArrayLength(ids) == 0) { WARN("no display ids"); return; }
    jlong id0; e->GetLongArrayRegion(ids, 0, 1, &id0);

    jmethodID mTok = e->GetStaticMethodID(dc, "getPhysicalDisplayToken", "(J)Landroid/os/IBinder;");
    if (!mTok) { e->ExceptionClear(); return; }
    jobject token = e->CallStaticObjectMethod(dc, mTok, id0);
    if (exc(e) || !token) return;

    jclass bCls = e->FindClass("android/window/ScreenCapture$DisplayCaptureArgs$Builder");
    if (exc(e) || !bCls) return;
    jobject bld = e->NewObject(bCls, e->GetMethodID(bCls, "<init>", "(Landroid/os/IBinder;)V"), token);
    if (exc(e)) return;

    jmethodID mSec = e->GetMethodID(bCls, "setCaptureSecureLayers",
        "(Z)Landroid/window/ScreenCapture$CaptureArgs$Builder;");
    if (!mSec) { e->ExceptionClear();
        mSec = e->GetMethodID(bCls, "setCaptureSecureLayers",
            "(Z)Landroid/window/ScreenCapture$DisplayCaptureArgs$Builder;"); }
    if (!mSec) { e->ExceptionClear(); WARN("no setCaptureSecureLayers"); return; }
    e->CallObjectMethod(bld, mSec, (jboolean) true);
    exc(e);

    jobject args = e->CallObjectMethod(bld,
        e->GetMethodID(bCls, "build", "()Landroid/window/ScreenCapture$DisplayCaptureArgs;"));
    if (exc(e) || !args) return;

    jclass sc = e->FindClass("android/window/ScreenCapture");
    jobject lis = e->CallStaticObjectMethod(sc, e->GetStaticMethodID(sc, "createSyncCaptureListener",
        "()Landroid/window/ScreenCapture$SynchronousScreenCaptureListener;"));
    if (exc(e) || !lis) return;

    jmethodID mCap = e->GetStaticMethodID(sc, "captureDisplay",
        "(Landroid/window/ScreenCapture$DisplayCaptureArgs;Landroid/window/ScreenCapture$ScreenCaptureListener;)I");
    e->CallStaticIntMethod(sc, mCap, args, lis);
    if (exc(e)) return;

    jobject buf = e->CallObjectMethod(lis, e->GetMethodID(e->GetObjectClass(lis), "getBuffer",
        "()Landroid/window/ScreenCapture$ScreenshotHardwareBuffer;"));
    if (exc(e) || !buf) { WARN("null buffer"); return; }
    jobject bmp = e->CallObjectMethod(buf, e->GetMethodID(e->GetObjectClass(buf), "asBitmap",
        "()Landroid/graphics/Bitmap;"));
    if (exc(e) || !bmp) { WARN("null bitmap"); return; }

    jclass cfCls = e->FindClass("android/graphics/Bitmap$CompressFormat");
    jobject PNG = e->GetStaticObjectField(cfCls, e->GetStaticFieldID(cfCls, "PNG",
        "Landroid/graphics/Bitmap$CompressFormat;"));
    jclass fos = e->FindClass("java/io/FileOutputStream");

    mkdir(SPOOL, 0700);
    char finalp[128], writep[160];
    snprintf(finalp, sizeof(finalp), SPOOL "/cap_%ld.png", (long) time(nullptr));
    snprintf(writep, sizeof(writep), "%s.tmp", finalp);   // write .tmp then rename: no partial reads
    jstring jp = e->NewStringUTF(writep);
    jobject os = e->NewObject(fos, e->GetMethodID(fos, "<init>", "(Ljava/lang/String;)V"), jp);
    if (exc(e)) return;
    jboolean ok = e->CallBooleanMethod(bmp, e->GetMethodID(e->GetObjectClass(bmp), "compress",
        "(Landroid/graphics/Bitmap$CompressFormat;ILjava/io/OutputStream;)Z"), PNG, 100, os);
    e->CallVoidMethod(os, e->GetMethodID(fos, "close", "()V"));
    exc(e);
    if (ok) { rename(writep, finalp); showToast(e, scl); }
    else unlink(writep);
}

static void *watch(void *) {
    JNIEnv *e = nullptr;
    if (gVm->AttachCurrentThread(&e, nullptr) != 0) return nullptr;
    // daemon sets this at startup; wait until it shows up
    const prop_info *pi = __system_property_find(TRIG);
    while (!pi) { sleep(2); pi = __system_property_find(TRIG); }
    uint32_t serial = __system_property_serial(pi);
    for (;;) {
        if (!__system_property_wait(pi, serial, &serial, nullptr)) continue;  // blocks until change
        doCapture(e);
    }
    return nullptr;
}

class ArtfulDodger : public zygisk::ModuleBase {
public:
    void onLoad(zygisk::Api *api, JNIEnv *env) override {
        api_ = api;
        env->GetJavaVM(&gVm);
    }
    void preAppSpecialize(zygisk::AppSpecializeArgs *) override {
        // Capture runs only in system_server. App children need none of our
        // code, hooks or threads, so release the library after specialization.
        api_->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
    }
    void postServerSpecialize(const zygisk::ServerSpecializeArgs *) override {
        pthread_t t; if (pthread_create(&t, nullptr, watch, nullptr) == 0) pthread_detach(t);
    }
private:
    zygisk::Api *api_ = nullptr;
};

REGISTER_ZYGISK_MODULE(ArtfulDodger)
