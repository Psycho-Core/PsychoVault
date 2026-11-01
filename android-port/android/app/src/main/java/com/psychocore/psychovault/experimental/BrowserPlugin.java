package com.psychocore.psychovault.experimental;

import android.annotation.SuppressLint;
import android.app.Activity;
import android.app.DownloadManager;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Environment;
import android.graphics.Color;
import android.graphics.Bitmap;
import android.net.Uri;
import android.view.View;
import android.view.MotionEvent;
import android.webkit.WebChromeClient;
import android.webkit.ValueCallback;
import android.webkit.URLUtil;
import android.webkit.WebResourceRequest;
import android.webkit.WebResourceResponse;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.FrameLayout;
import androidx.activity.result.ActivityResultLauncher;
import androidx.activity.result.contract.ActivityResultContracts;
import com.getcapacitor.JSObject;
import com.getcapacitor.Plugin;
import com.getcapacitor.PluginCall;
import com.getcapacitor.annotation.CapacitorPlugin;
import com.getcapacitor.PluginMethod;
import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.net.IDN;
import java.nio.charset.StandardCharsets;
import java.util.Collections;
import java.util.HashSet;
import java.util.Locale;
import java.util.Set;
import java.util.concurrent.atomic.AtomicReference;
import org.json.JSONArray;
import java.util.LinkedHashMap;
import java.util.Map;

/** Experimental browser views. No Capacitor bridge is installed in website views. */
@CapacitorPlugin(name = "PsychoBrowser")
public class BrowserPlugin extends Plugin {
    private final Map<String, WebView> tabs = new LinkedHashMap<>();
    private String active;
    private volatile BlockPolicy blockPolicy = new BlockPolicy(false, Collections.emptySet(), Collections.emptySet());

    private static class BlockPolicy {
        final boolean enabled;
        final Set<String> blocked, allowed;
        BlockPolicy(boolean enabled, Set<String> blocked, Set<String> allowed) {
            this.enabled = enabled;
            this.blocked = Collections.unmodifiableSet(new HashSet<>(blocked));
            this.allowed = Collections.unmodifiableSet(new HashSet<>(allowed));
        }
        boolean blocks(String request, String page) {
            if (!enabled) return false;
            String pageHost = host(page), host = host(request);
            if (host.isEmpty() || allowed.contains(pageHost)) return false;
            while (!host.isEmpty()) {
                if (blocked.contains(host)) return true;
                int dot = host.indexOf('.');
                if (dot < 0) break;
                String parent = host.substring(dot + 1);
                if (!parent.contains(".")) break;
                host = parent;
            }
            return false;
        }
    }
    private static String host(String value) {
        if (value == null || value.trim().isEmpty() || value.matches(".*[\\s@*].*")) return "";
        try {
            String text = value.trim();
            Uri uri = Uri.parse(text.contains("://") ? text : "https://" + text);
            if (!("https".equalsIgnoreCase(uri.getScheme()) || "http".equalsIgnoreCase(uri.getScheme()))
                    || uri.getUserInfo() != null || uri.getHost() == null) return "";
            String host = IDN.toASCII(uri.getHost()).toLowerCase(Locale.ROOT);
            return host.endsWith(".") ? host.substring(0, host.length() - 1) : host;
        } catch (RuntimeException ex) { return ""; }
    }
    private static Set<String> hostList(String text) {
        Set<String> result = new HashSet<>();
        for (String part : text.split("[\\n,]+")) {
            String host = host(part);
            if (!host.isEmpty()) result.add(host);
        }
        return result;
    }
    @PluginMethod public void configureBlocker(PluginCall call) {
        boolean enabled = call.getBoolean("blockAds", true);
        boolean bundled = call.getBoolean("blockUseBundled", true);
        Set<String> blocked = hostList(call.getString("blockCustomDomains", ""));
        if (enabled && bundled) {
            try (InputStream input = getContext().getAssets().open("public/blocklist.json");
                 ByteArrayOutputStream bytes = new ByteArrayOutputStream()) {
                byte[] buffer = new byte[4096]; int n;
                while ((n = input.read(buffer)) != -1) bytes.write(buffer, 0, n);
                JSONArray list = new JSONArray(new String(bytes.toByteArray(), StandardCharsets.UTF_8));
                for (int i = 0; i < list.length(); i++) blocked.addAll(hostList(list.getString(i)));
            } catch (Exception ex) {
                blockPolicy = new BlockPolicy(false, Collections.emptySet(), Collections.emptySet());
                call.reject("Could not load the bundled blocker list"); return;
            }
        }
        blockPolicy = new BlockPolicy(enabled, blocked, hostList(call.getString("blockAllowSites", "")));
        call.resolve();
    }
    private ActivityResultLauncher<Intent> filePicker;
    private ValueCallback<Uri[]> uploadCallback;
    private String uploadTab;
    private boolean pickerInFlight;

    @Override public void load() {
        // A dynamic registry entry does not expose a bridge to the website WebViews.
        filePicker = getActivity().getActivityResultRegistry().register(
            "psychovault-upload", new ActivityResultContracts.StartActivityForResult(), result -> {
                pickerInFlight = false;
                Uri[] selected = null;
                Intent data = result.getData();
                if (result.getResultCode() == Activity.RESULT_OK && data != null
                        && (data.getFlags() & Intent.FLAG_GRANT_READ_URI_PERMISSION) != 0) {
                    Uri[] candidates = WebChromeClient.FileChooserParams.parseResult(result.getResultCode(), data);
                    if (candidates != null && candidates.length > 0 && candidates.length <= 100) {
                        boolean safe = true;
                        for (Uri uri : candidates) {
                            if (uri == null || !"content".equalsIgnoreCase(uri.getScheme())
                                    || uri.getAuthority() == null
                                    || uri.getAuthority().equals(getContext().getPackageName() + ".fileprovider")
                                    || getContext().checkUriPermission(uri, android.os.Process.myPid(), android.os.Process.myUid(),
                                        Intent.FLAG_GRANT_READ_URI_PERMISSION) != PackageManager.PERMISSION_GRANTED) {
                                safe = false; break;
                            }
                        }
                        if (safe) selected = candidates;
                    }
                }
                finishUpload(selected);
            });
    }

    private void finishUpload(Uri[] selected) {
        ValueCallback<Uri[]> callback = uploadCallback;
        uploadCallback = null;
        uploadTab = null;
        if (callback != null) callback.onReceiveValue(selected);
    }

    private void message(String text) {
        JSObject data = new JSObject();
        data.put("message", text);
        notifyListeners("browserNotice", data);
    }

    private void download(String url, String contentDisposition, String mimeType) {
        if (!allowed(url) || Uri.parse(url).getUserInfo() != null) {
            message("This download type is not supported."); return;
        }
        try {
            DownloadManager manager = (DownloadManager) getContext().getSystemService(Context.DOWNLOAD_SERVICE);
            if (manager == null) throw new IllegalStateException("Download service unavailable");
            String name = URLUtil.guessFileName(url, contentDisposition, mimeType)
                .replaceAll("[\\\\/\\p{Cntrl}]", "_");
            if (name.isEmpty() || name.equals(".") || name.equals("..")) name = "download";
            if (name.length() > 120) name = name.substring(0, 120);
            DownloadManager.Request request = new DownloadManager.Request(Uri.parse(url));
            request.setTitle(name);
            request.setNotificationVisibility(DownloadManager.Request.VISIBILITY_VISIBLE_NOTIFY_COMPLETED);
            // Public Downloads requires storage permission on Android 9 and earlier.
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q)
                request.setDestinationInExternalPublicDir(Environment.DIRECTORY_DOWNLOADS, name);
            else
                request.setDestinationInExternalFilesDir(getContext(), Environment.DIRECTORY_DOWNLOADS, name);
            // Do not forward cookies or authorization headers to a download service that can redirect.
            manager.enqueue(request);
            message(Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q
                ? "Download started. Check Android notifications or Downloads. Sign-in downloads may fail."
                : "Download started in app storage. Removing the app deletes it. Sign-in downloads may fail.");
        } catch (RuntimeException ex) {
            message("Could not start the download.");
        }
    }

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
        AtomicReference<String> topUrl = new AtomicReference<>("");
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
            @Override public void onPageStarted(WebView view, String url, Bitmap favicon) { topUrl.set(url == null ? "" : url); }
            @Override public WebResourceResponse shouldInterceptRequest(WebView view, WebResourceRequest request) {
                String requestUrl = request.getUrl().toString();
                String pageUrl = request.isForMainFrame() ? requestUrl : topUrl.get();
                if (blockPolicy.blocks(requestUrl, pageUrl))
                    return new WebResourceResponse("text/plain", "UTF-8", new ByteArrayInputStream(new byte[0]));
                return null;
            }
            @Override public void onPageFinished(WebView view, String url) { event(id, url, view.getTitle()); }
        });
        // Default WebChromeClient still denies website permission requests.
        page.setWebChromeClient(new WebChromeClient() {
            @Override public void onReceivedTitle(WebView view, String title) { event(id, view.getUrl(), title); }
            @Override public boolean onShowFileChooser(WebView view, ValueCallback<Uri[]> callback, FileChooserParams params) {
                if (pickerInFlight || filePicker == null || (params.getMode() != FileChooserParams.MODE_OPEN
                        && params.getMode() != FileChooserParams.MODE_OPEN_MULTIPLE)) {
                    callback.onReceiveValue(null); return true;
                }
                try {
                    Intent intent = params.createIntent();
                    uploadCallback = callback;
                    uploadTab = id;
                    pickerInFlight = true;
                    filePicker.launch(intent);
                } catch (RuntimeException ex) {
                    pickerInFlight = false;
                    if (uploadCallback == null) callback.onReceiveValue(null); else finishUpload(null);
                    message("Could not open the Android file picker.");
                }
                return true;
            }
        });
        page.setDownloadListener((url, userAgent, disposition, mime, length) -> download(url, disposition, mime));
        page.setOnTouchListener((view, motion) -> {
            if (motion.getActionMasked() == MotionEvent.ACTION_DOWN) {
                notifyListeners("pageTouched", new JSObject());
            }
            return false;
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
            if (id != null && id.equals(uploadTab)) finishUpload(null);
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
            float density = getContext().getResources().getDisplayMetrics().density;
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
        finishUpload(null);
        if (filePicker != null) filePicker.unregister();
        for (WebView page : tabs.values()) page.destroy();
        tabs.clear();
        super.handleOnDestroy();
    }
}
