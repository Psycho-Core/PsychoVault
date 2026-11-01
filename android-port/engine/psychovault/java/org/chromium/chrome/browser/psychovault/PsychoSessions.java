package org.chromium.chrome.browser.psychovault;

import android.content.Context;
import android.content.SharedPreferences;

import org.chromium.base.ThreadUtils;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.tabmodel.TabModel;
import org.chromium.url.GURL;
import org.json.JSONArray;
import org.json.JSONObject;

import java.util.UUID;

/** Explicit, additive recovery snapshots. Chromium's own tab-state persistence is never cleared. */
final class PsychoSessions {
    private static final int MAX_SLOTS = 10;
    private static final int MAX_TABS = 500;
    private final Context mContext;
    private final Profile mProfile;
    private final String mName;

    PsychoSessions(Context context, Profile profile) {
        if (profile == null || profile.isOffTheRecord()) throw new IllegalArgumentException("Recovery requires a regular profile");
        mContext = context.getApplicationContext(); mProfile = profile;
        String name = PsychoProfileStoreJni.get().getProfileName(profile);
        if (name == null || !name.matches("[A-Za-z0-9_.-]+") || name.equals(".") || name.equals("..")) throw new IllegalStateException("Profile storage unavailable");
        mName = "psychovault_sessions_" + name;
    }

    private SharedPreferences prefs() { ThreadUtils.assertOnUiThread(); return mContext.getSharedPreferences(mName, Context.MODE_PRIVATE); }

    void capture(TabModel model, String reason) throws Exception {
        ThreadUtils.assertOnUiThread();
        if (model == null || model.isIncognito() || model.getProfile() != mProfile) return;
        JSONArray tabs = new JSONArray();
        for (int i = 0; i < Math.min(MAX_TABS, model.getCount()); i++) {
            Tab tab = model.getTabAt(i); if (tab == null) continue;
            String url = cleanUrl(tab.getUrl()); if (url == null) continue;
            tabs.put(new JSONObject().put("url", url).put("title", safeTitle(tab.getTitle())).put("pinned", tab.getIsPinned())
                    .put("muted", tab.getWebContents() != null && !tab.getWebContents().isDestroyed() && tab.getWebContents().isAudioMuted())
                    .put("workspace", tab.getTabGroupId() == null ? "" : tab.getTabGroupId().toString()));
        }
        if (tabs.length() == 0) return;
        JSONArray slots = readSlots();
        JSONObject snapshot = new JSONObject().put("id", UUID.randomUUID().toString()).put("at", System.currentTimeMillis())
                .put("reason", reason == null ? "background" : reason).put("tabs", tabs).put("activeId", model.getTabAt(model.index()) == null ? -1 : model.getTabAt(model.index()).getId());
        // Consecutive background saves replace only the newest background slot, not ten useful recovery points.
        if (slots.length() > 0 && "background".equals(reason) && "background".equals(slots.getJSONObject(0).optString("reason"))) slots = tail(slots, 1);
        JSONArray next = new JSONArray().put(snapshot);
        for (int i = 0; i < Math.min(MAX_SLOTS - 1, slots.length()); i++) next.put(slots.getJSONObject(i));
        if (!prefs().edit().putString("slots", next.toString()).commit()) throw new IllegalStateException("Recovery snapshot could not be saved");
    }

    JSONObject list() throws Exception {
        JSONArray slots = readSlots(), summaries = new JSONArray();
        for (int i = 0; i < slots.length(); i++) {
            JSONObject slot = slots.getJSONObject(i);
            summaries.put(new JSONObject().put("id", slot.getString("id")).put("at", slot.getLong("at")).put("reason", slot.optString("reason")).put("count", slot.getJSONArray("tabs").length()));
        }
        return new JSONObject().put("ok", true).put("items", summaries);
    }

    JSONArray restoreTabs(String id) throws Exception {
        JSONArray slots = readSlots();
        for (int i = 0; i < slots.length(); i++) if (slots.getJSONObject(i).optString("id").equals(id)) return validate(slots.getJSONObject(i).getJSONArray("tabs"));
        throw new IllegalArgumentException("Recovery snapshot not found");
    }

    void remove(String id) throws Exception {
        JSONArray slots = readSlots(), next = new JSONArray();
        for (int i = 0; i < slots.length(); i++) if (!slots.getJSONObject(i).optString("id").equals(id)) next.put(slots.getJSONObject(i));
        if (!prefs().edit().putString("slots", next.toString()).commit()) throw new IllegalStateException("Recovery snapshot could not be removed");
    }

    private JSONArray readSlots() throws Exception {
        String raw = prefs().getString("slots", "[]");
        if (raw.length() > 4 * 1024 * 1024) throw new IllegalStateException("Recovery data is too large; refusing to overwrite it");
        JSONArray slots = new JSONArray(raw);
        if (slots.length() > MAX_SLOTS) throw new IllegalStateException("Corrupt recovery index; refusing to overwrite it");
        for (int i = 0; i < slots.length(); i++) validate(slots.getJSONObject(i).getJSONArray("tabs"));
        return slots;
    }
    private static JSONArray tail(JSONArray value, int skip) throws Exception { JSONArray out = new JSONArray(); for (int i = skip; i < value.length(); i++) out.put(value.getJSONObject(i)); return out; }
    private static JSONArray validate(JSONArray tabs) throws Exception {
        if (tabs.length() > MAX_TABS) throw new IllegalStateException("Recovery snapshot has too many tabs");
        JSONArray out = new JSONArray();
        for (int i = 0; i < tabs.length(); i++) {
            JSONObject tab = tabs.getJSONObject(i); String url = cleanUrl(new GURL(tab.optString("url")));
            if (url == null) continue;
            out.put(new JSONObject().put("url", url).put("title", safeTitle(tab.optString("title"))).put("pinned", tab.optBoolean("pinned")).put("muted", tab.optBoolean("muted")));
        }
        return out;
    }
    private static String cleanUrl(GURL value) {
        if (value == null || !value.isValid() || !("https".equals(value.getScheme()) || "http".equals(value.getScheme())) || value.getSpec().length() > 8192) return null;
        return stripCredentials(value.getSpec());
    }
    private static String stripCredentials(String raw) {
        int scheme = raw.indexOf("://"); int at = raw.indexOf('@', scheme + 3); int slash = raw.indexOf('/', scheme + 3);
        if (at >= 0 && (slash < 0 || at < slash)) return raw.substring(0, scheme + 3) + raw.substring(at + 1);
        return raw;
    }
    private static String safeTitle(String title) { return title == null ? "" : title.substring(0, Math.min(500, title.length())); }
}
