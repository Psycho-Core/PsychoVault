package org.chromium.chrome.browser.psychovault;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.BroadcastReceiver;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.graphics.Color;
import android.graphics.Rect;
import android.net.Uri;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;

import androidx.activity.OnBackPressedCallback;

import org.json.JSONArray;
import org.json.JSONObject;
import org.chromium.base.ThreadUtils;
import org.chromium.base.version_info.VersionInfo;
import org.chromium.chrome.browser.browsing_data.BrowsingDataBridge;
import org.chromium.chrome.browser.browsing_data.BrowsingDataType;
import org.chromium.chrome.browser.browsing_data.TimePeriod;
import org.chromium.chrome.browser.compositor.CompositorViewHolder;
import org.chromium.chrome.browser.content.WebContentsFactory;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.tab.TabObserver;
import org.chromium.chrome.browser.tabmodel.TabClosureParams;
import org.chromium.chrome.browser.tabmodel.TabModel;
import org.chromium.chrome.browser.tabmodel.TabModelObserver;
import org.chromium.chrome.browser.tabmodel.TabModelSelector;
import org.chromium.components.embedder_support.view.ContentView;
import org.chromium.components.origin_matcher.OriginMatcher;
import org.chromium.content.browser.JavascriptInterface;
import org.chromium.content_public.browser.JavascriptInjector;
import org.chromium.content_public.browser.LoadUrlParams;
import org.chromium.content_public.browser.NavigationHandle;
import org.chromium.content_public.browser.Visibility;
import org.chromium.content_public.browser.WebContents;
import org.chromium.content_public.browser.WebContentsObserver;
import org.chromium.ui.base.ViewAndroidDelegate;
import org.chromium.url.GURL;

import java.util.ArrayList;
import java.util.ArrayDeque;
import java.util.Deque;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

/** Trusted desktop chrome, kept outside the website TabModel and outside website documents. */
public final class PsychoChromeHost implements PsychoBridge.Handler {
    public static final String CHROME_ORIGIN = "chrome://psychovault-chrome";
    public static final String PAGE_ORIGIN = "chrome://psychovault-pages";
    private static final int MAX_REGIONS = 64;
    private final Activity mActivity;
    private final TabModelSelector mSelector;
    private final CompositorViewHolder mCompositor;
    private final PsychoTabController mController;
    private final PsychoProfileStore mStore;
    private final PsychoExtensions mExtensions;
    private final PsychoPerformance mPerformance;
    private final FrameLayout mOverlay;
    private final Map<Boolean, Session> mSessions = new HashMap<>();
    private final Map<Integer, PageBinding> mPages = new HashMap<>();
    private final Map<Integer, WebContentsObserver> mNavigationObservers = new HashMap<>();
    private final Set<Integer> mObservedTabs = new HashSet<>();
    private final List<Rect> mHitRegions = new ArrayList<>();
    private final Deque<String> mClosedTabs = new ArrayDeque<>();
    private Session mActive;
    private Profile mServicesProfile;
    private PsychoWorkspaces mWorkspaces;
    private PsychoSessions mRecovery;
    private PsychoVaultStore mVault;
    private PsychoDownloads mDownloads;
    private PsychoContainers mContainers;
    private PsychoMedia mMedia;
    private String mLastUpdateRelease = "";
    private boolean mDestroyed, mPublishQueued, mTouchInChrome, mWorkspaceBusy, mDownloadQueued;
    private int mTouchPointer = -1, mTop, mLeft, mBottom, mShowingToken = -1, mPanicTabId = Tab.INVALID_TAB_ID;
    private JSONArray mPanicTabs;
    private final OnBackPressedCallback mBack = new OnBackPressedCallback(false) {
        @Override public void handleOnBackPressed() { if (mActive != null) mActive.bridge.emit("native-back", new JSONObject()); }
    };
    private final BroadcastReceiver mPowerReceiver = new BroadcastReceiver() {
        @Override public void onReceive(Context context, Intent intent) {
            if (mDestroyed || intent == null || intent.getAction() == null) return;
            boolean onBattery = Intent.ACTION_POWER_DISCONNECTED.equals(intent.getAction());
            if (!onBattery && !Intent.ACTION_POWER_CONNECTED.equals(intent.getAction())) return;
            try {
                JSONObject settings = mStore.read(profile());
                if (!settings.optBoolean("batteryAuto", false)) return;
                if (settings.optBoolean("batterySaver", false) == onBattery) return;
                saveSettings(new JSONObject().put("batterySaver", onBattery));
            } catch (Exception ignored) { /* Private profiles do not persist power changes. */ }
        }
    };
    private final Runnable mPublish = () -> { mPublishQueued = false; publish(); };
    private final Runnable mDownloadPublish = () -> { mDownloadQueued = false; publishDownloads(); };
    private final TabModelObserver mModelObserver = new TabModelObserver() {
        @Override public void didAddTab(Tab tab, int type, int creationState, boolean selected) {
            observeTab(tab); bindPage(tab);
            if (!mWorkspaceBusy && mWorkspaces != null) try { mWorkspaces.assign(tab); } catch (Exception ignored) { }
            changed();
        }
        @Override public void didSelectTab(Tab tab, int type, int lastId) { changed(); }
        @Override public void didMoveTab(Tab tab, int newIndex, int curIndex) { changed(); }
        @Override public void onTabCloseCommitted(List<Tab> tabs, boolean all, boolean restore, int source) { tabsClosed(tabs); }
        @Override public void onFinishingMultipleTabClosure(List<Tab> tabs, boolean restore) { tabsClosed(tabs); }
        @Override public void onDidActiveStateChange(TabModel model, boolean active) { if (active) selectSession(); changed(); }
    };
    private final TabObserver mTabObserver = new TabObserver() {
        @Override public void onContentChanged(Tab tab) { unbindPage(tab.getId()); unobserveNavigation(tab.getId()); mObservedTabs.remove(tab.getId()); mController.clearFind(tab.getId()); observeTab(tab); bindPage(tab); changed(); }
        @Override public void onUrlUpdated(Tab tab) { changed(); }
        @Override public void onTitleUpdated(Tab tab) { changed(); }
        @Override public void onLoadStarted(Tab tab, boolean differentDocument) { changed(); }
        @Override public void onLoadStopped(Tab tab, boolean differentDocument) { changed(); }
        @Override public void onDidFinishNavigationInPrimaryMainFrame(Tab tab, NavigationHandle navigation) { if (mPerformance != null) try { mPerformance.enhance(tab, mStore.read(profile())); } catch (Exception ignored) { } }
        @Override public void onDestroyed(Tab tab) { unbindPage(tab.getId()); unobserveNavigation(tab.getId()); mObservedTabs.remove(tab.getId()); mController.clearFind(tab.getId()); changed(); }
    };

    private static final class Session {
        final WebContents contents; final ContentView view; final ViewAndroidDelegate delegate;
        final PsychoBridge bridge; final OriginMatcher matcher;
        Session(WebContents c, ContentView v, ViewAndroidDelegate d, PsychoBridge b, OriginMatcher m) { contents = c; view = v; delegate = d; bridge = b; matcher = m; }
    }
    private static final class PageBinding {
        final PsychoBridge bridge; final OriginMatcher matcher;
        PageBinding(PsychoBridge b, OriginMatcher m) { bridge = b; matcher = m; }
    }

    public PsychoChromeHost(Activity activity, TabModelSelector selector, CompositorViewHolder compositor) {
        ThreadUtils.assertOnUiThread();
        mActivity = activity; mSelector = selector; mCompositor = compositor;
        mController = new PsychoTabController(selector, () -> ((org.chromium.chrome.browser.ChromeTabbedActivity) activity).getCurrentTabCreator());
        mStore = new PsychoProfileStore(activity); mExtensions = new PsychoExtensions(activity); mPerformance = new PsychoPerformance(activity);
        mExtensions.setListener(this::changed);
        mOverlay = new FrameLayout(activity) {
            @Override public boolean dispatchTouchEvent(MotionEvent event) {
                int action = event.getActionMasked();
                if (action == MotionEvent.ACTION_DOWN) {
                    mTouchPointer = event.getPointerId(0); mTouchInChrome = hit(Math.round(event.getX()), Math.round(event.getY()));
                    if (!mTouchInChrome && mActive != null) mActive.bridge.emit("page-touch", new JSONObject());
                } else if (action == MotionEvent.ACTION_POINTER_DOWN && !mTouchInChrome) {
                    int index = event.getActionIndex();
                    if (hit(Math.round(event.getX(index)), Math.round(event.getY(index)))) return false;
                } else if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_CANCEL) {
                    boolean handled = mTouchInChrome && super.dispatchTouchEvent(event); mTouchInChrome = false; mTouchPointer = -1; return handled;
                }
                return mTouchInChrome && super.dispatchTouchEvent(event);
            }
        };
        mOverlay.setBackgroundColor(Color.TRANSPARENT);
        mOverlay.addOnLayoutChangeListener((v, l, t, r, b, ol, ot, or, ob) -> { for (Session session : mSessions.values()) session.contents.setSize(r - l, b - t); });
        selector.addObserverToAllModels(mModelObserver);
        for (TabModel model : selector.getModels()) for (int j = 0; j < model.getCount(); j++) { observeTab(model.getTabAt(j)); bindPage(model.getTabAt(j)); }
        mShowingToken = compositor.getBrowserControlsManager().getBrowserVisibilityDelegate().showControlsPersistent();
        ViewGroup root = activity.findViewById(android.R.id.content);
        root.addView(mOverlay, new FrameLayout.LayoutParams(-1, -1));
        activity.getOnBackPressedDispatcher().addCallback(activity, mBack);
        IntentFilter power = new IntentFilter();
        power.addAction(Intent.ACTION_POWER_CONNECTED);
        power.addAction(Intent.ACTION_POWER_DISCONNECTED);
        try { activity.registerReceiver(mPowerReceiver, power); } catch (Exception ignored) { }
        selectSession();
    }

    private void clearOnExitIfNeeded() {
        if (!mActivity.isFinishing()) return;
        try {
            Profile profile = profile();
            if (profile == null || profile.isOffTheRecord()) return;
            String mode = mStore.read(profile).optString("clearOnExit", "nothing");
            if ("nothing".equals(mode)) return;
            int[] types = "all".equals(mode)
                    ? new int[] {BrowsingDataType.HISTORY, BrowsingDataType.CACHE, BrowsingDataType.SITE_DATA,
                            BrowsingDataType.SITE_SETTINGS, BrowsingDataType.FORM_DATA}
                    : new int[] {BrowsingDataType.CACHE};
            BrowsingDataBridge.getForProfile(profile).clearBrowsingData(null, types, TimePeriod.ALL_TIME);
        } catch (Exception ignored) { /* Exit cleanup is best-effort; never block activity teardown. */ }
    }

    private boolean hit(int x, int y) { for (Rect region : mHitRegions) if (region.contains(x, y)) return true; return false; }
    private Profile profile() { return mSelector.getCurrentModel().getProfile(); }
    private TabModel model() { return mSelector.getCurrentModel(); }
    private Tab currentTab() { return mSelector.getCurrentTab(); }

    private void selectSession() {
        if (mDestroyed) return;
        Profile profile = profile(); if (profile == null) return;
        if (profile != mServicesProfile) switchServices(profile);
        boolean incognito = profile.isOffTheRecord(); Session session = mSessions.get(incognito);
        if (session == null) {
            WebContents contents = WebContentsFactory.createWebContents(profile, false, true);
            ContentView view = ContentView.createContentView(mActivity, contents);
            ViewAndroidDelegate delegate = ViewAndroidDelegate.createBasicDelegate(view);
            contents.setDelegates(VersionInfo.getProductVersion(), delegate, view,
                    ((org.chromium.chrome.browser.app.ChromeActivity) mActivity).getWindowAndroid(), WebContents.createDefaultInternalsHolder());
            PsychoBridge bridge = new PsychoBridge(contents, this::profile, this, true, Tab.INVALID_TAB_ID);
            OriginMatcher matcher = origin(CHROME_ORIGIN); JavascriptInjector injector = JavascriptInjector.fromWebContents(contents);
            if (injector == null) { matcher.destroy(); bridge.destroy(); delegate.destroy(); contents.destroy(); throw new IllegalStateException("Chrome UI injector unavailable"); }
            injector.addPossiblyUnsafeInterfaceToOrigins(bridge, "PsychoVaultNative", JavascriptInterface.class, matcher);
            session = new Session(contents, view, delegate, bridge, matcher); mSessions.put(incognito, session);
            view.setLayoutParams(new FrameLayout.LayoutParams(-1, -1)); mOverlay.addView(view);
            contents.getNavigationController().loadUrl(new LoadUrlParams(CHROME_ORIGIN + "/index.html?private=" + (incognito ? 1 : 0)));
        }
        if (mActive != null) { mActive.view.setVisibility(View.GONE); mActive.contents.updateWebContentsVisibility(Visibility.HIDDEN); }
        mActive = session; session.view.setVisibility(View.VISIBLE); session.contents.updateWebContentsVisibility(Visibility.VISIBLE);
        try { if (mWorkspaces != null) { mWorkspaces.bind(model()); mWorkspaces.selectActive(); } applySettings(); } catch (Exception e) { /* The UI reports the failed operation; never invent a saved state. */ }
        changed();
    }

    private void switchServices(Profile profile) {
        if (mDownloads != null) mDownloads.destroy();
        if (mVault != null) mVault.destroy();
        mServicesProfile = profile; mWorkspaces = new PsychoWorkspaces(profile, mStore);
        mRecovery = profile.isOffTheRecord() ? null : new PsychoSessions(mActivity, profile);
        mVault = profile.isOffTheRecord() ? null : createVault(profile);
        mDownloads = new PsychoDownloads(profile, this::queueDownloads, mActivity);
        mContainers = new PsychoContainers(profile);
        mMedia = new PsychoMedia(mActivity, mPerformance);
        mClosedTabs.clear(); mPanicTabs = null; mPanicTabId = Tab.INVALID_TAB_ID;
        if (!profile.isOffTheRecord()) try { mExtensions.ensureBundled(profile, mStore); } catch (Exception e) { /* status reports registry, not a guessed success */ }
    }
    private PsychoVaultStore createVault(Profile profile) { try { return new PsychoVaultStore(mActivity, profile, this::changed); } catch (Exception e) { return null; } }

    private static OriginMatcher origin(String origin) {
        OriginMatcher matcher = new OriginMatcher();
        if (!matcher.setRuleList(List.of(origin)).isEmpty()) { matcher.destroy(); throw new IllegalArgumentException("Invalid trusted origin"); }
        return matcher;
    }
    private void observeTab(Tab tab) {
        if (tab == null || !mObservedTabs.add(tab.getId())) return;
        tab.addObserver(mTabObserver); WebContents contents = tab.getWebContents();
        if (contents == null || contents.isDestroyed()) return;
        final int id = tab.getId();
        mNavigationObservers.put(id, new WebContentsObserver(contents) {
            @Override public void didFinishNavigationInPrimaryMainFrame(NavigationHandle navigation) {
                if (mDestroyed || !navigation.hasCommitted() || navigation.isErrorPage() || navigation.isSameDocument() || !navigation.isInPrimaryMainFrame()) return;
                recordVisit(id, navigation.getUrl().getSpec());
            }
            @Override public void webContentsDestroyed() { observe(null); }
        });
    }
    private void recordVisit(int tabId, String url) {
        try {
            Profile profile = profile(); Tab tab = model().getTabById(tabId);
            if (profile == null || profile.isOffTheRecord() || tab == null || tab.isOffTheRecord()) return;
            if (!mStore.read(profile).optBoolean("remember", true)) return;
            Session regular = mSessions.get(false); if (regular == null) return;
            regular.bridge.emit("library:visit", new JSONObject().put("url", url).put("title", tab.getTitle()));
        } catch (Exception ignored) { }
    }
    private void unobserveNavigation(int id) { WebContentsObserver observer = mNavigationObservers.remove(id); if (observer != null) observer.observe(null); }
    private void bindPage(Tab tab) {
        if (tab == null) return; WebContents contents = tab.getWebContents();
        if (contents == null || contents.isDestroyed() || mPages.containsKey(tab.getId())) return;
        PsychoBridge bridge = new PsychoBridge(contents, this::profile, this, false, tab.getId()); OriginMatcher matcher = origin(PAGE_ORIGIN);
        JavascriptInjector injector = JavascriptInjector.fromWebContents(contents);
        if (injector == null) { matcher.destroy(); bridge.destroy(); return; }
        injector.addPossiblyUnsafeInterfaceToOrigins(bridge, "PsychoVaultNative", JavascriptInterface.class, matcher);
        mPages.put(tab.getId(), new PageBinding(bridge, matcher));
    }
    private void unbindPage(int id) {
        PageBinding binding = mPages.remove(id); if (binding == null) return; binding.bridge.destroy();
        Tab tab = mSelector.getTabById(id); WebContents contents = tab == null ? null : tab.getWebContents();
        JavascriptInjector injector = contents == null || contents.isDestroyed() ? null : JavascriptInjector.fromWebContents(contents);
        if (injector != null) injector.removeInterface("PsychoVaultNative"); binding.matcher.destroy();
    }
    private void tabsClosed(List<Tab> tabs) {
        for (Tab tab : tabs) {
            unbindPage(tab.getId()); mController.clearFind(tab.getId());
            if (mContainers != null) mContainers.tabClosed(tab.getId());
            if (!tab.isOffTheRecord()) { String url = tab.getUrl().getSpec(); if (url.startsWith("http://") || url.startsWith("https://")) { mClosedTabs.addFirst(url); while (mClosedTabs.size() > 20) mClosedTabs.removeLast(); } }
            if (mWorkspaces != null && !mWorkspaceBusy) try { mWorkspaces.tabRemoved(tab); } catch (Exception ignored) { }
        }
        changed();
    }
    private void changed() { if (!mDestroyed && !mPublishQueued) { mPublishQueued = true; mOverlay.post(mPublish); } }
    private void queueDownloads() { if (!mDestroyed && !mDownloadQueued) { mDownloadQueued = true; mOverlay.post(mDownloadPublish); } }
    private void publish() {
        if (mDestroyed) return; JSONObject state = snapshot(Tab.INVALID_TAB_ID);
        if (mActive != null) mActive.bridge.emit("state", state);
        for (PageBinding page : mPages.values()) page.bridge.emit("extensions", new JSONObject());
        publishDownloads();
    }
    private void publishDownloads() { if (mDestroyed || mActive == null || mDownloads == null) return; try { mActive.bridge.emit("downloads", mDownloads.list()); } catch (Exception ignored) { } }

    private void applySettings() throws Exception {
        JSONObject settings = mStore.read(profile()); PsychoNetwork.update(mActivity, profile(), settings);
        mPerformance.update(model(), settings); if (mVault != null) mVault.configure(settings);
        if (mDownloads != null) mDownloads.configure(settings);
        if (mContainers != null) mContainers.update(settings);
        if (mMedia != null) mMedia.update(model(), settings);
        if (mWorkspaces != null) mWorkspaces.bind(model());
    }
    private JSONObject saveSettings(JSONObject patch) throws Exception {
        JSONObject settings = mStore.write(profile(), patch); applySettings(); changed();
        if (mWorkspaces != null && !patch.optBoolean("workspacesEnabled", true)) mWorkspaces.bind(model());
        return settings;
    }

    @Override public JSONObject snapshot(int tabId) {
        if (tabId != Tab.INVALID_TAB_ID) {
            try { return new JSONObject().put("settings", mStore.read(profile())).put("private", profile().isOffTheRecord()); }
            catch (Exception e) { throw new IllegalStateException(e); }
        }
        JSONObject state = mController.snapshot();
        try {
            if (mWorkspaces != null) state = mWorkspaces.filter(state);
            JSONObject settings = mStore.read(profile()); Tab tab = mSelector.getCurrentTab();
            state.put("settings", settings).put("modCss", settings.optString("modCss")).put("title", tab == null ? "New Tab" : tab.getTitle());
            state.put("containers", settings.optJSONArray("containers") == null ? new JSONArray() : settings.optJSONArray("containers"));
            if (mContainers != null) { JSONArray tabs = state.optJSONArray("tabs"); if (tabs != null) for (int i = 0; i < tabs.length(); i++) { JSONObject item = tabs.getJSONObject(i); String container = mContainers.nameFor(item.getInt("id")); item.put("container", container == null ? JSONObject.NULL : container); } }
            state.put("private", profile() != null && profile().isOffTheRecord());
            state.put("nativeTopPx", mCompositor.getBrowserControlsManager().getTopControlsHeight());
            JSONObject network = PsychoNetwork.status(profile(), tab == null ? null : tab.getWebContents());
            state.put("blocked", network.optInt("blocked")).put("blockedTotal", network.optInt("blockedTotal")).put("protonProxyConfigured", network.optBoolean("proxyConfigured"));
            state.put("panicked", mPanicTabs != null);
        } catch (Exception e) { throw new IllegalStateException(e); }
        return state;
    }

    private String pagePath(int tabId) { Tab tab = mSelector.getTabById(tabId); return tab == null ? "" : tab.getUrl().getPath(); }
    private static JSONObject arg(JSONArray args) throws Exception { if (args.length() != 1) throw new IllegalArgumentException("Expected one argument"); return args.getJSONObject(0); }
    private static String str(JSONArray args) throws Exception { if (args.length() != 1 || !(args.get(0) instanceof String)) throw new IllegalArgumentException("Expected a string"); return args.getString(0); }

    @Override public void command(String channel, JSONArray args, int tabId, PsychoBridge.Result result) {
        if (mDestroyed) { result.send(false, "Android chrome is closed"); return; }
        try {
            if (tabId != Tab.INVALID_TAB_ID) { pageCommand(channel, args, tabId, result); return; }
            chromeCommand(channel, args, result);
        } catch (Exception e) { result.send(false, PsychoBridge.safeMessage(e)); }
    }

    private void pageCommand(String channel, JSONArray args, int tabId, PsychoBridge.Result result) throws Exception {
        String path = pagePath(tabId); boolean settings = "/settings.html".equals(path), recovery = "/recovery.html".equals(path);
        if (channel.equals("nav:go")) { result.send(true, mController.navigateTab(tabId, str(args))); changed(); return; }
        if (channel.equals("settings:get")) { result.send(true, mStore.read(profile())); return; }
        if (channel.equals("settings:set") && settings) { result.send(true, saveSettings(arg(args))); return; }
        if (channel.equals("tab:open") && settings) { result.send(true, mController.openWebTab(str(args))); changed(); return; }
        if (channel.equals("downloads:dir") && settings) { result.send(true, PsychoDownloads.directory(mActivity, mStore.read(profile()))); return; }
        if (channel.equals("downloads:pickdir") && settings) { pickDownloadDirectory(result); return; }
        if (channel.equals("update:check") && settings) { checkUpdates(result); return; }
        if (channel.equals("update:dismiss") && settings) { dismissUpdate(args, result); return; }
        if (channel.equals("spell:status") && settings) { result.send(true, new JSONObject().put("enabled", mStore.read(profile()).optBoolean("spellCheck")).put("words", mStore.read(profile()).optJSONArray("spellUserWords"))); return; }
        if ((channel.equals("spell:add") || channel.equals("spell:remove")) && settings) { spellWord(channel, args, result); return; }
        if (channel.startsWith("vault:") && settings) { vaultCommand(channel, args, result); return; }
        if (channel.startsWith("ext:") && settings) { extensionCommand(channel, args, result); return; }
        if (channel.startsWith("recovery:") && recovery) { recoveryCommand(channel, args, result); return; }
        result.send(false, "This internal page control is not connected on Android yet");
    }

    private void chromeCommand(String channel, JSONArray args, PsychoBridge.Result result) throws Exception {
        switch (channel) {
            case "ui:layout": layout(arg(args)); result.send(true, null); return;
            case "ui:back": mBack.setEnabled(arg(args).optBoolean("handled")); result.send(true, null); return;
            case "settings:get": result.send(true, mStore.read(profile())); return;
            case "settings:set": result.send(true, saveSettings(arg(args))); return;
            case "win:private": mSelector.selectModel(true); selectSession(); mController.openNewTab(); result.send(true, null); changed(); return;
            case "tab:reopen": { String url = mClosedTabs.pollFirst(); if (url == null) { result.send(false, "No recently closed tab"); return; } result.send(true, mController.openWebTab(url)); changed(); return; }
            case "tab:move": { JSONObject o = arg(args); result.send(true, mController.move(o.getInt("id"), o.isNull("before") ? -1 : o.getInt("before"))); changed(); return; }
            case "zoom:in": case "zoom:out": case "zoom:reset": result.send(true, mController.zoom(channel)); changed(); return;
            case "translate:page": { Tab tab = currentTab(); if (tab == null || profile().isOffTheRecord() || !web(tab.getUrl())) { result.send(false, "Translation is unavailable for this page or private profile"); return; } String target = mStore.read(profile()).optString("translateTarget", "en"); String url = "https://translate.google.com/translate?sl=auto&tl=" + Uri.encode(target) + "&u=" + Uri.encode(tab.getUrl().getSpec()); result.send(true, new JSONObject().put("ok", true).put("url", url).put("tab", mController.openWebTab(url))); changed(); return; }
            case "clipboard:write": { ClipboardManager clip = (ClipboardManager) mActivity.getSystemService(Context.CLIPBOARD_SERVICE); clip.setPrimaryClip(ClipData.newPlainText("PsychoVault", str(args))); result.send(true, new JSONObject().put("ok", true)); return; }
            case "open:external": { GURL url = new GURL(str(args)); if (!web(url)) throw new IllegalArgumentException("Only web links can be opened externally"); mActivity.startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(url.getSpec()))); result.send(true, new JSONObject().put("ok", true)); return; }
            case "downloads:list": result.send(true, mDownloads.list()); return;
            case "downloads:clear": result.send(true, mDownloads.clear()); return;
            case "downloads:control": { JSONObject o = arg(args); result.send(true, mDownloads.control(o.getString("id"), o.getString("action"))); return; }
            case "downloads:open": result.send(true, new JSONObject().put("ok", mDownloads.open(str(args)))); return;
            case "downloads:reveal": result.send(false, "Android opens the file with the system viewer; folder reveal is unavailable"); return;
            case "downloads:dir": result.send(true, PsychoDownloads.directory(mActivity, mStore.read(profile()))); return;
            case "downloads:pickdir": pickDownloadDirectory(result); return;
            case "update:check": checkUpdates(result); return;
            case "update:banners": { String dismissed = mStore.read(profile()).optString("dismissedUpdate"); result.send(true, new JSONObject().put("dismissed", !dismissed.isEmpty()).put("version", dismissed)); return; }
            case "update:dismiss": dismissUpdate(args, result); return;
            case "spell:status": result.send(true, new JSONObject().put("enabled", mStore.read(profile()).optBoolean("spellCheck")).put("words", mStore.read(profile()).optJSONArray("spellUserWords"))); return;
            case "spell:add": case "spell:remove": spellWord(channel, args, result); return;
            case "tab:container": { JSONObject o = arg(args); Tab tab = mSelector.getTabById(o.getInt("id")); if (mContainers == null || tab == null) { result.send(false, "Tab unavailable"); return; } int id = mContainers.moveToContainer(model(), tab, o.optString("name")); result.send(true, new JSONObject().put("ok", true).put("tab", id)); changed(); return; }
            case "tab:menu": { JSONObject o = arg(args); showTabMenu(o.getInt("id"), result); return; }
            case "ws:list": result.send(true, mWorkspaces.list()); return;
            case "ws:switch": mWorkspaces.switchTo(args.getInt(0)); result.send(true, mWorkspaces.list()); changed(); return;
            case "ws:new": { mWorkspaceBusy = true; try { int tabId = mController.createTab(mStore.read(profile()).optString("homepage", "psychovault://newtab")); mWorkspaces.create(str(args), model().getTabById(tabId)); } finally { mWorkspaceBusy = false; } result.send(true, mWorkspaces.list()); changed(); return; }
            case "ws:rename": { JSONObject o = arg(args); mWorkspaces.rename(o.getInt("i"), o.getString("name")); result.send(true, mWorkspaces.list()); changed(); return; }
            case "ws:close": { mWorkspaceBusy = true; try { result.send(true, mWorkspaces.close(args.getInt(0))); } finally { mWorkspaceBusy = false; } changed(); return; }
            case "ws:menu": result.send(false, "Use the workspace chip controls: tap to switch, double-tap to rename, or the close button."); return;
            case "rail:icons": result.send(true, railIcons()); return;
            case "catalogue:list": result.send(true, catalogue()); return;
            case "catalogue:toggle": result.send(true, toggleService(str(args))); return;
            case "perf:hot": { if (args.length() == 0) result.send(true, mPerformance.hot(model())); else { JSONObject o = arg(args); if ("close".equals(o.optString("action"))) mController.close(o.getInt("id")); else mPerformance.action(model(), o); result.send(true, mPerformance.hot(model())); } changed(); return; }
            case "perf:media": result.send(true, mPerformance.media(currentTab(), str(args))); return;
            case "perf:snapshot": mPerformance.snapshot(currentTab(), str(args), value -> { if (value.optBoolean("ok")) result.send(true, value); else result.send(false, value.optString("reason", "Snapshot failed")); }); return;
            case "perf:panic": result.send(true, panic(args.getBoolean(0))); return;
            case "perf:services": service(arg(args), result); return;
            case "perf:switches": result.send(true, PsychoNetwork.status(profile(), currentTab() == null ? null : currentTab().getWebContents())); return;
            case "proxy:status": result.send(true, PsychoNetwork.status(profile(), currentTab() == null ? null : currentTab().getWebContents())); return;
            case "vpncheck:open": result.send(true, new JSONObject().put("ok", true).put("tab", mController.openWebTab("https://browserleaks.com/webrtc"))); changed(); return;
            case "app:info": result.send(true, new JSONObject().put("version", VersionInfo.getProductVersion()).put("platform", "android")); return;
            case "proton:toggle": { String id = mExtensions.resolveId(profile(), PsychoExtensions.PROTON_ID); String page = mExtensions.resolvePage(profile(), id, "popup.html"); mController.openExtensionPage(id, page); result.send(false, "Proton opened. Connect inside Proton; protection is not verified."); changed(); return; }
            default:
                if (channel.startsWith("ext:")) { extensionCommand(channel, args, result); return; }
                if (channel.startsWith("vault:")) { vaultCommand(channel, args, result); return; }
                if (channel.startsWith("tab:") || channel.startsWith("nav:") || channel.startsWith("find:")) { result.send(true, mController.invoke(channel, args)); changed(); return; }
                result.send(false, "This desktop control is not connected on Android yet");
        }
    }

    private void extensionCommand(String channel, JSONArray args, PsychoBridge.Result result) throws Exception {
        switch (channel) {
            case "ext:list": result.send(true, mExtensions.list(profile())); return;
            case "ext:status": result.send(true, mExtensions.status(profile())); return;
            case "ext:toggle": { JSONObject o = arg(args); result.send(true, mExtensions.toggle(profile(), o.optString("id", o.optString("dir")), o.getBoolean("enabled"))); changed(); return; }
            case "ext:remove": mExtensions.remove(profile(), str(args), mStore, value -> { if (value.optBoolean("ok")) result.send(true, value); else result.send(false, value.optString("error", "Extension removal failed")); changed(); }); return;
            case "ext:restorebuiltin": mExtensions.restoreBundled(profile(), mStore); result.send(true, new JSONObject().put("ok", true).put("items", mExtensions.list(profile()))); changed(); return;
            case "ext:openpage": { JSONObject o = arg(args); String id = mExtensions.resolveId(profile(), o.optString("id", o.optString("dir"))); String page = mExtensions.resolvePage(profile(), id, o.getString("page")); mController.openExtensionPage(id, page); result.send(true, new JSONObject().put("ok", true).put("url", "chrome-extension://" + id + "/" + page)); changed(); return; }
            case "ext:addfolder": mExtensions.pick("zip", value -> { if (value.optBoolean("ok")) result.send(true, value); else result.send(false, value.optString("error", "Import failed")); }); return;
            case "ext:addcrx": mExtensions.pick("crx", value -> { if (value.optBoolean("ok")) result.send(true, value); else result.send(false, value.optString("error", "Install failed")); }); return;
            default: result.send(false, "Unknown extension control");
        }
    }

    private void vaultCommand(String channel, JSONArray args, PsychoBridge.Result result) throws Exception {
        if (mVault == null) { result.send(false, "The vault is unavailable in a private profile"); return; }
        switch (channel) {
            case "vault:status": result.send(true, mVault.status()); return;
            case "vault:list": result.send(true, mVault.list()); return;
            case "vault:lock": result.send(true, mVault.lock()); changed(); return;
            case "vault:unlock": mVault.unlock(str(args), value -> { result.send(true, value); changed(); }); return;
            case "vault:setMaster": mVault.setMaster(str(args), value -> { result.send(true, value); changed(); }); return;
            case "vault:add": result.send(true, mVault.add(arg(args))); changed(); return;
            case "vault:delete": result.send(true, mVault.remove(str(args))); changed(); return;
            case "vault:reveal": result.send(true, mVault.reveal(str(args))); return;
            case "vault:copy": { JSONObject o = arg(args); result.send(true, mVault.copy(o.getString("id"), o.optString("which", "pass"))); return; }
            case "vault:generate": result.send(true, mVault.generate(arg(args))); return;
            case "vault:forgetMaster": result.send(true, new JSONObject().put("ok", false).put("reason", "Android keeps the master password; no OS-keychain fallback is enabled in this build.")); return;
            case "vault:transfer": mVault.pickTransfer(mActivity, args.getString(0), args.getString(1), value -> { result.send(true, value); changed(); }); return;
            case "vault:recover": result.send(true, mVault.recover()); changed(); return;
            default: result.send(false, "Unknown vault control");
        }
    }

    private void recoveryCommand(String channel, JSONArray args, PsychoBridge.Result result) throws Exception {
        if (mRecovery == null) { result.send(false, "Recovery is unavailable in a private profile"); return; }
        if (channel.equals("recovery:list")) { result.send(true, mRecovery.list()); return; }
        if (channel.equals("recovery:remove")) { mRecovery.remove(str(args)); result.send(true, mRecovery.list()); return; }
        if (channel.equals("recovery:restore")) {
            JSONArray tabs = mRecovery.restoreTabs(str(args)); int opened = 0;
            for (int i = 0; i < tabs.length(); i++) { JSONObject tab = tabs.getJSONObject(i); int id = mController.openWebTab(tab.getString("url")); mController.setPinned(id, tab.optBoolean("pinned")); mController.setMuted(id, tab.optBoolean("muted")); opened++; }
            result.send(true, new JSONObject().put("ok", true).put("count", opened)); changed(); return;
        }
        result.send(false, "Unknown recovery control");
    }

    private JSONObject panic(boolean on) throws Exception {
        JSONObject settings = mStore.read(profile());
        if (!settings.optBoolean("panicEnabled", true)) throw new IllegalStateException("The panic button is disabled");
        if (on == (mPanicTabs != null)) return new JSONObject().put("panicked", on).put("held", mPanicTabs == null ? 0 : mPanicTabs.length());
        if (on) {
            if (mRecovery != null) mRecovery.capture(model(), "before-panic");
            mPanicTabs = mController.snapshot().getJSONArray("tabs");
            List<Tab> old = new ArrayList<>(); for (int i = 0; i < model().getCount(); i++) old.add(model().getTabAt(i));
            mPanicTabId = mController.openWebTab(settings.optString("panicUrl"));
            if (!old.isEmpty()) model().getTabRemover().closeTabs(TabClosureParams.closeTabs(old).allowUndo(false).allowUnloadHandlers(false).build(), false);
        } else {
            JSONArray held = mPanicTabs; mPanicTabs = null;
            for (int i = 0; i < held.length(); i++) { JSONObject tab = held.getJSONObject(i); String url = tab.optString("url"); if (!url.startsWith("http://") && !url.startsWith("https://") && !url.startsWith("chrome://psychovault-pages/")) continue; int id = mController.openWebTab(url); mController.setPinned(id, tab.optBoolean("pinned")); mController.setMuted(id, tab.optBoolean("muted")); }
            Tab panic = model().getTabById(mPanicTabId); if (panic != null) model().getTabRemover().closeTabs(TabClosureParams.closeTab(panic).allowUndo(false).build(), false);
            mPanicTabId = Tab.INVALID_TAB_ID;
        }
        changed(); return new JSONObject().put("panicked", on).put("held", mPanicTabs == null ? 0 : mPanicTabs.length());
    }

    private JSONObject railIcons() throws Exception {
        JSONObject out = new JSONObject(); if (!mStore.read(profile()).optBoolean("railIcons", true)) return out;
        JSONArray services = mStore.read(profile()).getJSONArray("services");
        for (int i = 0; i < services.length(); i++) { GURL url = new GURL(services.getJSONObject(i).getString("url")); out.put(url.getHost(), "https://" + url.getHost() + "/favicon.ico"); }
        return out;
    }
    private JSONArray catalogue() throws Exception {
        String[][] items = {{"Discord", "https://discord.com/app"}, {"Twitch", "https://www.twitch.tv/directory"}, {"YouTube Music", "https://music.youtube.com"}, {"YouTube", "https://www.youtube.com"}, {"Reddit", "https://www.reddit.com"}, {"GitHub", "https://github.com"}, {"WhatsApp", "https://web.whatsapp.com"}, {"Telegram", "https://web.telegram.org/a/"}, {"Spotify", "https://open.spotify.com"}};
        JSONArray services = mStore.read(profile()).getJSONArray("services"), out = new JSONArray();
        for (String[] item : items) { boolean on = false; for (int i = 0; i < services.length(); i++) if (new GURL(services.getJSONObject(i).getString("url")).getHost().equals(new GURL(item[1]).getHost())) on = true; out.put(new JSONObject().put("name", item[0]).put("url", item[1]).put("host", new GURL(item[1]).getHost()).put("on", on)); }
        return out;
    }
    private JSONObject toggleService(String name) throws Exception {
        JSONArray all = catalogue(), services = mStore.read(profile()).getJSONArray("services"); JSONObject target = null;
        for (int i = 0; i < all.length(); i++) if (all.getJSONObject(i).getString("name").equals(name)) target = all.getJSONObject(i);
        if (target == null) throw new IllegalArgumentException("Unknown service");
        JSONArray next = new JSONArray(); boolean on = false;
        for (int i = 0; i < services.length(); i++) { JSONObject item = services.getJSONObject(i); if (new GURL(item.getString("url")).getHost().equals(target.getString("host"))) on = true; else next.put(item); }
        if (!on) next.put(new JSONObject().put("name", target.getString("name")).put("url", target.getString("url")));
        saveSettings(new JSONObject().put("services", next)); return new JSONObject().put("ok", true).put("on", !on).put("count", next.length());
    }
    private void service(JSONObject request, PsychoBridge.Result result) throws Exception {
        JSONArray services = mStore.read(profile()).getJSONArray("services"); int index = request.getInt("i");
        if (index < 0 || index >= services.length()) throw new IllegalArgumentException("Unknown service");
        if ("menu".equals(request.optString("action"))) { result.send(false, "Service options are in Settings and the rail catalogue"); return; }
        GURL target = new GURL(services.getJSONObject(index).getString("url"));
        for (int i = 0; i < model().getCount(); i++) { Tab tab = model().getTabAt(i); if (web(tab.getUrl()) && tab.getUrl().getHost().equals(target.getHost())) { mController.activate(tab.getId()); result.send(true, new JSONObject().put("ok", true).put("reused", true)); changed(); return; } }
        mController.openWebTab(target.getSpec()); result.send(true, new JSONObject().put("ok", true)); changed();
    }
    private void checkUpdates(PsychoBridge.Result result) throws Exception {
        PsychoUpdates.check(mStore.read(profile()).optString("dismissedUpdate"), answer -> {
            mLastUpdateRelease = answer.optString("release", "");
            result.send(true, answer);
        });
    }

    private void dismissUpdate(JSONArray args, PsychoBridge.Result result) throws Exception {
        String version = args.length() == 1 && args.get(0) instanceof JSONObject ? args.getJSONObject(0).optString("version") : mLastUpdateRelease;
        if (version == null || version.isEmpty()) { result.send(false, "Nothing to dismiss"); return; }
        result.send(true, saveSettings(new JSONObject().put("dismissedUpdate", version)));
    }

    private void pickDownloadDirectory(PsychoBridge.Result result) {
        PsychoDownloads.pickDirectory(mActivity, path -> {
            try {
                if (path == null) { result.send(false, "cancelled"); return; }
                saveSettings(new JSONObject().put("downloadDir", path));
                result.send(true, path);
            } catch (Exception e) { result.send(false, PsychoBridge.safeMessage(e)); }
        });
    }

    private void spellWord(String channel, JSONArray args, PsychoBridge.Result result) throws Exception {
        String word = str(args).trim().toLowerCase(java.util.Locale.ROOT);
        if (word.isEmpty() || word.length() > 99 || word.matches(".*\\s.*")) throw new IllegalArgumentException("Invalid dictionary word");
        JSONObject settings = mStore.read(profile());
        JSONArray words = settings.optJSONArray("spellUserWords"); if (words == null) words = new JSONArray();
        java.util.TreeSet<String> set = new java.util.TreeSet<>();
        for (int i = 0; i < words.length(); i++) set.add(words.getString(i));
        if (channel.equals("spell:add")) set.add(word); else set.remove(word);
        JSONArray next = new JSONArray(); for (String w : set) next.put(w);
        result.send(true, saveSettings(new JSONObject().put("spellUserWords", next)));
    }

    private void showTabMenu(int id, PsychoBridge.Result result) throws Exception {
        Tab tab = mSelector.getTabById(id);
        if (tab == null) { result.send(false, "Tab unavailable"); return; }
        JSONObject settings = mStore.read(profile());
        JSONArray containers = settings.optJSONArray("containers"); if (containers == null) containers = new JSONArray();
        java.util.ArrayList<String> labels = new java.util.ArrayList<>();
        java.util.ArrayList<String> names = new java.util.ArrayList<>();
        labels.add("No container (default profile)"); names.add("");
        for (int i = 0; i < containers.length(); i++) { String name = containers.getJSONObject(i).getString("name"); labels.add("Container: " + name); names.add(name); }
        labels.add("Close tab"); names.add(null);
        new AlertDialog.Builder(mActivity).setTitle("Tab").setItems(labels.toArray(new String[0]), (dialog, which) -> {
            try {
                String name = names.get(which);
                if (name == null) { model().getTabRemover().closeTabs(TabClosureParams.closeTab(tab).build(), true); }
                else if (mContainers != null) { mContainers.moveToContainer(model(), tab, name); }
                changed();
            } catch (Exception ignored) { }
        }).show();
        result.send(true, new JSONObject().put("ok", true));
    }

    private static boolean web(GURL url) { return url != null && url.isValid() && ("https".equals(url.getScheme()) || "http".equals(url.getScheme())); }

    private void layout(JSONObject value) throws Exception {
        float density = mActivity.getResources().getDisplayMetrics().density;
        mTop = px(value.optDouble("extraTop", 0), density, mCompositor.getHeight() / 3);
        mLeft = px(value.optDouble("panelLeft", 0), density, mCompositor.getWidth());
        mBottom = px(value.optDouble("extraBottom", 0), density, mCompositor.getHeight() / 3);
        ViewGroup.LayoutParams base = mCompositor.getLayoutParams();
        if (!(base instanceof ViewGroup.MarginLayoutParams)) throw new IllegalStateException("Unsupported Android content layout");
        ViewGroup.MarginLayoutParams params = (ViewGroup.MarginLayoutParams) base;
        if (params.topMargin != mTop || params.leftMargin != mLeft || params.bottomMargin != mBottom) { params.topMargin = mTop; params.leftMargin = mLeft; params.bottomMargin = mBottom; mCompositor.setLayoutParams(params); }
        mHitRegions.clear(); JSONArray regions = value.getJSONArray("hitRegions");
        if (regions.length() > MAX_REGIONS) throw new IllegalArgumentException("Too many chrome hit regions");
        for (int i = 0; i < regions.length(); i++) { JSONObject r = regions.getJSONObject(i); int left = px(r.getDouble("left"), density, mOverlay.getWidth()), top = px(r.getDouble("top"), density, mOverlay.getHeight()), right = px(r.getDouble("right"), density, mOverlay.getWidth()), bottom = px(r.getDouble("bottom"), density, mOverlay.getHeight()); if (right > left && bottom > top) mHitRegions.add(new Rect(left, top, right, bottom)); }
    }
    private static int px(double value, float density, int limit) { if (!Double.isFinite(value) || value < 0 || value > 16384) throw new IllegalArgumentException("Invalid chrome layout"); return Math.min(limit, Math.round((float) value * density)); }

    public void resume() { if (mActive != null) mActive.contents.updateWebContentsVisibility(Visibility.VISIBLE); if (mDownloads != null) mDownloads.background(false); }
    public void pause() {
        if (mActive != null) mActive.contents.updateWebContentsVisibility(Visibility.OCCLUDED);
        if (mDownloads != null) mDownloads.background(true);
        if (mVault != null) mVault.background();
        if (mRecovery != null) try { if (mStore.read(profile()).optBoolean("restore", true)) mRecovery.capture(model(), "background"); } catch (Exception ignored) { }
    }
    public void handleActivityResult(int request, int result, Intent data) {
        if (mDestroyed) return;
        Tab tab = currentTab(); WebContents contents = tab == null ? null : tab.getWebContents();
        if (request == PsychoExtensions.PICK_ZIP || request == PsychoExtensions.PICK_CRX) mExtensions.handleActivityResult(request, result, data, profile(), contents, mStore);
        if (mVault != null) mVault.handleActivityResult(request, result, data);
    }
    public void destroy() {
        ThreadUtils.assertOnUiThread(); if (mDestroyed) return; mDestroyed = true; mBack.remove();
        try { mActivity.unregisterReceiver(mPowerReceiver); } catch (Exception ignored) { }
        clearOnExitIfNeeded();
        if (mShowingToken != -1) mCompositor.getBrowserControlsManager().getBrowserVisibilityDelegate().releasePersistentShowingToken(mShowingToken);
        mOverlay.removeCallbacks(mPublish); mOverlay.removeCallbacks(mDownloadPublish); mSelector.removeObserverFromAllModels(mModelObserver);
        for (TabModel model : mSelector.getModels()) for (int i = 0; i < model.getCount(); i++) model.getTabAt(i).removeObserver(mTabObserver);
        for (int id : new ArrayList<>(mNavigationObservers.keySet())) unobserveNavigation(id);
        for (int id : new ArrayList<>(mPages.keySet())) unbindPage(id);
        if (mDownloads != null) mDownloads.destroy(); if (mVault != null) mVault.destroy(); if (mMedia != null) mMedia.destroy(); if (mContainers != null) mContainers.destroy(); mExtensions.destroy(); mPerformance.destroy(); mController.destroy();
        if (mServicesProfile != null) PsychoNetwork.forget(mServicesProfile);
        for (Session session : mSessions.values()) {
            JavascriptInjector injector = JavascriptInjector.fromWebContents(session.contents); if (injector != null) injector.removeInterface("PsychoVaultNative");
            session.bridge.destroy(); session.matcher.destroy(); session.contents.updateWebContentsVisibility(Visibility.HIDDEN);
            session.contents.setDelegates(VersionInfo.getProductVersion(), ViewAndroidDelegate.createBasicDelegate(null), null, null, WebContents.createDefaultInternalsHolder());
            session.delegate.destroy(); session.contents.destroy();
        }
        mSessions.clear(); if (mOverlay.getParent() instanceof ViewGroup) ((ViewGroup) mOverlay.getParent()).removeView(mOverlay);
    }
}
