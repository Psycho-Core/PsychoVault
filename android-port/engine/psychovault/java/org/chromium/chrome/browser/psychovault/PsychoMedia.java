package org.chromium.chrome.browser.psychovault;

import android.content.Context;
import android.media.session.MediaSession;
import android.media.session.PlaybackState;
import android.os.Handler;
import android.os.Looper;
import android.view.KeyEvent;

import org.chromium.base.ThreadUtils;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.tabmodel.TabModel;
import org.chromium.content_public.browser.WebContents;
import org.json.JSONArray;
import org.json.JSONObject;

import java.util.HashSet;
import java.util.Locale;
import java.util.Set;

/**
 * Android media-button routing and auto-pause. The OS owns global media keys, so
 * PsychoVault registers a media session and asks the selected web tab to handle the
 * command; another app or the system may still take the key first.
 */
final class PsychoMedia {
    private final Context mContext;
    private final PsychoPerformance mPerformance;
    private final Handler mHandler = new Handler(Looper.getMainLooper());
    private MediaSession mSession;
    private TabModel mModel;
    private JSONObject mSettings = new JSONObject();
    private boolean mKeys, mAutoPause, mDestroyed;
    private int mPausedByUs = Tab.INVALID_TAB_ID;
    private final Runnable mAutoTask = new Runnable() {
        @Override public void run() {
            if (mDestroyed) return;
            try { autoPause(); } catch (Exception ignored) { }
            mHandler.postDelayed(this, 2500);
        }
    };

    PsychoMedia(Context context, PsychoPerformance performance) {
        mContext = context.getApplicationContext();
        mPerformance = performance;
        mHandler.postDelayed(mAutoTask, 2500);
    }

    void update(TabModel model, JSONObject settings) {
        ThreadUtils.assertOnUiThread();
        mModel = model; mSettings = settings;
        boolean keys = settings.optBoolean("mediaKeys", true);
        if (keys && mSession == null) {
            mSession = new MediaSession(mContext, "PsychoVault");
            mSession.setFlags(MediaSession.FLAG_HANDLES_MEDIA_BUTTONS | MediaSession.FLAG_HANDLES_TRANSPORT_CONTROLS);
            mSession.setCallback(new MediaSession.Callback() {
                @Override public boolean onMediaButtonEvent(KeyEvent event) {
                    if (event == null || event.getAction() != KeyEvent.ACTION_DOWN) return true;
                    int key = event.getKeyCode();
                    String command = null;
                    if (key == KeyEvent.KEYCODE_MEDIA_PLAY_PAUSE || key == KeyEvent.KEYCODE_MEDIA_PLAY || key == KeyEvent.KEYCODE_MEDIA_PAUSE) command = "toggle";
                    else if (key == KeyEvent.KEYCODE_MEDIA_STOP) command = "pause";
                    else if (key == KeyEvent.KEYCODE_MEDIA_NEXT) command = "next";
                    else if (key == KeyEvent.KEYCODE_MEDIA_PREVIOUS) command = "prev";
                    if (command != null) route(command);
                    return true;
                }
                @Override public void onPlay() { route("play"); }
                @Override public void onPause() { route("pause"); }
                @Override public void onSkipToNext() { route("next"); }
                @Override public void onSkipToPrevious() { route("prev"); }
            });
            PlaybackState state = new PlaybackState.Builder()
                    .setActions(PlaybackState.ACTION_PLAY | PlaybackState.ACTION_PAUSE | PlaybackState.ACTION_PLAY_PAUSE
                            | PlaybackState.ACTION_SKIP_TO_NEXT | PlaybackState.ACTION_SKIP_TO_PREVIOUS | PlaybackState.ACTION_STOP)
                    .setState(PlaybackState.STATE_NONE, PlaybackState.PLAYBACK_POSITION_UNKNOWN, 1f)
                    .build();
            mSession.setPlaybackState(state);
        }
        mKeys = keys;
        if (mSession != null) mSession.setActive(keys);
        mAutoPause = settings.optBoolean("mediaAutoPause", true);
        if (!mAutoPause) mPausedByUs = Tab.INVALID_TAB_ID;
    }

    private void route(String command) {
        ThreadUtils.postOnUiThread(() -> {
            try {
                Tab target = mediaTab();
                if (target != null) mPerformance.media(target, command);
            } catch (Exception ignored) { }
        });
    }

    private Tab mediaTab() {
        if (mModel == null || mModel.getCount() == 0) return null;
        Tab service = null, current = mModel.getTabAt(mModel.index());
        Set<String> hosts = serviceHosts();
        for (int i = 0; i < mModel.getCount(); i++) {
            Tab tab = mModel.getTabAt(i);
            if (audible(tab)) return tab;
            if (service == null && hosts.contains(tab.getUrl().getHost().toLowerCase(Locale.ROOT))) service = tab;
        }
        return service != null ? service : current;
    }

    private void autoPause() {
        if (!mAutoPause || mModel == null || mModel.isIncognito()) return;
        Set<String> hosts = serviceHosts();
        Tab service = null;
        for (int i = 0; i < mModel.getCount(); i++) {
            Tab tab = mModel.getTabAt(i);
            if (hosts.contains(tab.getUrl().getHost().toLowerCase(Locale.ROOT))) { service = tab; break; }
        }
        if (service == null) return;
        boolean other = false;
        for (int i = 0; i < mModel.getCount(); i++) {
            Tab tab = mModel.getTabAt(i);
            if (tab.getId() != service.getId() && audible(tab)) { other = true; break; }
        }
        try {
            if (other && audible(service)) { mPerformance.media(service, "pause"); mPausedByUs = service.getId(); }
            else if (!other && mPausedByUs == service.getId()) { mPerformance.media(service, "play"); mPausedByUs = Tab.INVALID_TAB_ID; }
        } catch (Exception ignored) { }
    }

    private Set<String> serviceHosts() {
        Set<String> out = new HashSet<>();
        JSONArray services = mSettings.optJSONArray("services");
        if (services != null) for (int i = 0; i < services.length(); i++) {
            try { out.add(new org.chromium.url.GURL(services.getJSONObject(i).getString("url")).getHost().toLowerCase(Locale.ROOT)); } catch (Exception ignored) { }
        }
        return out;
    }

    private boolean audible(Tab tab) {
        WebContents contents = tab == null ? null : tab.getWebContents();
        return contents != null && !contents.isDestroyed() && PsychoPerformanceJni.get().isAudible(contents);
    }

    void destroy() {
        mDestroyed = true;
        mHandler.removeCallbacksAndMessages(null);
        if (mSession != null) { mSession.setActive(false); mSession.release(); mSession = null; }
    }
}
