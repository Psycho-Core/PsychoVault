package org.chromium.chrome.browser.psychovault;

import org.chromium.base.Token;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.tab.TabSelectionType;
import org.chromium.chrome.browser.tabmodel.TabClosureParams;
import org.chromium.chrome.browser.tabmodel.TabGroupMergeNotificationType;
import org.chromium.chrome.browser.tabmodel.TabModel;
import org.json.JSONArray;
import org.json.JSONObject;

import java.util.ArrayList;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Objects;

/** Workspaces are live Chromium tab groups, so switching does not discard page state. */
final class PsychoWorkspaces {
    private final Profile mProfile;
    private final PsychoProfileStore mStore;
    private TabModel mModel;
    private Token mActive;
    private final List<Token> mOrder = new ArrayList<>();

    PsychoWorkspaces(Profile profile, PsychoProfileStore store) { mProfile = profile; mStore = store; }

    boolean enabled() throws Exception { return !mProfile.isOffTheRecord() && mStore.read(mProfile).optBoolean("workspacesEnabled", true); }

    void bind(TabModel model) throws Exception {
        if (mProfile.isOffTheRecord() || !enabled()) { mModel = model; mActive = null; mOrder.clear(); return; }
        if (mModel == model && mActive != null && model.getTabsInGroup(mActive) != null && !model.getTabsInGroup(mActive).isEmpty()) return;
        mModel = model;
        LinkedHashSet<Token> groups = new LinkedHashSet<>(model.getAllTabGroupIds());
        List<Tab> loose = new ArrayList<>();
        for (int i = 0; i < model.getCount(); i++) {
            Tab tab = model.getTabAt(i);
            if (tab.getTabGroupId() == null) loose.add(tab); else groups.add(tab.getTabGroupId());
        }
        if (groups.isEmpty() && !loose.isEmpty()) {
            model.createSingleTabGroup(loose.get(0));
            Token id = loose.get(0).getTabGroupId();
            if (loose.size() > 1) model.mergeListOfTabsToGroup(loose.subList(1, loose.size()), loose.get(0), null, TabGroupMergeNotificationType.NOTIFY_IF_NOT_NEW_GROUP);
            model.setTabGroupTitle(id, "General"); model.setTabGroupColor(id, 7); groups.add(id);
        } else if (!loose.isEmpty()) {
            Token target = groups.iterator().next();
            Tab destination = model.getTabsInGroup(target).get(0);
            model.mergeListOfTabsToGroup(loose, destination, null, TabGroupMergeNotificationType.NOTIFY_IF_NOT_NEW_GROUP);
        }
        mOrder.clear();
        JSONObject saved = mStore.read(mProfile);
        JSONArray order = saved.optJSONArray("workspaceOrder");
        if (order != null) for (int i = 0; i < order.length(); i++) { Token id = parse(order.getString(i)); if (groups.remove(id)) mOrder.add(id); }
        mOrder.addAll(groups);
        mActive = parse(saved.optString("workspaceActive", ""));
        if (mActive == null || !mOrder.contains(mActive)) mActive = mOrder.isEmpty() ? null : mOrder.get(0);
        persist();
    }

    void assign(Tab tab) throws Exception {
        if (tab == null || mProfile.isOffTheRecord() || !enabled() || mActive == null || tab.getTabGroupId() != null) return;
        List<Tab> existing = mModel.getTabsInGroup(mActive);
        if (existing == null || existing.isEmpty()) { mModel.createSingleTabGroup(tab); mActive = tab.getTabGroupId(); mOrder.add(mActive); persist(); }
        else mModel.mergeListOfTabsToGroup(List.of(tab), existing.get(0), null, TabGroupMergeNotificationType.NOTIFY_IF_NOT_NEW_GROUP);
    }

    boolean contains(Tab tab) throws Exception { return !enabled() || tab != null && Objects.equals(mActive, tab.getTabGroupId()); }

    void selectActive() throws Exception {
        if (!enabled() || mActive == null) return;
        Tab current = mModel.getTabAt(mModel.index());
        if (current != null && Objects.equals(current.getTabGroupId(), mActive)) return;
        List<Tab> tabs = mModel.getTabsInGroup(mActive);
        if (tabs != null && !tabs.isEmpty()) mModel.setIndex(mModel.indexOf(tabs.get(0)), TabSelectionType.FROM_USER);
    }

    JSONObject list() throws Exception {
        JSONObject out = new JSONObject(); JSONArray list = new JSONArray();
        if (!enabled()) return out.put("list", list).put("active", 0);
        int active = 0;
        for (int i = 0; i < mOrder.size(); i++) {
            Token id = mOrder.get(i); List<Tab> tabs = mModel.getTabsInGroup(id);
            if (tabs == null || tabs.isEmpty()) continue;
            if (Objects.equals(id, mActive)) active = list.length();
            String name = mModel.getTabGroupTitle(id);
            list.put(new JSONObject().put("name", name == null || name.isEmpty() ? "Workspace" : name).put("count", tabs.size()));
        }
        return out.put("list", list).put("active", active);
    }

    JSONObject filter(JSONObject state) throws Exception {
        if (!enabled()) return state.put("workspaces", list());
        JSONArray tabs = state.optJSONArray("tabs"), selected = new JSONArray();
        int activeId = -1;
        for (int i = 0; i < tabs.length(); i++) {
            JSONObject item = tabs.getJSONObject(i); Tab tab = mModel.getTabById(item.getInt("id"));
            if (tab != null && Objects.equals(tab.getTabGroupId(), mActive)) { selected.put(item); if (item.getInt("id") == state.optInt("activeId", -1)) activeId = item.getInt("id"); }
        }
        if (activeId < 0 && selected.length() > 0) activeId = selected.getJSONObject(0).getInt("id");
        state.put("tabs", selected).put("activeId", activeId < 0 ? JSONObject.NULL : activeId).put("workspaces", list());
        return state;
    }

    void switchTo(int index) throws Exception {
        requireEnabled(); if (index < 0 || index >= mOrder.size()) throw new IllegalArgumentException("Unknown workspace");
        mActive = mOrder.get(index); persist(); selectActive();
    }
    void create(String name, Tab tab) throws Exception {
        requireEnabled(); if (tab == null) throw new IllegalStateException("Could not create workspace tab");
        mModel.createSingleTabGroup(tab); Token id = tab.getTabGroupId();
        mModel.setTabGroupTitle(id, cleanName(name)); mModel.setTabGroupColor(id, 7);
        mOrder.add(id); mActive = id; persist();
    }
    void rename(int index, String name) throws Exception {
        requireEnabled(); if (index < 0 || index >= mOrder.size()) throw new IllegalArgumentException("Unknown workspace");
        mModel.setTabGroupTitle(mOrder.get(index), cleanName(name));
    }
    JSONObject close(int index) throws Exception {
        requireEnabled(); if (index < 0 || index >= mOrder.size()) throw new IllegalArgumentException("Unknown workspace");
        Token id = mOrder.get(index); List<Tab> tabs = new ArrayList<>(mModel.getTabsInGroup(id));
        boolean wasActive = Objects.equals(id, mActive);
        if (mOrder.size() == 1) {
            // Keep a live home tab before closing the old group; never close the activity's last tab.
            Tab home = mModel.getTabCreator().launchUrl("chrome://psychovault-pages/newtab.html?private=0", org.chromium.chrome.browser.tab.TabLaunchType.FROM_CHROME_UI);
            if (home == null) throw new IllegalStateException("Could not keep a home tab open");
            mModel.createSingleTabGroup(home); Token replacement = home.getTabGroupId();
            mModel.setTabGroupTitle(replacement, mModel.getTabGroupTitle(id)); mModel.setTabGroupColor(replacement, 7);
            mOrder.set(index, replacement); mActive = replacement;
        } else { mOrder.remove(index); if (wasActive) mActive = mOrder.get(Math.min(index, mOrder.size() - 1)); }
        persist(); selectActive();
        if (!tabs.isEmpty()) mModel.getTabRemover().closeTabs(TabClosureParams.closeTabs(tabs).allowUndo(true).build(), true);
        return new JSONObject().put("ok", true).put("count", mOrder.size());
    }
    void tabRemoved(Tab tab) throws Exception {
        if (mProfile.isOffTheRecord() || !enabled() || tab.getTabGroupId() == null) return;
        List<Tab> remaining = mModel.getTabsInGroup(tab.getTabGroupId());
        if (remaining != null && !remaining.isEmpty()) return;
        Token id = tab.getTabGroupId(); mOrder.remove(id);
        if (Objects.equals(mActive, id)) mActive = mOrder.isEmpty() ? null : mOrder.get(0);
        if (mOrder.isEmpty()) {
            Tab home = mModel.getTabCreator().launchUrl("chrome://psychovault-pages/newtab.html?private=0", org.chromium.chrome.browser.tab.TabLaunchType.FROM_CHROME_UI);
            if (home != null) { mModel.createSingleTabGroup(home); mActive = home.getTabGroupId(); mOrder.add(mActive); mModel.setTabGroupTitle(mActive, "General"); }
        }
        persist(); selectActive();
    }
    private void requireEnabled() throws Exception { if (!enabled()) throw new IllegalStateException("Workspaces are unavailable in this profile"); }
    private void persist() throws Exception {
        JSONArray order = new JSONArray(); for (Token id : mOrder) order.put(id.toString());
        mStore.writeInternalDurable(mProfile, new JSONObject().put("workspaceOrder", order).put("workspaceActive", mActive == null ? "" : mActive.toString()));
    }
    private static Token parse(String raw) {
        if (raw == null || !raw.matches("[0-9a-fA-F]{32}")) return null;
        return new Token(Long.parseUnsignedLong(raw.substring(0, 16), 16), Long.parseUnsignedLong(raw.substring(16), 16));
    }
    private static String cleanName(String name) { String value = name == null ? "" : name.trim(); return value.isEmpty() ? "Workspace" : value.substring(0, Math.min(24, value.length())); }
}
