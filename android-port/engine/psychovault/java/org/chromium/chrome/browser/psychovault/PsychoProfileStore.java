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
            "speedDial", "hideSidebar", "highContrast", "reduceMotion", "showTabCloseButtons",
            "blockAds", "blockUseBundled", "blockCustomDomains", "blockAllowSites", "workspacesEnabled",
            "translateTarget", "blockAutoplay", "blockThirdPartyCookies", "sendDNT", "cpuLimit",
            "sleepExclude", "sleepTabsMin", "batterySaver", "vaultEnabled", "vaultLockMin",
            "enhanceVideo", "enhanceImages", "askSaveLocation", "panicEnabled", "panicUrl",
            "services", "railIcons", "spellCheck", "spellBuiltin", "spellLangs", "webRtc", "doh",
            "dohTemplate", "stripReferrer", "denyPerms", "modCss", "restore");
    private static final Set<String> INTERNAL = Set.of("removedBundledExtensions", "extensions", "workspaceOrder", "workspaceActive");
    private static final Set<String> BOOL_KEYS = Set.of("remember", "restore", "hideSidebar", "highContrast", "reduceMotion", "showTabCloseButtons",
            "blockAds", "blockUseBundled", "workspacesEnabled", "blockAutoplay", "blockThirdPartyCookies", "sendDNT", "batterySaver",
            "vaultEnabled", "enhanceVideo", "enhanceImages", "askSaveLocation", "panicEnabled", "railIcons", "spellCheck", "spellBuiltin", "stripReferrer");
    private static final Set<String> NUMBER_KEYS = Set.of("sleepTabsMin", "vaultLockMin");
    private static final Set<String> ARRAY_KEYS = Set.of("customSearchEngines", "speedDial", "services", "extensions", "workspaceOrder", "removedBundledExtensions");
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
        return writeInternal(profile, patch, false);
    }

    /** Destructive workspace/extension inventory changes must reach disk before tabs close. */
    JSONObject writeInternalDurable(Profile profile, JSONObject patch) throws Exception {
        return writeInternal(profile, patch, true);
    }

    private JSONObject writeInternal(Profile profile, JSONObject patch, boolean durable) throws Exception {
        if (patch == null) throw new IllegalArgumentException("Settings patch required");
        JSONObject current = read(profile);
        for (Iterator<String> it = patch.keys(); it.hasNext();) {
            String key = it.next();
            if (!ALLOWED.contains(key) && !INTERNAL.contains(key)) throw new IllegalArgumentException("This setting is not connected on Android yet");
            Object value = patch.get(key);
            if (BOOL_KEYS.contains(key) && !(value instanceof Boolean)) throw new IllegalArgumentException("Expected a true/false value for " + key);
            if (NUMBER_KEYS.contains(key) && !(value instanceof Number)) throw new IllegalArgumentException("Expected a number for " + key);
            if (ARRAY_KEYS.contains(key) && !(value instanceof JSONArray)) throw new IllegalArgumentException("Expected a list for " + key);
            if ((ALLOWED.contains(key) && !BOOL_KEYS.contains(key) && !NUMBER_KEYS.contains(key) && !ARRAY_KEYS.contains(key) || key.equals("workspaceActive")) && !(value instanceof String)) throw new IllegalArgumentException("Expected text for " + key);
            current.put(key, value);
        }
        JSONObject validated = defaults(current);
        if (current.has("removedBundledExtensions")) validated.put("removedBundledExtensions", current.getJSONArray("removedBundledExtensions"));
        SharedPreferences.Editor editor = prefs(profile).edit();
        editor.putString(KEY, validated.toString());
        if (durable) { if (!editor.commit()) throw new IllegalStateException("Browser data could not be saved"); } else editor.apply();
        return validated;
    }

    static JSONObject defaults(JSONObject saved) throws Exception {
        JSONObject out = new JSONObject();
        out.put("search", enumValue(saved, "search", "duckduckgo", Set.of("duckduckgo", "brave", "startpage", "wikipedia")));
        out.put("theme", enumValue(saved, "theme", "midnight", Set.of("midnight", "graphite", "forest", "plum", "ocean")));
        out.put("accent", enumValue(saved, "accent", "cyan", Set.of("cyan", "red", "purple", "green", "orange", "pink", "gold", "teal", "indigo", "rose")));
        out.put("homepage", homepage(saved.optString("homepage", "psychovault://newtab")));
        out.put("remember", saved.optBoolean("remember", true));
        out.put("restore", saved.optBoolean("restore", true));
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
        out.put("blockAds", saved.optBoolean("blockAds", true));
        out.put("blockUseBundled", saved.optBoolean("blockUseBundled", true));
        out.put("blockCustomDomains", text(saved, "blockCustomDomains", "", 20000));
        out.put("blockAllowSites", text(saved, "blockAllowSites", "", 20000));
        out.put("workspacesEnabled", saved.optBoolean("workspacesEnabled", true));
        out.put("showSpeedDial", true);
        out.put("translateTarget", text(saved, "translateTarget", "en", 35));
        out.put("blockAutoplay", saved.optBoolean("blockAutoplay", false));
        out.put("blockThirdPartyCookies", saved.optBoolean("blockThirdPartyCookies", false));
        out.put("sendDNT", saved.optBoolean("sendDNT", true));
        out.put("cpuLimit", enumValue(saved, "cpuLimit", "off", Set.of("off", "balanced", "low")));
        out.put("sleepExclude", text(saved, "sleepExclude", "", 20000));
        out.put("sleepTabsMin", Math.max(0, Math.min(1440, saved.optInt("sleepTabsMin", 0))));
        out.put("batterySaver", saved.optBoolean("batterySaver", false));
        out.put("vaultEnabled", saved.optBoolean("vaultEnabled", true));
        out.put("vaultLockMin", Math.max(0, Math.min(1440, saved.optInt("vaultLockMin", 5))));
        out.put("enhanceVideo", saved.optBoolean("enhanceVideo", false));
        out.put("enhanceImages", saved.optBoolean("enhanceImages", false));
        out.put("askSaveLocation", saved.optBoolean("askSaveLocation", true));
        out.put("panicEnabled", saved.optBoolean("panicEnabled", true));
        out.put("panicUrl", homepage(saved.optString("panicUrl", "https://en.wikipedia.org/wiki/Main_Page")));
        out.put("services", services(saved.optJSONArray("services")));
        out.put("railIcons", saved.optBoolean("railIcons", true));
        out.put("spellCheck", saved.optBoolean("spellCheck", true));
        out.put("spellBuiltin", saved.optBoolean("spellBuiltin", true));
        out.put("spellLangs", text(saved, "spellLangs", "", 500));
        out.put("webRtc", enumValue(saved, "webRtc", "publiconly", Set.of("default", "publiconly", "disableudp")));
        out.put("doh", enumValue(saved, "doh", "off", Set.of("off", "automatic", "secure")));
        String dohTemplate = text(saved, "dohTemplate", "", 2048);
        if (!dohTemplate.isEmpty() && !dohTemplate.startsWith("https://")) throw new IllegalArgumentException("Secure DNS template must use HTTPS");
        out.put("dohTemplate", dohTemplate);
        out.put("stripReferrer", saved.optBoolean("stripReferrer", true));
        out.put("denyPerms", text(saved, "denyPerms", "geolocation, media, clipboard-read, midi, sync-xhr, wake-lock, notifications", 2000));
        out.put("modCss", text(saved, "modCss", "", 20000));
        if (saved.has("extensions")) out.put("extensions", saved.getJSONArray("extensions"));
        if (saved.has("workspaceOrder")) out.put("workspaceOrder", saved.getJSONArray("workspaceOrder"));
        if (saved.has("workspaceActive")) out.put("workspaceActive", saved.optString("workspaceActive"));
        return out;
    }

    private static String text(JSONObject o, String key, String fallback, int limit) {
        String value = o.optString(key, fallback);
        if (value.length() > limit) throw new IllegalArgumentException("Setting is too long: " + key);
        return value;
    }

    private static JSONArray services(JSONArray input) throws Exception {
        JSONArray out = new JSONArray();
        if (input == null) {
            String[][] initial = {{"Discord", "https://discord.com/app"}, {"YouTube Music", "https://music.youtube.com"}, {"YouTube", "https://www.youtube.com"}, {"GitHub", "https://github.com"}};
            for (String[] item : initial) out.put(new JSONObject().put("name", item[0]).put("url", item[1]));
            return out;
        }
        if (input.length() > 12) throw new IllegalArgumentException("The rail holds 12 services");
        for (int i = 0; i < input.length(); i++) { JSONObject item = input.getJSONObject(i); String name = item.optString("name"), url = homepage(item.optString("url")); if (name.isEmpty() || name.length() > 80 || !url.startsWith("https://")) throw new IllegalArgumentException("Invalid rail service"); out.put(new JSONObject().put("name", name).put("url", url)); }
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
