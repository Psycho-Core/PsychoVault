package org.chromium.chrome.browser.psychovault;

import org.chromium.base.ThreadUtils;
import org.chromium.chrome.browser.profiles.Profile;
import org.jni_zero.CalledByNative;
import org.jni_zero.NativeMethods;
import org.json.JSONArray;
import org.json.JSONObject;

/** Profile-scoped view of Chromium's real download manager. GUIDs, not renderer paths, identify files. */
final class PsychoDownloads {
    private final Profile mProfile;
    private long mNative;
    private Runnable mListener = () -> {};
    private JSONArray mLast = new JSONArray();
    private long mLastAt;

    PsychoDownloads(Profile profile, Runnable listener) {
        ThreadUtils.assertOnUiThread();
        if (profile == null) throw new IllegalArgumentException("Profile unavailable");
        mProfile = profile;
        mListener = listener == null ? mListener : listener;
        mNative = PsychoDownloadsJni.get().attach(profile, this);
        if (mNative == 0) throw new IllegalStateException("Download manager unavailable");
    }

    JSONArray list() throws Exception {
        ThreadUtils.assertOnUiThread();
        if (mNative == 0) return mLast;
        String json = PsychoDownloadsJni.get().list(mNative);
        JSONArray items = new JSONArray(json == null ? "[]" : json);
        long now = System.currentTimeMillis();
        long elapsed = Math.max(1, now - mLastAt);
        for (int i = 0; i < items.length(); i++) {
            JSONObject item = items.getJSONObject(i);
            long previous = 0;
            for (int j = 0; j < mLast.length(); j++) {
                JSONObject old = mLast.getJSONObject(j);
                if (item.optString("id").equals(old.optString("id"))) { previous = old.optLong("received"); break; }
            }
            long received = item.optLong("received");
            item.put("speed", "progress".equals(item.optString("state")) && mLastAt > 0
                    ? Math.max(0, (received - previous) * 1000 / elapsed) : 0);
        }
        mLast = items; mLastAt = now;
        return items;
    }

    JSONObject control(String guid, String action) throws Exception {
        requireGuid(guid);
        if (mNative == 0) throw new IllegalStateException("Download manager is closed");
        boolean ok = PsychoDownloadsJni.get().control(mNative, guid, action == null ? "" : action);
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
        if (mNative != 0) PsychoDownloadsJni.get().detach(mNative);
        mNative = 0; mListener = () -> {};
    }

    static org.json.JSONObject directory(android.content.Context context) throws Exception {
        return new JSONObject().put("ok", true).put("dir", android.os.Environment.DIRECTORY_DOWNLOADS).put("systemManaged", true);
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
