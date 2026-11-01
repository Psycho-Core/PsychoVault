package org.chromium.chrome.browser.psychovault;

import android.app.Activity;
import android.graphics.Color;
import android.graphics.Rect;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;

import org.json.JSONArray;
import org.json.JSONObject;
import org.chromium.base.ThreadUtils;
import org.chromium.chrome.browser.compositor.CompositorViewHolder;
import org.chromium.chrome.browser.content.WebContentsFactory;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.tab.TabObserver;
import org.chromium.chrome.browser.tabmodel.TabModel;
import org.chromium.chrome.browser.tabmodel.TabModelObserver;
import org.chromium.chrome.browser.tabmodel.TabModelSelector;
import org.chromium.components.embedder_support.view.ContentView;
import org.chromium.components.origin_matcher.OriginMatcher;
import org.chromium.content.browser.JavascriptInterface;
import org.chromium.content_public.browser.JavascriptInjector;
import org.chromium.content_public.browser.LoadUrlParams;
import org.chromium.content_public.browser.Visibility;
import org.chromium.content_public.browser.WebContents;
import org.chromium.content_public.browser.WebContentsObserver;
import org.chromium.content_public.browser.NavigationHandle;
import org.chromium.base.VersionInfo;
import org.chromium.ui.base.ViewAndroidDelegate;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

/** Trusted desktop chrome, kept outside the website TabModel and outside website documents. */
public final class PsychoChromeHost implements PsychoBridge.Handler {
    public static final String CHROME_ORIGIN = "chrome://psychovault-chrome";
    public static final String PAGE_ORIGIN = "chrome://psychovault-pages";
    private static final int MAX_REGIONS = 32;
    private final Activity mActivity;
    private final TabModelSelector mSelector;
    private final CompositorViewHolder mCompositor;
    private final PsychoTabController mController;
    private final PsychoProfileStore mStore;
    private final PsychoExtensions mExtensions;
    private final FrameLayout mOverlay;
    private final Map<Boolean, Session> mSessions = new HashMap<>();
    private final Map<Integer, PageBinding> mPages = new HashMap<>();
    private final Map<Integer, WebContentsObserver> mNavigationObservers = new HashMap<>();
    private final Set<Integer> mObservedTabs = new HashSet<>();
    private final List<Rect> mHitRegions = new ArrayList<>();
    private Session mActive;
    private boolean mDestroyed;
    private boolean mPublishQueued;
    private boolean mTouchInChrome;
    private int mTop;
    private int mLeft;
    private int mBottom;
    private int mShowingToken = -1;
    private final Runnable mPublish = () -> { mPublishQueued = false; publish(); };
    private final TabModelObserver mModelObserver = new TabModelObserver() {
        @Override public void didAddTab(Tab tab, int type, int creationState, boolean selected) {
            observeTab(tab); bindPage(tab); changed();
        }
        @Override public void didSelectTab(Tab tab, int type, int lastId) { changed(); }
        @Override public void didMoveTab(Tab tab, int newIndex, int curIndex) { changed(); }
        @Override public void onTabCloseCommitted(List<Tab> tabs, boolean all, boolean restore, int source) {
            for (Tab tab : tabs) unbindPage(tab.getId());
            changed();
        }
        @Override public void onFinishingMultipleTabClosure(List<Tab> tabs, boolean restore) {
            for (Tab tab : tabs) unbindPage(tab.getId());
            changed();
        }
        @Override public void onDidActiveStateChange(TabModel model, boolean active) {
            if (active) selectSession();
            changed();
        }
    };
    private final TabObserver mTabObserver = new TabObserver() {
        @Override public void onContentChanged(Tab tab) { unbindPage(tab.getId()); unobserveNavigation(tab.getId()); mObservedTabs.remove(tab.getId()); observeTab(tab); bindPage(tab); changed(); }
        @Override public void onUrlUpdated(Tab tab) { changed(); }
        @Override public void onTitleUpdated(Tab tab) { changed(); }
        @Override public void onLoadStarted(Tab tab, boolean differentDocument) { changed(); }
        @Override public void onLoadStopped(Tab tab, boolean differentDocument) { changed(); }
        @Override public void onDestroyed(Tab tab) { unbindPage(tab.getId()); unobserveNavigation(tab.getId()); mObservedTabs.remove(tab.getId()); changed(); }
    };

    private static final class Session {
        final WebContents contents;
        final ContentView view;
        final ViewAndroidDelegate delegate;
        final PsychoBridge bridge;
        final OriginMatcher matcher;
        Session(WebContents c, ContentView v, ViewAndroidDelegate d, PsychoBridge b, OriginMatcher m) {
            contents = c; view = v; delegate = d; bridge = b; matcher = m;
        }
    }

    private static final class PageBinding {
        final PsychoBridge bridge;
        final OriginMatcher matcher;
        PageBinding(PsychoBridge b, OriginMatcher m) { bridge = b; matcher = m; }
    }

    public PsychoChromeHost(Activity activity, TabModelSelector selector, CompositorViewHolder compositor) {
        ThreadUtils.assertOnUiThread();
        mActivity = activity;
        mSelector = selector;
        mCompositor = compositor;
        mController = new PsychoTabController(selector, () -> ((org.chromium.chrome.browser.ChromeTabbedActivity) activity).getCurrentTabCreator());
        mStore = new PsychoProfileStore(activity);
        mExtensions = new PsychoExtensions(activity);
        mOverlay = new FrameLayout(activity) {
            @Override public boolean dispatchTouchEvent(MotionEvent event) {
                int x = Math.round(event.getX()), y = Math.round(event.getY());
                if (event.getActionMasked() == MotionEvent.ACTION_DOWN) {
                    mTouchInChrome = false;
                    for (Rect region : mHitRegions) if (region.contains(x, y)) { mTouchInChrome = true; break; }
                    if (!mTouchInChrome && mActive != null) mActive.bridge.emit("page-touch", new JSONObject());
                }
                // A transparent WebUI must not swallow website gestures. Returning false lets
                // Android dispatch the complete gesture to the compositor underneath.
                return mTouchInChrome && super.dispatchTouchEvent(event);
            }
        };
        mOverlay.setBackgroundColor(Color.TRANSPARENT);
        mOverlay.addOnLayoutChangeListener((v, l, t, r, b, ol, ot, or, ob) -> {
            for (Session session : mSessions.values()) session.contents.setSize(r - l, b - t);
        });
        selector.addObserverToAllModels(mModelObserver);
        for (TabModel model : selector.getModels()) {
            for (int j = 0; j < model.getCount(); j++) { observeTab(model.getTabAt(j)); bindPage(model.getTabAt(j)); }
        }
        mShowingToken = compositor.getBrowserControlsManager().getBrowserVisibilityDelegate().showControlsPersistent();
        ViewGroup root = activity.findViewById(android.R.id.content);
        root.addView(mOverlay, new FrameLayout.LayoutParams(-1, -1));
        selectSession();
        try { mExtensions.ensureBundled(profile(), mStore); } catch (Exception e) { /* UI reports extension status honestly. */ }
    }

    private Profile profile() { return mSelector.getCurrentModel().getProfile(); }

    private void selectSession() {
        if (mDestroyed) return;
        Profile profile = profile();
        if (profile == null) return;
        boolean incognito = profile.isOffTheRecord();
        Session session = mSessions.get(incognito);
        if (session == null) {
            WebContents contents = WebContentsFactory.createWebContents(profile, false, true);
            ContentView view = ContentView.createContentView(mActivity, contents);
            ViewAndroidDelegate delegate = ViewAndroidDelegate.createBasicDelegate(view);
            contents.setDelegates(VersionInfo.getProductVersion(), delegate, view,
                    ((org.chromium.chrome.browser.app.ChromeActivity) activity).getWindowAndroid(),
                    WebContents.createDefaultInternalsHolder());
            PsychoBridge bridge = new PsychoBridge(contents, this::profile, this, true, Tab.INVALID_TAB_ID);
            OriginMatcher matcher = origin(CHROME_ORIGIN);
            JavascriptInjector injector = JavascriptInjector.fromWebContents(contents);
            if (injector == null) { matcher.destroy(); bridge.destroy(); delegate.destroy(); contents.destroy(); throw new IllegalStateException("Chrome UI injector unavailable"); }
            injector.addPossiblyUnsafeInterfaceToOrigins(bridge, "PsychoVaultNative", JavascriptInterface.class, matcher);
            session = new Session(contents, view, delegate, bridge, matcher);
            mSessions.put(incognito, session);
            view.setLayoutParams(new FrameLayout.LayoutParams(-1, -1));
            mOverlay.addView(view);
            contents.getNavigationController().loadUrl(new LoadUrlParams(CHROME_ORIGIN + "/index.html?private=" + (incognito ? 1 : 0)));
        }
        if (mActive != null) { mActive.view.setVisibility(View.GONE); mActive.contents.updateWebContentsVisibility(Visibility.HIDDEN); }
        mActive = session;
        session.view.setVisibility(View.VISIBLE);
        session.contents.updateWebContentsVisibility(Visibility.VISIBLE);
        changed();
    }

    private static OriginMatcher origin(String origin) {
        OriginMatcher matcher = new OriginMatcher();
        if (!matcher.setRuleList(List.of(origin)).isEmpty()) { matcher.destroy(); throw new IllegalArgumentException("Invalid trusted origin"); }
        return matcher;
    }

    private void observeTab(Tab tab) {
        if (tab == null || !mObservedTabs.add(tab.getId())) return;
        tab.addObserver(mTabObserver);
        WebContents contents = tab.getWebContents();
        if (contents == null || contents.isDestroyed()) return;
        final int id = tab.getId();
        mNavigationObservers.put(id, new WebContentsObserver(contents) {
            @Override public void didFinishNavigationInPrimaryMainFrame(NavigationHandle navigation) {
                if (mDestroyed || !navigation.hasCommitted() || navigation.isErrorPage()
                        || navigation.isSameDocument() || !navigation.isInPrimaryMainFrame()) return;
                recordVisit(id, navigation.getUrl().getSpec());
            }
            @Override public void webContentsDestroyed() { observe(null); }
        });
    }

    /** Only the host calls this, from a committed primary-main-frame navigation. */
    private void recordVisit(int tabId, String url) {
        try {
            Profile profile = profile();
            TabModel current = mSelector.getCurrentModel();
            Tab tab = current.getTabById(tabId);
            if (profile == null || profile.isOffTheRecord() || tab == null || tab.isOffTheRecord()) return;
            if (!mStore.read(profile).optBoolean("remember", true)) return;
            Session regular = mSessions.get(false);
            if (regular == null) return;
            JSONObject visit = new JSONObject().put("url", url).put("title", tab.getTitle());
            regular.bridge.emit("library:visit", visit);
        } catch (Exception e) {
            // History is best-effort; a storage failure must not break navigation.
        }
    }

    private void unobserveNavigation(int id) {
        WebContentsObserver observer = mNavigationObservers.remove(id);
        if (observer != null) observer.observe(null);
    }

    private void bindPage(Tab tab) {
        if (tab == null) return;
        WebContents contents = tab.getWebContents();
        if (contents == null || contents.isDestroyed() || mPages.containsKey(tab.getId())) return;
        PsychoBridge bridge = new PsychoBridge(contents, this::profile, this, false, tab.getId());
        OriginMatcher matcher = origin(PAGE_ORIGIN);
        JavascriptInjector injector = JavascriptInjector.fromWebContents(contents);
        if (injector == null) { matcher.destroy(); bridge.destroy(); return; }
        injector.addPossiblyUnsafeInterfaceToOrigins(bridge, "PsychoVaultNative", JavascriptInterface.class, matcher);
        mPages.put(tab.getId(), new PageBinding(bridge, matcher));
    }

    private void unbindPage(int id) {
        PageBinding binding = mPages.remove(id);
        if (binding == null) return;
        binding.bridge.destroy();
        Tab tab = mSelector.getTabById(id);
        WebContents contents = tab == null ? null : tab.getWebContents();
        JavascriptInjector injector = contents == null || contents.isDestroyed() ? null : JavascriptInjector.fromWebContents(contents);
        if (injector != null) injector.removeInterface("PsychoVaultNative");
        binding.matcher.destroy();
    }

    private void changed() {
        if (!mDestroyed && !mPublishQueued) { mPublishQueued = true; mOverlay.post(mPublish); }
    }

    private void publish() {
        if (mDestroyed) return;
        JSONObject state = snapshot(Tab.INVALID_TAB_ID);
        for (Session session : mSessions.values()) {
            if (session == mActive) session.bridge.emit("state", state);
        }
        for (PageBinding page : mPages.values()) page.bridge.emit("extensions", new JSONObject());
    }

    @Override public JSONObject snapshot(int tabId) {
        if (tabId != Tab.INVALID_TAB_ID) {
            try { return new JSONObject().put("settings", mStore.read(profile())).put("private", profile().isOffTheRecord()); }
            catch (Exception e) { throw new IllegalStateException(e); }
        }
        JSONObject state = mController.snapshot();
        try {
            state.put("settings", mStore.read(profile())).put("workspaces", new JSONObject().put("list", new JSONArray()).put("active", 0));
            Tab tab = mSelector.getCurrentTab();
            state.put("title", tab == null ? "New Tab" : tab.getTitle());
            state.put("private", profile() != null && profile().isOffTheRecord());
            state.put("nativeTopPx", mCompositor.getBrowserControlsManager().getTopControlsHeight());
        } catch (Exception e) { throw new IllegalStateException(e); }
        return state;
    }

    @Override public void command(String channel, JSONArray args, int tabId, PsychoBridge.Result result) {
        if (mDestroyed) { result.send(false, "Android chrome is closed"); return; }
        try {
            if (tabId != Tab.INVALID_TAB_ID) {
                if (channel.equals("nav:go") && args.length() == 1 && args.get(0) instanceof String) { result.send(true, mController.navigateTab(tabId, args.getString(0))); changed(); return; }
                if (channel.equals("settings:get")) { result.send(true, mStore.read(profile())); return; }
                if (channel.equals("settings:set") && args.length() == 1) { result.send(true, mStore.write(profile(), args.getJSONObject(0))); changed(); return; }
                result.send(false, "This internal page control is not connected on Android yet"); return;
            }
            if (channel.equals("ui:layout")) { layout(args.getJSONObject(0)); result.send(true, null); return; }
            if (channel.equals("settings:get")) { result.send(true, mStore.read(profile())); return; }
            if (channel.equals("settings:set") && args.length() == 1) { result.send(true, mStore.write(profile(), args.getJSONObject(0))); changed(); return; }
            if (channel.equals("rail:icons")) { result.send(true, new JSONObject()); return; }
            if (channel.equals("ext:list")) { result.send(true, mExtensions.list(profile())); return; }
            if (channel.equals("ext:status")) { result.send(true, mExtensions.status(profile())); return; }
            if (channel.equals("ext:toggle") && args.length() == 1) { JSONObject o = args.getJSONObject(0); mExtensions.toggle(profile(), o.optString("id", o.optString("dir")), o.getBoolean("enabled")); result.send(true, new JSONObject().put("ok", true).put("items", mExtensions.list(profile()))); return; }
            if (channel.equals("ext:remove") && args.length() == 1) { mExtensions.remove(profile(), args.getString(0), mStore); result.send(true, new JSONObject().put("ok", true).put("items", mExtensions.list(profile()))); return; }
            if (channel.equals("ext:restorebuiltin")) { mExtensions.restoreBundled(profile(), mStore); result.send(true, new JSONObject().put("ok", true).put("items", mExtensions.list(profile()))); return; }
            if (channel.equals("ext:openpage") && args.length() == 1) { JSONObject o = args.getJSONObject(0); String id = mExtensions.resolveId(profile(), o.optString("id", o.optString("dir"))); String page = o.getString("page"); mController.openExtensionPage(id, page); result.send(true, new JSONObject().put("ok", true).put("url", "chrome-extension://" + id + "/" + page)); return; }
            if (channel.equals("proton:toggle")) { mController.openExtensionPage(PsychoExtensions.PROTON_ID, "popup.html"); result.send(false, "Proton opened. Connect inside Proton; protection is not verified."); return; }
            if (channel.equals("tab:new") || channel.equals("nav:go") || channel.equals("tab:open")
                    || channel.startsWith("nav:") || channel.startsWith("tab:")) {
                Object value = mController.invoke(channel, args); changed(); result.send(true, value); return;
            }
            result.send(false, "This desktop control is not connected on Android yet");
        } catch (Exception e) { result.send(false, PsychoBridge.safeMessage(e)); }
    }

    private void layout(JSONObject value) throws Exception {
        float density = mActivity.getResources().getDisplayMetrics().density;
        mTop = px(value.optDouble("extraTop", 0), density, mCompositor.getHeight() / 3);
        mLeft = px(value.optDouble("panelLeft", 0), density, mCompositor.getWidth());
        mBottom = px(value.optDouble("extraBottom", 0), density, mCompositor.getHeight() / 3);
        ViewGroup.LayoutParams base = mCompositor.getLayoutParams();
        if (!(base instanceof ViewGroup.MarginLayoutParams)) throw new IllegalStateException("Unsupported Android content layout");
        ViewGroup.MarginLayoutParams params = (ViewGroup.MarginLayoutParams) base;
        if (params.topMargin != mTop || params.leftMargin != mLeft || params.bottomMargin != mBottom) {
            params.topMargin = mTop; params.leftMargin = mLeft; params.bottomMargin = mBottom;
            mCompositor.setLayoutParams(params);
        }
        mHitRegions.clear();
        JSONArray regions = value.getJSONArray("hitRegions");
        if (regions.length() > MAX_REGIONS) throw new IllegalArgumentException("Too many chrome hit regions");
        for (int i = 0; i < regions.length(); i++) {
            JSONObject r = regions.getJSONObject(i);
            int left = px(r.getDouble("left"), density, mOverlay.getWidth());
            int top = px(r.getDouble("top"), density, mOverlay.getHeight());
            int right = px(r.getDouble("right"), density, mOverlay.getWidth());
            int bottom = px(r.getDouble("bottom"), density, mOverlay.getHeight());
            if (right > left && bottom > top) mHitRegions.add(new Rect(left, top, right, bottom));
        }
    }

    private static int px(double value, float density, int limit) {
        if (!Double.isFinite(value) || value < 0 || value > 16384) throw new IllegalArgumentException("Invalid chrome layout");
        return Math.min(limit, Math.round((float) value * density));
    }

    public void resume() { if (mActive != null) mActive.contents.updateWebContentsVisibility(Visibility.VISIBLE); }
    public void pause() { if (mActive != null) mActive.contents.updateWebContentsVisibility(Visibility.OCCLUDED); }

    public void destroy() {
        ThreadUtils.assertOnUiThread();
        if (mDestroyed) return;
        mDestroyed = true;
        if (mShowingToken != -1) mCompositor.getBrowserControlsManager().getBrowserVisibilityDelegate().releasePersistentShowingToken(mShowingToken);
        mOverlay.removeCallbacks(mPublish);
        mSelector.removeObserverFromAllModels(mModelObserver);
        for (TabModel model : models()) for (int i = 0; i < model.getCount(); i++) model.getTabAt(i).removeObserver(mTabObserver);
        for (int id : new ArrayList<>(mNavigationObservers.keySet())) unobserveNavigation(id);
        for (int id : new ArrayList<>(mPages.keySet())) unbindPage(id);
        for (Session session : mSessions.values()) {
            JavascriptInjector injector = JavascriptInjector.fromWebContents(session.contents);
            if (injector != null) injector.removeInterface("PsychoVaultNative");
            session.bridge.destroy(); session.matcher.destroy();
            session.contents.updateWebContentsVisibility(Visibility.HIDDEN);
            session.contents.setDelegates(VersionInfo.getProductVersion(),
                    ViewAndroidDelegate.createBasicDelegate(null), null, null,
                    WebContents.createDefaultInternalsHolder());
            session.delegate.destroy(); session.contents.destroy();
        }
        mSessions.clear();
        ((ViewGroup) mOverlay.getParent()).removeView(mOverlay);
    }

    private List<TabModel> models() {
        List<TabModel> result = new ArrayList<>();
        result.addAll(mSelector.getModels());
        return result;
    }
}
