import io

def patch(path, subs):
    s = open(path, encoding="utf8").read()
    for old, new in subs:
        assert old in s, (path, old[:70])
        s = s.replace(old, new, 1)
    open(path, "w", encoding="utf8").write(s)

# ---------- profile ----------
patch("platform/Psycho_profile.hpp", [(
"""  bool spell_check = true;
  std::vector<std::string> user_words;""",
"""  bool spell_check = true;
  std::vector<std::string> user_words;
  // My vault relocks itself this many minutes after I unlock it.
  int vault_lock_minutes = 10;""")])

patch("platform/Psycho_profile.cpp", [(
"""    p.spell_check = settings.flag("spellCheck", true);""",
"""    p.spell_check = settings.flag("spellCheck", true);
    p.vault_lock_minutes = int(std::clamp(settings.num("vaultLockMin", 10), 1.0, 1440.0));"""),
(
"""  settings.set("spellCheck", spell_check);""",
"""  settings.set("spellCheck", spell_check);
  settings.set("vaultLockMin", vault_lock_minutes);""")])

# ---------- shell ----------
subs = []
subs.append((
'''#include "../platform/Psycho_profile.hpp"''',
'''#include "../platform/Psycho_profile.hpp"
#include "../platform/Psycho_vault.hpp"'''))

subs.append((
'''#include <chrono>''',
'''#include <algorithm>
#include <chrono>
#include <iterator>'''))

subs.append((
"""  std::string editing_setting;""",
"""  std::string editing_setting;
  // My vault lives in RAM only while vault_open, and every secret byte is
  // wiped the moment I lock. The passphrase itself is never stored anywhere.
  pv::Vault vault;
  bool vault_open = false;
  DWORD vault_unlocked_at = 0;"""))

subs.append((
"""  bool save_profile() {""",
"""  // A string of secrets is zeroed before it goes out of scope anywhere I can
  // see it, so a passphrase or a copied password does not sit readable in RAM.
  static void scrub(std::string &text) {
    if (!text.empty())
      std::fill(text.begin(), text.end(), '\\0');
    text.clear();
  }
  std::string vault_path() const { return profile.directory + "/vault.bin"; }
  bool vault_exists() const {
    DWORD attr = GetFileAttributesW(utf16_of(vault_path()).c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
  }
  bool vault_save() {
    std::string error;
    pv::Bytes blob = vault.seal();
    std::string data(reinterpret_cast<const char *>(blob.data()), blob.size());
    if (!write_file_atomic(vault_path(), data, error)) {
      status = "I could not save my vault: " + error;
      return false;
    }
    vault.dirty = false;
    return true;
  }
  void vault_wipe() {
    std::fill(vault.key.begin(), vault.key.end(), uint8_t(0));
    for (auto &entry : vault.entries)
      scrub(entry.second);
    vault.entries.clear();
  }
  void vault_lock() {
    if (!vault_open)
      return;
    vault_wipe();
    vault_open = false;
    vault_unlocked_at = 0;
    if (hwnd)
      InvalidateRect(hwnd, nullptr, FALSE);
    status = "My vault is locked";
  }
  // A vault row is "site | user | password | note"; copying hands over only
  // the password segment, never the whole row.
  static std::string vault_password_of(const std::string &row) {
    size_t first = row.find(" | ");
    size_t second = first == std::string::npos ? first : row.find(" | ", first + 3);
    if (second == std::string::npos)
      return row;
    size_t third = row.find(" | ", second + 3);
    return row.substr(second + 3,
                      third == std::string::npos ? third : third - second - 3);
  }

  bool save_profile() {"""))

subs.append((
"""    auto note = [&](const std::string &text) {""",
"""    auto entry_button = [&](const std::string &label, const char *action, int index) {
      Rect r{x, y - 2, 240, 32};
      c.fill(r, PANEL);
      c.stroke(r, BORDER);
      c.text(r.x + 12, r.y + 8, label, 12, TEXT);
      hits.push_back({r, action, index, {}});
      y += 46;
    };
    auto note = [&](const std::string &text) {"""))

subs.append((
"""    case 7:
      note("My vault is not implemented in this build.");
      note("I will not show a password field that stores nothing.");
      note("I have not migrated any existing vault data.");
      break;""",
"""    case 7: {
      if (!vault_exists()) {
        note("No vault yet - mine keeps logins in one encrypted vault.bin next to my profile.");
        note("Everything inside runs on my own ChaCha20-Poly1305 and PBKDF2 code.");
        note("The passphrase is the only key, so I cannot recover a forgotten one.");
        button("Create my vault with this passphrase", "vault-create");
      } else if (!vault_open) {
        note("My vault is locked - entries wait encrypted on disk.");
        button("Unlock my vault with this passphrase", "vault-unlock");
      } else {
        note("My vault is unlocked and holds " +
             std::to_string(vault.entries.size()) + " login(s).");
        note("Add rows as:  site | user | password | note   (note may be left off).");
        button("Add the row from my search box", "vault-add");
        button("Generate a 20 character password onto my clipboard", "vault-generate");
        int row = 0;
        for (const auto &entry : vault.entries) {
          if (row == 10) {
            note("  ... my list shows the first 10");
            break;
          }
          note("  " + entry.first);
          entry_button("Copy password - " + entry.first, "vault-copy", row);
          entry_button("Remove - " + entry.first, "vault-remove", row);
          row++;
        }
        button("Lock my vault now", "vault-lock");
      }
      value_row("Auto-lock minutes after unlock", std::to_string(profile.vault_lock_minutes),
                "setting-vault-lock");
      break;
    }"""))

subs.append((
"""        } else if (h.action == "setting-panic-url") {
          begin_setting_edit("panicUrl", profile.panic_url);
        } else if (h.action == "workspaces") {""",
"""        } else if (h.action == "setting-panic-url") {
          begin_setting_edit("panicUrl", profile.panic_url);
        } else if (h.action == "setting-vault-lock") {
          begin_setting_edit("vaultLockMin", std::to_string(profile.vault_lock_minutes));
        } else if (h.action == "vault-create" || h.action == "vault-unlock") {
          begin_setting_edit(h.action == "vault-create" ? "vaultCreate" : "vaultUnlock", "");
        } else if (h.action == "vault-add") {
          begin_setting_edit("vaultAdd", "");
        } else if (h.action == "vault-lock") {
          vault_lock();
        } else if (h.action == "vault-copy") {
          if (vault_open && h.index >= 0 && size_t(h.index) < vault.entries.size()) {
            auto it = vault.entries.begin();
            std::advance(it, h.index);
            std::string name = it->first;
            std::string secret = vault_password_of(it->second);
            write_clipboard(secret);
            scrub(secret);
            status = "Password for " + name + " is on my clipboard";
          }
        } else if (h.action == "vault-remove") {
          if (vault_open && h.index >= 0 && size_t(h.index) < vault.entries.size()) {
            auto it = vault.entries.begin();
            std::advance(it, h.index);
            std::string name = it->first;
            std::fill(it->second.begin(), it->second.end(), '\\0');
            vault.entries.erase(it);
            vault.dirty = true;
            if (vault_save())
              status = "Removed " + name + " from my vault";
            InvalidateRect(hwnd, nullptr, FALSE);
          }
        } else if (h.action == "vault-generate") {
          // 64 characters is one clean index per random byte, so every
          // character is equally likely and no modulo leaning creeps in.
          static const char alphabet[] =
              "abcdefghijkmnpqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ23456789!@#$%^&*";
          auto rnd = random_bytes(20);
          std::string fresh;
          for (uint8_t byte : rnd)
            fresh.push_back(alphabet[byte & 63]);
          write_clipboard(fresh);
          scrub(fresh);
          status = "Fresh password on my clipboard - paste it into my add row";
        } else if (h.action == "workspaces") {"""))

subs.append((
'''    } else if (key == "panicUrl") {''',
'''    } else if (key == "vaultLockMin") {
      int minutes = std::atoi(value.c_str());
      if (value.empty() || minutes < 1 || minutes > 1440) {
        status = "Auto-lock is between 1 and 1440 minutes";
      } else {
        profile.vault_lock_minutes = minutes;
        save_profile();
        status = "My vault locks " + std::to_string(minutes) + " minutes after I unlock it";
      }
    } else if (key == "vaultCreate") {
      if (value.size() < 8) {
        status = "A vault passphrase needs at least 8 characters";
      } else if (vault_exists()) {
        status = "A vault already exists - unlock it instead";
      } else {
        try {
          vault = pv::Vault::create(value);
          vault_open = true;
          vault_unlocked_at = GetTickCount();
          vault_save();
          status = "My vault is created and unlocked";
        } catch (const std::exception &e) {
          status = std::string("Vault failed: ") + e.what();
        }
      }
      scrub(value);
    } else if (key == "vaultUnlock") {
      try {
        std::string raw = read_text_file(vault_path());
        pv::Bytes blob(raw.begin(), raw.end());
        vault = pv::Vault::open(blob, value);
        vault_open = true;
        vault_unlocked_at = GetTickCount();
        status = "My vault is unlocked";
      } catch (const std::runtime_error &) {
        status = "That passphrase does not match my vault";
      }
      scrub(value);
    } else if (key == "vaultAdd") {
      if (!vault_open) {
        status = "Unlock my vault first";
      } else {
        size_t pipe = value.find(" | ");
        std::string name = pipe == std::string::npos ? value : value.substr(0, pipe);
        if (name.empty() || pipe == std::string::npos) {
          status = "Use the row form: site | user | password | note";
        } else if (vault.entries.count(name)) {
          status = "I already keep a login named " + name;
        } else {
          vault.entries[name] = value;
          vault.dirty = true;
          if (vault_save())
            status = "Saved " + name + " in my vault";
        }
      }
      scrub(value);
    } else if (key == "panicUrl") {'''))

subs.append((
"""      b->extensions.tick();
      b->flush_history();""",
"""      b->extensions.tick();
      b->flush_history();
      if (b->vault_open && b->profile.vault_lock_minutes > 0 &&
          GetTickCount() - b->vault_unlocked_at >
              DWORD(b->profile.vault_lock_minutes) * 60000)
        b->vault_lock();"""))

subs.append((
"""  b.save_session();
  return int(msg.wParam);""",
"""  b.vault_wipe();
  b.save_session();
  return int(msg.wParam);"""))

patch("shell/Psycho_win_main.cpp", subs)
print("step1 patches applied")
