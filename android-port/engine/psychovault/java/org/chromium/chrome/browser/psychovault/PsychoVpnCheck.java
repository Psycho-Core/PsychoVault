package org.chromium.chrome.browser.psychovault;

import org.chromium.base.Callback;
import org.chromium.base.ThreadUtils;
import org.chromium.chrome.browser.profiles.Profile;
import org.jni_zero.CalledByNative;
import org.jni_zero.NativeMethods;
import org.json.JSONObject;

import java.util.HashMap;
import java.util.Map;

/** Public-IP lookup through the selected Chromium profile, not a proxy-bypassing Java socket. */
final class PsychoVpnCheck {
    private int mSequence;
    private boolean mDestroyed;
    private final Map<Integer, Callback<JSONObject>> mPending = new HashMap<>();

    void check(Profile profile, Callback<JSONObject> callback) {
        ThreadUtils.assertOnUiThread();
        if (mDestroyed || profile == null) { callback.onResult(fail("VPN check is unavailable")); return; }
        int id = ++mSequence;
        mPending.put(id, callback);
        PsychoVpnCheckJni.get().check(profile, this, id);
    }

    @CalledByNative
    private void onNativeResult(int id, String json) {
        ThreadUtils.postOnUiThread(() -> {
            Callback<JSONObject> callback = mPending.remove(id);
            if (callback == null || mDestroyed) return;
            try {
                JSONObject raw = new JSONObject(json == null ? "{}" : json);
                JSONObject out = new JSONObject().put("ok", raw.optBoolean("ok"));
                if (!raw.optBoolean("ok")) out.put("error", "The public IP service could not be reached through this profile.");
                else {
                    out.put("ip", bounded(raw.optString("ip"), 64));
                    out.put("city", bounded(raw.optString("city"), 100));
                    out.put("region", bounded(raw.optString("region"), 100));
                    out.put("country", bounded(raw.optString("country_name"), 100));
                }
                callback.onResult(out);
            } catch (Exception e) { callback.onResult(fail("The public IP service returned an invalid response")); }
        });
    }

    private static String bounded(String value, int limit) { return value.length() <= limit ? value : value.substring(0, limit); }
    private static JSONObject fail(String message) { try { return new JSONObject().put("ok", false).put("error", message); } catch (Exception e) { throw new IllegalStateException(e); } }
    void destroy() { ThreadUtils.assertOnUiThread(); mDestroyed = true; mPending.clear(); PsychoVpnCheckJni.get().cancelAll(this); }

    @NativeMethods
    interface Natives {
        void check(Profile profile, PsychoVpnCheck owner, int requestId);
        void cancelAll(PsychoVpnCheck owner);
    }
}
