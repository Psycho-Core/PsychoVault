package com.psychocore.psychovault.experimental;

import android.annotation.SuppressLint;
import android.graphics.Color;
import android.net.Uri;
import android.view.View;
import android.webkit.WebChromeClient;
import android.webkit.WebResourceRequest;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.FrameLayout;
import com.getcapacitor.JSObject;
import com.getcapacitor.Plugin;
import com.getcapacitor.PluginCall;
import com.getcapacitor.annotation.CapacitorPlugin;
import com.getcapacitor.PluginMethod;
import java.util.LinkedHashMap;
import java.util.Map;

/** Experimental browser views. No Capacitor bridge is installed in website views. */
@CapacitorPlugin(name = "PsychoBrowser")
public class BrowserPlugin extends Plugin {
    private final Map<String, WebView> tabs = new LinkedHashMap<>();
    private String active;

    private boolean allowed(String url) {
        if (url == null) return false;
        Uri uri = Uri.parse(url);
        String scheme = uri.getScheme();
        return ("https".equalsIgnoreCase(scheme) || "http".equalsIgnoreCase(scheme))
            && uri.getHost() != null && !uri.getHost().isEmpty();
    }

    private void event(String id, String url, String title) {
        JSObject data = new JSObject();
        data.put("id", id);
        data.put("url", url == null ? "" : url);
        data.put("title", title == null ? "" : title);
        notifyListeners("pageChanged", data);
    }

    @SuppressLint("SetJavaScriptEnabled")
    private WebView newPage(String id) {
        WebView page = new WebView(getContext());
        page.setBackgroundColor(Color.WHITE);
        page.getSettings().setJavaScriptEnabled(true);
        page.getSettings().setDomStorageEnabled(true);
        page.getSettings().setAllowFileAccess(false);
        page.getSettings().setAllowContentAccess(false);
        page.getSettings().setJavaScriptCanOpenWindowsAutomatically(false);
        page.getSettings().setSupportMultipleWindows(false);
        page.setWebViewClient(new WebViewClient() {
            @Override public boolean shouldOverrideUrlLoading(WebView view, WebResourceRequest request) {
                return !allowed(request.getUrl().toString());
            }
            @Override public void onPageFinished(WebView view, String url) { event(id, url, view.getTitle()); }
        });
        // Default WebChromeClient denies permission requests and does not implement file uploads.
        page.setWebChromeClient(new WebChromeClient() {
            @Override public void onReceivedTitle(WebView view, String title) { event(id, view.getUrl(), title); }
        });
        page.setVisibility(View.GONE);
        ((FrameLayout) getActivity().findViewById(android.R.id.content)).addView(page,
            new FrameLayout.LayoutParams(1, 1));
        return page;
    }

    @PluginMethod public void create(PluginCall call) {
        String id = call.getString("id");
        if (id == null || id.isEmpty()) { call.reject("Invalid tab ID"); return; }
        getActivity().runOnUiThread(() -> {
            if (tabs.containsKey(id)) { call.reject("Invalid tab ID"); return; }
            tabs.put(id, newPage(id));
            activateId(id);
            call.resolve();
        });
    }

    private void activateId(String id) {
        active = id;
        for (Map.Entry<String, WebView> entry : tabs.entrySet())
            entry.getValue().setVisibility(entry.getKey().equals(id) ? View.VISIBLE : View.GONE);
    }

    @PluginMethod public void activate(PluginCall call) {
        String id = call.getString("id");
        getActivity().runOnUiThread(() -> {
            if (!tabs.containsKey(id)) { call.reject("Unknown tab"); return; }
            activateId(id); call.resolve();
        });
    }

    @PluginMethod public void close(PluginCall call) {
        String id = call.getString("id");
        getActivity().runOnUiThread(() -> {
            WebView page = tabs.remove(id);
            if (page == null) { call.reject("Unknown tab"); return; }
            ((FrameLayout) getActivity().findViewById(android.R.id.content)).removeView(page);
            page.destroy();
            if (id.equals(active)) active = null;
            call.resolve();
        });
    }

    @PluginMethod public void navigate(PluginCall call) {
        String url = call.getString("url");
        String id = call.getString("id");
        if (!allowed(url)) { call.reject("Only HTTP and HTTPS URLs are supported"); return; }
        getActivity().runOnUiThread(() -> {
            WebView page = tabs.get(id);
            if (page == null) { call.reject("No active tab"); return; }
            page.loadUrl(url); call.resolve();
        });
    }

    @PluginMethod public void command(PluginCall call) {
        String command = call.getString("command", "");
        String id = call.getString("id");
        getActivity().runOnUiThread(() -> {
            WebView page = tabs.get(id);
            if (page == null) { call.reject("No active tab"); return; }
            switch (command) {
                case "back": if (page.canGoBack()) page.goBack(); break;
                case "forward": if (page.canGoForward()) page.goForward(); break;
                case "reload": page.reload(); break;
                default: call.reject("Unsupported command"); return;
            }
            call.resolve();
        });
    }

    @PluginMethod public void bounds(PluginCall call) {
        Float x = call.getFloat("x"), y = call.getFloat("y"), w = call.getFloat("width"), h = call.getFloat("height");
        if (x == null || y == null || w == null || h == null || w < 0 || h < 0 || w > 10000 || h > 10000) {
            call.reject("Invalid bounds"); return;
        }
        getActivity().runOnUiThread(() -> {
            // CSS coordinates are relative to the Capacitor view. Include its position in the Android root.
            View ui = getBridge().getWebView();
            FrameLayout root = getActivity().findViewById(android.R.id.content);
            int[] uiPos = new int[2], rootPos = new int[2];
            ui.getLocationOnScreen(uiPos); root.getLocationOnScreen(rootPos);
            float density = getResources().getDisplayMetrics().density;
            for (WebView page : tabs.values()) {
                FrameLayout.LayoutParams params = new FrameLayout.LayoutParams(
                    Math.max(1, Math.round(w * density)), Math.max(1, Math.round(h * density)));
                params.leftMargin = uiPos[0] - rootPos[0] + Math.round(x * density);
                params.topMargin = uiPos[1] - rootPos[1] + Math.round(y * density);
                page.setLayoutParams(params);
            }
            call.resolve();
        });
    }

    @Override protected void handleOnDestroy() {
        for (WebView page : tabs.values()) page.destroy();
        tabs.clear();
        super.handleOnDestroy();
    }
}
