// Copyright 2026 Psycho-Core. See the repository licence.
package org.chromium.chrome.browser;

import org.chromium.base.ThreadUtils;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.tab.TabLaunchType;
import org.chromium.chrome.browser.tab.TabSelectionType;
import org.chromium.chrome.browser.tabmodel.ChromeTabCreator;
import org.chromium.chrome.browser.tabmodel.TabClosureParams;
import org.chromium.chrome.browser.tabmodel.TabModel;
import org.chromium.chrome.browser.tabmodel.TabModelSelector;
import org.chromium.content_public.browser.LoadUrlParams;
import org.chromium.url.GURL;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.util.function.Supplier;
import java.util.List;
import java.util.concurrent.CopyOnWriteArrayList;

/** Native tab operations for the browser chrome, not an API for web content.
 *
 * The caller MUST authenticate a dedicated trusted UI document and marshal calls
 * onto Chromium's UI thread. Do not expose this object or its methods to arbitrary
 * pages or extension content. Every operation uses only the selected tab model;
 * an ID from another profile cannot be activated or closed through this controller.
 */
public final class PsychoTabController {
    private final TabModelSelector mSelector;
    private final Supplier<ChromeTabCreator> mCreator;
    private final List<Runnable> mListeners = new CopyOnWriteArrayList<>();

    public PsychoTabController(TabModelSelector selector, Supplier<ChromeTabCreator> creator) {
        if (selector == null || creator == null) throw new IllegalArgumentException("Tab model unavailable");
        mSelector = selector;
        mCreator = creator;
    }

    private TabModel model() {
        ThreadUtils.assertOnUiThread();
        return mSelector.getCurrentModel();
    }

    // Only already-resolved web URLs enter this method. Address-bar text and
    // internal URLs must be resolved by their own trusted policy before invoking it.
    private static String requireWebUrl(String raw) {
        if (raw == null || raw.length() > 8192) throw new IllegalArgumentException("Invalid URL");
        GURL url = new GURL(raw);
        if (!url.isValid() || !("https".equals(url.getScheme()) || "http".equals(url.getScheme()))) {
            throw new IllegalArgumentException("Only HTTP and HTTPS URLs are accepted");
        }
        return url.getSpec();
    }

    private String requireNavigationUrl(String raw) {
        boolean incognito = model().isIncognito();
        String suffix = "?private=" + (incognito ? "1" : "0");
        if (("chrome://psychovault-pages/newtab.html" + suffix).equals(raw)
                || ("chrome://psychovault-pages/settings.html" + suffix).equals(raw)) return raw;
        return requireWebUrl(raw);
    }

    /** Create a tab in the currently selected regular or incognito model. */
    public int openWebTab(String raw) {
        TabModel current = model();
        String url = requireNavigationUrl(raw);
        Tab tab = mCreator.get().createNewTab(new LoadUrlParams(url), TabLaunchType.FROM_CHROME_UI, current.getTabAt(current.index()));
        if (tab == null) throw new IllegalStateException("Chromium did not create a tab");
        return tab.getId();
    }

    /** Open PsychoVault's internal new-tab page in the selected profile. */
    public void openNewTab() {
        model();
        Tab tab = mCreator.get().createNewTab(new LoadUrlParams(requireNavigationUrl("")), TabLaunchType.FROM_CHROME_UI, null);
        if (tab == null) throw new IllegalStateException("Chromium did not create a tab");
    }

    public boolean activate(int id) {
        TabModel current = model();
        Tab tab = current.getTabById(id);
        if (tab == null) return false;
        int index = current.indexOf(tab);
        if (index < 0) return false;
        current.setIndex(index, TabSelectionType.FROM_USER);
        return true;
    }

    public boolean close(int id) {
        TabModel current = model();
        Tab tab = current.getTabById(id);
        if (tab == null) return false;
        current.getTabRemover().closeTabs(TabClosureParams.closeTab(tab).build(), true);
        return true; // Chromium may still ask for confirmation.
    }

    public boolean navigate(String raw) {
        Tab tab = currentTab();
        if (tab == null) return false;
        tab.loadUrl(new LoadUrlParams(requireNavigationUrl(raw)));
        return true;
    }

    public boolean back() {
        Tab tab = currentTab();
        if (tab == null || !tab.canGoBack()) return false;
        tab.goBack(); return true;
    }

    public boolean forward() {
        Tab tab = currentTab();
        if (tab == null || !tab.canGoForward()) return false;
        tab.goForward(); return true;
    }

    public boolean reload() {
        Tab tab = currentTab();
        if (tab == null) return false;
        tab.reload(); return true;
    }

    public boolean stop() {
        Tab tab = currentTab();
        if (tab == null) return false;
        tab.stopLoading(); return true;
    }

    public boolean navigateTab(int id, String raw) {
        Tab tab = model().getTabById(id);
        if (tab == null) return false;
        tab.loadUrl(new LoadUrlParams(requireNavigationUrl(raw)));
        return true;
    }

    public Runnable subscribe(Runnable listener) {
        mListeners.add(listener);
        return () -> mListeners.remove(listener);
    }

    public void stateChanged() {
        ThreadUtils.assertOnUiThread();
        for (Runnable listener : mListeners) listener.run();
    }

    private Tab currentTab() {
        model();
        return mSelector.getCurrentTab();
    }

    /** Dispatch the implemented desktop bridge channels. Caller must authenticate origin first. */
    public Object invoke(String channel, JSONArray args) throws JSONException {
        model(); // All operations and argument checks run on the browser UI thread.
        if (channel == null || args == null) throw new IllegalArgumentException("Invalid request");
        switch (channel) {
            case "tab:new":
                requireCount(args, 1);
                if (args.isNull(0) || "".equals(args.optString(0))) {
                    openNewTab(); return JSONObject.NULL;
                }
                return openWebTab(requireString(args, 0));
            case "tab:open":
                requireCount(args, 1); return openWebTab(requireString(args, 0));
            case "tab:activate":
                requireCount(args, 1); return activate(requireTabId(args, 0));
            case "tab:close":
                requireCount(args, 1); return close(requireTabId(args, 0));
            case "nav:go":
                requireCount(args, 1); return navigate(requireString(args, 0));
            case "nav:home":
                requireCount(args, 0); return navigate("psychovault://newtab");
            case "nav:back":
                requireCount(args, 0); return back();
            case "nav:forward":
                requireCount(args, 0); return forward();
            case "nav:reload":
                requireCount(args, 0); return reload();
            case "nav:stop":
                requireCount(args, 0); return stop();
            default:
                throw new IllegalArgumentException("Unsupported browser operation");
        }
    }

    private static void requireCount(JSONArray args, int expected) {
        if (args.length() != expected) throw new IllegalArgumentException("Invalid argument count");
    }

    private static String requireString(JSONArray args, int index) throws JSONException {
        Object value = args.get(index);
        if (!(value instanceof String)) throw new IllegalArgumentException("Expected a string");
        return (String) value;
    }

    private static int requireTabId(JSONArray args, int index) throws JSONException {
        Object value = args.get(index);
        if (!(value instanceof Number)) throw new IllegalArgumentException("Expected a tab ID");
        double number = ((Number) value).doubleValue();
        if (number < 0 || number > Integer.MAX_VALUE || number != Math.floor(number)) {
            throw new IllegalArgumentException("Invalid tab ID");
        }
        return (int) number;
    }

    /** Snapshot is intentionally restricted to the selected profile. */
    public JSONObject snapshot() throws JSONException {
        TabModel current = model();
        Tab active = mSelector.getCurrentTab();
        JSONArray tabs = new JSONArray();
        for (int i = 0; i < current.getCount(); i++) {
            Tab tab = current.getTabAt(i);
            if (tab == null) continue;
            JSONObject item = new JSONObject();
            item.put("id", tab.getId());
            item.put("url", tab.getUrl().getSpec());
            item.put("title", tab.getTitle());
            item.put("loading", tab.isLoading());
            tabs.put(item);
        }
        JSONObject state = new JSONObject();
        state.put("tabs", tabs);
        state.put("activeId", active == null ? JSONObject.NULL : active.getId());
        state.put("url", active == null ? "" : active.getUrl().getSpec());
        state.put("loading", active != null && active.isLoading());
        state.put("canGoBack", active != null && active.canGoBack());
        state.put("canGoForward", active != null && active.canGoForward());
        state.put("private", current.isIncognito());
        return state;
    }
}
