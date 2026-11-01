package org.chromium.chrome.browser.psychovault;

import android.content.Context;
import android.content.SharedPreferences;

import org.json.JSONArray;
import org.json.JSONObject;
import org.chromium.base.ThreadUtils;
import org.chromium.chrome.browser.profiles.Profile;
import org.jni_zero.NativeMethods;

import java.util.Iterator;
import java.util.Set;

/**
 * Per-profile PsychoVault settings, stored outside website-accessible storage.
 * The profile directory basename comes from native Profile::GetBaseName(), so two
 * regular profiles never share a file and an incognito profile reads/writes its
 * original profile's preferences rather than creating a durable OTR store.
 * All reads and writes happen on Chromium's UI thread; SharedPreferences.apply()
 * schedules the disk write. A failed disk write is not reported synchronously by
 * Android, so this is not a substitute for a later device persistence test.
 */
final class PsychoProfileStore {
    private static final String KEY = "settings";
    private static final Set<String> ALLOWED = Set.of(
            "search", "customSearchEngines", "homepage", "remember", "theme", "accent",
            "speedDial", "hideSidebar", "highContrast", "reduceMotion", "showTabCloseButtons");
    private final Context mContext;

    PsychoProfileStore(Context context) { mContext = context.getApplicationContext(); }

    private SharedPreferences prefs(Profile profile) {
        ThreadUtils.assertOnUiThread();
        if (profile == null) throw new IllegalStateException("Profile unavailable");
        Profile original = profile.isOffTheRecord() ? profile.getOriginalProfile() : profile;
        String name = PsychoProfileStoreJni.get().getProfileName(original);
        if (name == null || name.isEmpty() || name.contains("/") || name.contains("\\") || name.equals(".") || name.equals("..")) {
            throw new IllegalStateException("Profile storage unavailable");
        }
        return mContext.getSharedPreferences("psychovault_" + name, Context.MODE_PRIVATE);
    }

    JSONObject read(Profile profile) throws Exception {
        String raw = prefs(profile).getString(KEY, null);
        JSONObject value = raw == null ? new JSONObject() : new JSONObject(raw);
        return defaults(value);
    }

    /** Merge only allowlisted, validated keys. Unknown keys are refused, not silently saved. */
    JSONObject write(Profile profile, JSONObject patch) throws Exception {
        if (profile != null && profile.isOffTheRecord()) throw new IllegalArgumentException("Private windows do not save settings");
        if (patch == null) throw new IllegalArgumentException("Settings patch required");
        for (Iterator<String> it = patch.keys(); it.hasNext();) {
            if (!ALLOWED.contains(it.next())) throw new IllegalArgumentException("This setting is not connected on Android yet");
        }
        return writeInternal(profile, patch);
    }

    /** Host-only persistence for bundled-extension removal; not a desktop settings key. */
    JSONObject writeInternal(Profile profile, JSONObject patch) throws Exception {
        if (patch == null) throw new IllegalArgumentException("Settings patch required");
        JSONObject current = read(profile);
        for (Iterator<String> it = patch.keys(); it.hasNext();) {
            String key = it.next();
            if (!ALLOWED.contains(key) && !key.equals("removedBundledExtensions")) throw new IllegalArgumentException("This setting is not connected on Android yet");
            current.put(key, patch.get(key));
        }
        JSONObject validated = defaults(current);
        if (current.has("removedBundledExtensions")) validated.put("removedBundledExtensions", current.getJSONArray("removedBundledExtensions"));
        SharedPreferences.Editor editor = prefs(profile).edit();
        editor.putString(KEY, validated.toString());
        editor.apply();
        return validated;
    }

    static JSONObject defaults(JSONObject saved) throws Exception {
        JSONObject out = new JSONObject();
        out.put("search", enumValue(saved, "search", "duckduckgo", Set.of("duckduckgo", "brave", "startpage", "wikipedia")));
        out.put("theme", enumValue(saved, "theme", "midnight", Set.of("midnight", "graphite", "forest", "plum", "ocean")));
        out.put("accent", enumValue(saved, "accent", "cyan", Set.of("cyan", "red", "purple", "green", "orange", "pink", "gold", "teal", "indigo", "rose")));
        out.put("homepage", homepage(saved.optString("homepage", "psychovault://newtab")));
        out.put("remember", saved.optBoolean("remember", true));
        out.put("hideSidebar", saved.optBoolean("hideSidebar", false));
        out.put("highContrast", saved.optBoolean("highContrast", false));
        out.put("reduceMotion", saved.optBoolean("reduceMotion", false));
        out.put("showTabCloseButtons", saved.optBoolean("showTabCloseButtons", false));
        JSONArray custom = engines(saved.optJSONArray("customSearchEngines"));
        out.put("customSearchEngines", custom);
        String search = out.optString("search", "duckduckgo");
        if (search.startsWith("custom:")) {
            boolean found = false;
            for (int i = 0; i < custom.length(); i++) if (search.equals("custom:" + custom.getJSONObject(i).getString("shortcut"))) found = true;
            if (!found) out.put("search", "duckduckgo");
        }
        out.put("speedDial", tiles(saved.optJSONArray("speedDial")));
        if (saved.has("removedBundledExtensions")) out.put("removedBundledExtensions", saved.getJSONArray("removedBundledExtensions"));
        // Fixed, honest values for features whose Android handlers do not exist yet.
        out.put("blockAds", false).put("workspacesEnabled", false).put("showSpeedDial", true);
        return out;
    }

    private static String enumValue(JSONObject o, String key, String fallback, Set<String> allowed) {
        String v = o.optString(key, fallback);
        if (v.startsWith("custom:") && key.equals("search")) return v;
        return allowed.contains(v) ? v : fallback;
    }

    private static String homepage(String raw) {
        if (raw == null || raw.isEmpty() || raw.equals("psychovault://newtab")) return "psychovault://newtab";
        if (raw.length() > 8192) throw new IllegalArgumentException("Home page is too long");
        org.chromium.url.GURL url = new org.chromium.url.GURL(raw);
        if (!url.isValid() || !("https".equals(url.getScheme()) || "http".equals(url.getScheme()))) {
            throw new IllegalArgumentException("Home page must be the new tab or an HTTP/HTTPS URL");
        }
        return url.getSpec();
    }

    private static JSONArray engines(JSONArray input) throws Exception {
        JSONArray out = new JSONArray();
        if (input == null) return out;
        if (input.length() > 50) throw new IllegalArgumentException("Too many search shortcuts");
        java.util.HashSet<String> seen = new java.util.HashSet<>();
        for (int i = 0; i < input.length(); i++) {
            JSONObject e = input.getJSONObject(i);
            String shortcut = e.optString("shortcut", ""), name = e.optString("name", ""), url = e.optString("url", "");
            if (!shortcut.matches("(?i)[a-z][a-z0-9_-]{0,19}") || name.isEmpty() || name.length() > 80
                    || url.length() > 2048 || !url.contains("%s") || !seen.add(shortcut.toLowerCase(java.util.Locale.ROOT))) {
                throw new IllegalArgumentException("Invalid or duplicate search shortcut");
            }
            String a = url.replace("%s", "sample"), b = url.replace("%s", "different");
            org.chromium.url.GURL ua = new org.chromium.url.GURL(a), ub = new org.chromium.url.GURL(b);
            if (!ua.isValid() || !ub.isValid() || !"https".equals(ua.getScheme())
                    || !ua.getUsername().isEmpty() || !ua.getPassword().isEmpty()
                    || !ua.getOrigin().getSpec().equals(ub.getOrigin().getSpec())) {
                throw new IllegalArgumentException("Search shortcuts must use one HTTPS origin");
            }
            out.put(new JSONObject().put("shortcut", shortcut).put("name", name).put("url", url));
        }
        return out;
    }

    private static JSONArray tiles(JSONArray input) throws Exception {
        JSONArray out = new JSONArray();
        if (input == null) {
            String[][] initial = {{"GitHub", "https://github.com"}, {"YouTube", "https://youtube.com"},
                    {"Reddit", "https://reddit.com"}, {"Wikipedia", "https://wikipedia.org"},
                    {"Mail", "https://mail.google.com"}, {"News", "https://news.ycombinator.com"}};
            for (String[] t : initial) out.put(new JSONObject().put("title", t[0]).put("url", t[1]));
            return out;
        }
        if (input.length() > 24) throw new IllegalArgumentException("Too many speed dial tiles");
        for (int i = 0; i < input.length(); i++) {
            JSONObject t = input.getJSONObject(i);
            String title = t.optString("title", ""), url = t.optString("url", "");
            if (title.isEmpty() || title.length() > 80) throw new IllegalArgumentException("Invalid tile name");
            out.put(new JSONObject().put("title", title).put("url", homepage(url)));
        }
        return out;
    }

    @NativeMethods
    interface Natives {
        String getProfileName(Profile profile);
    }
}
