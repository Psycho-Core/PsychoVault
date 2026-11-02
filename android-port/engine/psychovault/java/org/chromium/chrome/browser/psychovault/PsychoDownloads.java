package org.chromium.chrome.browser.psychovault;

import android.app.Activity;
import android.app.AlertDialog;
import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;

import org.chromium.base.Callback;
import org.chromium.base.ThreadUtils;
import org.chromium.chrome.browser.download.DirectoryOption;
import org.chromium.chrome.browser.download.DownloadDirectoryProvider;
import org.chromium.chrome.browser.profiles.Profile;
import org.jni_zero.CalledByNative;
import org.jni_zero.NativeMethods;
import org.json.JSONArray;
import org.json.JSONObject;

import java.io.File;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;

/** Profile-scoped view of Chromium's real download manager. GUIDs, not renderer paths, identify files. */
final class PsychoDownloads {
    private static final String CHANNEL = "psychovault-downloads";
    private final Profile mProfile;
    private final Context mContext;
    private final Handler mHandler = new Handler(Looper.getMainLooper());
    private long mNative;
    private Runnable mListener = () -> {};
    private JSONArray mLast = new JSONArray();
    private long mLastAt;
    private int mCapKB;
    private boolean mNotify = true, mBackground;
    private final Map<String, Throttle> mThrottle = new HashMap<>();
    private final Map<String, String> mStates = new HashMap<>();
    private int mNotificationId = 4000;

    private static final class Throttle {
        long startAt = SystemClock.elapsedRealtime();
        long stalledMs;
        long pauseUntil;
        boolean pausedByUs;
    }

    PsychoDownloads(Profile profile, Runnable listener, Context context) {
        ThreadUtils.assertOnUiThread();
        if (profile == null) throw new IllegalArgumentException("Profile unavailable");
        mProfile = profile;
        mContext = context.getApplicationContext();
        mListener = listener == null ? mListener : listener;
        mNative = PsychoDownloadsJni.get().attach(profile, this);
        if (mNative == 0) throw new IllegalStateException("Download manager unavailable");
    }

    void configure(JSONObject settings) {
        mCapKB = Math.max(0, settings.optInt("netLimit", 0));
        mNotify = settings.optBoolean("downloadNotify", true);
        if (mCapKB == 0) resumeAllThrottled();
    }

    void background(boolean background) { mBackground = background; }

    JSONArray list() throws Exception {
        ThreadUtils.assertOnUiThread();
        if (mNative == 0) return mLast;
        String json = PsychoDownloadsJni.get().list(mNative);
        JSONArray items = new JSONArray(json == null ? "[]" : json);
        long now = System.currentTimeMillis();
        long elapsed = Math.max(1, now - mLastAt);
        Set<String> live = new HashSet<>();
        for (int i = 0; i < items.length(); i++) {
            JSONObject item = items.getJSONObject(i);
            String id = item.optString("id");
            live.add(id);
            long previous = 0;
            for (int j = 0; j < mLast.length(); j++) {
                JSONObject old = mLast.getJSONObject(j);
                if (id.equals(old.optString("id"))) { previous = old.optLong("received"); break; }
            }
            long received = item.optLong("received");
            String state = item.optString("state");
            item.put("speed", "progress".equals(state) && mLastAt > 0
                    ? Math.max(0, (received - previous) * 1000 / elapsed) : 0);
            applyThrottle(id, state, received);
            notifyTransition(id, state, item.optString("name"));
        }
        mThrottle.keySet().retainAll(live);
        mStates.keySet().retainAll(live);
        mLast = items; mLastAt = now;
        return items;
    }

    private void applyThrottle(String guid, String state, long received) {
        if (mCapKB <= 0 || mNative == 0) return;
        Throttle t = mThrottle.get(guid);
        long now = SystemClock.elapsedRealtime();
        if ("paused".equals(state)) {
            if (t != null && t.pausedByUs && now >= t.pauseUntil) {
                try { PsychoDownloadsJni.get().control(mNative, guid, "resume"); } catch (Exception ignored) { }
                t.stalledMs += Math.max(0, now - (t.pauseUntil - 40));
                t.pausedByUs = false;
            }
            return;
        }
        if (!"progress".equals(state)) { mThrottle.remove(guid); return; }
        if (t == null) { t = new Throttle(); mThrottle.put(guid, t); }
        if (t.pausedByUs) t.pausedByUs = false;
        long capBytes = mCapKB * 1024L;
        long busy = Math.max(0, now - t.startAt - t.stalledMs);
        long allowed = capBytes == 0 ? 0 : received * 1000L / capBytes;
        long ahead = allowed - busy;
        if (ahead > 40) {
            long pause = Math.min(ahead, 15000);
            try {
                if (PsychoDownloadsJni.get().control(mNative, guid, "pause")) {
                    t.pausedByUs = true;
                    t.pauseUntil = now + pause + 40;
                    final String id = guid;
                    mHandler.postDelayed(() -> {
                        try { if (mNative != 0) { PsychoDownloadsJni.get().control(mNative, id, "resume"); mListener.run(); } } catch (Exception ignored) { }
                    }, pause + 40);
                }
            } catch (Exception ignored) { }
        }
    }

    private void resumeAllThrottled() {
        for (Map.Entry<String, Throttle> entry : new ArrayList<>(mThrottle.entrySet())) {
            if (entry.getValue().pausedByUs && mNative != 0) {
                try { PsychoDownloadsJni.get().control(mNative, entry.getKey(), "resume"); } catch (Exception ignored) { }
            }
        }
        mThrottle.clear();
    }

    private void notifyTransition(String guid, String state, String name) {
        String previous = mStates.put(guid, state);
        if (!mNotify || !mBackground || previous == null || previous.equals(state)) return;
        boolean done = "complete".equals(state) || "completed".equals(state);
        boolean failed = "interrupted".equals(state) || "cancelled".equals(state) || "canceled".equals(state);
        if (!done && !failed) return;
        try {
            NotificationManager manager = (NotificationManager) mContext.getSystemService(Context.NOTIFICATION_SERVICE);
            if (manager == null) return;
            if (Build.VERSION.SDK_INT >= 33 && mContext.checkSelfPermission(android.Manifest.permission.POST_NOTIFICATIONS) != android.content.pm.PackageManager.PERMISSION_GRANTED) return;
            if (Build.VERSION.SDK_INT >= 26) {
                NotificationChannel channel = new NotificationChannel(CHANNEL, "Downloads", NotificationManager.IMPORTANCE_LOW);
                manager.createNotificationChannel(channel);
            }
            Intent intent = mContext.getPackageManager().getLaunchIntentForPackage(mContext.getPackageName());
            PendingIntent pending = intent == null ? null : PendingIntent.getActivity(mContext, 0, intent, PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
            Notification.Builder builder = Build.VERSION.SDK_INT >= 26 ? new Notification.Builder(mContext, CHANNEL) : new Notification.Builder(mContext);
            builder.setSmallIcon(android.R.drawable.stat_sys_download_done)
                    .setContentTitle(done ? "Download complete" : "Download failed")
                    .setContentText(name == null || name.isEmpty() ? guid : name)
                    .setAutoCancel(true);
            if (pending != null) builder.setContentIntent(pending);
            manager.notify(++mNotificationId, builder.build());
        } catch (Exception ignored) { /* Notifications are best-effort and never block a download. */ }
    }

    JSONObject control(String guid, String action) throws Exception {
        requireGuid(guid);
        if (mNative == 0) throw new IllegalStateException("Download manager is closed");
        boolean ok = PsychoDownloadsJni.get().control(mNative, guid, action == null ? "" : action);
        if (ok && ("pause".equals(action) || "cancel".equals(action))) mThrottle.remove(guid);
        return new JSONObject().put("ok", ok).put("error", ok ? "" : "That action is not available for this download.");
    }

    boolean open(String guid) {
        requireGuid(guid);
        return mNative != 0 && PsychoDownloadsJni.get().open(mNative, guid);
    }

    JSONArray clear() throws Exception {
        if (mNative != 0) PsychoDownloadsJni.get().clear(mNative);
        return list();
    }

    void destroy() {
        ThreadUtils.assertOnUiThread();
        mHandler.removeCallbacksAndMessages(null);
        if (mNative != 0) PsychoDownloadsJni.get().detach(mNative);
        mNative = 0; mListener = () -> {};
    }

    static JSONObject directory(Context context, JSONObject settings) throws Exception {
        String chosen = settings == null ? "" : settings.optString("downloadDir", "");
        File primary = DownloadDirectoryProvider.getPrimaryDownloadDirectory();
        String dir = !chosen.isEmpty() ? chosen : primary == null ? android.os.Environment.DIRECTORY_DOWNLOADS : primary.getAbsolutePath();
        return new JSONObject().put("ok", true).put("dir", dir).put("systemManaged", chosen.isEmpty());
    }

    static void pickDirectory(Activity activity, Callback<String> callback) {
        DownloadDirectoryProvider.getInstance().getAllDirectoriesOptions(options -> {
            ArrayList<DirectoryOption> valid = new ArrayList<>();
            for (DirectoryOption option : options) if (option != null && option.location != null && !option.location.isEmpty()) valid.add(option);
            if (valid.isEmpty()) { callback.onResult(null); return; }
            String[] labels = new String[valid.size()];
            for (int i = 0; i < valid.size(); i++) {
                DirectoryOption option = valid.get(i);
                String name = option.name == null || option.name.isEmpty() ? option.location : option.name;
                labels[i] = name + " (" + android.text.format.Formatter.formatFileSize(activity, option.availableSpace) + " free)";
            }
            new AlertDialog.Builder(activity)
                    .setTitle("Download folder")
                    .setItems(labels, (dialog, which) -> callback.onResult(valid.get(which).location))
                    .setNegativeButton(android.R.string.cancel, (dialog, which) -> callback.onResult(null))
                    .show();
        });
    }

    static void requireGuid(String guid) {
        if (guid == null || !guid.matches("[0-9a-fA-F-]{36}")) throw new IllegalArgumentException("Invalid download ID");
    }

    @CalledByNative
    private void onNativeChanged() {
        ThreadUtils.assertOnUiThread();
        if (mNative != 0) mListener.run();
    }

    @NativeMethods
    interface Natives {
        long attach(Profile profile, PsychoDownloads owner);
        String list(long pointer);
        boolean control(long pointer, String guid, String action);
        boolean open(long pointer, String guid);
        void clear(long pointer);
        void detach(long pointer);
    }
}
