package org.chromium.chrome.browser.psychovault;

import org.json.JSONArray;
import org.json.JSONObject;

import java.util.ArrayDeque;
import java.util.Deque;

/** Bounded Android error journal. It stores channel names and exception classes only. */
final class PsychoErrorLog {
    private static final int LIMIT = 100;
    private final Deque<JSONObject> mEntries = new ArrayDeque<>();

    synchronized void record(String channel, Exception error) {
        try {
            String name = channel == null || !channel.matches("[a-z]+:[A-Za-z]+") ? "unknown" : channel;
            String kind = error == null ? "Exception" : error.getClass().getSimpleName();
            if (!kind.matches("[A-Za-z0-9_]+")) kind = "Exception";
            mEntries.addLast(new JSONObject().put("at", System.currentTimeMillis()).put("channel", name).put("error", kind));
            while (mEntries.size() > LIMIT) mEntries.removeFirst();
        } catch (Exception ignored) { }
    }

    synchronized JSONObject view() throws Exception {
        JSONArray rows = new JSONArray();
        for (JSONObject entry : mEntries) rows.put(entry);
        return new JSONObject().put("ok", true).put("entries", rows)
                .put("message", rows.length() == 0 ? "No Android browser errors have been recorded in this session." : "Recent Android browser errors. Messages, URLs and paths are not recorded.");
    }
}
