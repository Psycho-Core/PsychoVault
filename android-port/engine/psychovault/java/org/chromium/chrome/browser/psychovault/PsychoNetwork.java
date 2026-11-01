package org.chromium.chrome.browser.psychovault;

import android.content.Context;

import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.content_public.browser.WebContents;
import org.jni_zero.NativeMethods;
import org.json.JSONArray;
import org.json.JSONObject;

import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.Locale;

/** Profile policy hand-off to the native request throttle. It never claims a VPN connection. */
final class PsychoNetwork {
    private static JSONArray sBundled;
    private PsychoNetwork() {}

    static synchronized void update(Context context, Profile profile, JSONObject settings) throws Exception {
        if (sBundled == null) {
            try (InputStream in = context.getAssets().open("psychovault/bundled/blocklist.json")) {
                byte[] data = PsychoBridge.readLimited(in, 4 * 1024 * 1024);
                JSONArray raw = new JSONArray(new String(data, StandardCharsets.UTF_8)); JSONArray valid = new JSONArray();
                for (int i = 0; i < raw.length(); i++) { String domain = raw.optString(i).trim().toLowerCase(Locale.ROOT); if (validDomain(domain)) valid.put(domain); }
                sBundled = valid;
            }
        }
        JSONObject payload = new JSONObject(settings.toString()).put("bundledDomains", sBundled);
        if (profile.isOffTheRecord()) {
            Profile original = profile.getOriginalProfile();
            if (original != null) PsychoNetworkJni.get().update(original, payload.toString());
        }
        PsychoNetworkJni.get().update(profile, payload.toString());
    }

    static JSONObject status(Profile profile, WebContents contents) throws Exception {
        String json = PsychoNetworkJni.get().status(profile, contents);
        return json == null ? new JSONObject() : new JSONObject(json);
    }
    static void forget(Profile profile) { if (profile != null) PsychoNetworkJni.get().forget(profile); }
    static boolean validDomain(String domain) {
        return domain != null && domain.length() <= 253 && domain.matches("[a-z0-9.-]+") && !domain.startsWith(".") && !domain.endsWith(".") && !domain.contains("..");
    }

    @NativeMethods
    interface Natives {
        void update(Profile profile, String json);
        String status(Profile profile, WebContents contents);
        void forget(Profile profile);
    }
}
