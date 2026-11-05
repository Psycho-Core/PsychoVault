def patch(path, subs):
    s = open(path, encoding="utf8").read()
    for old, new in subs:
        assert old in s, (path, old[:70])
        s = s.replace(old, new, 1)
    open(path, "w", encoding="utf8").write(s)

# ---------- profile ----------
patch("platform/Psycho_profile.hpp", [(
"""  // My vault relocks itself this many minutes after I unlock it.
  int vault_lock_minutes = 10;""",
"""  // My vault relocks itself this many minutes after I unlock it.
  int vault_lock_minutes = 10;
  // My own dictionary feeds my checker when this is on; my words always count.
  bool spell_builtin = true;
  // A file path or URL I keep extra dictionary words from, cached in my profile.
  std::string spell_dict_source;
  // The one language pack I ship is English; the list persists for later packs.
  std::string spell_langs = "en";""")])

patch("platform/Psycho_profile.cpp", [(
"""    p.spell_check = settings.flag("spellCheck", true);""",
"""    p.spell_check = settings.flag("spellCheck", true);
    p.spell_builtin = settings.flag("spellBuiltin", true);
    p.spell_dict_source = settings.str("spellDictUrl");
    p.spell_langs = settings.str("spellLangs", "en");"""),
(
"""  settings.set("spellCheck", spell_check);""",
"""  settings.set("spellCheck", spell_check);
  settings.set("spellBuiltin", spell_builtin);
  settings.set("spellDictUrl", spell_dict_source);
  settings.set("spellLangs", spell_langs);""")])

# ---------- shell ----------
patch("shell/Psycho_win_main.cpp", [
(
'''#include "../platform/Psycho_profile.hpp"
#include "../platform/Psycho_vault.hpp"''',
'''#include "../platform/Psycho_profile.hpp"
#include "../platform/Psycho_spell.hpp"
#include "../platform/Psycho_vault.hpp"'''),
(
"""  pv::Vault vault;
  bool vault_open = false;
  DWORD vault_unlocked_at = 0;""",
"""  pv::Vault vault;
  bool vault_open = false;
  DWORD vault_unlocked_at = 0;
  // My spelling engine: the embedded starter vocabulary, words from whatever
  // dictionary source I pointed it at, and the words I taught it myself.
  Spell spell;
  std::string spell_cover; // the text my marks underneath belong to
  std::vector<SpellMark> spell_marks;"""),
(
"""    profile_error = profile.error;
    cookies.from_json(profile.cookies);""",
"""    profile_error = profile.error;
    spell.builtin_enabled = profile.spell_builtin;
    spell.load_builtin_embedded();
    spell.set_users(profile.user_words);
    spell_reload_source();
    cookies.from_json(profile.cookies);"""),
(
"""  static std::string vault_password_of(const std::string &row) {""",
"""  std::string spell_extra_path() const { return profile.directory + "/owned-spell-extra.txt"; }
  void spell_reload_source() {
    try {
      spell.replace_extra(read_text_file(spell_extra_path()));
    } catch (const std::exception &) {
      spell.replace_extra("");
    }
  }

  static std::string vault_password_of(const std::string &row) {"""),
(
"""    case 6:
      toggle("Spell check what I type", profile.spell_check, "setting-spellcheck", "");
      note("Words I added: " + std::to_string(profile.user_words.size()));
      button("Add a word from my search box", "setting-spell-add");
      button("Remove my last added word", "setting-spell-remove");
      break;""",
"""    case 6:
      toggle("Spell check my search box", profile.spell_check, "setting-spellcheck", "");
      toggle("Use my built-in dictionary", profile.spell_builtin, "setting-spell-builtin", "");
      note("My dictionary holds " + std::to_string(spell.builtin_count()) +
           " built-in words, " + std::to_string(spell.extra_count()) +
           " from my source, " + std::to_string(profile.user_words.size()) + " of my own.");
      button("Add a word from my search box", "setting-spell-add");
      button("Remove my last added word", "setting-spell-remove");
      value_row("Dictionary source (file or URL)", profile.spell_dict_source,
                "setting-spell-source");
      note("Languages: " + profile.spell_langs + " - English is the one pack I ship.");
      note("A misspelled word in my home search box gets a red squiggle, and");
      note("Tab swaps the word at my cursor for my first suggestion.");
      break;"""),
(
"""        } else if (h.action == "setting-spellcheck") {
          profile.spell_check = !profile.spell_check; save_profile();
        } else if (h.action == "setting-spell-add") {
          if (!home_search.empty()) { profile.add_user_word(home_search); save_profile(); status = "I added the word"; }
          else status = "Type a word in my home search box first";
        } else if (h.action == "setting-spell-remove") {
          if (!profile.user_words.empty()) { profile.remove_user_word(profile.user_words.back()); save_profile(); }
        }""",
"""        } else if (h.action == "setting-spellcheck") {
          profile.spell_check = !profile.spell_check; save_profile();
        } else if (h.action == "setting-spell-builtin") {
          profile.spell_builtin = !profile.spell_builtin;
          spell.builtin_enabled = profile.spell_builtin;
          save_profile();
        } else if (h.action == "setting-spell-source") {
          begin_setting_edit("spellSource", profile.spell_dict_source);
        } else if (h.action == "setting-spell-add") {
          if (!home_search.empty()) { profile.add_user_word(home_search); spell_reload_users(); save_profile(); status = "I added the word"; }
          else status = "Type a word in my home search box first";
        } else if (h.action == "setting-spell-remove") {
          if (!profile.user_words.empty()) { profile.remove_user_word(profile.user_words.back()); spell_reload_users(); save_profile(); }
        }"""),
(
"""    } else if (key == "panicUrl") {""",
"""    } else if (key == "spellSource") {
      profile.spell_dict_source = value;
      save_profile();
      if (value.empty()) {
        spell.replace_extra("");
        std::string error;
        write_file_atomic(spell_extra_path(), "", error);
        status = "My dictionary source is cleared";
      } else if (starts(value, "http://") || starts(value, "https://")) {
        auto parsed = parse_url(value);
        if (!parsed) {
          status = "That is not a URL I can fetch";
        } else {
          Resource resource = fetch_url(*parsed, 15000, 4u * 1024u * 1024u);
          if (resource.ok && !resource.text.empty()) {
            spell.replace_extra(resource.text);
            std::string error;
            write_file_atomic(spell_extra_path(), resource.text, error);
            status = "My dictionary learned " + std::to_string(spell.extra_count()) +
                     " words from that source";
          } else {
            status = "That dictionary source did not answer: " + resource.error;
          }
        }
      } else {
        try {
          std::string text = read_text_file(value);
          spell.replace_extra(text);
          std::string error;
          write_file_atomic(spell_extra_path(), text, error);
          status = "My dictionary learned " + std::to_string(spell.extra_count()) +
                   " words from that file";
        } catch (const std::exception &) {
          status = "I cannot read that dictionary file";
        }
      }
    } else if (key == "panicUrl") {"""),
(
"""    if (home_search.empty()) { c.text(search.x + 16, search.y + 12,
           home_search, 14, TEXT); } // never true; kept honest below
""",
"""""")
])
print("shell patch A done")
