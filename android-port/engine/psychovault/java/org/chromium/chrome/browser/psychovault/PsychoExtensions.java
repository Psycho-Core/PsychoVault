package org.chromium.chrome.browser.psychovault;

import android.content.Context;

import org.json.JSONArray;
import org.json.JSONObject;
import org.chromium.base.ThreadUtils;
import org.chromium.chrome.browser.profiles.Profile;
import org.jni_zero.NativeMethods;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.util.HashSet;
import java.util.Set;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/**
 * Extension management for the trusted PsychoVault chrome only.
 * Bundled extensions are unpacked into the regular profile and loaded through
 * Chromium's UnpackedInstaller. Proton keeps its manifest key, so its extension
 * ID is unchanged; this does not sign a CRX or impersonate a store signature.
 * Loading an extension is not evidence that its proxy, auth or leak behaviour works.
 */
final class PsychoExtensions {
    static final String PROTON_ID = "jplgfhpmjnbigmhklmmbgecoobifkmpa";
    private static final String[] BUNDLED = {"proton-vpn", "dark-reader"};
    private final Context mContext;

    PsychoExtensions(Context context) { mContext = context.getApplicationContext(); }

    private File root(Profile profile) {
        if (profile == null || profile.isOffTheRecord()) throw new IllegalStateException("Extensions require a regular profile");
        String name = PsychoProfileStoreJni.get().getProfileName(profile);
        if (name == null || name.isEmpty()) throw new IllegalStateException("Profile storage unavailable");
        File dir = new File(mContext.getFilesDir(), "psychovault-extensions/" + name);
        if (!dir.isDirectory() && !dir.mkdirs()) throw new IllegalStateException("Extension storage unavailable");
        return dir;
    }

    /** Install bundled extensions once per profile unless the user removed them. */
    void ensureBundled(Profile profile, PsychoProfileStore store) throws Exception {
        if (profile.isOffTheRecord()) return;
        JSONObject settings = store.read(profile);
        Set<String> removed = new HashSet<>();
        JSONArray arr = settings.optJSONArray("removedBundledExtensions");
        if (arr != null) for (int i = 0; i < arr.length(); i++) removed.add(arr.getString(i));
        for (String name : BUNDLED) {
            if (removed.contains(name)) continue;
            File dir = new File(root(profile), name);
            if (!new File(dir, "manifest.json").isFile()) extract(name, dir);
            PsychoExtensionsJni.get().loadUnpacked(profile, dir.getAbsolutePath());
        }
    }

    void restoreBundled(Profile profile, PsychoProfileStore store) throws Exception {
        JSONObject patch = new JSONObject().put("removedBundledExtensions", new JSONArray());
        store.writeInternal(profile, patch);
        ensureBundled(profile, store);
    }

    private void extract(String name, File dir) throws Exception {
        File tmp = new File(dir.getParentFile(), name + ".tmp-" + System.nanoTime());
        if (!tmp.mkdirs()) throw new IllegalStateException("Could not stage extension");
        try (InputStream in = mContext.getAssets().open("psychovault/bundled/" + name + ".zip");
             ZipInputStream zip = new ZipInputStream(in)) {
            ZipEntry entry;
            while ((entry = zip.getNextEntry()) != null) {
                if (entry.isDirectory()) continue;
                String path = entry.getName().replace('\\', '/');
                if (path.startsWith("/") || path.contains("../") || path.equals("..")) throw new IllegalStateException("Unsafe extension archive path");
                File out = new File(tmp, path);
                if (!out.toPath().normalize().startsWith(tmp.toPath())) throw new IllegalStateException("Unsafe extension archive path");
                if (!out.getParentFile().isDirectory() && !out.getParentFile().mkdirs()) throw new IllegalStateException("Could not create extension directory");
                try (FileOutputStream output = new FileOutputStream(out)) {
                    byte[] buffer = new byte[65536];
                    int n;
                    while ((n = zip.read(buffer)) > 0) output.write(buffer, 0, n);
                }
            }
        } catch (Exception e) {
            deleteTree(tmp);
            throw e;
        }
        if (!new File(tmp, "manifest.json").isFile()) { deleteTree(tmp); throw new IllegalStateException("Extension manifest missing"); }
        if (dir.exists()) deleteTree(dir);
        if (!tmp.renameTo(dir)) { deleteTree(tmp); throw new IllegalStateException("Could not install bundled extension"); }
    }

    private static void deleteTree(File file) {
        File[] children = file.listFiles();
        if (children != null) for (File child : children) deleteTree(child);
        file.delete();
    }

    JSONArray list(Profile profile) throws Exception {
        String json = PsychoExtensionsJni.get().list(profile);
        return new JSONArray(json == null ? "[]" : json);
    }

    JSONObject status(Profile profile) throws Exception {
        JSONArray items = list(profile);
        JSONObject out = new JSONObject().put("items", items).put("proxy", true).put("privacy", true);
        for (int i = 0; i < items.length(); i++) {
            JSONObject item = items.getJSONObject(i);
            if (PROTON_ID.equals(item.optString("id"))) out.put("proton", item);
        }
        return out;
    }

    String resolveId(Profile profile, String value) throws Exception {
        if (value != null && value.matches("[a-p]{32}")) return value;
        JSONArray items = list(profile);
        for (int i = 0; i < items.length(); i++) {
            JSONObject item = items.getJSONObject(i);
            if (value != null && value.equals(item.optString("dir"))) return item.getString("id");
        }
        throw new IllegalArgumentException("Unknown extension");
    }

    void toggle(Profile profile, String id, boolean enabled) throws Exception {
        id = resolveId(profile, id);
        requireId(id);
        PsychoExtensionsJni.get().setEnabled(profile, id, enabled);
    }

    void remove(Profile profile, String id, PsychoProfileStore store) throws Exception {
        id = resolveId(profile, id);
        requireId(id);
        JSONArray items = list(profile);
        JSONObject target = null;
        for (int i = 0; i < items.length(); i++) {
            JSONObject item = items.getJSONObject(i);
            if (id.equals(item.optString("id"))) { target = item; break; }
        }
        PsychoExtensionsJni.get().uninstall(profile, id);
        if (target != null) {
            JSONObject item = target;
            if (item.optBoolean("bundled")) {
                JSONObject settings = store.read(profile);
                JSONArray removed = settings.optJSONArray("removedBundledExtensions");
                if (removed == null) removed = new JSONArray();
                removed.put(item.optString("dir"));
                store.writeInternal(profile, new JSONObject().put("removedBundledExtensions", removed));
            }
        }
    }

    static void requireId(String id) {
        if (id == null || !id.matches("[a-p]{32}")) throw new IllegalArgumentException("Invalid extension ID");
    }

    @NativeMethods
    interface Natives {
        String list(Profile profile);
        void setEnabled(Profile profile, String id, boolean enabled);
        void uninstall(Profile profile, String id);
        void loadUnpacked(Profile profile, String path);
    }
}
