package org.chromium.chrome.browser.psychovault;

import org.json.JSONArray;
import org.json.JSONObject;
import org.chromium.base.ThreadUtils;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.content.browser.JavascriptInterface;
import org.chromium.content_public.browser.WebContents;
import org.chromium.url.GURL;

import java.util.function.Supplier;

/** One document's bridge. Origin matching is necessary but not sufficient: check every call. */
final class PsychoBridge {
    private final WebContents mContents;
    private final Supplier<Profile> mCurrentProfile;
    private final Handler mHandler;
    private final boolean mChrome;
    private final int mTabId;
    private boolean mDestroyed;
    private boolean mReady;

    interface Handler {
        void command(String channel, JSONArray args, int tabId, Result result);
        JSONObject snapshot(int tabId);
    }

    interface Result {
        void send(boolean ok, Object value);
    }

    PsychoBridge(WebContents contents, Supplier<Profile> currentProfile, Handler handler,
            boolean chrome, int tabId) {
        mContents = contents;
        mCurrentProfile = currentProfile;
        mHandler = handler;
        mChrome = chrome;
        mTabId = tabId;
    }

    private boolean trusted() {
        if (mDestroyed || mContents.isDestroyed()) return false;
        Profile current = mCurrentProfile.get();
        Profile own = Profile.fromWebContents(mContents);
        if (current == null || own == null || current != own) return false;
        GURL url = mContents.getLastCommittedUrl();
        if (url == null || !url.isValid() || url.getSpec().contains("#")) return false;
        String expectedOrigin = mChrome ? PsychoChromeHost.CHROME_ORIGIN : PsychoChromeHost.PAGE_ORIGIN;
        if (!url.getSpec().startsWith(expectedOrigin + "/")) return false;
        String path = url.getSpec().substring(expectedOrigin.length());
        boolean expectedPrivate = own.isOffTheRecord();
        String suffix = "?private=" + (expectedPrivate ? 1 : 0);
        return mChrome ? path.equals("/index.html" + suffix)
                : path.equals("/newtab.html" + suffix) || path.equals("/settings.html" + suffix);
    }

    // This is the ONLY JavaScript-exposed method. Calls arrive off the UI thread.
    @JavascriptInterface
    public void post(String json) {
        if (json == null || json.length() > 65536) return;
        ThreadUtils.postOnUiThread(() -> accept(json));
    }

    private void accept(String json) {
        if (!trusted()) return;
        long id = -1;
        try {
            JSONObject request = new JSONObject(json);
            id = request.getLong("id");
            if (id < 0 || id > Integer.MAX_VALUE) return;
            String channel = request.getString("channel");
            JSONArray args = request.getJSONArray("args");
            if (args.length() > 2) throw new IllegalArgumentException("Too many arguments");
            if (channel.equals("ui:ready")) {
                mReady = true;
                reply(id, true, mHandler.snapshot(mTabId));
                emit("state", mHandler.snapshot(mTabId));
                return;
            }
            if (!mReady) throw new IllegalStateException("UI is not ready");
            final long requestId = id;
            mHandler.command(channel, args, mTabId, (ok, value) -> reply(requestId, ok, value));
        } catch (Exception e) {
            if (id >= 0) reply(id, false, safeMessage(e));
        }
    }

    static String safeMessage(Exception e) {
        // Do not serialize URLs, JavaScript, profile paths or native stack traces into chrome.
        return e instanceof IllegalArgumentException || e instanceof IllegalStateException
                ? String.valueOf(e.getMessage()) : "Android browser operation failed";
    }

    private void reply(long id, boolean ok, Object value) {
        evaluate("window.PsychoNativeTransport.reply(" + id + "," + ok + ","
                + JSONObject.quote(value instanceof String ? (String) value : String.valueOf(value)) + ")");
    }

    void emit(String event, Object payload) {
        if (!mReady || !trusted()) return;
        String json;
        try { json = JSONObject.valueToString(payload); } catch (RuntimeException e) { return; }
        evaluate("window.PsychoNativeTransport.emit(" + JSONObject.quote(event) + "," + json + ")");
    }

    private void evaluate(String script) {
        // evaluateJavaScript is restricted to WebUI by content/public/browser/WebContents.
        // Recheck immediately before evaluation as well as when accepting a command.
        if (trusted()) mContents.evaluateJavaScript(script, null);
    }

    void destroy() {
        mDestroyed = true;
        mReady = false;
    }

    int tabId() { return mTabId; }
}
