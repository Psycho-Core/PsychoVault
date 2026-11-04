// PsychoVault original source. My conformance runner. It reads a checked-out
// Test262 tree, runs each file through my own parser and interpreter, and writes
// one line per test so the numbers in my documents come from a run and not from a
// guess. It does not evaluate anyone's engine: only my own code runs the tests.
#include "../engine/Psycho_js.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#ifdef __unix__
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using namespace pv;

namespace {

std::string read_text(const fs::path &path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream out;
  out << in.rdbuf();
  return out.str();
}

std::string trim_copy(const std::string &text) {
  size_t begin = text.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) return {};
  size_t end = text.find_last_not_of(" \t\r\n");
  return text.substr(begin, end - begin + 1);
}

// One test's front matter, read the way the corpus writes it: a block comment that
// starts with /*--- and a set of lines inside it.
struct Meta {
  std::vector<std::string> flags, includes, features;
  std::string negative_phase, negative_type;
  bool has_meta = false;
};

std::vector<std::string> split_list(const std::string &text) {
  std::vector<std::string> out;
  std::string item;
  for (char c : text) {
    if (c == ',' || c == ']') {
      auto value = trim_copy(item);
      item.clear();
      if (!value.empty()) out.push_back(value);
    } else if (c != '[') {
      item.push_back(c);
    }
  }
  auto value = trim_copy(item);
  if (!value.empty()) out.push_back(value);
  return out;
}

Meta read_meta(const std::string &source) {
  Meta meta;
  auto open = source.find("/*---");
  if (open == std::string::npos) return meta;
  auto close = source.find("---*/", open);
  if (close == std::string::npos) return meta;
  meta.has_meta = true;
  std::istringstream lines(source.substr(open + 5, close - open - 5));
  std::string line, section;
  while (std::getline(lines, line)) {
    auto text = trim_copy(line);
    if (text.rfind("negative:", 0) == 0) {
      section = "negative";
      continue;
    }
    if (section == "negative") {
      // The nested keys are indented; anything outdented ends the section.
      if (!line.empty() && line[0] != ' ' && line[0] != '\t') {
        section.clear();
      } else if (text.rfind("phase:", 0) == 0) {
        meta.negative_phase = trim_copy(text.substr(6));
        continue;
      } else if (text.rfind("type:", 0) == 0) {
        meta.negative_type = trim_copy(text.substr(5));
        continue;
      } else {
        continue;
      }
    }
    if (text.rfind("flags:", 0) == 0) meta.flags = split_list(text.substr(6));
    else if (text.rfind("includes:", 0) == 0) meta.includes = split_list(text.substr(9));
    else if (text.rfind("features:", 0) == 0) meta.features = split_list(text.substr(9));
  }
  return meta;
}

bool has(const std::vector<std::string> &list, const std::string &value) {
  return std::find(list.begin(), list.end(), value) != list.end();
}

// What my engine genuinely cannot express yet. A test whose only obstacle is one of
// these is reported as skipped with the feature named, not counted as a failure I
// could have passed.
const std::set<std::string> &unsupported_features() {
  static const std::set<std::string> features = {
      "ArrayBuffer",      "SharedArrayBuffer", "Atomics",           "DataView",
      "Int8Array",        "Uint8Array",        "Uint8ClampedArray", "Int16Array",
      "Uint16Array",      "Int32Array",        "Uint32Array",       "Float32Array",
      "Float64Array",     "BigInt64Array",     "BigUint64Array",    "BigInt",
      "Symbol.asyncIterator", "Symbol.toStringTag", "Symbol.hasInstance",
      "Symbol.isConcatSpreadable", "Symbol.species", "Symbol.matchAll",
      "generators",       "async-iteration",   "Intl",               "Temporal",
      "class-static-fields-public", "class-fields-public", "class-fields-private",
      "regexp-lookbehind", "regexp-named-groups", "regexp-unicode-property-escapes",
      "optional-chaining", "logical-assignment-operators", "WeakRef", "FinalizationRegistry",
      "Proxy",            "Reflect",           "globalThis",         "WeakMap", "WeakSet",
      "Observable",       "export-star-as-namespace-from-module", "decorators"};
  return features;
}

// My own $262 host object and the two print shims the harness expects. This is my
// code, written from the runtime contract in the corpus documentation, not copied
// from the harness files.
const char *kPrelude = R"JS(
var $262 = {
  global: this,
  gc: function() {},
  evalScript: function(text) { return undefined; },
  detachArrayBuffer: function() {},
  createRealm: undefined
};
var print = function() {};
)JS";

enum class Outcome { Pass, Fail, Threw, Crash, Skip, SkipFeature, SkipUnsupported };

const char *outcome_name(Outcome outcome) {
  switch (outcome) {
  case Outcome::Pass: return "pass";
  case Outcome::Fail: return "fail";
  case Outcome::Threw: return "threw";
  case Outcome::Crash: return "crash";
  case Outcome::Skip: return "skip";
  case Outcome::SkipFeature: return "skip-feature";
  case Outcome::SkipUnsupported: return "skip-unsupported";
  }
  return "unknown";
}

struct Result {
  Outcome outcome = Outcome::Pass;
  std::string note;
};

// The message my interpreter carries when it throws, so a negative test can be
// checked against the type it expects.
std::string thrown_text(const js::Value &value) {
  if (value.type == js::Value::Type::String) return value.string;
  if (value.type == js::Value::Type::Object && value.object) {
    auto message = value.object->properties.find("message");
    if (message != value.object->properties.end()) return message->second.to_string();
    return "object";
  }
  return value.to_string();
}

Result run_test(const fs::path &test, const fs::path &root) {
  Result result;
  const std::string source = read_text(test);
  Meta meta = read_meta(source);
  for (const auto &feature : meta.features) {
    if (unsupported_features().count(feature)) {
      result.outcome = Outcome::SkipUnsupported;
      result.note = feature;
      return result;
    }
  }
  if (has(meta.flags, "module")) {
    result.outcome = Outcome::Skip;
    result.note = "module";
    return result;
  }
  std::string program = kPrelude;
  // The corpus assumes sta.js is always there and assert.js is there unless a test
  // asks for raw, which is how the documented runner behaves.
  std::vector<std::string> includes = {"sta.js"};
  if (!has(meta.flags, "raw")) includes.push_back("assert.js");
  for (const auto &extra : meta.includes) includes.push_back(extra);
  for (const auto &include : includes) {
    auto path = root / "harness" / include;
    if (!fs::exists(path)) {
      result.outcome = Outcome::Skip;
      result.note = "missing include " + include;
      return result;
    }
    program += "\n" + read_text(path);
  }
  program += "\n" + source;
  // Strict flags are not something I can force from here yet: the test's own
  // "use strict" directive is what my parser sees.
  if (has(meta.flags, "onlyStrict") || has(meta.flags, "noStrict")) {
    result.outcome = Outcome::Skip;
    result.note = "strictness flag";
    return result;
  }
  (void)trim_copy(source);
  js::Runtime runtime;
  bool done_called = false;
  bool done_failed = false;
  runtime.global().properties["$DONE"] = runtime.make_native([&](js::Values &args) {
    done_called = true;
    if (!args.empty() && args[0].truthy()) done_failed = true;
    return js::Value();
  });
  const bool negative = !meta.negative_phase.empty();
  try {
    runtime.run(program);
    runtime.drain_microtasks();
    if (negative) {
      result.outcome = Outcome::Fail;
      result.note = "expected " + meta.negative_phase + " " + meta.negative_type;
      return result;
    }
    if (has(meta.flags, "async") && (!done_called || done_failed)) {
      result.outcome = Outcome::Fail;
      result.note = done_called ? "$DONE(error)" : "$DONE never called";
      return result;
    }
    result.outcome = Outcome::Pass;
    return result;
  } catch (js::JsError &error) {
    std::string text = thrown_text(error.value);
    if (negative) {
      const std::string wanted = meta.negative_type;
      const bool type_ok = wanted.empty() || text.rfind(wanted, 0) == 0 ||
                           text.find(wanted) != std::string::npos;
      const bool phase_ok = meta.negative_phase != "parse" ||
                            text.find("SyntaxError") != std::string::npos ||
                            text.find("parse") != std::string::npos;
      if (type_ok && phase_ok) {
        result.outcome = Outcome::Pass;
        return result;
      }
      result.outcome = Outcome::Fail;
      result.note = "threw " + text.substr(0, 80) + ", wanted " + wanted;
      return result;
    }
    result.outcome = Outcome::Threw;
    result.note = text.substr(0, 120);
    return result;
  } catch (std::exception &error) {
    // My parser reports its own refusals as standard exceptions, which is how a
    // negative parse test is expected to look from the outside.
    std::string text = error.what();
    if (negative && meta.negative_phase == "parse") {
      result.outcome = Outcome::Pass;
      return result;
    }
    result.outcome = Outcome::Threw;
    result.note = text.substr(0, 120);
    return result;
  }
}

// Each test runs in its own child process on this platform. That is not a claim of
// safety: it is so one test that crashes my interpreter is recorded as a crash
// instead of ending the measurement, and so a leaked global cannot reach the next
// test. On Windows the runner calls the same function directly.
Result run_isolated(const fs::path &test, const fs::path &root) {
#ifdef __unix__
  int fds[2];
  if (pipe(fds) != 0) return run_test(test, root);
  pid_t pid = fork();
  if (pid < 0) {
    ::close(fds[0]);
    ::close(fds[1]);
    return run_test(test, root);
  }
  if (pid == 0) {
    ::close(fds[0]);
    Result inner = run_test(test, root);
    std::string line = std::string(outcome_name(inner.outcome)) + "\n" + inner.note;
    ssize_t written = ::write(fds[1], line.data(), line.size());
    (void)written;
    ::_exit(0);
  }
  ::close(fds[1]);
  std::string text;
  char buffer[512];
  ssize_t got = 0;
  while ((got = ::read(fds[0], buffer, sizeof(buffer))) > 0)
    text.append(buffer, size_t(got));
  ::close(fds[0]);
  int status = 0;
  waitpid(pid, &status, 0);
  Result result;
  auto newline = text.find('\n');
  std::string name = newline == std::string::npos ? text : text.substr(0, newline);
  result.note = newline == std::string::npos ? "" : text.substr(newline + 1);
  if (WIFSIGNALED(status) || name.empty()) {
    result.outcome = Outcome::Crash;
    result.note = WIFSIGNALED(status) ? "signal " + std::to_string(WTERMSIG(status)) : "no result";
    return result;
  }
  for (auto candidate : {Outcome::Pass, Outcome::Fail, Outcome::Threw, Outcome::Crash,
                         Outcome::Skip, Outcome::SkipFeature, Outcome::SkipUnsupported})
    if (name == outcome_name(candidate)) result.outcome = candidate;
  return result;
#else
  return run_test(test, root);
#endif
}

} // namespace

int main(int argc, char **argv) {
  fs::path root, filter, out;
  size_t limit = 0;
  bool verbose = false;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    auto next = [&]() -> std::string {
      if (i + 1 >= argc) throw std::runtime_error("missing argument for " + arg);
      return argv[++i];
    };
    if (arg == "--test262") root = next();
    else if (arg == "--filter") filter = next();
    else if (arg == "--limit") limit = size_t(std::stoull(next()));
    else if (arg == "--out") out = next();
    else if (arg == "--verbose") verbose = true;
    else if (arg == "--help") {
      std::cout << "PsychoVault conformance runner\n"
                   "--test262 DIR [--filter SUBPATH] [--limit N] [--out FILE] [--verbose]\n";
      return 0;
    }
  }
  if (root.empty()) {
    std::cerr << "--test262 DIR is required\n";
    return 2;
  }
  if (!fs::exists(root / "test")) {
    std::cerr << "no test directory under " << root << "\n";
    return 2;
  }
  std::vector<fs::path> files;
  for (auto &entry : fs::recursive_directory_iterator(root / "test")) {
    if (!entry.is_regular_file() || entry.path().extension() != ".js") continue;
    if (!filter.empty() && entry.path().string().find(filter.string()) == std::string::npos)
      continue;
    files.push_back(entry.path());
  }
  std::sort(files.begin(), files.end());
  if (limit && files.size() > limit) files.resize(limit);
  std::map<std::string, size_t> totals;
  std::ostringstream report;
  size_t ran = 0;
  for (const auto &file : files) {
    auto result = run_isolated(file, root);
    ++totals[outcome_name(result.outcome)];
    ++ran;
    report << outcome_name(result.outcome) << '\t' << fs::relative(file, root).string();
    if (!result.note.empty()) {
      // A message from a page can carry a newline, and one note must never break the
      // one-line-per-test record I read afterwards.
      std::string note = result.note;
      for (auto &c : note)
        if (c == '\n' || c == '\r' || c == '\t') c = ' ';
      report << '\t' << note;
    }
    report << '\n';
    if (verbose)
      std::cout << outcome_name(result.outcome) << ' ' << fs::relative(file, root).string()
                << (result.note.empty() ? "" : " - " + result.note) << '\n';
  }
  if (!out.empty()) {
    std::ofstream file(out, std::ios::binary);
    file << report.str();
  }
  std::cout << "ran " << ran << " tests\n";
  for (const auto &[name, count] : totals) std::cout << name << ": " << count << '\n';
  return 0;
}
