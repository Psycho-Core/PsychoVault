package org.chromium.chrome.browser.psychovault;

import org.chromium.base.ThreadUtils;
import org.chromium.base.VersionInfo;
import org.chromium.base.Callback;
import org.json.JSONArray;
import org.json.JSONObject;

import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;

/**
 * Read-only Android release check against the public GitHub releases feed. It never
 * downloads or installs an APK; dismissal is stored per release tag in settings.
 */
final class PsychoUpdates {
    private static final String RELEASES = "https://api.github.com/repos/Psycho-Core/PsychoVault/releases?per_page=30";
    private PsychoUpdates() {}

    static void check(String dismissed, Callback<JSONObject> callback) {
        new Thread(() -> {
            JSONObject result;
            HttpURLConnection connection = null;
            try {
                connection = (HttpURLConnection) new URL(RELEASES).openConnection();
                connection.setConnectTimeout(10000);
                connection.setReadTimeout(15000);
                connection.setRequestProperty("Accept", "application/vnd.github+json");
                connection.setRequestProperty("User-Agent", "PsychoVault-Android");
                int status = connection.getResponseCode();
                if (status != 200) {
                    result = new JSONObject().put("ok", false).put("message", "GitHub returned HTTP " + status).put("version", VersionInfo.getProductVersion());
                } else {
                    String body;
                    try (InputStream in = connection.getInputStream()) {
                        byte[] data = PsychoBridge.readLimited(in, 2 * 1024 * 1024);
                        body = new String(data, StandardCharsets.UTF_8);
                    }
                    JSONArray releases = new JSONArray(body);
                    JSONObject latest = null;
                    for (int i = 0; i < releases.length(); i++) {
                        JSONObject release = releases.getJSONObject(i);
                        String tag = release.optString("tag_name");
                        if (tag.startsWith("android-chromium-") && !release.optBoolean("draft")) { latest = release; break; }
                    }
                    result = new JSONObject().put("ok", true).put("version", VersionInfo.getProductVersion());
                    if (latest == null) {
                        result.put("result", new JSONObject().put("reason", "no-android-release"));
                    } else {
                        String tag = latest.optString("tag_name");
                        result.put("release", tag).put("url", latest.optString("html_url"))
                                .put("dismissed", tag.equals(dismissed == null ? "" : dismissed))
                                .put("result", new JSONObject().put("reason", "android-release-found").put("version", tag));
                    }
                }
            } catch (Exception e) {
                try { result = new JSONObject().put("ok", false).put("message", String.valueOf(e.getMessage())).put("version", VersionInfo.getProductVersion()); }
                catch (Exception ignored) { result = new JSONObject(); }
            } finally {
                if (connection != null) connection.disconnect();
            }
            final JSONObject answer = result;
            ThreadUtils.postOnUiThread(() -> callback.onResult(answer));
        }, "psychovault-update-check").start();
    }
}
