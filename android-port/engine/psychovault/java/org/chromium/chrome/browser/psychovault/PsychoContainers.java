package org.chromium.chrome.browser.psychovault;

import org.chromium.base.ThreadUtils;
import org.chromium.chrome.browser.profiles.OtrProfileId;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.profiles.ProfileManager;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.tab.TabBuilder;
import org.chromium.chrome.browser.tab.TabCreationState;
import org.chromium.chrome.browser.tab.TabLaunchType;
import org.chromium.chrome.browser.tab.TabSelectionType;
import org.chromium.chrome.browser.tabmodel.TabClosureParams;
import org.chromium.chrome.browser.tabmodel.TabModel;
import org.chromium.content_public.browser.LoadUrlParams;
import org.json.JSONArray;
import org.json.JSONObject;

import java.util.HashMap;
import java.util.HashSet;
import java.util.Locale;
import java.util.Map;
import java.util.Set;

/**
 * Named containers backed by real named off-the-record profiles. Each container gets
 * its own cookie jar, storage and cache, but Android OTR profiles are ephemeral:
 * container sign-ins do not survive an app restart the way desktop partitions do.
 */
final class PsychoContainers {
    private final Profile mRegular;
    private final Map<String, Profile> mProfiles = new HashMap<>();
    private final Map<Integer, String> mTabContainers = new HashMap<>();
    private final Set<String> mAllowed = new HashSet<>();

    PsychoContainers(Profile profile) {
        ThreadUtils.assertOnUiThread();
        mRegular = profile.isOffTheRecord() ? profile.getOriginalProfile() : profile;
    }

    void update(JSONObject settings) throws Exception {
        mAllowed.clear();
        JSONArray containers = settings.optJSONArray("containers");
        if (containers != null) for (int i = 0; i < containers.length(); i++) mAllowed.add(containers.getJSONObject(i).getString("name").toLowerCase(Locale.ROOT));
    }

    String nameFor(int tabId) { return mTabContainers.get(tabId); }

    void tabClosed(int tabId) { mTabContainers.remove(tabId); }

    int moveToContainer(TabModel model, Tab tab, String name) throws Exception {
        ThreadUtils.assertOnUiThread();
        if (model == null || tab == null) throw new IllegalArgumentException("Tab unavailable");
        if (model.isIncognito() || tab.isOffTheRecord()) throw new IllegalStateException("Containers are only available in regular windows");
        String normalized = name == null ? "" : name.trim();
        if (!normalized.isEmpty() && !mAllowed.contains(normalized.toLowerCase(Locale.ROOT))) throw new IllegalArgumentException("Unknown container");
        String current = mTabContainers.get(tab.getId());
        if (normalized.equals(current == null ? "" : current)) return tab.getId();
        Profile target = mRegular;
        if (!normalized.isEmpty()) {
            String key = normalized.toLowerCase(Locale.ROOT);
            Profile container = mProfiles.get(key);
            if (container == null) {
                container = mRegular.getOrCreateOffTheRecordProfile(OtrProfileId.createUnique("pv-container-" + key));
                mProfiles.put(key, container);
            }
            target = container;
        }
        String url = tab.getUrl().getSpec();
        int index = model.indexOf(tab);
        Tab replacement = TabBuilder.createLiveTab(target, false)
                .setWindow(tab.getWindowAndroid())
                .setParent(tab)
                .setLaunchType(TabLaunchType.FROM_LONGPRESS_FOREGROUND)
                .build();
        replacement.loadUrl(new LoadUrlParams(url));
        model.addTab(replacement, Math.max(0, index), TabLaunchType.FROM_LONGPRESS_FOREGROUND, TabCreationState.LIVE_IN_FOREGROUND);
        if (normalized.isEmpty()) mTabContainers.remove(replacement.getId()); else mTabContainers.put(replacement.getId(), normalized);
        model.setIndex(model.indexOf(replacement), TabSelectionType.FROM_USER);
        model.getTabRemover().closeTabs(TabClosureParams.closeTab(tab).build(), true);
        mTabContainers.remove(tab.getId());
        return replacement.getId();
    }

    void destroy() {
        ThreadUtils.assertOnUiThread();
        mTabContainers.clear();
        for (Profile profile : mProfiles.values()) {
            try { profile.wipe(); } catch (Exception ignored) { }
        }
        mProfiles.clear();
    }
}
