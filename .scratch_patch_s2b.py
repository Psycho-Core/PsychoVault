def patch(path, subs):
    s = open(path, encoding="utf8").read()
    for old, new in subs:
        assert old in s, (path, old[:70])
        s = s.replace(old, new, 1)
    open(path, "w", encoding="utf8").write(s)

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
  void spell_reload_users() { spell.set_users(profile.user_words); }

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
"""    hits.push_back({search, "home-search", 0, {}});""",
"""    hits.push_back({search, "home-search", 0, {}});
    // My spell checker marks words that fail my dictionary with a red squiggle
    // and offers fixes underneath the box. URLs are left alone.
    if (profile.spell_check && !home_search.empty() && home_focus) {
      if (spell_cover != home_search) {
        spell_marks = spell_check_text(spell, home_search);
        spell_cover = home_search;
      }
      constexpr uint32_t SPELL_RED = 0xff0000ff;
      for (const auto &mark : spell_marks) {
        double x0 = search.x + 16 + c.text_width(home_search.substr(0, mark.start), 14);
        double w = c.text_width(home_search.substr(mark.start, mark.length), 14);
        double y0 = search.y + 36;
        for (double sx = x0; sx + 3 <= x0 + w; sx += 3)
          c.line(sx, y0 + (int((sx - x0) / 3) % 2 ? 0.0 : 2.0), sx + 3,
                 y0 + (int((sx - x0) / 3 + 1) % 2 ? 0.0 : 2.0), SPELL_RED, 1);
      }
      const SpellMark *hint = nullptr;
      for (const auto &mark : spell_marks)
        if (home_search_caret >= mark.start && home_search_caret <= mark.start + mark.length)
          hint = &mark;
      if (!hint && !spell_marks.empty())
        hint = &spell_marks.front();
      if (hint) {
        std::string message = "\\"" + home_search.substr(hint->start, hint->length) +
                              "\\" is not in my dictionary";
        if (!hint->suggestions.empty()) {
          message += " - try ";
          for (size_t i = 0; i < hint->suggestions.size(); ++i)
            message += (i ? ", " : "") + hint->suggestions[i];
          message += " (Tab takes the first)";
        }
        c.text(search.x, search.y + 52, message, 12, MUTED);
      }
    }"""),
(
"""      if (wp == VK_BACK) {
        size_t previous = previous_cp(text, caret);
        text.erase(previous, caret - previous);
        caret = previous;
      } else if (wp == VK_DELETE) {""",
"""      if (wp == VK_TAB && home_focus && profile.spell_check) {
        // The squiggle at my cursor takes my first suggestion when I press Tab.
        for (const auto &mark : spell_marks) {
          if (caret >= mark.start && caret <= mark.start + mark.length &&
              !mark.suggestions.empty()) {
            text.replace(mark.start, mark.length, mark.suggestions[0]);
            caret = mark.start + mark.suggestions[0].size();
            spell_cover.clear();
            break;
          }
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
      }
      if (wp == VK_BACK) {
        size_t previous = previous_cp(text, caret);
        text.erase(previous, caret - previous);
        caret = previous;
      } else if (wp == VK_DELETE) {"""),
])
print("shell patch done")
