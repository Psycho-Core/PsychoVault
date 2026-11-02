package org.chromium.chrome.browser.psychovault;

import android.content.ContentValues;
import android.content.Context;
import android.net.Uri;
import android.os.Handler;
import android.os.Looper;
import android.provider.MediaStore;

import org.chromium.base.ThreadUtils;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.tabmodel.TabModel;
import org.chromium.content_public.browser.ChildProcessImportance;
import org.chromium.content_public.browser.WebContents;
import org.jni_zero.CalledByNative;
import org.jni_zero.NativeMethods;
import org.json.JSONArray;
import org.json.JSONObject;

import java.io.OutputStream;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;
import java.util.function.Consumer;

/** Real tab discard/priority/media/snapshot operations; no fabricated CPU or bandwidth numbers. */
final class PsychoPerformance {
    private final Context mContext;
    private final Handler mHandler = new Handler(Looper.getMainLooper());
    private final Map<Integer, Consumer<JSONObject>> mSnapshots = new HashMap<>();
    private int mSequence;
    private boolean mDestroyed;
    private final Runnable mSleepTask = new Runnable() {
        @Override public void run() {
            if (mDestroyed) return;
            try { sleepIdle(mModel, mSettings); } catch (Exception ignored) { }
            mHandler.postDelayed(this, 60000);
        }
    };
    private TabModel mModel;
    private JSONObject mSettings = new JSONObject();

    PsychoPerformance(Context context) { mContext = context.getApplicationContext(); mHandler.postDelayed(mSleepTask, 60000); }
    void update(TabModel model, JSONObject settings) throws Exception { mModel = model; mSettings = settings; applyPriority(model, settings); }

    JSONArray hot(TabModel model) throws Exception {
        JSONArray out = new JSONArray();
        for (int i = 0; i < model.getCount(); i++) {
            Tab tab = model.getTabAt(i); WebContents wc = tab.getWebContents();
            out.put(new JSONObject().put("id", tab.getId()).put("title", tab.getTitle()).put("url", tab.getUrl().getSpec())
                    .put("discarded", tab.isFrozen() || wc == null || wc.isDestroyed()).put("active", i == model.index())
                    .put("audible", wc != null && !wc.isDestroyed() && PsychoPerformanceJni.get().isAudible(wc))
                    .put("memMB", JSONObject.NULL).put("cpu", JSONObject.NULL));
        }
        return out;
    }

    JSONObject action(TabModel model, JSONObject request) throws Exception {
        int id = request.getInt("id"); Tab tab = model.getTabById(id);
        if (tab == null) throw new IllegalArgumentException("Tab is not in this profile");
        String action = request.optString("action");
        if ("sleep".equals(action)) {
            Tab active = model.getTabAt(model.index());
            if (active != null && id == active.getId()) throw new IllegalStateException("Select another tab before sleeping this one");
            tab.discard();
        } else if ("wake".equals(action)) model.setIndex(model.indexOf(tab), org.chromium.chrome.browser.tab.TabSelectionType.FROM_USER);
        else throw new IllegalArgumentException("Unknown tab performance action");
        return new JSONObject().put("ok", true).put("items", hot(model));
    }

    JSONObject media(Tab tab, String command) throws Exception {
        if (tab == null || tab.getWebContents() == null || tab.getWebContents().isDestroyed()) throw new IllegalStateException("No live page to control");
        if (!java.util.Set.of("play", "pause", "toggle", "next", "prev").contains(command)) throw new IllegalArgumentException("Unknown media command");
        PsychoPerformanceJni.get().media(tab.getWebContents(), command);
        return new JSONObject().put("ok", true).put("tab", tab.getId()).put("how", "requested").put("verified", false);
    }

    void enhance(Tab tab, JSONObject settings) {
        WebContents contents = tab == null ? null : tab.getWebContents();
        if (contents == null || contents.isDestroyed() || !web(tab.getUrl())) return;
        PsychoPerformanceJni.get().enhance(contents, settings.optBoolean("enhanceVideo"), settings.optBoolean("enhanceImages"), settings.optBoolean("reduceMotion"));
    }

    void snapshot(Tab tab, String kind, Consumer<JSONObject> callback) {
        if (!"png".equals(kind)) { callback.accept(reason("PDF export is not connected on Android yet")); return; }
        WebContents contents = tab == null ? null : tab.getWebContents();
        if (contents == null || contents.isDestroyed() || !web(tab.getUrl())) { callback.accept(reason("Select a live web page first")); return; }
        int id = ++mSequence; mSnapshots.put(id, callback);
        PsychoPerformanceJni.get().snapshot(contents, this, id);
        mHandler.postDelayed(() -> completeSnapshot(id, null), 10000);
    }

    @CalledByNative
    private void onNativeSnapshot(int id, byte[] png) {
        ThreadUtils.assertOnUiThread();
        if (mDestroyed || !mSnapshots.containsKey(id)) return;
        if (png == null || png.length == 0 || png.length > 32 * 1024 * 1024) { completeSnapshot(id, null); return; }
        new Thread(() -> {
            String name = "psychovault-" + System.currentTimeMillis() + ".png";
            Uri uri = null; boolean ok = false;
            try {
                ContentValues values = new ContentValues();
                values.put(MediaStore.Images.Media.DISPLAY_NAME, name); values.put(MediaStore.Images.Media.MIME_TYPE, "image/png");
                values.put(MediaStore.Images.Media.RELATIVE_PATH, android.os.Environment.DIRECTORY_PICTURES + "/PsychoVault");
                values.put(MediaStore.Images.Media.IS_PENDING, 1);
                uri = mContext.getContentResolver().insert(MediaStore.Images.Media.EXTERNAL_CONTENT_URI, values);
                if (uri != null) {
                    try (OutputStream out = mContext.getContentResolver().openOutputStream(uri)) { if (out == null) throw new IllegalStateException("Snapshot output unavailable"); out.write(png); }
                    ContentValues done = new ContentValues(); done.put(MediaStore.Images.Media.IS_PENDING, 0);
                    mContext.getContentResolver().update(uri, done, null, null); ok = true;
                }
            } catch (Exception e) {
                if (uri != null) try { mContext.getContentResolver().delete(uri, null, null); } catch (Exception ignored) { }
            }
            final boolean saved = ok; final String file = name;
            ThreadUtils.postOnUiThread(() -> {
                if (!saved) { completeSnapshot(id, null); return; }
                try { completeSnapshot(id, new JSONObject().put("ok", true).put("file", "Pictures/PsychoVault/" + file).put("bytes", png.length)); }
                catch (Exception e) { completeSnapshot(id, null); }
            });
        }, "psychovault-snapshot-save").start();
    }

    private void completeSnapshot(int id, JSONObject result) {
        Consumer<JSONObject> callback = mSnapshots.remove(id);
        if (callback != null) callback.accept(result == null ? reason("Snapshot failed or timed out") : result);
    }
    private static JSONObject reason(String text) { try { return new JSONObject().put("ok", false).put("reason", text); } catch (Exception e) { throw new IllegalStateException(e); } }

    private void applyPriority(TabModel model, JSONObject settings) {
        if (model == null) return;
        String limit = settings.optString("cpuLimit", "off");
        boolean low = settings.optBoolean("batterySaver") || "low".equals(limit);
        int importance = low ? ChildProcessImportance.NOT_PERCEPTIBLE : "balanced".equals(limit) ? ChildProcessImportance.MODERATE : ChildProcessImportance.IMPORTANT;
        for (int i = 0; i < model.getCount(); i++) {
            WebContents wc = model.getTabAt(i).getWebContents();
            if (wc != null && !wc.isDestroyed()) wc.setPrimaryPageImportance(importance, importance);
        }
    }
    private void sleepIdle(TabModel model, JSONObject settings) throws Exception {
        if (model == null || model.isIncognito()) return;
        int minutes = Math.max(0, Math.min(1440, settings.optInt("sleepTabsMin", 0)));
        if (settings.optBoolean("batterySaver")) minutes = minutes == 0 ? 3 : Math.min(3, minutes);
        if (minutes == 0) return;
        Set<String> excluded = hosts(settings.optString("sleepExclude"));
        long cutoff = System.currentTimeMillis() - minutes * 60000L;
        Tab active = model.getTabAt(model.index());
        for (int i = 0; i < model.getCount(); i++) {
            Tab tab = model.getTabAt(i);
            if (active != null && tab.getId() == active.getId() || tab.isFrozen() || tab.getTimestampMillis() > cutoff || excluded.contains(tab.getUrl().getHost().toLowerCase(java.util.Locale.ROOT))) continue;
            WebContents wc = tab.getWebContents();
            if (wc != null && !wc.isDestroyed() && PsychoPerformanceJni.get().isAudible(wc)) continue;
            tab.discard();
        }
    }
    static Set<String> hosts(String raw) {
        Set<String> out = new HashSet<>();
        for (String part : raw.split("[\\n,]")) { String host = part.trim().toLowerCase(java.util.Locale.ROOT); if (!host.isEmpty() && !host.contains("/")) out.add(host); }
        return out;
    }
    private static boolean web(org.chromium.url.GURL url) { return url != null && url.isValid() && ("https".equals(url.getScheme()) || "http".equals(url.getScheme())); }
    void destroy() { mDestroyed = true; mHandler.removeCallbacksAndMessages(null); mSnapshots.clear(); }

    @NativeMethods
    interface Natives {
        boolean isAudible(WebContents contents);
        void media(WebContents contents, String command);
        void enhance(WebContents contents, boolean video, boolean images, boolean reduceMotion);
        void snapshot(WebContents contents, PsychoPerformance owner, int requestId);
    }
}
