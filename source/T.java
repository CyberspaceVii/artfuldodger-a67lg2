package com.wl;

import android.content.Context;
import android.os.Handler;
import android.os.Looper;
import android.widget.Toast;

// Public APIs only; the system Context is passed in from native.
public class T {
    public static void toast(final Context ctx, final String msg) {
        new Handler(Looper.getMainLooper()).post(new Runnable() {
            public void run() {
                try { Toast.makeText(ctx, msg, Toast.LENGTH_SHORT).show(); } catch (Throwable t) {}
            }
        });
    }
}
