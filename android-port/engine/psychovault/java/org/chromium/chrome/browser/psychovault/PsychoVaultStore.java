package org.chromium.chrome.browser.psychovault;

import android.content.ClipData;
import android.content.ClipDescription;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.Intent;
import android.net.Uri;
import android.os.Handler;
import android.os.Looper;
import android.os.PersistableBundle;
import android.util.Base64;

import org.chromium.base.ThreadUtils;
import org.chromium.chrome.browser.profiles.Profile;
import org.jni_zero.NativeMethods;
import org.json.JSONArray;
import org.json.JSONObject;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;
import java.security.SecureRandom;
import java.util.Arrays;
import java.util.UUID;
import java.util.function.Consumer;

import javax.crypto.Cipher;
import javax.crypto.spec.GCMParameterSpec;
import javax.crypto.spec.SecretKeySpec;

/**
 * The existing PsychoVault master-password format, not a second password manager.
 * vault.bin is IV(12) || AES-256-GCM ciphertext+tag. vault.key.json contains the
 * same scrypt salt/verifier as desktop. Decrypted entries and key material live
 * only in this host object while unlocked; websites never receive this object.
 * Java strings cannot be reliably zeroed, so memory scrubbing is best-effort.
 */
final class PsychoVaultStore {
    private static final int MAX_FILE = 20 * 1024 * 1024;
    private final Context mContext;
    private final File mDir;
    private final Handler mHandler = new Handler(Looper.getMainLooper());
    private final SecureRandom mRandom = new SecureRandom();
    private final Runnable mListener;
    private JSONArray mEntries;
    private byte[] mKey;
    private int mLockMin = 5;
    private boolean mEnabled = true;
    private boolean mBusy;
    private boolean mDestroyed;
    private long mClipboardAt;
    private String mTransferAction, mTransferPassword;
    private Consumer<JSONObject> mTransferCallback;
    static final int PICK_BACKUP = 0x7013, PICK_RESTORE = 0x7014, PICK_CSV = 0x7015;
    private Runnable mLockTask;
    private final Runnable mClipboardTask = () -> {
        clearOwnClipboard(); lock();
    };

    PsychoVaultStore(Context context, Profile profile, Runnable listener) throws Exception {
        ThreadUtils.assertOnUiThread();
        if (profile == null || profile.isOffTheRecord()) throw new IllegalArgumentException("The vault requires a regular profile");
        mContext = context.getApplicationContext();
        String name = PsychoProfileStoreJni.get().getProfileName(profile);
        if (name == null || !name.matches("[A-Za-z0-9_.-]+") || name.equals(".") || name.equals("..")) throw new IllegalStateException("Profile storage unavailable");
        mDir = new File(mContext.getFilesDir(), "psychovault-vault/" + name);
        if (!mDir.isDirectory() && !mDir.mkdirs()) throw new IllegalStateException("Vault storage unavailable");
        mListener = listener == null ? () -> {} : listener;
    }

    void configure(JSONObject settings) {
        mEnabled = settings.optBoolean("vaultEnabled", true);
        mLockMin = Math.max(0, Math.min(1440, settings.optInt("vaultLockMin", 5)));
        if (!mEnabled) lock(); else armLock();
    }

    JSONObject status() throws Exception {
        return new JSONObject().put("enabled", mEnabled).put("method", metaFile().isFile() ? "master" : "none")
                .put("locked", mKey == null).put("hasFile", dataFile().isFile()).put("count", mEntries == null ? 0 : mEntries.length())
                .put("lockMin", mLockMin).put("canStore", metaFile().isFile()).put("recovery", marker().isFile());
    }

    JSONObject list() throws Exception {
        if (!mEnabled) return new JSONObject().put("error", "disabled").put("entries", new JSONArray()).put("status", status());
        if (mKey == null) return new JSONObject().put("error", "locked").put("entries", new JSONArray()).put("status", status());
        JSONArray entries = new JSONArray();
        for (int i = 0; i < mEntries.length(); i++) {
            JSONObject e = mEntries.getJSONObject(i), out = new JSONObject();
            for (String key : new String[]{"id", "site", "user", "note", "at"}) out.put(key, e.opt(key));
            entries.put(out);
        }
        return new JSONObject().put("entries", entries).put("status", status());
    }

    void unlock(String password, Consumer<JSONObject> callback) {
        derive(password, null, key -> {
            try {
                if (mDestroyed) return;
                if (key == null) throw new IllegalArgumentException("Could not derive the vault key");
                if (marker().isFile()) throw new IllegalStateException("Vault recovery is required; files were left unchanged.");
                JSONObject meta = readJson(metaFile());
                if (!"scrypt-N16384-r8-p1".equals(meta.optString("kdf"))) throw new IllegalArgumentException("Unsupported vault key format");
                JSONObject check = new JSONObject(new String(open(Base64.decode(meta.getString("check"), Base64.NO_WRAP), key), StandardCharsets.UTF_8));
                if (check.optInt("v") != 1) throw new IllegalArgumentException("Wrong master password");
                JSONArray entries = dataFile().isFile() ? new JSONArray(new String(open(read(dataFile()), key), StandardCharsets.UTF_8)) : new JSONArray();
                validateEntries(entries);
                mKey = key; key = null; mEntries = entries; armLock(); mListener.run();
                callback.accept(ok());
            } catch (Exception e) {
                lock(); callback.accept(fail(e));
            } finally { if (key != null) Arrays.fill(key, (byte) 0); }
        });
    }

    void setMaster(String password, Consumer<JSONObject> callback) {
        if (password == null || password.length() < 8 || password.length() > 1024) { callback.accept(reason("Use between 8 and 1024 characters")); return; }
        if (dataFile().isFile() && mKey == null) { callback.accept(reason("Unlock the existing vault first")); return; }
        final byte[] salt = bytes(16);
        derive(password, salt, key -> {
            byte[] oldKey = mKey; JSONArray oldEntries = mEntries;
            try {
                if (mDestroyed || key == null) throw new IllegalStateException("Vault key derivation failed");
                JSONObject meta = new JSONObject().put("salt", b64(salt)).put("check", b64(seal("{\"v\":1}".getBytes(StandardCharsets.UTF_8), key)))
                        .put("kdf", "scrypt-N16384-r8-p1").put("aead", "aes-256-gcm");
                JSONArray entries = oldEntries == null ? new JSONArray() : oldEntries;
                byte[] encrypted = seal(entries.toString().getBytes(StandardCharsets.UTF_8), key);
                // A journal and both old copies survive a failed migration. No plaintext fallback.
                atomic(marker(), "{\"version\":1,\"state\":\"master-change\"}".getBytes(StandardCharsets.UTF_8));
                if (metaFile().isFile()) atomic(new File(mDir, "vault.key.json.backup"), read(metaFile()));
                if (dataFile().isFile()) atomic(new File(mDir, "vault.bin.backup"), read(dataFile()));
                atomic(metaFile(), meta.toString().getBytes(StandardCharsets.UTF_8));
                atomic(dataFile(), encrypted);
                if (!marker().delete()) throw new IllegalStateException("Vault migration journal could not be cleared");
                mKey = key; key = null; if (oldKey != null && oldKey != mKey) Arrays.fill(oldKey, (byte) 0); mEntries = entries; armLock(); mListener.run(); callback.accept(ok());
            } catch (Exception e) {
                mKey = oldKey; mEntries = oldEntries; callback.accept(fail(e));
            } finally { if (key != null) Arrays.fill(key, (byte) 0); }
        });
    }

    JSONObject add(JSONObject input) throws Exception {
        requireUnlocked();
        String raw = input.optString("site", "").trim();
        if (raw.length() > 2048) throw new IllegalArgumentException("Site is too long");
        org.chromium.url.GURL url = new org.chromium.url.GURL(raw.contains("://") ? raw : "https://" + raw);
        if (!url.isValid() || !"https".equals(url.getScheme()) || !url.getUsername().isEmpty() || !url.getPassword().isEmpty()) throw new IllegalArgumentException("Use an HTTPS site without embedded credentials");
        String site = url.getHost().toLowerCase(java.util.Locale.ROOT), user = input.optString("user", ""), pass = input.optString("pass", ""), note = input.optString("note", "");
        if (site.isEmpty() || pass.isEmpty() || pass.length() > 512 || user.length() > 200 || note.length() > 2000) throw new IllegalArgumentException("Invalid vault entry");
        JSONObject entry = new JSONObject().put("id", input.optString("id", UUID.randomUUID().toString())).put("site", site)
                .put("user", user).put("pass", pass).put("note", note).put("at", System.currentTimeMillis());
        JSONArray next = copy(mEntries);
        int at = -1;
        for (int i = 0; i < next.length(); i++) if (site.equals(next.getJSONObject(i).optString("site")) && user.equals(next.getJSONObject(i).optString("user"))) { at = i; break; }
        if (at >= 0) next.put(at, entry); else next.put(entry);
        commit(next); return ok();
    }

    JSONObject remove(String id) throws Exception {
        requireUnlocked(); JSONArray next = new JSONArray();
        for (int i = 0; i < mEntries.length(); i++) if (!mEntries.getJSONObject(i).optString("id").equals(id)) next.put(mEntries.getJSONObject(i));
        commit(next); return ok();
    }

    JSONObject reveal(String id) throws Exception {
        requireUnlocked();
        for (int i = 0; i < mEntries.length(); i++) if (mEntries.getJSONObject(i).optString("id").equals(id)) { armLock(); return new JSONObject().put("pass", mEntries.getJSONObject(i).getString("pass")); }
        return new JSONObject().put("error", "not found");
    }

    JSONObject copy(String id, String which) throws Exception {
        requireUnlocked();
        for (int i = 0; i < mEntries.length(); i++) {
            JSONObject e = mEntries.getJSONObject(i);
            if (!e.optString("id").equals(id)) continue;
            ClipboardManager clipboard = (ClipboardManager) mContext.getSystemService(Context.CLIPBOARD_SERVICE);
            ClipData clip = ClipData.newPlainText("PsychoVault", e.optString("user".equals(which) ? "user" : "pass"));
            PersistableBundle extra = new PersistableBundle(); extra.putBoolean(ClipDescription.EXTRA_IS_SENSITIVE, true);
            clip.getDescription().setExtras(extra); clipboard.setPrimaryClip(clip);
            mClipboardAt = clip.getDescription().getTimestamp();
            mHandler.removeCallbacks(mClipboardTask);
            if (!"user".equals(which)) mHandler.postDelayed(mClipboardTask, 60000);
            armLock(); return new JSONObject().put("ok", true);
        }
        return new JSONObject().put("error", "not found");
    }

    String generate(JSONObject options) {
        int count = Math.max(3, Math.min(8, options.optInt("words", 4)));
        String[] words = "anchor atlas beacon branch candle canyon cedar cinder cobalt copper coral cradle crater crimson cypress daisy delta ember fathom flint forest garnet glacier granite harbor heron indigo ivory jasper juniper kestrel lantern lattice lotus maple marble meadow mesa nebula nickel oak obsidian onyx opal orion pebble pine pixel quartz quill raven reef ridge rocket rose ruby saffron sage slate solstice sparrow spruce steel summit timber topaz tundra umber valley velvet vertex violet walnut willow zephyr zenith".split(" ");
        if ("chars".equals(options.optString("mode"))) {
            String alphabet = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789!@#$%^&*-_+=";
            int length = Math.max(8, Math.min(64, options.optInt("length", 20))); StringBuilder out = new StringBuilder();
            for (int i = 0; i < length; i++) out.append(alphabet.charAt(mRandom.nextInt(alphabet.length())));
            return out.toString();
        }
        StringBuilder out = new StringBuilder();
        for (int i = 0; i < count; i++) { String word = words[mRandom.nextInt(words.length)]; out.append(i == 0 ? Character.toUpperCase(word.charAt(0)) : word.charAt(0)).append(word.substring(1)).append('-'); }
        out.append(10 + mRandom.nextInt(90));
        return out.toString();
    }

    JSONObject recover() throws Exception {
        if (!marker().isFile()) return reason("No vault recovery journal is present");
        File metaBackup = new File(mDir, "vault.key.json.backup"), dataBackup = new File(mDir, "vault.bin.backup");
        if (!metaBackup.isFile() || !dataBackup.isFile()) return reason("Recovery backups are missing. The encrypted files were left unchanged.");
        byte[] meta = read(metaBackup), data = read(dataBackup);
        atomic(metaFile(), meta); atomic(dataFile(), data);
        if (!marker().delete()) throw new IllegalStateException("Vault recovery journal could not be cleared");
        lock(); mListener.run(); return ok();
    }

    void pickTransfer(android.app.Activity activity, String action, String password, Consumer<JSONObject> callback) {
        try {
            if (!"backup".equals(action) && !"restore".equals(action) && !"csv".equals(action)) throw new IllegalArgumentException("Unknown transfer action");
            if ("backup".equals(action) && (password == null || password.length() < 12 || password.length() > 1024)) throw new IllegalArgumentException("Backup password must be at least 12 characters");
            requireUnlocked();
            if (mTransferCallback != null) throw new IllegalStateException("Another vault transfer is already open");
            mTransferAction = action; mTransferPassword = password == null ? "" : password; mTransferCallback = callback;
            Intent intent;
            int request;
            if ("backup".equals(action)) { intent = new Intent(Intent.ACTION_CREATE_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("application/octet-stream").putExtra(Intent.EXTRA_TITLE, "PsychoVault-backup.pvbackup"); request = PICK_BACKUP; }
            else { intent = new Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("*/*"); request = "csv".equals(action) ? PICK_CSV : PICK_RESTORE; }
            activity.startActivityForResult(intent, request);
        } catch (Exception e) { mTransferCallback = null; callback.accept(fail(e)); }
    }

    void handleActivityResult(int request, int result, Intent data) {
        if (request != PICK_BACKUP && request != PICK_RESTORE && request != PICK_CSV) return;
        final Consumer<JSONObject> callback = mTransferCallback; final String action = mTransferAction, password = mTransferPassword;
        mTransferCallback = null; mTransferAction = null; mTransferPassword = null;
        if (callback == null) return;
        if (result != android.app.Activity.RESULT_OK || data == null || data.getData() == null) { callback.accept(reason("Transfer cancelled")); return; }
        final Uri uri = data.getData();
        new Thread(() -> {
            try {
                byte[] bytes;
                if ("backup".equals(action)) bytes = null;
                else { try (java.io.InputStream in = mContext.getContentResolver().openInputStream(uri)) { if (in == null) throw new IllegalStateException("Could not read the selected file"); bytes = PsychoBridge.readLimited(in, MAX_FILE); } }
                ThreadUtils.postOnUiThread(() -> processTransfer(action, password, uri, bytes, callback));
            } catch (Exception e) { ThreadUtils.postOnUiThread(() -> callback.accept(fail(e))); }
        }, "psychovault-vault-transfer").start();
    }

    private void processTransfer(String action, String password, Uri uri, byte[] bytes, Consumer<JSONObject> callback) {
        try {
            requireUnlocked();
            if ("backup".equals(action)) {
                final JSONArray entries = copy(mEntries); final byte[] salt = bytes(16);
                derive(password, salt, key -> {
                    try {
                        if (key == null) throw new IllegalStateException("Backup key derivation failed");
                        byte[] sealed = seal(entries.toString().getBytes(StandardCharsets.UTF_8), key);
                        byte[] iv = Arrays.copyOfRange(sealed, 0, 12), body = Arrays.copyOfRange(sealed, 12, sealed.length - 16), tag = Arrays.copyOfRange(sealed, sealed.length - 16, sealed.length);
                        JSONObject backup = new JSONObject().put("format", "psychovault-backup").put("version", 1).put("kdf", "scrypt-16384-8-1")
                                .put("salt", b64(salt)).put("iv", b64(iv)).put("tag", b64(tag)).put("data", b64(body));
                        try (java.io.OutputStream out = mContext.getContentResolver().openOutputStream(uri, "wt")) { if (out == null) throw new IllegalStateException("Backup output unavailable"); out.write(backup.toString().getBytes(StandardCharsets.UTF_8)); }
                        armLock(); callback.accept(new JSONObject().put("ok", true).put("count", entries.length()).put("skipped", 0));
                    } catch (Exception e) { callback.accept(fail(e)); }
                    finally { if (key != null) Arrays.fill(key, (byte) 0); }
                });
                return;
            }
            String text = new String(bytes, StandardCharsets.UTF_8);
            JSONArray incoming;
            if ("csv".equals(action)) incoming = parseCsv(text);
            else {
                JSONObject backup = new JSONObject(text);
                if (!"psychovault-backup".equals(backup.optString("format")) || backup.optInt("version") != 1 || !"scrypt-16384-8-1".equals(backup.optString("kdf"))) throw new IllegalArgumentException("Unsupported backup format");
                byte[] salt = Base64.decode(backup.getString("salt"), Base64.NO_WRAP), iv = Base64.decode(backup.getString("iv"), Base64.NO_WRAP), tag = Base64.decode(backup.getString("tag"), Base64.NO_WRAP), body = Base64.decode(backup.getString("data"), Base64.NO_WRAP);
                if (salt.length != 16 || iv.length != 12 || tag.length != 16 || body.length > MAX_FILE) throw new IllegalArgumentException("Invalid backup");
                derive(password, salt, key -> {
                    try {
                        if (key == null) throw new IllegalStateException("Backup key derivation failed");
                        byte[] sealed = new byte[body.length + tag.length]; System.arraycopy(body, 0, sealed, 0, body.length); System.arraycopy(tag, 0, sealed, body.length, tag.length);
                        byte[] packed = new byte[12 + sealed.length]; System.arraycopy(iv, 0, packed, 0, 12); System.arraycopy(sealed, 0, packed, 12, sealed.length);
                        JSONArray entries = new JSONArray(new String(open(packed, key), StandardCharsets.UTF_8));
                        callback.accept(merge(entries));
                    } catch (Exception e) { callback.accept(fail(e)); }
                    finally { if (key != null) Arrays.fill(key, (byte) 0); }
                });
                return;
            }
            callback.accept(merge(incoming));
        } catch (Exception e) { callback.accept(fail(e)); }
    }

    private JSONObject merge(JSONArray incoming) throws Exception {
        requireUnlocked(); validateEntries(incoming);
        java.util.Set<String> keys = new java.util.HashSet<>();
        for (int i = 0; i < mEntries.length(); i++) { JSONObject e = mEntries.getJSONObject(i); keys.add(e.optString("site").toLowerCase(java.util.Locale.ROOT) + "\0" + e.optString("user")); }
        JSONArray next = copy(mEntries); int skipped = 0;
        for (int i = 0; i < incoming.length(); i++) {
            JSONObject e = incoming.getJSONObject(i); String site = e.optString("site").trim().toLowerCase(java.util.Locale.ROOT), user = e.optString("user"), pass = e.optString("pass");
            if (site.isEmpty() || pass.isEmpty() || pass.length() > 512 || user.length() > 200 || e.optString("note").length() > 2000) { skipped++; continue; }
            String key = site + "\0" + user; if (!keys.add(key)) { skipped++; continue; }
            next.put(new JSONObject().put("id", UUID.randomUUID().toString()).put("site", site).put("user", user).put("pass", pass).put("note", e.optString("note")).put("at", System.currentTimeMillis()));
        }
        int count = next.length() - mEntries.length(); commit(next);
        return new JSONObject().put("ok", true).put("count", count).put("skipped", skipped).put("status", status());
    }

    private static JSONArray parseCsv(String text) throws Exception {
        if (text.startsWith("\uFEFF")) text = text.substring(1);
        java.util.List<java.util.List<String>> rows = new java.util.ArrayList<>(); java.util.List<String> row = new java.util.ArrayList<>();
        StringBuilder field = new StringBuilder(); boolean quoted = false, closed = false;
        for (int i = 0; i < text.length(); i++) {
            char c = text.charAt(i);
            if (quoted) { if (c == '"' && i + 1 < text.length() && text.charAt(i + 1) == '"') { field.append('"'); i++; } else if (c == '"') { quoted = false; closed = true; } else field.append(c); continue; }
            if (c == '"' && field.length() == 0 && !closed) { quoted = true; continue; }
            if (c == ',' || c == '\n' || c == '\r') { row.add(field.toString()); field.setLength(0); closed = false; if (c != ',') { rows.add(row); row = new java.util.ArrayList<>(); if (c == '\r' && i + 1 < text.length() && text.charAt(i + 1) == '\n') i++; } continue; }
            if (c == '"' || closed) throw new IllegalArgumentException("Malformed CSV quoting");
            field.append(c);
        }
        if (quoted) throw new IllegalArgumentException("Unclosed CSV quote");
        if (field.length() > 0 || !row.isEmpty() || closed) { row.add(field.toString()); rows.add(row); }
        if (rows.isEmpty()) throw new IllegalArgumentException("Empty CSV file");
        java.util.List<String> headers = rows.remove(0); for (int i = 0; i < headers.size(); i++) headers.set(i, headers.get(i).trim().toLowerCase(java.util.Locale.ROOT));
        int site = first(headers, "url", "origin", "website"), user = first(headers, "username", "user", "login"), pass = first(headers, "password"), note = first(headers, "note", "notes");
        if (site < 0 || pass < 0) throw new IllegalArgumentException("CSV must contain url and password columns");
        JSONArray out = new JSONArray();
        for (java.util.List<String> r : rows) out.put(new JSONObject().put("site", cell(r, site)).put("user", cell(r, user)).put("pass", cell(r, pass)).put("note", cell(r, note)));
        return out;
    }
    private static int first(java.util.List<String> headers, String... names) { for (String name : names) { int i = headers.indexOf(name); if (i >= 0) return i; } return -1; }
    private static String cell(java.util.List<String> row, int index) { return index >= 0 && index < row.size() ? row.get(index) : ""; }

    JSONObject lock() {
        ThreadUtils.assertOnUiThread();
        if (mKey != null) Arrays.fill(mKey, (byte) 0);
        mKey = null; mEntries = null;
        if (mLockTask != null) mHandler.removeCallbacks(mLockTask);
        mLockTask = null;
        try { return status(); } catch (Exception e) { return reason("Vault locked"); }
    }

    void background() { clearOwnClipboard(); lock(); mListener.run(); }
    void destroy() { mDestroyed = true; mHandler.removeCallbacks(mClipboardTask); background(); }

    private void derive(String password, byte[] requestedSalt, Consumer<byte[]> callback) {
        if (mBusy || mDestroyed) { callback.accept(null); return; }
        mBusy = true;
        final String value = password == null ? "" : password;
        new Thread(() -> {
            byte[] salt;
            try { salt = requestedSalt != null ? requestedSalt : metaFile().isFile() ? Base64.decode(readJson(metaFile()).getString("salt"), Base64.NO_WRAP) : bytes(16); }
            catch (Exception e) { salt = new byte[0]; }
            final byte[] key = salt.length == 16 ? PsychoVaultStoreJni.get().deriveKey(value, salt) : null;
            ThreadUtils.postOnUiThread(() -> { mBusy = false; callback.accept(key); });
        }, "psychovault-vault-kdf").start();
    }

    private void requireUnlocked() {
        if (!mEnabled) throw new IllegalStateException("Enable the vault first");
        if (marker().isFile()) throw new IllegalStateException("Vault recovery is required before writing");
        if (mKey == null || mEntries == null) throw new IllegalStateException("The vault is locked");
    }
    private void commit(JSONArray next) throws Exception {
        validateEntries(next); byte[] encrypted = seal(next.toString().getBytes(StandardCharsets.UTF_8), mKey);
        atomic(dataFile(), encrypted); mEntries = next; armLock(); mListener.run();
    }
    private void armLock() {
        if (mLockTask != null) mHandler.removeCallbacks(mLockTask);
        mLockTask = null;
        if (mKey != null && mLockMin > 0) { mLockTask = () -> { lock(); mListener.run(); }; mHandler.postDelayed(mLockTask, mLockMin * 60000L); }
    }
    private void clearOwnClipboard() {
        if (mClipboardAt == 0) return;
        ClipboardManager clipboard = (ClipboardManager) mContext.getSystemService(Context.CLIPBOARD_SERVICE);
        ClipData current = clipboard.getPrimaryClip();
        if (current != null && current.getDescription().getTimestamp() == mClipboardAt && "PsychoVault".equals(String.valueOf(current.getDescription().getLabel()))) clipboard.setPrimaryClip(ClipData.newPlainText("", ""));
        mClipboardAt = 0;
    }
    private JSONObject ok() throws Exception { return new JSONObject().put("ok", true).put("status", status()); }
    private JSONObject reason(String text) { try { return new JSONObject().put("ok", false).put("reason", text).put("status", status()); } catch (Exception e) { throw new IllegalStateException(e); } }
    private JSONObject fail(Exception e) { return reason(PsychoBridge.safeMessage(e)); }
    private File dataFile() { return new File(mDir, "vault.bin"); }
    private File metaFile() { return new File(mDir, "vault.key.json"); }
    private File marker() { return new File(mDir, "vault-migration.json"); }
    private byte[] bytes(int n) { byte[] out = new byte[n]; mRandom.nextBytes(out); return out; }
    private static String b64(byte[] value) { return Base64.encodeToString(value, Base64.NO_WRAP); }
    private static JSONArray copy(JSONArray value) throws Exception { return new JSONArray(value.toString()); }
    private static JSONObject readJson(File file) throws Exception { return new JSONObject(new String(read(file), StandardCharsets.UTF_8)); }
    private static byte[] read(File file) throws Exception {
        if (!file.isFile() || file.length() > MAX_FILE) throw new IllegalStateException("Vault file is missing or too large");
        try (FileInputStream in = new FileInputStream(file)) { return PsychoBridge.readLimited(in, MAX_FILE); }
    }
    private static void atomic(File file, byte[] data) throws Exception {
        File tmp = new File(file.getParentFile(), file.getName() + "." + UUID.randomUUID() + ".tmp");
        try (FileOutputStream out = new FileOutputStream(tmp, false)) { out.write(data); out.getFD().sync(); }
        if (!tmp.renameTo(file)) { tmp.delete(); throw new IllegalStateException("Could not save the encrypted vault"); }
    }
    private static byte[] seal(byte[] data, byte[] key) throws Exception {
        byte[] iv = new byte[12]; new SecureRandom().nextBytes(iv);
        Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding"); cipher.init(Cipher.ENCRYPT_MODE, new SecretKeySpec(key, "AES"), new GCMParameterSpec(128, iv));
        byte[] body = cipher.doFinal(data); byte[] out = Arrays.copyOf(iv, iv.length + body.length); System.arraycopy(body, 0, out, iv.length, body.length); return out;
    }
    private static byte[] open(byte[] data, byte[] key) throws Exception {
        if (data.length < 28) throw new IllegalArgumentException("Invalid encrypted vault");
        Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding"); cipher.init(Cipher.DECRYPT_MODE, new SecretKeySpec(key, "AES"), new GCMParameterSpec(128, data, 0, 12));
        return cipher.doFinal(data, 12, data.length - 12);
    }
    private static void validateEntries(JSONArray entries) throws Exception {
        if (entries.length() > 100000) throw new IllegalArgumentException("Too many vault entries");
        java.util.Set<String> ids = new java.util.HashSet<>();
        for (int i = 0; i < entries.length(); i++) {
            JSONObject e = entries.getJSONObject(i);
            if (!e.optString("id").matches("[A-Za-z0-9_-]{1,80}") || !ids.add(e.getString("id")) || e.optString("site").isEmpty()
                    || e.optString("pass").isEmpty() || e.optString("pass").length() > 512 || e.optString("user").length() > 200 || e.optString("note").length() > 2000) throw new IllegalArgumentException("Corrupt vault entry; refusing to overwrite it");
        }
    }

    @NativeMethods
    interface Natives { byte[] deriveKey(String password, byte[] salt); }
}
