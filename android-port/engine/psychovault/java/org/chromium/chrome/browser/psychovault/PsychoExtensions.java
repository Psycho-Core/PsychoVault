package org.chromium.chrome.browser.psychovault;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.net.Uri;
import android.content.Context;

import org.json.JSONArray;
import org.json.JSONObject;
import org.chromium.base.ThreadUtils;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.content_public.browser.WebContents;
import org.jni_zero.CalledByNative;
import org.jni_zero.NativeMethods;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.function.Consumer;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/** General extension management. Load/install/uninstall results come from Chromium, not a guessed status. */
final class PsychoExtensions {
    static final String PROTON_ID = "jplgfhpmjnbigmhklmmbgecoobifkmpa";
    static final int PICK_ZIP = 0x7011, PICK_CRX = 0x7012;
    private static final String[] BUNDLED = {"proton-vpn", "dark-reader"};
    private static final long MAX_ARCHIVE = 50L * 1024 * 1024, MAX_UNPACKED = 150L * 1024 * 1024;
    private final Activity mActivity;
    private final Context mContext;
    private final Map<Integer, Consumer<JSONObject>> mPending = new HashMap<>();
    private final Map<String, File> mPendingRemoval = new HashMap<>();
    private int mSequence;
    private String mLastError = "";
    private Runnable mListener = () -> {};
    private Consumer<JSONObject> mPickerCallback;

    PsychoExtensions(Activity activity) { mActivity = activity; mContext = activity.getApplicationContext(); }
    void setListener(Runnable listener) { mListener = listener == null ? () -> {} : listener; }

    private File root(Profile profile) {
        if (profile == null || profile.isOffTheRecord()) throw new IllegalStateException("Extensions require a regular profile");
        String name = PsychoProfileStoreJni.get().getProfileName(profile);
        if (name == null || !name.matches("[A-Za-z0-9_.-]+") || name.equals(".") || name.equals("..")) throw new IllegalStateException("Profile storage unavailable");
        File dir = new File(mContext.getFilesDir(), "psychovault-extensions/" + name);
        if (!dir.isDirectory() && !dir.mkdirs()) throw new IllegalStateException("Extension storage unavailable");
        return dir;
    }

    void ensureBundled(Profile profile, PsychoProfileStore store) throws Exception {
        if (profile.isOffTheRecord()) return;
        JSONObject settings = store.read(profile);
        Set<String> removed = set(settings.optJSONArray("removedBundledExtensions"));
        for (String name : BUNDLED) {
            if (removed.contains(name)) continue;
            File dir = new File(root(profile), name);
            if (!validManifest(new File(dir, "manifest.json"))) { deleteTree(dir); extractAsset(name, dir); }
            if (!alreadyRegistered(profile, dir)) load(profile, dir, result -> { if (!result.optBoolean("ok")) mLastError = result.optString("error"); syncInstalled(profile, store); mListener.run(); });
        }
        JSONArray saved = settings.optJSONArray("extensions");
        if (saved != null) for (int i = 0; i < saved.length(); i++) {
            File dir = new File(root(profile), saved.getJSONObject(i).optString("dir"));
            if (dir.getParentFile().equals(root(profile)) && validManifest(new File(dir, "manifest.json")) && !alreadyRegistered(profile, dir)) load(profile, dir, result -> { syncInstalled(profile, store); mListener.run(); });
        }
    }

    void restoreBundled(Profile profile, PsychoProfileStore store) throws Exception {
        store.writeInternalDurable(profile, new JSONObject().put("removedBundledExtensions", new JSONArray()));
        ensureBundled(profile, store);
    }

    private boolean alreadyRegistered(Profile profile, File dir) throws Exception {
        JSONArray items = list(profile); String path = dir.getAbsolutePath();
        for (int i = 0; i < items.length(); i++) if (path.equals(items.getJSONObject(i).optString("path"))) return true;
        return false;
    }
    private void load(Profile profile, File dir, Consumer<JSONObject> callback) {
        int id = ++mSequence; mPending.put(id, callback);
        if (!PsychoExtensionsJni.get().loadUnpacked(profile, dir.getAbsolutePath(), this, id)) complete(id, false, "Extension installer unavailable");
        mActivity.getWindow().getDecorView().postDelayed(() -> { if (mPending.containsKey(id)) complete(id, false, "Extension load timed out"); }, 30000);
    }

    void pick(String kind, Consumer<JSONObject> callback) {
        if (mPickerCallback != null) { callback.accept(fail("Another file picker is already open")); return; }
        mPickerCallback = callback;
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("*/*");
        mActivity.startActivityForResult(intent, "crx".equals(kind) ? PICK_CRX : PICK_ZIP);
    }

    void handleActivityResult(int request, int result, Intent data, Profile profile, WebContents contents, PsychoProfileStore store) {
        if (request != PICK_ZIP && request != PICK_CRX) return;
        Consumer<JSONObject> callback = mPickerCallback; mPickerCallback = null;
        if (callback == null) return;
        if (result != Activity.RESULT_OK || data == null || data.getData() == null) { callback.accept(fail("Import cancelled")); return; }
        Uri uri = data.getData(); boolean crx = request == PICK_CRX;
        new Thread(() -> {
            File staged = null;
            try {
                File parent = crx ? new File(mContext.getCacheDir(), "psychovault-crx") : root(profile);
                if (!parent.isDirectory() && !parent.mkdirs()) throw new IllegalStateException("Extension staging unavailable");
                staged = new File(parent, (crx ? "import-" : "imported-") + UUID.randomUUID());
                if (crx) copyDocument(uri, staged, MAX_ARCHIVE); else extractDocument(uri, staged);
                final File ready = staged; staged = null;
                ThreadUtils.postOnUiThread(() -> {
                    if (crx) {
                        int id = ++mSequence;
                        mPending.put(id, value -> { if (!value.optBoolean("ok")) ready.delete(); else syncInstalled(profile, store); callback.accept(value); });
                        if (!PsychoExtensionsJni.get().installCrx(profile, contents, ready.getAbsolutePath(), this, id)) { ready.delete(); complete(id, false, "CRX installer unavailable"); }
                        mActivity.getWindow().getDecorView().postDelayed(() -> { if (mPending.containsKey(id)) { ready.delete(); complete(id, false, "CRX install timed out or was cancelled"); } }, 120000);
                    } else load(profile, ready, value -> { if (!value.optBoolean("ok")) deleteTree(ready); else syncInstalled(profile, store); callback.accept(value); });
                });
            } catch (Exception e) {
                if (staged != null) deleteTree(staged);
                ThreadUtils.postOnUiThread(() -> callback.accept(fail(PsychoBridge.safeMessage(e))));
            }
        }, "psychovault-extension-import").start();
    }

    private void copyDocument(Uri uri, File out, long limit) throws Exception {
        try (InputStream in = mContext.getContentResolver().openInputStream(uri); OutputStream output = new FileOutputStream(out)) {
            if (in == null) throw new IllegalStateException("Could not read the selected file");
            byte[] buffer = new byte[65536]; long total = 0; int n;
            while ((n = in.read(buffer)) > 0) { total += n; if (total > limit) throw new IllegalArgumentException("Extension file is too large"); output.write(buffer, 0, n); }
        }
    }
    private void extractDocument(Uri uri, File dir) throws Exception {
        if (!dir.mkdirs()) throw new IllegalStateException("Could not stage extension");
        try (InputStream in = mContext.getContentResolver().openInputStream(uri)) {
            if (in == null) throw new IllegalStateException("Could not read the selected archive");
            extract(new ZipInputStream(in), dir);
        } catch (Exception e) { deleteTree(dir); throw e; }
        if (!validManifest(new File(dir, "manifest.json"))) { deleteTree(dir); throw new IllegalArgumentException("Extension manifest missing or unsupported"); }
    }
    private void extractAsset(String name, File dir) throws Exception {
        File tmp = new File(dir.getParentFile(), name + ".tmp-" + System.nanoTime());
        if (!tmp.mkdirs()) throw new IllegalStateException("Could not stage extension");
        try (InputStream in = mContext.getAssets().open("psychovault/bundled/" + name + ".zip")) { extract(new ZipInputStream(in), tmp); }
        catch (Exception e) { deleteTree(tmp); throw e; }
        if (!validManifest(new File(tmp, "manifest.json"))) { deleteTree(tmp); throw new IllegalStateException("Bundled extension manifest missing or unsupported"); }
        if (dir.exists()) deleteTree(dir);
        if (!tmp.renameTo(dir)) { deleteTree(tmp); throw new IllegalStateException("Could not install bundled extension"); }
    }
    private static void extract(ZipInputStream zip, File dir) throws Exception {
        long total = 0; int entries = 0; ZipEntry entry;
        while ((entry = zip.getNextEntry()) != null) {
            if (++entries > 3000) throw new IllegalArgumentException("Extension archive has too many files");
            String path = entry.getName().replace('\\', '/');
            if (path.startsWith("/") || path.contains("../") || path.equals("..") || path.contains(":")) throw new IllegalStateException("Unsafe extension archive path");
            File out = new File(dir, path);
            if (!out.toPath().normalize().startsWith(dir.toPath())) throw new IllegalStateException("Unsafe extension archive path");
            if (entry.isDirectory()) { if (!out.isDirectory() && !out.mkdirs()) throw new IllegalStateException("Could not create extension directory"); continue; }
            if (!out.getParentFile().isDirectory() && !out.getParentFile().mkdirs()) throw new IllegalStateException("Could not create extension directory");
            try (FileOutputStream output = new FileOutputStream(out)) { byte[] buffer = new byte[65536]; int n; while ((n = zip.read(buffer)) > 0) { total += n; if (total > MAX_UNPACKED) throw new IllegalArgumentException("Extension archive expands too far"); output.write(buffer, 0, n); } }
        }
    }
    private static boolean validManifest(File file) {
        try (InputStream in = new java.io.FileInputStream(file)) {
            byte[] data = PsychoBridge.readLimited(in, 2 * 1024 * 1024);
            JSONObject manifest = new JSONObject(new String(data, java.nio.charset.StandardCharsets.UTF_8));
            int version = manifest.optInt("manifest_version");
            return (version == 2 || version == 3) && !manifest.optString("name").isEmpty() && !manifest.optString("version").isEmpty();
        } catch (Exception e) { return false; }
    }
    private static void deleteTree(File file) { if (file == null) return; File[] children = file.listFiles(); if (children != null) for (File child : children) deleteTree(child); file.delete(); }

    JSONArray list(Profile profile) throws Exception { String json = PsychoExtensionsJni.get().list(profile); return new JSONArray(json == null ? "[]" : json); }
    JSONObject status(Profile profile) throws Exception {
        JSONArray items = list(profile);
        JSONObject out = new JSONObject().put("items", items).put("lastError", mLastError).put("pending", mPending.size());
        for (int i = 0; i < items.length(); i++) if (PROTON_ID.equals(items.getJSONObject(i).optString("id"))) out.put("proton", items.getJSONObject(i));
        return out;
    }
    String resolveId(Profile profile, String value) throws Exception {
        JSONArray items = list(profile);
        for (int i = 0; i < items.length(); i++) { JSONObject item = items.getJSONObject(i); if (value != null && (value.equals(item.optString("id")) || value.equals(item.optString("dir")))) { requireId(item.getString("id")); return item.getString("id"); } }
        throw new IllegalArgumentException("Unknown extension");
    }
    String resolvePage(Profile profile, String id, String page) throws Exception {
        requireId(id); JSONArray items = list(profile);
        for (int i = 0; i < items.length(); i++) {
            JSONObject item = items.getJSONObject(i); if (!id.equals(item.optString("id"))) continue;
            if (page == null || page.length() > 512 || page.startsWith("/") || page.contains("..") || page.contains("\\") || page.contains("?") || page.contains("#")) throw new IllegalArgumentException("Invalid extension page");
            File root = new File(item.optString("path")).getCanonicalFile(), target = new File(root, page).getCanonicalFile();
            if (!target.toPath().startsWith(root.toPath()) || !target.isFile()) throw new IllegalArgumentException("Extension page not found");
            return page;
        }
        throw new IllegalArgumentException("Unknown extension");
    }
    JSONObject toggle(Profile profile, String id, boolean enabled) throws Exception {
        id = resolveId(profile, id);
        boolean ok = PsychoExtensionsJni.get().setEnabled(profile, id, enabled);
        if (!ok) throw new IllegalStateException("Chromium refused the extension state change");
        return new JSONObject().put("ok", true).put("items", list(profile));
    }
    void remove(Profile profile, String id, PsychoProfileStore store, Consumer<JSONObject> callback) throws Exception {
        id = resolveId(profile, id); JSONArray items = list(profile); JSONObject target = null;
        for (int i = 0; i < items.length(); i++) if (id.equals(items.getJSONObject(i).optString("id"))) target = items.getJSONObject(i);
        if (target == null) throw new IllegalArgumentException("Unknown extension");
        final String resolved = id; final JSONObject item = target;
        int request = ++mSequence;
        mPending.put(request, result -> {
            if (!result.optBoolean("ok")) { callback.accept(result); return; }
            try {
                if (item.optBoolean("bundled")) {
                    JSONArray removed = store.read(profile).optJSONArray("removedBundledExtensions"); if (removed == null) removed = new JSONArray();
                    removed.put(item.optString("dir")); store.writeInternalDurable(profile, new JSONObject().put("removedBundledExtensions", removed));
                } else { File dir = new File(item.optString("path")); if (dir.getParentFile().equals(root(profile))) deleteTree(dir); syncInstalled(profile, store); }
                callback.accept(new JSONObject().put("ok", true).put("items", list(profile)));
            } catch (Exception e) { callback.accept(fail(PsychoBridge.safeMessage(e))); }
        });
        if (!PsychoExtensionsJni.get().uninstall(profile, resolved, this, request)) complete(request, false, "Chromium refused to uninstall this extension");
    }
    void syncInstalled(Profile profile, PsychoProfileStore store) {
        try {
            JSONArray items = list(profile), saved = new JSONArray();
            for (int i = 0; i < items.length(); i++) { JSONObject item = items.getJSONObject(i); if (item.optBoolean("bundled")) continue; File dir = new File(item.optString("path")); if (!dir.getParentFile().equals(root(profile))) continue; saved.put(new JSONObject().put("id", item.getString("id")).put("dir", dir.getName()).put("enabled", item.optBoolean("enabled"))); }
            store.writeInternalDurable(profile, new JSONObject().put("extensions", saved));
        } catch (Exception e) { mLastError = "Extension inventory could not be saved"; }
    }
    private void complete(int id, boolean ok, String value) {
        Consumer<JSONObject> callback = mPending.remove(id); if (callback == null) return;
        try { callback.accept(new JSONObject().put("ok", ok).put(ok ? "id" : "error", value == null || value.isEmpty() ? (ok ? "" : "Extension operation failed") : value)); }
        catch (Exception e) { callback.accept(fail("Extension operation failed")); }
    }
    @CalledByNative
    private void onInstallPrompt(long pointer, String name, String permissionsJson) {
        ThreadUtils.assertOnUiThread();
        StringBuilder message = new StringBuilder("Install ").append(name).append("?\n\nPermissions requested:\n");
        try { JSONArray permissions = new JSONArray(permissionsJson); if (permissions.length() == 0) message.append("No additional permissions listed."); for (int i = 0; i < permissions.length(); i++) message.append("• ").append(permissions.getString(i)).append('\n'); }
        catch (Exception e) { message.append("Permission details unavailable; cancel if unsure."); }
        new AlertDialog.Builder(mActivity).setTitle("Install extension").setMessage(message.toString())
                .setPositiveButton("Install", (d, w) -> PsychoExtensionsJni.get().resolveInstallPrompt(pointer, true))
                .setNegativeButton("Cancel", (d, w) -> PsychoExtensionsJni.get().resolveInstallPrompt(pointer, false))
                .setOnCancelListener(d -> PsychoExtensionsJni.get().resolveInstallPrompt(pointer, false)).show();
    }
    @CalledByNative private void onNativeResult(int request, boolean ok, String value) { ThreadUtils.assertOnUiThread(); complete(request, ok, value); mListener.run(); }
    private static JSONObject fail(String text) { try { return new JSONObject().put("ok", false).put("error", text); } catch (Exception e) { throw new IllegalStateException(e); } }
    private static Set<String> set(JSONArray array) throws Exception { Set<String> out = new HashSet<>(); if (array != null) for (int i = 0; i < array.length(); i++) out.add(array.getString(i)); return out; }
    static void requireId(String id) { if (id == null || !id.matches("[a-p]{32}")) throw new IllegalArgumentException("Invalid extension ID"); }
    void destroy() { mPending.clear(); mPickerCallback = null; mListener = () -> {}; }

    @NativeMethods
    interface Natives {
        String list(Profile profile);
        boolean setEnabled(Profile profile, String id, boolean enabled);
        boolean uninstall(Profile profile, String id, PsychoExtensions owner, int request);
        boolean loadUnpacked(Profile profile, String path, PsychoExtensions owner, int request);
        boolean installCrx(Profile profile, WebContents contents, String path, PsychoExtensions owner, int request);
        void resolveInstallPrompt(long pointer, boolean accepted);
    }
}
