// PsychoVault original source. No command string is ever assembled here: I
// validate argv tokens as data, canonicalise URLs, and return typed options.
#include "Psycho_commandline.hpp"
#include "../internal/Psycho_internal.hpp"
#include "../../engine/Psycho_base.hpp"
#include <cctype>
#include <set>
#include <utility>

namespace pv::rt {
namespace {

bool refuse(Error &error, const char *code, const std::string &message) {
  error.code = code;
  error.message = message;
  return false;
}

bool has_control(const std::string &value) {
  for (unsigned char c : value)
    if (c < 0x20 || c == 0x7f) return true;
  return false;
}

bool valid_path_value(const std::string &value, size_t limit) {
  return !value.empty() && value.size() <= limit && !has_control(value) &&
         !starts(value, "--");
}

bool canonical_url(const std::string &raw, std::string &canonical,
                   Error &error) {
  if (raw.empty() || raw.size() > 8192 || has_control(raw))
    return refuse(error, "commandline.url", "URL is empty, too long, or contains control bytes");
  // Do not trim or remove URL bytes: an argv URL is data, and ambiguous
  // whitespace/control repair would make validation differ from navigation.
  for (unsigned char c : raw)
    if (std::isspace(c))
      return refuse(error, "commandline.url", "URL contains unescaped whitespace");
  if (raw == "about:blank") {
    canonical = raw;
    return true;
  }
  const size_t scheme = raw.find("://");
  if (scheme != std::string::npos &&
      lower(std::string_view(raw).substr(0, scheme)) == "psychovault") {
    InternalUrl internal;
    if (!InternalRegistry::stock().parse(raw, internal, error)) {
      const std::string detail = error.message;
      return refuse(error, "commandline.url", detail);
    }
    canonical = std::move(internal.canon);
    return true;
  }
  const auto parsed = parse_url(raw);
  if (!parsed || !parsed->network())
    return refuse(error, "commandline.url",
                  "URL must be a valid HTTP(S) address or registered internal page");
  canonical = parsed->href();
  return true;
}

bool append_url(const std::string &raw, CommandLineOptions &options, Error &error) {
  if (options.urls.size() >= 64)
    return refuse(error, "commandline.url-count", "at most 64 URLs may be opened");
  std::string canonical;
  if (!canonical_url(raw, canonical, error)) return false;
  options.urls.push_back(std::move(canonical));
  return true;
}

} // namespace

bool valid_language_tag(const std::string &language) {
  if (language.empty() || language.size() > 63) return false;
  size_t start = 0;
  size_t part_index = 0;
  while (start < language.size()) {
    size_t end = language.find('-', start);
    if (end == std::string::npos) end = language.size();
    const size_t length = end - start;
    if (length == 0 || length > 8) return false;
    for (size_t i = start; i < end; ++i) {
      const unsigned char c = (unsigned char)language[i];
      if (part_index == 0 ? !std::isalpha(c) : !std::isalnum(c)) return false;
    }
    if (part_index == 0 && length < 2) return false;
    ++part_index;
    if (end == language.size()) break;
    start = end + 1;
  }
  return part_index > 0;
}

bool parse_command_line(const std::vector<std::string> &arguments,
                        CommandLineOptions &options, Error &error) {
  options = CommandLineOptions{};
  error = Error{};
  if (arguments.size() > 256)
    return refuse(error, "commandline.argument-count", "at most 256 arguments are accepted");

  std::set<std::string> seen;
  bool positional_only = false;
  auto option_value = [&](size_t &index, const char *option,
                          size_t maximum) -> const std::string * {
    if (index + 1 >= arguments.size() || arguments[index + 1].empty() ||
        starts(arguments[index + 1], "--") ||
        arguments[index + 1].size() > maximum || has_control(arguments[index + 1])) {
      refuse(error, "commandline.missing-value",
             std::string(option) + " needs one non-empty, valid value");
      return nullptr;
    }
    ++index;
    return &arguments[index];
  };
  auto once = [&](const std::string &option) {
    if (seen.insert(option).second) return true;
    return refuse(error, "commandline.duplicate-option",
                  "option '" + option + "' may only be supplied once");
  };

  for (size_t i = 0; i < arguments.size(); ++i) {
    const std::string &argument = arguments[i];
    if (!positional_only && argument == "--") {
      positional_only = true;
      continue;
    }
    if (positional_only || !starts(argument, "--")) {
      if (!append_url(argument, options, error)) return false;
      continue;
    }
    if (argument == "--url") {
      const std::string *value = option_value(i, "--url", 8192);
      if (!value || !append_url(*value, options, error)) return false;
    } else if (argument == "--profile") {
      if (!once(argument)) return false;
      const std::string *value = option_value(i, "--profile", 32768);
      if (!value) return false;
      if (!valid_path_value(*value, 32768))
        return refuse(error, "commandline.profile", "profile path is invalid");
      options.profile_directory = *value;
    } else if (argument == "--language") {
      if (!once(argument)) return false;
      const std::string *value = option_value(i, "--language", 63);
      if (!value) return false;
      if (!valid_language_tag(*value))
        return refuse(error, "commandline.language", "language must be a BCP 47-style tag");
      const size_t subtag = value->find('-');
      if (lower(std::string_view(*value).substr(0, subtag)) != "en")
        return refuse(error, "commandline.language-unavailable",
                      "English is the only UI language currently shipped");
      options.language = *value;
    } else if (argument == "--capture") {
      if (!once(argument)) return false;
      const std::string *value = option_value(i, "--capture", 32768);
      if (!value) return false;
      if (!valid_path_value(*value, 32768))
        return refuse(error, "commandline.capture", "capture path is invalid");
      options.capture_path = *value;
    } else if (argument == "--private") {
      if (!once(argument)) return false;
      options.private_mode = true;
    } else if (argument == "--diagnostics") {
      if (!once(argument)) return false;
      options.diagnostics = true;
    } else if (argument == "--smoke") {
      if (!once(argument)) return false;
      options.smoke = true;
    } else if (argument == "--expect-image") {
      if (!once(argument)) return false;
      options.expect_image = true;
    } else {
      return refuse(error, "commandline.unknown-option", "unknown option '" + argument + "'");
    }
  }
  if (options.urls.size() + size_t(options.diagnostics) > 64)
    return refuse(error, "commandline.url-count", "diagnostics plus URLs may open at most 64 tabs");
  return true;
}

std::vector<std::string> command_line_startup_urls(const CommandLineOptions &options) {
  std::vector<std::string> urls;
  urls.reserve(options.urls.size() + size_t(options.diagnostics));
  if (options.diagnostics) urls.emplace_back("psychovault://nettest");
  urls.insert(urls.end(), options.urls.begin(), options.urls.end());
  return urls;
}

} // namespace pv::rt
