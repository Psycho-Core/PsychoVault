#include "Psycho_js.hpp"
#include "Psycho_regex.hpp"
#include <chrono>
#include <cstdio>
#include <thread>
#include <cstdlib>
#include <iomanip>
namespace pv::js {
namespace {
bool callable(const Value &value) {
  return value.type == Value::Type::Object && value.object &&
         (value.object->kind == Object::Kind::Function || value.object->kind == Object::Kind::Native);
}
} // namespace
Value Value::undef() {
  return {};
}
Value Value::null() {
  Value v;
  v.type = Type::Null;
  return v;
}
std::string number_string(double n) {
  if (std::isnan(n))
    return "NaN";
  if (std::isinf(n))
    return n < 0 ? "-Infinity" : "Infinity";
  if (n == 0)
    return "0";
  std::ostringstream s;
  s.precision(15);
  s << n;
  return s.str();
}
bool Value::truthy() const {
  switch (type) {
  case Type::Undefined:
  case Type::Null:
    return false;
  case Type::Boolean:
    return boolean;
  case Type::Number:
    return number != 0 && !std::isnan(number);
  case Type::String:
    return !string.empty();
  case Type::Object:
    return true;
  }
  return false;
}
double Value::to_number() const {
  switch (type) {
  case Type::Undefined:
    return std::numeric_limits<double>::quiet_NaN();
  case Type::Null:
    return 0;
  case Type::Boolean:
    return boolean ? 1 : 0;
  case Type::Number:
    return number;
  case Type::String: {
    auto s = trim(string);
    if (s.empty())
      return 0;
    char *end = nullptr;
    double n;
    if (starts(s, "0x") || starts(s, "0X"))
      n = double(std::strtoull(s.c_str() + 2, &end, 16));
    else
      n = std::strtod(s.c_str(), &end);
    return end && *end == 0 ? n : std::numeric_limits<double>::quiet_NaN();
  }
  case Type::Object:
    if (object && object->kind == Object::Kind::Array) {
      std::string s;
      for (auto &p : object->properties)
        if (p.first != "length" && p.second.truthy()) {
          if (!s.empty())
            s += ',';
          s += p.second.to_string();
        }
      return Value(s).to_number();
    }
    return std::numeric_limits<double>::quiet_NaN();
  }
  return 0;
}
std::string Value::to_string() const {
  switch (type) {
  case Type::Undefined:
    return "undefined";
  case Type::Null:
    return "null";
  case Type::Boolean:
    return boolean ? "true" : "false";
  case Type::Number:
    return number_string(number);
  case Type::String:
    return string;
  case Type::Object:
    if (!object)
      return "null";
    if (object->kind == Object::Kind::Array) {
      std::string out;
      size_t n = size_t(object->properties["length"].to_number());
      for (size_t i = 0; i < n; ++i) {
        if (i)
          out += ',';
        auto it = object->properties.find(std::to_string(i));
        if (it != object->properties.end() && it->second.type != Type::Null &&
            it->second.type != Type::Undefined)
          out += it->second.to_string();
      }
      return out;
    }
    if (object->kind == Object::Kind::Function)
      return "function " + (object->function ? object->function->name : std::string()) +
             "() { [PsychoVault code] }";
    if (object->kind == Object::Kind::Native)
      return "function () { [PsychoVault code] }";
    if (object->host_type == "RegExp")
      return "/" + object->properties["source"].to_string() + "/" +
             object->properties["flags"].to_string();
    if (object->kind == Object::Kind::Host)
      return "[object " + object->host_type + "]";
    if (!object->host_type.empty())
      return "[object " + object->host_type + "]";
    return "[object Object]";
  }
  return {};
}
std::string Value::type_of() const {
  switch (type) {
  case Type::Undefined:
    return "undefined";
  case Type::Null:
    return "object";
  case Type::Boolean:
    return "boolean";
  case Type::Number:
    return "number";
  case Type::String:
    return "string";
  case Type::Object:
    return object &&
                   (object->kind == Object::Kind::Function || object->kind == Object::Kind::Native)
               ? "function"
               : "object";
  }
  return "undefined";
}
Json Value::to_json(int depth) const {
  if (depth > 64)
    throw std::runtime_error("JSON nesting limit");
  switch (type) {
  case Type::Undefined:
    return Json();
  case Type::Null:
    return Json();
  case Type::Boolean:
    return Json(boolean);
  case Type::Number:
    return std::isfinite(number) ? Json(number) : Json();
  case Type::String:
    return Json(string);
  case Type::Object:
    break;
  }
  if (!object)
    return Json();
  if (object->kind == Object::Kind::Array) {
    Json j = Json::make_array();
    size_t n =
        std::min(size_t(100000), size_t(std::max(0.0, object->properties["length"].to_number())));
    for (size_t i = 0; i < n; ++i) {
      auto it = object->properties.find(std::to_string(i));
      j.array.push_back(it == object->properties.end() ? Json() : it->second.to_json(depth + 1));
    }
    return j;
  }
  Json j = Json::make_object();
  for (auto &p : object->properties)
    if (p.second.type != Type::Undefined && p.second.type_of() != "function")
      j.set(p.first, p.second.to_json(depth + 1));
  return j;
}
bool strict_equals(const Value &a, const Value &b) {
  if (a.type != b.type)
    return false;
  switch (a.type) {
  case Value::Type::Undefined:
  case Value::Type::Null:
    return true;
  case Value::Type::Boolean:
    return a.boolean == b.boolean;
  case Value::Type::Number:
    return a.number == b.number;
  case Value::Type::String:
    return a.string == b.string;
  case Value::Type::Object:
    return a.object == b.object;
  }
  return false;
}
static bool loose_equals(const Value &a, const Value &b) {
  if (a.type == b.type)
    return strict_equals(a, b);
  if ((a.type == Value::Type::Null && b.type == Value::Type::Undefined) ||
      (a.type == Value::Type::Undefined && b.type == Value::Type::Null))
    return true;
  if (a.type == Value::Type::Number && b.type == Value::Type::String)
    return a.number == b.to_number();
  if (a.type == Value::Type::String && b.type == Value::Type::Number)
    return a.to_number() == b.number;
  if (a.type == Value::Type::Boolean)
    return loose_equals(Value(a.to_number()), b);
  if (b.type == Value::Type::Boolean)
    return loose_equals(a, Value(b.to_number()));
  return false;
}
static uint32_t uint32(double n) {
  if (!std::isfinite(n) || n == 0)
    return 0;
  double x = std::trunc(std::fabs(n));
  x = std::fmod(x, 4294967296.0);
  uint32_t v = uint32_t(x);
  return n < 0 ? uint32_t(0x100000000ll - v) : v;
}
static int32_t int32(double n) {
  return int32_t(uint32(n));
}
static Value binary(const std::string &op, const Value &a, const Value &b) {
  if (op == "+") {
    if (a.type == Value::Type::String || b.type == Value::Type::String ||
        (a.type == Value::Type::Object && a.object && a.object->kind != Object::Kind::Array) ||
        (b.type == Value::Type::Object && b.object && b.object->kind != Object::Kind::Array))
      return Value(a.to_string() + b.to_string());
    return Value(a.to_number() + b.to_number());
  }
  if (op == "===")
    return Value(strict_equals(a, b));
  if (op == "!==")
    return Value(!strict_equals(a, b));
  if (op == "==")
    return Value(loose_equals(a, b));
  if (op == "!=")
    return Value(!loose_equals(a, b));
  if (op == "<" || op == ">" || op == "<=" || op == ">=") {
    bool strings = a.type == Value::Type::String && b.type == Value::Type::String;
    if (strings) {
      if (op == "<")
        return Value(a.string < b.string);
      if (op == ">")
        return Value(a.string > b.string);
      if (op == "<=")
        return Value(a.string <= b.string);
      return Value(a.string >= b.string);
    }
    double x = a.to_number(), y = b.to_number();
    if (std::isnan(x) || std::isnan(y))
      return Value(false);
    if (op == "<")
      return Value(x < y);
    if (op == ">")
      return Value(x > y);
    if (op == "<=")
      return Value(x <= y);
    return Value(x >= y);
  }
  double x = a.to_number(), y = b.to_number();
  if (op == "-")
    return Value(x - y);
  if (op == "*")
    return Value(x * y);
  if (op == "/")
    return Value(x / y);
  if (op == "%")
    return Value(std::fmod(x, y));
  if (op == "**")
    return Value(std::pow(x, y));
  if (op == "&")
    return Value(int32(uint32(x) & uint32(y)));
  if (op == "|")
    return Value(int32(uint32(x) | uint32(y)));
  if (op == "^")
    return Value(int32(uint32(x) ^ uint32(y)));
  if (op == "<<")
    return Value(int32(uint32(x) << (uint32(y) & 31)));
  if (op == ">>")
    return Value(int32(int32(x) >> (uint32(y) & 31)));
  if (op == ">>>")
    return Value(double(uint32(x) >> (uint32(y) & 31)));
  if (op == "in") {
    if (b.type != Value::Type::Object || !b.object)
      throw JsError{Value("TypeError: right side of in is not an object")};
    return Value(b.object->properties.count(a.to_string()) != 0);
  }
  if (op == "instanceof") {
    if (b.type != Value::Type::Object || !b.object)
      throw JsError{Value("TypeError: right side of instanceof is not an object")};
    if (a.type != Value::Type::Object || !a.object) return Value(false);
    // I walk the prototype chain I build with new and class extends.
    Value ctor = b;
    auto proto = ctor.object->properties.find("prototype");
    if (proto == ctor.object->properties.end()) return Value(false);
    auto chain = a.object->properties.find("__proto__");
    int guard = 0;
    while (chain != a.object->properties.end() && guard++ < 64) {
      if (chain->second.type == Value::Type::Object && chain->second.object &&
          proto->second.type == Value::Type::Object &&
          chain->second.object == proto->second.object)
        return Value(true);
      if (chain->second.type != Value::Type::Object || !chain->second.object) break;
      auto next = chain->second.object->properties.find("__proto__");
      if (next == chain->second.object->properties.end()) break;
      chain = next;
    }
    // Built-in array/string wrappers have no chain I can walk.
    if (ctor.object->kind == Object::Kind::Native) return Value(false);
    return Value(false);
  }
  throw std::runtime_error("unsupported JavaScript operator " + op);
}
static Value from_json(const Json &j, Runtime &r) {
  switch (j.type) {
  case Json::Type::Null:
    return Value::null();
  case Json::Type::Bool:
    return Value(j.boolean);
  case Json::Type::Number:
    return Value(j.number);
  case Json::Type::String:
    return Value(j.string);
  case Json::Type::Array: {
    Values vs;
    for (auto &x : j.array)
      vs.push_back(from_json(x, r));
    return r.make_array(vs);
  }
  case Json::Type::Object: {
    Value o = r.make_object();
    for (auto &p : j.object)
      o.object->properties[p.first] = from_json(p.second, r);
    return o;
  }
  }
  return Value();
}

Runtime::Runtime(Host *host) : host_(host) {
  global_ = static_cast<Env *>(allocate(Object::Kind::Environment));
  extra_roots_.push_back(global_);
  install_builtins();
  reset_fuel();
}
Runtime::~Runtime() = default;
Object *Runtime::allocate(Object::Kind kind) {
  if (objects_.size() - free_.size() > 100000)
    throw std::runtime_error("JavaScript object limit");
  std::unique_ptr<Object> o;
  if (kind == Object::Kind::Environment)
    o = std::make_unique<Env>();
  else
    o = std::make_unique<Object>();
  o->kind = kind;
  Object *p = o.get();
  objects_.push_back(std::move(o));
  ++allocated_since_gc_;
  return p;
}
size_t Runtime::object_count() const {
  return objects_.size() - free_.size();
}
void Runtime::mark(Object *o) {
  std::vector<Object *> stack;
  if (o)
    stack.push_back(o);
  while (!stack.empty()) {
    Object *p = stack.back();
    stack.pop_back();
    if (!p || p->marked)
      continue;
    p->marked = true;
    if (p->prototype)
      stack.push_back(p->prototype);
    if (p->parent)
      stack.push_back(p->parent);
    if (p->closure)
      stack.push_back(p->closure);
    for (auto &v : p->properties)
      if (v.second.type == Value::Type::Object && v.second.object)
        stack.push_back(v.second.object);
    if (p->promise_result.type == Value::Type::Object && p->promise_result.object)
      stack.push_back(p->promise_result.object);
    for (auto &reaction : p->reactions) {
      if (reaction.on_fulfilled.type == Value::Type::Object && reaction.on_fulfilled.object)
        stack.push_back(reaction.on_fulfilled.object);
      if (reaction.on_rejected.type == Value::Type::Object && reaction.on_rejected.object)
        stack.push_back(reaction.on_rejected.object);
      if (reaction.child.type == Value::Type::Object && reaction.child.object)
        stack.push_back(reaction.child.object);
    }
  }
}
void Runtime::collect() {
  if (depth_ != 0)
    return;
  for (auto &o : objects_)
    if (o)
      o->marked = false;
  for (auto o : extra_roots_)
    mark(o);
  for (auto e : env_stack_)
    mark(e);
  for (auto &t : timers) {
    if (t.callback.type == Value::Type::Object)
      mark(t.callback.object);
    for (auto &v : t.args)
      if (v.type == Value::Type::Object)
        mark(v.object);
  }
  for (auto &task : microtasks_)
    for (auto &v : task.roots)
      if (v.type == Value::Type::Object && v.object)
        mark(v.object);
  size_t live = 0;
  for (auto &o : objects_)
    if (o) {
      if (!o->marked) {
        if (o->kind == Object::Kind::Host)
          hosts_.erase(o->host);
        o.reset();
      } else
        ++live;
    }
  // Compact only at a safe point. AST closures and live values point to
  // retained objects.
  std::vector<std::unique_ptr<Object>> kept;
  kept.reserve(live);
  for (auto &o : objects_)
    if (o)
      kept.push_back(std::move(o));
  objects_ = std::move(kept);
  free_.clear();
  allocated_since_gc_ = 0;
}
void Runtime::root(Object *o) {
  if (o && std::find(extra_roots_.begin(), extra_roots_.end(), o) == extra_roots_.end())
    extra_roots_.push_back(o);
}
void Runtime::unroot(Object *o) {
  std::erase(extra_roots_, o);
}
Value Runtime::make_object(Object::Kind kind) {
  return Value(allocate(kind));
}
Value Runtime::make_array(const Values &values) {
  Value v = make_object(Object::Kind::Array);
  for (size_t i = 0; i < values.size(); ++i)
    v.object->properties[std::to_string(i)] = values[i];
  v.object->properties["length"] = Value(double(values.size()));
  return v;
}
Value Runtime::make_native(std::function<Value(Values &)> fn) {
  Value v = make_object(Object::Kind::Native);
  v.object->native = std::move(fn);
  return v;
}
Value Runtime::host_value(int id, const std::string &type) {
  auto it = hosts_.find(id);
  if (it != hosts_.end())
    return Value(it->second);
  Value v = make_object(Object::Kind::Host);
  v.object->host = id;
  v.object->host_type = type;
  hosts_[id] = v.object;
  return v;
}
void Runtime::reset_fuel() {
  fuel_ = 2000000;
}
void Runtime::tick() {
  if (fuel_ == 0)
    throw std::runtime_error("JavaScript task instruction limit");
  --fuel_;
}
Env *Runtime::find_env(Env &env, const std::string &name) {
  for (Env *e = &env; e; e = static_cast<Env *>(e->parent))
    if (e->properties.count(name))
      return e;
  return nullptr;
}
Value Runtime::declare(Env &env, const std::string &name, const Value &value) {
  env.properties[name] = value;
  return value;
}
Value Runtime::assign_identifier(Env &env, const std::string &name, const Value &value) {
  Env *e = find_env(env, name);
  if (e)
    e->properties[name] = value;
  else
    global_->properties[name] = value;
  return value;
}
// I expand a spread operand into individual values. A string spreads by code
// point, an array by index, and anything else is a TypeError like the spec says.
static void append_spread(Values &out, const Value &source) {
  if (source.type == Value::Type::String) {
    for (auto cp : utf8_decode(source.string)) {
      out.push_back(Value(utf8_encode(cp)));
      if (out.size() > 100000)
        throw JsError{Value("RangeError: spread size limit")};
    }
    return;
  }
  if (source.type == Value::Type::Object && source.object &&
      source.object->kind == Object::Kind::Array) {
    size_t n = size_t(std::max(0.0, source.object->properties["length"].to_number()));
    for (size_t i = 0; i < n; ++i) {
      auto it = source.object->properties.find(std::to_string(i));
      out.push_back(it == source.object->properties.end() ? Value() : it->second);
      if (out.size() > 100000)
        throw JsError{Value("RangeError: spread size limit")};
    }
    return;
  }
  throw JsError{Value("TypeError: value is not iterable")};
}
Value Runtime::get_member(const Value &base, const std::string &key) {
  if (base.type == Value::Type::Undefined || base.type == Value::Type::Null)
    throw JsError{Value("TypeError: cannot read property " + key)};
  if (base.type == Value::Type::Object && base.object) {
    auto accessor = base.object->properties.find("get:" + key);
    if (accessor != base.object->properties.end()) {
      Values none;
      Value self = base;
      return call(accessor->second, none, self);
    }
    if ((base.object->kind == Object::Kind::Function ||
         base.object->kind == Object::Kind::Native) &&
        (key == "call" || key == "apply" || key == "bind")) {
      // I forward the receiver explicitly so a detached method keeps working.
      Value fn = base;
      Runtime &r = *this;
      if (key == "call")
        return make_native([&r, fn](Values &a) {
          Value receiver = a.empty() ? Value() : a[0];
          Values rest(a.begin() + (a.empty() ? 0 : 1), a.end());
          return r.call(fn, rest, receiver);
        });
      if (key == "apply")
        return make_native([&r, fn](Values &a) {
          Value receiver = a.empty() ? Value() : a[0];
          Values rest;
          if (a.size() > 1)
            append_spread(rest, a[1]);
          return r.call(fn, rest, receiver);
        });
      return make_native([&r, fn](Values &a) {
        Value receiver = a.empty() ? Value() : a[0];
        Values bound(a.begin() + (a.empty() ? 0 : 1), a.end());
        return r.make_native([&r, fn, receiver, bound](Values &a) mutable {
          Values all = bound;
          for (auto &v : a)
            all.push_back(v);
          Value self = receiver;
          return r.call(fn, all, self);
        });
      });
    }
    if (key == "hasOwnProperty") {
      auto props = base.object->properties;
      return make_native([props](Values &a) {
        auto name = a.empty() ? std::string() : a[0].to_string();
        if (name == "length" && props.count("length")) return Value(true);
        return Value(props.count(name) != 0 && name.compare(0, 4, "get:") != 0 &&
                     name.compare(0, 4, "set:") != 0);
      });
    }
  }
  if (base.type == Value::Type::String) {
    if (key == "length")
      return Value(double(utf8_decode(base.string).size()));
    std::string s = base.string;
    if (key == "toUpperCase" || key == "toLowerCase")
      return make_native([s, key](Values &) {
        std::string out = s;
        for (char &c : out)
          if (c >= 'A' && c <= 'Z' && key == "toLowerCase")
            c += 32;
          else if (c >= 'a' && c <= 'z' && key == "toUpperCase")
            c -= 32;
        return Value(out);
      });
    if (key == "trim" || key == "trimStart" || key == "trimEnd")
      return make_native([s, key](Values &) {
        auto v = trim(s);
        if (key == "trimStart") {
          size_t n = 0;
          while (n < s.size() && static_cast<unsigned char>(s[n]) <= 32)
            ++n;
          v = s.substr(n);
        }
        if (key == "trimEnd")
          v = s.substr(0, s.size() - v.size());
        return Value(v);
      });
    if (key == "charAt" || key == "at" || key == "codePointAt" || key == "charCodeAt")
      return make_native([s, key](Values &a) {
        auto cps = utf8_decode(s);
        int i = a.empty() ? 0 : int(a[0].to_number());
        if (i < 0)
          i += int(cps.size());
        if (i < 0 || i >= int(cps.size()))
          return key == "charAt" ? Value("")
                 : key == "at"   ? Value()
                                 : Value(std::numeric_limits<double>::quiet_NaN());
        return key == "charAt" || key == "at" ? Value(utf8_encode(cps[i])) : Value(double(cps[i]));
      });
    if (key == "slice" || key == "substring" || key == "substr")
      return make_native([s, key](Values &a) {
        auto cps = utf8_decode(s);
        int n = int(cps.size()), x = a.empty() ? 0 : int(a[0].to_number()),
            y = a.size() > 1 ? int(a[1].to_number()) : n;
        if (key == "substr")
          y = x + y;
        if (x < 0)
          x += n;
        if (y < 0)
          y += n;
        x = std::clamp(x, 0, n);
        y = std::clamp(y, 0, n);
        if (key == "substring" && x > y)
          std::swap(x, y);
        std::string out;
        for (int i = x; i < y; ++i)
          out += utf8_encode(cps[i]);
        return Value(out);
      });
    if (key == "split")
      return make_native(
          [this, s](Values &a) { return string_split(s, a.empty() ? Value() : a[0]); });
    if (key == "match")
      return make_native(
          [this, s](Values &a) { return string_match(s, a.empty() ? Value() : a[0]); });
    if (key == "search")
      return make_native(
          [this, s](Values &a) { return string_search(s, a.empty() ? Value() : a[0]); });
    if (key == "indexOf" || key == "lastIndexOf" || key == "includes" || key == "startsWith" ||
        key == "endsWith")
      return make_native([s, key](Values &a) {
        std::string needle = a.empty() ? "undefined" : a[0].to_string();
        size_t n = key == "lastIndexOf" ? s.rfind(needle) : s.find(needle);
        if (key == "includes")
          return Value(n != std::string::npos);
        if (key == "startsWith")
          return Value(starts(s, needle));
        if (key == "endsWith")
          return Value(ends(s, needle));
        return Value(n == std::string::npos ? -1.0 : double(n));
      });
    if (key == "replace" || key == "replaceAll")
      return make_native([this, s, key](Values &a) {
        return string_replace(s, a.empty() ? Value() : a[0], a.size() > 1 ? a[1] : Value(),
                              key == "replaceAll");
      });
    if (key == "repeat")
      return make_native([s](Values &a) {
        int n = a.empty() ? 0 : int(a[0].to_number());
        if (n < 0 || size_t(n) * s.size() > kMaxDocument)
          throw std::runtime_error("repeat limit");
        std::string out;
        out.reserve(s.size() * n);
        for (int i = 0; i < n; ++i)
          out += s;
        return Value(out);
      });
    if (key == "padStart" || key == "padEnd")
      return make_native([s, key](Values &a) {
        size_t n = a.empty() ? 0 : size_t(std::max(0.0, a[0].to_number()));
        std::string pad = a.size() > 1 ? a[1].to_string() : " ";
        if (n > 1000000)
          throw std::runtime_error("padding limit");
        auto cps = utf8_decode(s), padding = utf8_decode(pad);
        size_t count = cps.size() >= n || padding.empty() ? 0 : n - cps.size();
        std::string fill;
        for (size_t i = 0; i < count; ++i)
          fill += utf8_encode(padding[i % padding.size()]);
        return Value(key == "padStart" ? fill + s : s + fill);
      });
    if (key == "toString" || key == "valueOf")
      return make_native([s](Values &) { return Value(s); });
    auto n = std::strtod(key.c_str(), nullptr);
    if (n >= 0 && n < utf8_decode(s).size() && number_string(n) == key)
      return Value(utf8_encode(utf8_decode(s)[size_t(n)]));
    return Value();
  }
  if (base.type == Value::Type::Number) {
    double n = base.number;
    if (key == "toFixed")
      return make_native([n](Values &a) {
        int digits = a.empty() ? 0 : std::clamp(int(a[0].to_number()), 0, 100);
        std::ostringstream s;
        s << std::fixed << std::setprecision(digits) << n;
        return Value(s.str());
      });
    if (key == "toString")
      return make_native([n](Values &a) {
        int base = a.empty() ? 10 : int(a[0].to_number());
        if (base == 10)
          return Value(number_string(n));
        if (base < 2 || base > 36)
          throw std::runtime_error("radix range");
        bool neg = n < 0;
        uint64_t v = uint64_t(std::fabs(n));
        std::string out;
        do {
          out += "0123456789abcdefghijklmnopqrstuvwxyz"[v % base];
          v /= base;
        } while (v);
        if (neg)
          out += '-';
        std::reverse(out.begin(), out.end());
        return Value(out);
      });
  }
  if (base.type != Value::Type::Object || !base.object)
    return Value();
  Object *o = base.object;
  if (o->kind == Object::Kind::Host && host_)
    return host_->get(o->host, key);
  if (o->kind == Object::Kind::Array && key == "length")
    return o->properties["length"];
  for (Object *p = o; p; p = p->prototype) {
    auto it = p->properties.find(key);
    if (it != p->properties.end())
      return it->second;
  }
  if (o->host_type == "Promise") {
    Value self = base;
    if (key == "then")
      return make_native([this, self](Values &a) {
        return promise_then(self, a.empty() ? Value() : a[0], a.size() > 1 ? a[1] : Value());
      });
    if (key == "catch")
      return make_native(
          [this, self](Values &a) { return promise_then(self, Value(), a.empty() ? Value() : a[0]); });
    if (key == "finally")
      return make_native([this, self](Values &a) {
        Value handler = a.empty() ? Value() : a[0];
        Value on_ok = make_native([this, handler](Values &args) {
          if (callable(handler)) {
            Values none;
            call(handler, none, Value());
          }
          return args.empty() ? Value() : args[0];
        });
        Value on_err = make_native([this, handler](Values &args) -> Value {
          if (callable(handler)) {
            Values none;
            call(handler, none, Value());
          }
          // I pass the rejection along instead of swallowing it.
          throw JsError{args.empty() ? Value("undefined") : args[0]};
        });
        return promise_then(self, on_ok, on_err);
      });
  }
  if (o->host_type == "RegExp") {
    Value self = base;
    if (key == "test")
      return make_native([this, self](Values &a) {
        return Value(regex_exec(self, a.empty() ? Value() : a[0]).type != Value::Type::Null);
      });
    if (key == "exec")
      return make_native(
          [this, self](Values &a) { return regex_exec(self, a.empty() ? Value() : a[0]); });
    if (key == "toString")
      return make_native([self](Values &) {
        return Value("/" + self.object->properties["source"].to_string() + "/" +
                     self.object->properties["flags"].to_string());
      });
  }
  if (o->kind == Object::Kind::Function && key == "name")
    return Value(o->function ? o->function->name : "");
  if (o->kind == Object::Kind::Array) {
    Runtime &r = *this;
    Value arr = base;
    if (key == "push")
      return make_native([arr](Values &a) {
        auto *o = arr.object;
        size_t n = size_t(o->properties["length"].to_number());
        for (auto &v : a)
          o->properties[std::to_string(n++)] = v;
        o->properties["length"] = Value(double(n));
        return Value(double(n));
      });
    if (key == "pop")
      return make_native([arr](Values &) {
        auto *o = arr.object;
        size_t n = size_t(o->properties["length"].to_number());
        if (!n)
          return Value();
        Value v = o->properties[std::to_string(n - 1)];
        o->properties.erase(std::to_string(n - 1));
        o->properties["length"] = Value(double(n - 1));
        return v;
      });
    if (key == "join")
      return make_native([arr](Values &a) {
        std::string sep = a.empty() ? "," : a[0].to_string(), out;
        size_t n = size_t(arr.object->properties["length"].to_number());
        for (size_t i = 0; i < n; ++i) {
          if (i)
            out += sep;
          auto it = arr.object->properties.find(std::to_string(i));
          if (it != arr.object->properties.end())
            out += it->second.to_string();
        }
        return Value(out);
      });
    if (key == "includes" || key == "indexOf")
      return make_native([arr, key](Values &a) {
        Value needle = a.empty() ? Value() : a[0];
        size_t n = size_t(arr.object->properties["length"].to_number());
        for (size_t i = 0; i < n; ++i) {
          auto it = arr.object->properties.find(std::to_string(i));
          if (strict_equals(it == arr.object->properties.end() ? Value() : it->second, needle))
            return key == "includes" ? Value(true) : Value(double(i));
        }
        return key == "includes" ? Value(false) : Value(-1.0);
      });
    if (key == "slice" || key == "concat")
      return make_native([&r, arr, key](Values &a) {
        Values out;
        size_t n = size_t(arr.object->properties["length"].to_number());
        int start = a.empty() || key == "concat" ? 0 : int(a[0].to_number()),
            end = a.size() > 1 && key == "slice" ? int(a[1].to_number()) : int(n);
        if (start < 0)
          start += int(n);
        if (end < 0)
          end += int(n);
        start = std::clamp(start, 0, int(n));
        end = std::clamp(end, 0, int(n));
        for (int i = start; i < end; ++i)
          out.push_back(arr.object->properties[std::to_string(i)]);
        if (key == "concat")
          for (auto &v : a) {
            if (v.type == Value::Type::Object && v.object &&
                v.object->kind == Object::Kind::Array) {
              size_t m = size_t(v.object->properties["length"].to_number());
              for (size_t i = 0; i < m; ++i)
                out.push_back(v.object->properties[std::to_string(i)]);
            } else
              out.push_back(v);
          }
        return r.make_array(out);
      });
    if (key == "reverse")
      return make_native([arr](Values &) {
        auto &p = arr.object->properties;
        size_t n = size_t(p["length"].to_number());
        for (size_t i = 0; i < n / 2; ++i)
          std::swap(p[std::to_string(i)], p[std::to_string(n - 1 - i)]);
        return arr;
      });
    if (key == "shift" || key == "unshift")
      return make_native([arr, key](Values &a) {
        auto &p = arr.object->properties;
        size_t n = size_t(p["length"].to_number());
        if (key == "shift") {
          Value first = p["0"];
          for (size_t i = 1; i < n; ++i)
            p[std::to_string(i - 1)] = p[std::to_string(i)];
          p.erase(std::to_string(n - 1));
          p["length"] = Value(double(n ? n - 1 : 0));
          return n ? first : Value();
        }
        for (size_t i = n; i > 0; --i)
          p[std::to_string(i + a.size() - 1)] = p[std::to_string(i - 1)];
        for (size_t i = 0; i < a.size(); ++i)
          p[std::to_string(i)] = a[i];
        p["length"] = Value(double(n + a.size()));
        return Value(double(n + a.size()));
      });
    if (key == "forEach" || key == "map" || key == "filter" || key == "find" || key == "some" ||
        key == "every" || key == "reduce")
      return make_native([&r, arr, key](Values &a) {
        if (a.empty())
          throw std::runtime_error("array callback required");
        Value fn = a[0];
        size_t n = size_t(arr.object->properties["length"].to_number());
        Values out;
        Value acc = a.size() > 1 ? a[1] : Value();
        bool reduction = key == "reduce" && a.size() > 1;
        bool every = true, some = false;
        for (size_t i = 0; i < n; ++i) {
          Value v = arr.object->properties[std::to_string(i)];
          if (key == "reduce") {
            if (!reduction) {
              acc = v;
              reduction = true;
              continue;
            }
            acc = r.call(fn, {acc, v, Value(double(i)), arr});
            continue;
          }
          Value result = r.call(fn, {v, Value(double(i)), arr});
          if (key == "map")
            out.push_back(result);
          if (key == "filter" && result.truthy())
            out.push_back(v);
          if (key == "find" && result.truthy())
            return v;
          if (key == "some" && result.truthy())
            some = true;
          if (key == "every" && !result.truthy())
            every = false;
        }
        if (key == "map" || key == "filter")
          return r.make_array(out);
        if (key == "reduce")
          return acc;
        if (key == "find")
          return Value();
        if (key == "some")
          return Value(some);
        if (key == "every")
          return Value(every);
        return Value();
      });
    if (key == "sort")
      return make_native([&r, arr](Values &a) {
        auto &p = arr.object->properties;
        size_t n = size_t(p["length"].to_number());
        std::vector<Value> values;
        for (size_t i = 0; i < n; ++i)
          values.push_back(p[std::to_string(i)]);
        Value fn = a.empty() ? Value() : a[0];
        std::stable_sort(values.begin(), values.end(), [&](const Value &x, const Value &y) {
          if (fn.truthy())
            return r.call(fn, {x, y}).to_number() < 0;
          return x.to_string() < y.to_string();
        });
        for (size_t i = 0; i < n; ++i)
          p[std::to_string(i)] = values[i];
        return arr;
      });
  }
  if (base.type == Value::Type::Object && base.object) {
    auto own = base.object->properties.find(key);
    if (own != base.object->properties.end())
      return own->second;
    // I walk the prototype chain my classes and new expressions build.
    auto proto = base.object->properties.find("__proto__");
    int guard = 0;
    while (proto != base.object->properties.end() && guard++ < 64) {
      if (proto->second.type != Value::Type::Object || !proto->second.object)
        break;
      Object *p = proto->second.object;
      auto accessor = p->properties.find("get:" + key);
      if (accessor != p->properties.end()) {
        Values none;
        Value self = base;
        return call(accessor->second, none, self);
      }
      auto found = p->properties.find(key);
      if (found != p->properties.end())
        return found->second;
      proto = p->properties.find("__proto__");
    }
  }
  return Value();
}
bool Runtime::set_member(const Value &base, const std::string &key, const Value &value) {
  if (base.type != Value::Type::Object || !base.object)
    return false;
  Object *o = base.object;
  if (o->kind == Object::Kind::Host && host_)
    return host_->set(o->host, key, value);
  if (o->kind == Object::Kind::Array && key == "length") {
    size_t n = size_t(std::max(0.0, value.to_number()));
    if (n > 100000)
      throw std::runtime_error("array length limit");
    for (auto it = o->properties.begin(); it != o->properties.end();) {
      if (it->first != "length") {
        char *end = nullptr;
        size_t i = std::strtoull(it->first.c_str(), &end, 10);
        if (end && !*end && i >= n) {
          it = o->properties.erase(it);
          continue;
        }
      }
      ++it;
    }
    o->properties["length"] = Value(double(n));
    return true;
  }
  o->properties[key] = value;
  if (o->kind == Object::Kind::Array) {
    char *end = nullptr;
    size_t i = std::strtoull(key.c_str(), &end, 10);
    if (end && !*end && i < 100000 && i + 1 > o->properties["length"].to_number())
      o->properties["length"] = Value(double(i + 1));
  }
  return true;
}
Value Runtime::call(Value fn, const Values &args, Value this_value) {
  // An async function runs its body now, but the caller only ever sees a
  // promise: a return value fulfils it and a throw rejects it.
  if (fn.type == Value::Type::Object && fn.object && fn.object->kind == Object::Kind::Function &&
      fn.object->function && fn.object->function->async) {
    Value promise = make_promise();
    try {
      settle_promise(promise.object, 1, call_sync(fn, args, this_value));
    } catch (JsError &error) {
      settle_promise(promise.object, 2, error.value);
    } catch (const std::exception &error) {
      settle_promise(promise.object, 2, Value(std::string("Error: ") + error.what()));
    }
    return promise;
  }
  return call_sync(fn, args, this_value);
}
Value Runtime::call_sync(Value fn, const Values &args, Value this_value) {
  tick();
  if (++depth_ > 128) {
    --depth_;
    throw std::runtime_error("JavaScript call stack limit");
  }
  Value result;
  try {
    if (fn.type != Value::Type::Object || !fn.object)
      throw JsError{Value("TypeError: value is not callable")};
    Object *o = fn.object;
    if (o->kind == Object::Kind::Native) {
      result = o->native(const_cast<Values &>(args));
    } else if (o->kind == Object::Kind::Function && o->function) {
      Env *env = static_cast<Env *>(allocate(Object::Kind::Environment));
      env->parent = o->closure ? o->closure : global_;
      env_stack_.push_back(env);
      Function *f = o->function;
      for (size_t i = 0; i < f->params.size(); ++i) {
        const auto &param = f->params[i];
        if (param.size() > 3 && param.compare(0, 3, "...") == 0) {
          Values rest;
          for (size_t j = i; j < args.size(); ++j) rest.push_back(args[j]);
          env->properties[param.substr(3)] = make_array(rest);
          break;
        }
        env->properties[param] = i < args.size() ? args[i] : Value();
      }
      env->properties["arguments"] = make_array(args);
      Value saved = this_;
      // An arrow keeps the this it captured, ignoring how it was called.
      this_ = o->has_lexical_this ? o->lexical_this : this_value;
      Flow flow;
      if (f->expression)
        result = evaluate(*f->expression, *env, this_);
      else {
        hoist(f->body, *env);
        flow = execute_block(f->body, *env, this_);
        if (flow.kind == Flow::Kind::Return)
          result = flow.value;
      }
      this_ = saved;
      env_stack_.pop_back();
    } else
      throw JsError{Value("TypeError: value is not callable")};
  } catch (...) {
    --depth_;
    throw;
  }
  --depth_;
  return result;
}
// I link an instance to its own prototype once, then run initialisation from
// the base class down. Linking must not be repeated by a base constructor or a
// derived method would resolve against the base prototype instead of its own.
void Runtime::init_instance(const Value &ctor, Value &object, Values &args) {
  auto proto = ctor.object->properties.find("prototype");
  if (proto != ctor.object->properties.end())
    object.object->properties["__proto__"] = proto->second;
  object.object->properties["__class__"] = ctor;
  Value result = run_init(ctor, object, args);
  if (result.type == Value::Type::Object && result.object && result.object != object.object)
    object = result;
}
Value Runtime::run_init(const Value &ctor, Value &object, Values &args) {
  auto super = ctor.object->properties.find("__super__");
  bool has_super = super != ctor.object->properties.end();
  auto assign_fields = [&]() {
    if (const Expr *ast = ctor.object->class_ast) {
      Env *scope = ctor.object->closure ? ctor.object->closure : global_;
      for (auto &field : ast->fields) {
        Value v = field.second ? evaluate(*field.second, *scope, object) : Value();
        object.object->properties[field.first] = v;
      }
    }
  };
  if (ctor.object->function) {
    // A derived constructor initialises its fields after super() has run.
    if (!has_super)
      assign_fields();
    Value result = call(ctor, args, object);
    if (has_super)
      assign_fields();
    return result;
  }
  if (has_super) {
    Values same = args;
    run_init(super->second, object, same);
  }
  assign_fields();
  return Value();
}
Value Runtime::construct(const Value &constructor, Values &args) {
  if (constructor.type != Value::Type::Object || !constructor.object)
    throw JsError{Value("TypeError: invalid constructor")};
  if (constructor.object->kind == Object::Kind::Native)
    return constructor.object->native(args);
  Value object = make_object();
  init_instance(constructor, object, args);
  return object;
}
void Runtime::hoist(const std::vector<std::unique_ptr<Stmt>> &statements, Env &env) {
  for (auto &s : statements) {
    if (s->kind == Stmt::Kind::Function) {
      Value fn = make_object(Object::Kind::Function);
      fn.object->function = s->function;
      fn.object->closure = &env;
      env.properties[s->name] = fn;
    }
    if (s->kind == Stmt::Kind::Variables && s->text == "var")
      for (auto &d : s->declarations)
        if (!env.properties.count(d.first))
          env.properties[d.first] = Value();
  }
}
Runtime::Flow Runtime::execute_block(const std::vector<std::unique_ptr<Stmt>> &statements, Env &env,
                                     Value this_value) {
  Flow last;
  for (auto &s : statements) {
    Flow f = execute(*s, env, this_value);
    if (f.kind != Flow::Kind::None)
      return f;
    if (f.value.type != Value::Type::Undefined)
      last.value = f.value;
  }
  return last;
}
Runtime::Flow Runtime::execute(const Stmt &s, Env &env, Value this_value) {
  tick();
  switch (s.kind) {
  case Stmt::Kind::Empty:
    return {};
  case Stmt::Kind::Expression:
    last_statement_ = evaluate(*s.expression, env, this_value);
    return {Flow::Kind::None, last_statement_};
  case Stmt::Kind::Variables:
    for (auto &d : s.declarations) {
      Value v = d.second ? evaluate(*d.second, env, this_value) : Value();
      if (s.text == "var") {
        if (!find_env(env, d.first))
          global_->properties[d.first] = Value();
        if (d.second)
          assign_identifier(env, d.first, v);
      } else
        declare(env, d.first, v);
    }
    return {};
  case Stmt::Kind::Function:
    return {};
  case Stmt::Kind::Block: {
    Env *block = static_cast<Env *>(allocate(Object::Kind::Environment));
    block->parent = &env;
    env_stack_.push_back(block);
    hoist(s.body, *block);
    Flow f = execute_block(s.body, *block, this_value);
    env_stack_.pop_back();
    return f;
  }
  case Stmt::Kind::If:
    return evaluate(*s.condition, env, this_value).truthy()
               ? execute_block(s.body, env, this_value)
               : execute_block(s.alternate, env, this_value);
  case Stmt::Kind::While:
    while (evaluate(*s.condition, env, this_value).truthy()) {
      Flow f = execute_block(s.body, env, this_value);
      if (f.kind == Flow::Kind::Break)
        break;
      if (f.kind == Flow::Kind::Return)
        return f;
    }
    return {};
  case Stmt::Kind::Do:
    do {
      Flow f = execute_block(s.body, env, this_value);
      if (f.kind == Flow::Kind::Break)
        break;
      if (f.kind == Flow::Kind::Return)
        return f;
    } while (evaluate(*s.condition, env, this_value).truthy());
    return {};
  case Stmt::Kind::For: {
    Env *loop = static_cast<Env *>(allocate(Object::Kind::Environment));
    loop->parent = &env;
    env_stack_.push_back(loop);
    hoist(s.body, *loop);
    if (s.body[0])
      execute(*s.body[0], *loop, this_value);
    while (!s.condition || evaluate(*s.condition, *loop, this_value).truthy()) {
      Flow f = execute(*s.body[1], *loop, this_value);
      if (f.kind == Flow::Kind::Break)
        break;
      if (f.kind == Flow::Kind::Return) {
        env_stack_.pop_back();
        return f;
      }
      if (s.increment)
        evaluate(*s.increment, *loop, this_value);
    }
    env_stack_.pop_back();
    return {};
  }
  case Stmt::Kind::ForIn: {
    Value iterable = evaluate(*s.condition, env, this_value);
    Values keys;
    if (s.text == "of" && iterable.type == Value::Type::String) {
      // I iterate a string by code point, not by byte.
      for (auto cp : utf8_decode(iterable.string))
        keys.push_back(Value(utf8_encode(cp)));
    } else if (iterable.type == Value::Type::Object && iterable.object) {
      if (iterable.object->kind == Object::Kind::Array) {
        size_t n = size_t(iterable.object->properties["length"].to_number());
        for (size_t i = 0; i < n; ++i)
          keys.push_back(s.text == "of" ? iterable.object->properties[std::to_string(i)]
                                        : Value(std::to_string(i)));
      } else
        for (auto &p : iterable.object->properties)
          keys.push_back(s.text == "of" ? p.second : Value(p.first));
    }
    for (auto &v : keys) {
      if (s.text == "of")
        assign_identifier(env, s.name, v);
      else
        assign_identifier(env, s.name, v);
      Flow f = execute_block(s.body, env, this_value);
      if (f.kind == Flow::Kind::Break)
        break;
      if (f.kind == Flow::Kind::Return)
        return f;
    }
    return {};
  }
  case Stmt::Kind::Return:
    return {Flow::Kind::Return, s.expression ? evaluate(*s.expression, env, this_value) : Value()};
  case Stmt::Kind::Break:
    return {Flow::Kind::Break, {}};
  case Stmt::Kind::Continue:
    return {Flow::Kind::Continue, {}};
  case Stmt::Kind::Throw:
    throw JsError{s.expression ? evaluate(*s.expression, env, this_value) : Value()};
  case Stmt::Kind::Try: {
    Flow f;
    try {
      Env *block = static_cast<Env *>(allocate(Object::Kind::Environment));
      block->parent = &env;
      env_stack_.push_back(block);
      hoist(s.body, *block);
      f = execute_block(s.body, *block, this_value);
      env_stack_.pop_back();
    } catch (JsError &error) {
      env_stack_.pop_back();
      Env *block = static_cast<Env *>(allocate(Object::Kind::Environment));
      block->parent = &env;
      env_stack_.push_back(block);
      if (!s.name.empty())
        block->properties[s.name] = error.value;
      f = execute_block(s.alternate, *block, this_value);
      env_stack_.pop_back();
    }
    Flow final = execute_block(s.final, env, this_value);
    return final.kind == Flow::Kind::None ? f : final;
  }
  }
  return {};
}
Value Runtime::evaluate(Expr &e, Env &env, Value this_value) {
  tick();
  switch (e.kind) {
  case Expr::Kind::Literal:
    return e.literal;
  case Expr::Kind::Regex:
    return make_regex(e.text, e.flags);
  case Expr::Kind::Await:
    return await_value(evaluate(*e.left, env, this_value));
  case Expr::Kind::This:
    return this_value;
  case Expr::Kind::Identifier: {
    Env *found = find_env(env, e.text);
    if (!found)
      throw JsError{Value("ReferenceError: " + e.text + " is not defined")};
    return found->properties[e.text];
  }
  case Expr::Kind::Array: {
    Values vs;
    for (auto &item : e.items) {
      if (item->kind == Expr::Kind::Unary && item->text == "spread") {
        Value source = evaluate(*item->left, env, this_value);
        append_spread(vs, source);
      } else
        vs.push_back(evaluate(*item, env, this_value));
    }
    return make_array(vs);
  }
  case Expr::Kind::Object: {
    Value o = make_object();
    for (auto &p : e.properties)
      o.object->properties[p.first] = evaluate(*p.second, env, this_value);
    for (auto &p : e.computed_properties)
      o.object->properties[evaluate(*p.first, env, this_value).to_string()] =
          evaluate(*p.second, env, this_value);
    return o;
  }
  case Expr::Kind::Class: {
    Value ctor = make_object(Object::Kind::Function);
    ctor.object->function = e.function;
    ctor.object->closure = &env;
    ctor.object->class_ast = &e;
    Value proto = make_object();
    if (e.left) {
      Value super = evaluate(*e.left, env, this_value);
      if (super.type != Value::Type::Object || !super.object)
        throw JsError{Value("TypeError: class extends a non-constructor")};
      ctor.object->properties["__super__"] = super;
      Value super_proto = get_member(super, "prototype");
      if (super_proto.type == Value::Type::Object)
        proto.object->properties["__proto__"] = super_proto;
    }
    for (auto &m : e.properties) {
      Value fn = evaluate(*m.second, env, this_value);
      if (m.first.compare(0, 7, "static:") == 0)
        ctor.object->properties[m.first.substr(7)] = fn;
      else
        proto.object->properties[m.first] = fn;
    }
    ctor.object->properties["prototype"] = proto;
    if (!e.text.empty())
      ctor.object->properties["name"] = Value(e.text);
    return ctor;
  }
  case Expr::Kind::Function: {
    Value fn = make_object(Object::Kind::Function);
    fn.object->function = e.function;
    fn.object->closure = &env;
    if (e.function && e.function->arrow) {
      fn.object->has_lexical_this = true;
      fn.object->lexical_this = this_value.type != Value::Type::Undefined ? this_value : this_;
    }
    return fn;
  }
  case Expr::Kind::Member: {
    Value base = evaluate(*e.left, env, this_value);
    if (e.optional && (base.type == Value::Type::Null || base.type == Value::Type::Undefined))
      return Value();
    std::string key = e.computed ? evaluate(*e.right, env, this_value).to_string() : e.text;
    return get_member(base, key);
  }
  case Expr::Kind::Unary: {
    if (e.text == "delete") {
      if (e.left->kind == Expr::Kind::Member) {
        Value base = evaluate(*e.left->left, env, this_value);
        auto key = e.left->computed
                       ? evaluate(*e.left->right, env, this_value).to_string()
                       : e.left->text;
        if (base.type == Value::Type::Object && base.object) {
          auto &props = base.object->properties;
          bool existed = props.erase(key) != 0;
          if (existed && base.object->kind == Object::Kind::Array && key != "length") {
            // I keep my array length consistent after deleting an index.
            size_t n = size_t(std::max(0.0, props["length"].to_number()));
            size_t live = 0;
            for (size_t i = 0; i < n; ++i)
              if (props.count(std::to_string(i))) ++live;
            (void)live;
          }
          return Value(existed);
        }
      }
      return Value(true);
    }
    if (e.text == "typeof" && e.left->kind == Expr::Kind::Identifier &&
        !find_env(env, e.left->text))
      return Value("undefined");
    Value v = evaluate(*e.left, env, this_value);
    if (e.text == "!")
      return Value(!v.truthy());
    if (e.text == "-")
      return Value(-v.to_number());
    if (e.text == "+")
      return Value(v.to_number());
    if (e.text == "~")
      return Value(double(int32_t(~uint32(v.to_number()))));
    if (e.text == "typeof")
      return Value(v.type_of());
    if (e.text == "void")
      return Value();
    break;
  }
  case Expr::Kind::Template: {
    std::string out;
    for (const auto &part : e.items) {
      out += evaluate(*part, env, this_value).to_string();
      if (out.size() > kMaxDocument)
        throw std::runtime_error("template literal size limit");
    }
    return Value(out);
  }
  case Expr::Kind::Binary: {
    Value a = evaluate(*e.left, env, this_value), b = evaluate(*e.right, env, this_value);
    return binary(e.text, a, b);
  }
  case Expr::Kind::Logical: {
    Value a = evaluate(*e.left, env, this_value);
    if (e.text == "&&")
      return a.truthy() ? evaluate(*e.right, env, this_value) : a;
    if (e.text == "||")
      return a.truthy() ? a : evaluate(*e.right, env, this_value);
    return a.type == Value::Type::Null || a.type == Value::Type::Undefined
               ? evaluate(*e.right, env, this_value)
               : a;
  }
  case Expr::Kind::Assign: {
    Value v = evaluate(*e.right, env, this_value);
    if (e.text != "=") {
      Value old;
      if (e.left->kind == Expr::Kind::Identifier)
        old = evaluate(*e.left, env, this_value);
      else
        old = get_member(evaluate(*e.left->left, env, this_value),
                         e.left->computed ? evaluate(*e.left->right, env, this_value).to_string()
                                          : e.left->text);
      v = binary(e.text, old, v);
    }
    if (e.left->kind == Expr::Kind::Identifier)
      assign_identifier(env, e.left->text, v);
    else if (e.left->kind == Expr::Kind::Member) {
      Value base = evaluate(*e.left->left, env, this_value);
      set_member(base,
                 e.left->computed ? evaluate(*e.left->right, env, this_value).to_string()
                                  : e.left->text,
                 v);
    } else
      throw JsError{Value("ReferenceError: invalid assignment")};
    return v;
  }
  case Expr::Kind::Update: {
    Value old;
    if (e.left->kind == Expr::Kind::Identifier)
      old = evaluate(*e.left, env, this_value);
    else
      old = get_member(evaluate(*e.left->left, env, this_value),
                       e.left->computed ? evaluate(*e.left->right, env, this_value).to_string()
                                        : e.left->text);
    Value next = binary(e.text == "++" ? "+" : "-", old, Value(1));
    if (e.left->kind == Expr::Kind::Identifier)
      assign_identifier(env, e.left->text, next);
    else
      set_member(evaluate(*e.left->left, env, this_value),
                 e.left->computed ? evaluate(*e.left->right, env, this_value).to_string()
                                  : e.left->text,
                 next);
    return e.prefix ? next : Value(old.to_number());
  }
  case Expr::Kind::Conditional:
    return evaluate(*e.left, env, this_value).truthy() ? evaluate(*e.right, env, this_value)
                                                       : evaluate(*e.third, env, this_value);
  case Expr::Kind::Call: {
    Value fn, this_value_call;
    Values args;
    if (e.left->kind == Expr::Kind::Identifier && e.left->text == "super") {
      // super(...) runs the base constructor against the instance I am building.
      Value cls = get_member(this_, "__class__");
      Value base_ctor = get_member(cls, "__super__");
      for (auto &a : e.items)
        args.push_back(evaluate(*a, env, this_value));
      Value self = this_;
      run_init(base_ctor, self, args);
      return Value();
    }
    if (e.left->kind == Expr::Kind::Member) {
      Value base = e.left->left->kind == Expr::Kind::Identifier && e.left->left->text == "super"
                       ? get_member(get_member(get_member(this_, "__class__"), "__super__"),
                                    "prototype")
                       : evaluate(*e.left->left, env, this_value);
      std::string key =
          e.left->computed ? evaluate(*e.left->right, env, this_value).to_string() : e.left->text;
      fn = get_member(base, key);
      this_value_call = e.left->left->kind == Expr::Kind::Identifier &&
                                e.left->left->text == "super"
                            ? this_
                            : base;
    } else
      fn = evaluate(*e.left, env, this_value);
    for (auto &a : e.items) {
      if (a->kind == Expr::Kind::Unary && a->text == "spread") {
        Value source = evaluate(*a->left, env, this_value);
        append_spread(args, source);
      } else
        args.push_back(evaluate(*a, env, this_value));
    }
    return call(fn, args, this_value_call);
  }
  case Expr::Kind::New: {
    Value fn = evaluate(*e.left, env, this_value);
    Values args;
    for (auto &a : e.items)
      args.push_back(evaluate(*a, env, this_value));
    return construct(fn, args);
  }
  }
  return Value();
}
Value Runtime::run(std::string_view source) {
  if (program_bytes_ + source.size() > 16u * 1024u * 1024u)
    throw std::runtime_error("retained script byte limit");
  program_bytes_ += source.size();
  programs_.push_back(std::make_unique<Program>(parse(source)));
  Program &program = *programs_.back();
  reset_fuel();
  env_stack_.push_back(global_);
  hoist(program.statements, *global_);
  Value last;
  try {
    for (auto &s : program.statements) {
      Flow f = execute(*s, *global_, Value(global_));
      last = f.value;
      if (depth_ == 0 && allocated_since_gc_ > 4096)
        collect();
    }
  } catch (...) {
    env_stack_.pop_back();
    try {
      drain_microtasks();
    } catch (...) {
    }
    collect();
    throw;
  }
  env_stack_.pop_back();
  drain_microtasks();
  collect();
  return last;
}


// ---------------------------------------------------------------------------
// Patterns
// ---------------------------------------------------------------------------
Value Runtime::make_regex(const std::string &pattern, const std::string &flags) {
  std::string error;
  auto compiled = Regex::compile(pattern, flags, error);
  if (!compiled)
    throw JsError{Value("SyntaxError: " + error)};
  Value value = make_object();
  value.object->host_type = "RegExp";
  value.object->internal = std::make_shared<Regex>(std::move(*compiled));
  const Regex *re = static_cast<const Regex *>(value.object->internal.get());
  value.object->properties["source"] = Value(pattern);
  value.object->properties["flags"] = Value(flags);
  value.object->properties["global"] = Value(re->global());
  value.object->properties["ignoreCase"] = Value(re->ignore_case());
  value.object->properties["multiline"] = Value(re->multiline());
  value.object->properties["sticky"] = Value(re->sticky());
  value.object->properties["lastIndex"] = Value(0.0);
  if (regex_prototype_) {
    value.object->prototype = regex_prototype_;
    value.object->properties["__proto__"] = Value(regex_prototype_);
  }
  return value;
}

namespace {
// I slice a run of code points back into text.
std::string slice_codepoints(const std::vector<uint32_t> &cps, size_t begin, size_t end) {
  std::string out;
  for (size_t i = begin; i < end && i < cps.size(); ++i)
    out += utf8_encode(cps[i]);
  return out;
}
} // namespace

Value Runtime::regex_exec(const Value &self, const Value &input) {
  if (self.type != Value::Type::Object || !self.object)
    return Value::null();
  auto compiled = std::static_pointer_cast<Regex>(self.object->internal);
  if (!compiled)
    return Value::null();
  std::string subject;
  if (input.type != Value::Type::Undefined && input.type != Value::Type::Null)
    subject = input.to_string();
  auto cps = utf8_decode(subject);
  bool stateful = compiled->global() || compiled->sticky();
  long long last = stateful ? (long long)self.object->properties["lastIndex"].to_number() : 0;
  RegexResult result;
  bool matched = false;
  try {
    matched = compiled->search(cps, size_t(std::max(0LL, last)), result);
  } catch (const std::exception &e) {
    throw JsError{Value(std::string("RegExp: ") + e.what())};
  }
  if (!matched) {
    if (stateful)
      self.object->properties["lastIndex"] = Value(0.0);
    return Value::null();
  }
  if (stateful)
    self.object->properties["lastIndex"] = Value(double(result.groups[0].end));
  Values pieces;
  for (const auto &group : result.groups) {
    pieces.push_back(group.matched ? Value(slice_codepoints(cps, group.begin, group.end)) : Value());
  }
  if (pieces.empty())
    pieces.push_back(Value());
  Value out = make_array(pieces);
  out.object->properties["index"] = Value(double(result.groups[0].begin));
  out.object->properties["input"] = Value(subject);
  return out;
}

Value Runtime::string_match(const std::string &subject, const Value &pattern) {
  if (pattern.type != Value::Type::Object || !pattern.object ||
      pattern.object->host_type != "RegExp")
    return regex_exec(make_regex(pattern.to_string(), ""), Value(subject));
  auto compiled = std::static_pointer_cast<Regex>(pattern.object->internal);
  if (!compiled)
    return Value::null();
  if (!compiled->global())
    return regex_exec(pattern, Value(subject));
  auto cps = utf8_decode(subject);
  Values out;
  size_t position = 0;
  try {
    while (position <= cps.size()) {
      RegexResult result;
      if (!compiled->search(cps, position, result))
        break;
      out.push_back(Value(slice_codepoints(cps, result.groups[0].begin, result.groups[0].end)));
      position = result.groups[0].end == result.groups[0].begin ? result.groups[0].end + 1
                                                               : result.groups[0].end;
      if (out.size() > 100000)
        break;
    }
  } catch (const std::exception &e) {
    throw JsError{Value(std::string("RegExp: ") + e.what())};
  }
  pattern.object->properties["lastIndex"] = Value(0.0);
  return out.empty() ? Value::null() : make_array(out);
}

Value Runtime::string_search(const std::string &subject, const Value &pattern) {
  Value target = pattern;
  if (pattern.type != Value::Type::Object || !pattern.object ||
      pattern.object->host_type != "RegExp")
    target = make_regex(pattern.to_string(), "");
  auto compiled = std::static_pointer_cast<Regex>(target.object->internal);
  if (!compiled)
    return Value(-1.0);
  RegexResult result;
  try {
    if (!compiled->search(utf8_decode(subject), 0, result))
      return Value(-1.0);
  } catch (const std::exception &e) {
    throw JsError{Value(std::string("RegExp: ") + e.what())};
  }
  return Value(double(result.groups[0].begin));
}

Value Runtime::string_split(const std::string &subject, const Value &separator) {
  if (separator.type == Value::Type::Object && separator.object &&
      separator.object->host_type == "RegExp") {
    auto compiled = std::static_pointer_cast<Regex>(separator.object->internal);
    if (!compiled)
      return make_array({Value(subject)});
    auto cps = utf8_decode(subject);
    Values out;
    size_t position = 0, scan = 0;
    try {
      while (scan <= cps.size()) {
        RegexResult result;
        if (!compiled->search(cps, scan, result))
          break;
        size_t begin = result.groups[0].begin, end = result.groups[0].end;
        if (end == begin) {
          if (begin >= cps.size())
            break;
          if (begin == scan) {
            ++scan;
            continue;
          }
        }
        out.push_back(Value(slice_codepoints(cps, position, begin)));
        for (size_t g = 1; g < result.groups.size(); ++g)
          out.push_back(result.groups[g].matched
                            ? Value(slice_codepoints(cps, result.groups[g].begin, result.groups[g].end))
                            : Value());
        position = end;
        scan = end == begin ? end + 1 : end;
        if (out.size() > 100000)
          break;
      }
    } catch (const std::exception &e) {
      throw JsError{Value(std::string("RegExp: ") + e.what())};
    }
    out.push_back(Value(slice_codepoints(cps, position, cps.size())));
    return make_array(out);
  }
  Values out;
  if (separator.type == Value::Type::Undefined)
    return make_array({Value(subject)});
  std::string sep = separator.to_string();
  if (sep.empty()) {
    for (auto cp : utf8_decode(subject))
      out.emplace_back(utf8_encode(cp));
    return make_array(out);
  }
  size_t position = 0;
  while (true) {
    size_t found = subject.find(sep, position);
    out.emplace_back(subject.substr(position, found == std::string::npos ? found : found - position));
    if (found == std::string::npos)
      break;
    position = found + sep.size();
    if (out.size() > 100000)
      break;
  }
  return make_array(out);
}

namespace {
// I expand $&, $`, $', $$ and $1..$99 the way a replacement string expects.
std::string expand_replacement(const std::string &replacement, const RegexResult &result,
                               const std::vector<uint32_t> &cps) {
  std::string out;
  for (size_t i = 0; i < replacement.size(); ++i) {
    char c = replacement[i];
    if (c != '$' || i + 1 >= replacement.size()) {
      out += c;
      continue;
    }
    char next = replacement[i + 1];
    if (next == '$') {
      out += '$';
      ++i;
    } else if (next == '&') {
      out += slice_codepoints(cps, result.groups[0].begin, result.groups[0].end);
      ++i;
    } else if (next == '`') {
      out += slice_codepoints(cps, 0, result.groups[0].begin);
      ++i;
    } else if (next == '\'') {
      out += slice_codepoints(cps, result.groups[0].end, cps.size());
      ++i;
    } else if (next >= '0' && next <= '9') {
      size_t index = size_t(next - '0');
      size_t consumed = 2;
      if (i + 2 < replacement.size() && replacement[i + 2] >= '0' && replacement[i + 2] <= '9') {
        size_t two = index * 10 + size_t(replacement[i + 2] - '0');
        if (two < result.groups.size()) {
          index = two;
          consumed = 3;
        }
      }
      if (index < result.groups.size() && result.groups[index].matched)
        out += slice_codepoints(cps, result.groups[index].begin, result.groups[index].end);
      i += consumed - 1;
    } else {
      out += c;
    }
  }
  return out;
}
} // namespace

Value Runtime::string_replace(const std::string &subject, const Value &pattern,
                              const Value &replacement, bool all) {
  bool is_regex = pattern.type == Value::Type::Object && pattern.object &&
                  pattern.object->host_type == "RegExp";
  if (!is_regex) {
    std::string from = pattern.to_string();
    auto cps = utf8_decode(subject);
    if (from.empty()) {
      // An empty search string inserts at the start, like the language says.
      std::string inserted;
      if (callable(replacement)) {
        Values args{Value(""), Value(0.0), Value(subject)};
        inserted = call(replacement, args, Value()).to_string();
      } else {
        RegexResult empty_result;
        empty_result.groups.push_back(RegexGroup{true, 0, 0});
        inserted = expand_replacement(replacement.to_string(), empty_result, cps);
      }
      return Value(inserted + subject);
    }
    std::string out;
    size_t position = 0;
    size_t count = 0;
    while (position <= subject.size()) {
      size_t found = subject.find(from, position);
      if (found == std::string::npos) {
        out += subject.substr(position);
        break;
      }
      out += subject.substr(position, found - position);
      size_t match_begin = found, match_end = found + from.size();
      if (callable(replacement)) {
        Values args{Value(from), Value(double(match_begin)), Value(subject)};
        out += call(replacement, args, Value()).to_string();
      } else {
        RegexResult plain;
        plain.groups.push_back(RegexGroup{true, match_begin, match_end});
        out += expand_replacement(replacement.to_string(), plain, cps);
      }
      position = match_end;
      if (!all || ++count > 100000)
        {
          out += subject.substr(position);
          break;
        }
    }
    return Value(out);
  }
  auto compiled = std::static_pointer_cast<Regex>(pattern.object->internal);
  if (!compiled)
    return Value(subject);
  bool every = all || compiled->global();
  auto cps = utf8_decode(subject);
  std::string out;
  size_t position = 0, scan = 0;
  try {
    while (scan <= cps.size()) {
      RegexResult result;
      if (!compiled->search(cps, scan, result))
        break;
      size_t begin = result.groups[0].begin, end = result.groups[0].end;
      out += slice_codepoints(cps, position, begin);
      if (callable(replacement)) {
        Values args;
        for (const auto &group : result.groups)
          args.push_back(group.matched ? Value(slice_codepoints(cps, group.begin, group.end))
                                       : Value());
        args.push_back(Value(double(begin)));
        args.push_back(Value(subject));
        out += call(replacement, args, Value()).to_string();
      } else {
        out += expand_replacement(replacement.to_string(), result, cps);
      }
      position = end;
      if (!every) {
        out += slice_codepoints(cps, position, cps.size());
        return Value(out);
      }
      if (end == begin) {
        if (end >= cps.size())
          break;
        out += utf8_encode(cps[end]);
        ++end;
      }
      scan = end;
      position = end;
    }
  } catch (const std::exception &e) {
    throw JsError{Value(std::string("RegExp: ") + e.what())};
  }
  out += slice_codepoints(cps, position, cps.size());
  return Value(out);
}

// ---------------------------------------------------------------------------
// Promises
// ---------------------------------------------------------------------------
Value Runtime::make_promise() {
  Value promise = make_object();
  promise.object->host_type = "Promise";
  promise.object->promise_state = 0;
  if (promise_prototype_) {
    promise.object->prototype = promise_prototype_;
    promise.object->properties["__proto__"] = Value(promise_prototype_);
  }
  return promise;
}

void Runtime::enqueue_microtask(std::function<void()> run, std::vector<Value> roots) {
  if (microtasks_.size() >= 200000)
    throw std::runtime_error("microtask limit");
  microtasks_.push_back(Microtask{std::move(run), std::move(roots)});
}

Value Runtime::settle_reaction(Object *promise, const PromiseReaction &reaction, int state,
                               const Value &value) {
  Value handler = state == 1 ? reaction.on_fulfilled : reaction.on_rejected;
  if (!callable(handler)) {
    if (reaction.child.type == Value::Type::Object && reaction.child.object)
      settle_promise(reaction.child.object, state, value);
    return Value();
  }
  try {
    Values args{value};
    Value result = call(handler, args, Value());
    if (reaction.child.type == Value::Type::Object && reaction.child.object)
      settle_promise(reaction.child.object, 1, result);
  } catch (JsError &error) {
    if (reaction.child.type == Value::Type::Object && reaction.child.object)
      settle_promise(reaction.child.object, 2, error.value);
  } catch (const std::exception &error) {
    if (reaction.child.type == Value::Type::Object && reaction.child.object)
      settle_promise(reaction.child.object, 2, Value(std::string(error.what())));
  }
  (void)promise;
  return Value();
}

void Runtime::settle_promise(Object *promise, int state, const Value &value) {
  if (!promise || promise->promise_state != 0)
    return;
  if (value.type == Value::Type::Object && value.object == promise)
    return;
  if (state == 1 && value.type == Value::Type::Object && value.object) {
    // I adopt another thenable rather than settling on it.
    Value then;
    try {
      then = get_member(value, "then");
    } catch (JsError &) {
      then = Value();
    } catch (const std::exception &) {
      then = Value();
    }
    if (callable(then)) {
      Value target(promise);
      auto already = std::make_shared<bool>(false);
      Runtime *runtime = this;
      Value resolve_fn = make_native([runtime, target, already](Values &a) {
        if (*already)
          return Value();
        *already = true;
        runtime->settle_promise(target.object, 1, a.empty() ? Value() : a[0]);
        return Value();
      });
      Value reject_fn = make_native([runtime, target, already](Values &a) {
        if (*already)
          return Value();
        *already = true;
        runtime->settle_promise(target.object, 2, a.empty() ? Value() : a[0]);
        return Value();
      });
      try {
        Values args{resolve_fn, reject_fn};
        call(then, args, value);
      } catch (JsError &error) {
        if (!*already) {
          *already = true;
          settle_promise(promise, 2, error.value);
        }
      } catch (const std::exception &error) {
        if (!*already) {
          *already = true;
          settle_promise(promise, 2, Value(std::string(error.what())));
        }
      }
      return;
    }
  }
  promise->promise_state = state;
  promise->promise_result = value;
  if (state == 2 && promise->reactions.empty())
    promise->promise_handled = false;
  std::vector<PromiseReaction> waiting = promise->reactions;
  promise->reactions.clear();
  Value self(promise);
  for (const auto &reaction : waiting) {
    PromiseReaction copy = reaction;
    Value reason = value;
    int settled = state;
    enqueue_microtask(
        [this, promise, copy, settled, reason]() {
          settle_reaction(promise, copy, settled, reason);
        },
        {self, copy.on_fulfilled, copy.on_rejected, copy.child, value});
  }
}

Value Runtime::await_value(const Value &value) {
  if (!(value.type == Value::Type::Object && value.object && value.object->host_type == "Promise"))
    return value;
  Object *promise = value.object;
  auto started = std::chrono::steady_clock::now();
  // I keep going while anything at all could settle this promise. If nothing
  // can, I stop instead of freezing the page forever.
  while (promise->promise_state == 0) {
    if (!microtasks_.empty()) {
      drain_microtasks(10000);
      continue;
    }
    if (pump_ && pump_())
      continue;
    auto waited = std::chrono::steady_clock::now() - started;
    if (waited > std::chrono::milliseconds(5000))
      throw JsError{Value("Error: await never settled")};
    if (!pump_ && microtasks_.empty())
      throw JsError{Value("Error: await on a promise that never settles")};
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  if (promise->promise_state == 2) {
    promise->promise_handled = true;
    throw JsError{promise->promise_result};
  }
  return promise->promise_result;
}
Value Runtime::promise_then(const Value &promise, const Value &on_fulfilled,
                            const Value &on_rejected) {
  if (promise.type != Value::Type::Object || !promise.object)
    throw JsError{Value("TypeError: then called on a non-promise")};
  Object *object = promise.object;
  Value child = make_promise();
  if (object->promise_state == 0) {
    object->reactions.push_back(PromiseReaction{on_fulfilled, on_rejected, child});
    return child;
  }
  if (object->promise_state == 2)
    object->promise_handled = true;
  PromiseReaction reaction{on_fulfilled, on_rejected, child};
  int state = object->promise_state;
  Value value = object->promise_result;
  enqueue_microtask(
      [this, object, reaction, state, value]() mutable {
        settle_reaction(object, reaction, state, value);
      },
      {promise, on_fulfilled, on_rejected, child, value});
  return child;
}

void Runtime::drain_microtasks(size_t limit) {
  size_t ran = 0;
  while (!microtasks_.empty()) {
    if (++ran > limit)
      throw std::runtime_error("microtask limit");
    Microtask task = std::move(microtasks_.front());
    microtasks_.pop_front();
    reset_fuel();
    task.run();
  }
  if (depth_ == 0 && allocated_since_gc_ > 4096)
    collect();
}

void Runtime::install_builtins() {
  Env &g = *global_;
  g.properties["globalThis"] = Value(&g);
  g.properties["window"] = Value(&g);
  g.properties["undefined"] = Value();
  g.properties["NaN"] = Value(std::numeric_limits<double>::quiet_NaN());
  g.properties["Infinity"] = Value(std::numeric_limits<double>::infinity());
  g.properties["document"] = host_value(-1, "Document");
  Value nav = make_object();
  nav.object->properties["userAgent"] = Value("PsychoVault/0.1 (owned engine; development)");
  nav.object->properties["platform"] = Value(
#ifdef _WIN32
      "Win32"
#else
      "Linux x86_64"
#endif
  );
  g.properties["navigator"] = nav;
  Value console = make_object();
  for (std::string level : {"log", "info", "warn", "error", "debug"})
    console.object->properties[level] = make_native([this, level](Values &a) {
      std::string line = level + ": ";
      for (size_t i = 0; i < a.size(); ++i) {
        if (i)
          line += ' ';
        line += a[i].type == Value::Type::String   ? a[i].string
                : a[i].type == Value::Type::Object ? a[i].to_json().dump()
                                                   : a[i].to_string();
      }
      if (host_)
        host_->log(line);
      return Value();
    });
  g.properties["console"] = console;
  Value math = make_object();
  for (std::string name :
       {"floor", "ceil", "round", "trunc", "abs", "sqrt", "sin", "cos", "tan", "log", "exp"})
    math.object->properties[name] = make_native([name](Values &a) {
      double x = a.empty() ? std::numeric_limits<double>::quiet_NaN() : a[0].to_number();
      if (name == "floor")
        return Value(std::floor(x));
      if (name == "ceil")
        return Value(std::ceil(x));
      if (name == "round")
        return Value(std::floor(x + 0.5));
      if (name == "trunc")
        return Value(std::trunc(x));
      if (name == "abs")
        return Value(std::fabs(x));
      if (name == "sqrt")
        return Value(std::sqrt(x));
      if (name == "sin")
        return Value(std::sin(x));
      if (name == "cos")
        return Value(std::cos(x));
      if (name == "tan")
        return Value(std::tan(x));
      if (name == "log")
        return Value(std::log(x));
      return Value(std::exp(x));
    });
  math.object->properties["PI"] = Value(3.14159265358979323846);
  math.object->properties["E"] = Value(2.71828182845904523536);
  math.object->properties["min"] = make_native([](Values &a) {
    double x = std::numeric_limits<double>::infinity();
    for (auto &v : a)
      x = std::min(x, v.to_number());
    return Value(x);
  });
  math.object->properties["max"] = make_native([](Values &a) {
    double x = -std::numeric_limits<double>::infinity();
    for (auto &v : a)
      x = std::max(x, v.to_number());
    return Value(x);
  });
  math.object->properties["pow"] = make_native([](Values &a) {
    return Value(
        std::pow(a.size() > 0 ? a[0].to_number() : 0, a.size() > 1 ? a[1].to_number() : 0));
  });
  math.object->properties["random"] = make_native([](Values &) { return Value(0.5); });
  g.properties["Math"] = math;
  math.object->properties["random"] = make_native([](Values &) -> Value {
    auto bytes = random_bytes(8);
    uint64_t bits = 0;
    for (auto b : bytes)
      bits = (bits << 8) | b;
    return Value(double(bits >> 11) / 9007199254740992.0);
  });
  g.properties["String"] =
      make_native([](Values &a) { return Value(a.empty() ? "" : a[0].to_string()); });
  g.properties["Number"] =
      make_native([](Values &a) { return Value(a.empty() ? 0 : a[0].to_number()); });
  g.properties["Boolean"] =
      make_native([](Values &a) { return Value(!a.empty() && a[0].truthy()); });
  g.properties["parseInt"] = make_native([](Values &a) {
    std::string s = trim(a.empty() ? "" : a[0].to_string());
    int base = a.size() > 1 ? int(a[1].to_number()) : starts(s, "0x") ? 16 : 10;
    char *end = nullptr;
    double n = double(std::strtoll(s.c_str(), &end, base));
    return Value(end == s.c_str() ? std::numeric_limits<double>::quiet_NaN() : n);
  });
  g.properties["parseFloat"] = make_native([](Values &a) {
    std::string s = trim(a.empty() ? "" : a[0].to_string());
    char *end = nullptr;
    double n = std::strtod(s.c_str(), &end);
    return Value(end == s.c_str() ? std::numeric_limits<double>::quiet_NaN() : n);
  });
  g.properties["isNaN"] = make_native([](Values &a) {
    return Value(
        std::isnan(a.empty() ? std::numeric_limits<double>::quiet_NaN() : a[0].to_number()));
  });
  Value array = make_native([this](Values &a) {
    if (a.size() == 1 && a[0].type == Value::Type::Number) {
      size_t n = size_t(a[0].to_number());
      if (n > 100000)
        throw std::runtime_error("array limit");
      return make_array(Values(n));
    }
    return make_array(a);
  });
  array.object->properties["from"] = make_native([this](Values &a) {
    Values out;
    if (!a.empty()) append_spread(out, a[0]);
    if (a.size() > 1 && a[1].type == Value::Type::Object &&
        a[1].object->kind == Object::Kind::Function)
      for (size_t i = 0; i < out.size(); ++i) {
        Values one{out[i], Value(double(i))};
        Value receiver;
        out[i] = call(a[1], one, receiver);
      }
    return make_array(out);
  });
  array.object->properties["of"] = make_native([this](Values &a) { return make_array(a); });
  g.properties["Array"] = array;
  // Map and Set keep insertion order because my object properties are ordered.
  g.properties["Map"] = make_native([this](Values &a) {
    Value self = make_object();
    Value store = make_object();
    Value order = make_array({});
    self.object->properties["__entries__"] = store;
    self.object->properties["__order__"] = order;
    self.object->properties["size"] = Value(0.0);
    self.object->properties["set"] = make_native([store, order, self](Values &args) mutable {
      auto key = args.empty() ? std::string("undefined") : args[0].to_string();
      if (!store.object->properties.count(key)) {
        size_t n = size_t(order.object->properties["length"].to_number());
        order.object->properties[std::to_string(n)] = Value(key);
        order.object->properties["length"] = Value(double(n + 1));
        self.object->properties["size"] = Value(double(n + 1));
      }
      store.object->properties[key] = args.size() > 1 ? args[1] : Value();
      return self;
    });
    self.object->properties["get"] = make_native([store](Values &args) {
      auto key = args.empty() ? std::string("undefined") : args[0].to_string();
      auto it = store.object->properties.find(key);
      return it == store.object->properties.end() ? Value() : it->second;
    });
    self.object->properties["has"] = make_native([store](Values &args) {
      auto key = args.empty() ? std::string("undefined") : args[0].to_string();
      return Value(store.object->properties.count(key) != 0);
    });
    self.object->properties["delete"] = make_native([store, order, self](Values &args) mutable {
      auto key = args.empty() ? std::string("undefined") : args[0].to_string();
      bool had = store.object->properties.erase(key) != 0;
      if (had) {
        Values kept;
        size_t n = size_t(order.object->properties["length"].to_number());
        for (size_t i = 0; i < n; ++i) {
          Value k = order.object->properties[std::to_string(i)];
          if (k.to_string() != key) kept.push_back(k);
        }
        order.object->properties.clear();
        for (size_t i = 0; i < kept.size(); ++i)
          order.object->properties[std::to_string(i)] = kept[i];
        order.object->properties["length"] = Value(double(kept.size()));
        self.object->properties["size"] = Value(double(kept.size()));
      }
      return Value(had);
    });
    self.object->properties["clear"] = make_native([store, order, self](Values &) mutable {
      store.object->properties.clear();
      order.object->properties.clear();
      order.object->properties["length"] = Value(0.0);
      self.object->properties["size"] = Value(0.0);
      return Value();
    });
    for (std::string which : {std::string("keys"), std::string("values"), std::string("entries")})
      self.object->properties[which] = make_native([this, store, order, which](Values &) {
        Values out;
        size_t n = size_t(order.object->properties["length"].to_number());
        for (size_t i = 0; i < n; ++i) {
          Value k = order.object->properties[std::to_string(i)];
          auto found = store.object->properties.find(k.to_string());
          if (found == store.object->properties.end()) continue;
          if (which == "keys")
            out.push_back(k);
          else if (which == "values")
            out.push_back(found->second);
          else
            out.push_back(make_array({k, found->second}));
        }
        return make_array(out);
      });
    self.object->properties["forEach"] = make_native([this, self](Values &args) {
      if (args.empty()) return Value();
      Value entries_fn = get_member(self, "entries");
      Values none;
      Value entries = call(entries_fn, none, self);
      size_t n = size_t(entries.object->properties["length"].to_number());
      for (size_t i = 0; i < n; ++i) {
        Value pair = entries.object->properties[std::to_string(i)];
        Values call_args{pair.object->properties["1"], pair.object->properties["0"], self};
        Value receiver = args.size() > 1 ? args[1] : Value();
        call(args[0], call_args, receiver);
      }
      return Value();
    });
    if (!a.empty()) {
      Values init;
      append_spread(init, a[0]);
      for (auto &pair : init) {
        Values kv;
        append_spread(kv, pair);
        Values setargs{kv.empty() ? Value() : kv[0], kv.size() > 1 ? kv[1] : Value()};
        call(self.object->properties["set"], setargs, self);
      }
    }
    return self;
  });
  // Object rest for my destructuring patterns copies the keys not taken.
  g.properties["__pv_rest"] = make_native([this](Values &a) {
    Value out = make_object();
    if (a.empty() || a[0].type != Value::Type::Object || !a[0].object)
      return out;
    for (auto &p : a[0].object->properties) {
      bool excluded = false;
      for (size_t i = 1; i < a.size(); ++i)
        if (a[i].to_string() == p.first) { excluded = true; break; }
      if (!excluded && p.first.compare(0, 4, "get:") != 0 && p.first.compare(0, 4, "set:") != 0 &&
          p.first != "__proto__" && p.first != "__class__")
        out.object->properties[p.first] = p.second;
    }
    return out;
  });
  g.properties["Set"] = make_native([this](Values &a) {
    Value self = make_object();
    Values init;
    if (!a.empty()) append_spread(init, a[0]);
    Values unique;
    for (auto &v : init) {
      bool found = false;
      for (auto &u : unique)
        if (strict_equals(u, v)) { found = true; break; }
      if (!found) unique.push_back(v);
    }
    self.object->properties["__values__"] = make_array(unique);
    self.object->properties["size"] = Value(double(unique.size()));
    self.object->properties["add"] = make_native([self](Values &args) mutable {
      Value list = self.object->properties["__values__"];
      size_t n = size_t(list.object->properties["length"].to_number());
      Value v = args.empty() ? Value() : args[0];
      bool found = false;
      for (size_t i = 0; i < n; ++i)
        if (strict_equals(list.object->properties[std::to_string(i)], v)) { found = true; break; }
      if (!found) {
        list.object->properties[std::to_string(n)] = v;
        list.object->properties["length"] = Value(double(n + 1));
        self.object->properties["size"] = Value(double(n + 1));
      }
      return self;
    });
    self.object->properties["has"] = make_native([self](Values &args) {
      Value list = self.object->properties["__values__"];
      size_t n = size_t(list.object->properties["length"].to_number());
      Value v = args.empty() ? Value() : args[0];
      for (size_t i = 0; i < n; ++i)
        if (strict_equals(list.object->properties[std::to_string(i)], v)) return Value(true);
      return Value(false);
    });
    self.object->properties["delete"] = make_native([this, self](Values &args) mutable {
      Value list = self.object->properties["__values__"];
      size_t n = size_t(list.object->properties["length"].to_number());
      Value v = args.empty() ? Value() : args[0];
      Values kept;
      for (size_t i = 0; i < n; ++i) {
        Value item = list.object->properties[std::to_string(i)];
        if (!strict_equals(item, v)) kept.push_back(item);
      }
      bool removed = kept.size() != n;
      self.object->properties["__values__"] = make_array(kept);
      self.object->properties["size"] = Value(double(kept.size()));
      return Value(removed);
    });
    self.object->properties["clear"] = make_native([this, self](Values &) mutable {
      self.object->properties["__values__"] = make_array({});
      self.object->properties["size"] = Value(0.0);
      return Value();
    });
    for (std::string which : {std::string("keys"), std::string("values"), std::string("entries")})
      self.object->properties[which] = make_native([this, self, which](Values &) {
        Value list = self.object->properties["__values__"];
        Values out;
        size_t n = size_t(list.object->properties["length"].to_number());
        for (size_t i = 0; i < n; ++i) {
          Value v = list.object->properties[std::to_string(i)];
          out.push_back(which == "entries" ? make_array({v, v}) : v);
        }
        return make_array(out);
      });
    self.object->properties["forEach"] = make_native([this, self](Values &args) {
      if (args.empty()) return Value();
      Value list = self.object->properties["__values__"];
      size_t n = size_t(list.object->properties["length"].to_number());
      for (size_t i = 0; i < n; ++i) {
        Value v = list.object->properties[std::to_string(i)];
        Values call_args{v, v, self};
        Value receiver = args.size() > 1 ? args[1] : Value();
        call(args[0], call_args, receiver);
      }
      return Value();
    });
    return self;
  });
  Value object = make_native([this](Values &) { return make_object(); });
  // keys/values/entries need the runtime for allocation, so bind this
  // explicitly.
  object.object->properties["keys"] = make_native([this](Values &a) {
    Values out;
    if (!a.empty() && a[0].type == Value::Type::Object)
      for (auto &p : a[0].object->properties)
        if (a[0].object->kind != Object::Kind::Array || p.first != "length")
          out.emplace_back(p.first);
    return make_array(out);
  });
  object.object->properties["values"] = make_native([this](Values &a) {
    Values out;
    if (!a.empty() && a[0].type == Value::Type::Object)
      for (auto &p : a[0].object->properties)
        if (a[0].object->kind != Object::Kind::Array || p.first != "length")
          out.push_back(p.second);
    return make_array(out);
  });
  object.object->properties["entries"] = make_native([this](Values &a) {
    Values out;
    if (!a.empty() && a[0].type == Value::Type::Object)
      for (auto &p : a[0].object->properties)
        if (a[0].object->kind != Object::Kind::Array || p.first != "length")
          out.push_back(make_array({Value(p.first), p.second}));
    return make_array(out);
  });
  object.object->properties["assign"] = make_native([](Values &a) {
    if (a.empty())
      return Value();
    for (size_t i = 1; i < a.size(); ++i)
      if (a[i].type == Value::Type::Object)
        for (auto &p : a[i].object->properties)
          a[0].object->properties[p.first] = p.second;
    return a[0];
  });
  g.properties["Object"] = object;
  Value json = make_object();
  json.object->properties["stringify"] =
      make_native([](Values &a) { return a.empty() ? Value() : Value(a[0].to_json().dump()); });
  json.object->properties["parse"] = make_native([this](Values &a) {
    auto parsed = Json::parse(a.empty() ? "" : a[0].to_string(), 2u * 1024u * 1024u);
    if (!parsed)
      throw JsError{Value("SyntaxError: invalid JSON")};
    return from_json(*parsed, *this);
  });
  g.properties["JSON"] = json;
  Value date = make_object();
  date.object->properties["now"] = make_native([](Values &) {
    return Value(double(std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count()));
  });
  g.properties["Date"] = date;
  for (std::string name : {"Error", "TypeError", "RangeError", "SyntaxError"})
    g.properties[name] = make_native([this, name](Values &a) {
      Value e = make_object();
      e.object->properties["name"] = Value(name);
      e.object->properties["message"] = Value(a.empty() ? "" : a[0].to_string());
      return e;
    });
  g.properties["setTimeout"] = make_native([this](Values &a) {
    if (a.empty() || a[0].type_of() != "function")
      throw JsError{Value("TypeError: timer callback required")};
    double delay = std::clamp(a.size() > 1 ? a[1].to_number() : 0, 0.0, 86400000.0);
    if (timers.size() >= 256)
      throw std::runtime_error("timer limit");
    double now = double(std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now().time_since_epoch())
                            .count());
    Timer t{next_timer++, now + delay, a[0],
            Values(a.begin() + std::min(size_t(2), a.size()), a.end()), false};
    timers.push_back(t);
    return Value(t.id);
  });
  g.properties["clearTimeout"] = make_native([this](Values &a) {
    double id = a.empty() ? 0 : a[0].to_number();
    std::erase_if(timers, [id](const Timer &t) { return t.id == id; });
    return Value();
  });
  // My pattern constructor and the prototype my literals share.
  Value regex_proto = make_object();
  regex_prototype_ = regex_proto.object;
  root(regex_prototype_);
  Value regexp = make_native([this](Values &a) {
    if (!a.empty() && a[0].type == Value::Type::Object && a[0].object &&
        a[0].object->host_type == "RegExp") {
      std::string pattern = a[0].object->properties["source"].to_string();
      std::string flags = a.size() > 1 && a[1].type != Value::Type::Undefined
                              ? a[1].to_string()
                              : a[0].object->properties["flags"].to_string();
      return make_regex(pattern, flags);
    }
    std::string pattern =
        a.empty() || a[0].type == Value::Type::Undefined ? std::string() : a[0].to_string();
    std::string flags =
        a.size() > 1 && a[1].type != Value::Type::Undefined ? a[1].to_string() : std::string();
    return make_regex(pattern, flags);
  });
  regexp.object->properties["prototype"] = regex_proto;
  g.properties["RegExp"] = regexp;

  // My promise constructor. The executor runs now; handlers run as microtasks.
  Value promise_proto = make_object();
  promise_prototype_ = promise_proto.object;
  root(promise_prototype_);
  Value promise_ctor = make_native([this](Values &a) {
    if (a.empty() || !callable(a[0]))
      throw JsError{Value("TypeError: Promise resolver is not a function")};
    Value promise = make_promise();
    auto settled = std::make_shared<bool>(false);
    Runtime *runtime = this;
    Value resolve_fn = make_native([runtime, promise, settled](Values &args) {
      if (*settled)
        return Value();
      *settled = true;
      runtime->settle_promise(promise.object, 1, args.empty() ? Value() : args[0]);
      return Value();
    });
    Value reject_fn = make_native([runtime, promise, settled](Values &args) {
      if (*settled)
        return Value();
      *settled = true;
      runtime->settle_promise(promise.object, 2, args.empty() ? Value() : args[0]);
      return Value();
    });
    Values executor_args{resolve_fn, reject_fn};
    try {
      call(a[0], executor_args, Value());
    } catch (JsError &error) {
      if (!*settled) {
        *settled = true;
        settle_promise(promise.object, 2, error.value);
      }
    } catch (const std::exception &error) {
      if (!*settled) {
        *settled = true;
        settle_promise(promise.object, 2, Value(std::string(error.what())));
      }
    }
    return promise;
  });
  promise_ctor.object->properties["prototype"] = promise_proto;
  promise_ctor.object->properties["resolve"] = make_native([this](Values &a) {
    if (!a.empty() && a[0].type == Value::Type::Object && a[0].object &&
        a[0].object->host_type == "Promise")
      return a[0];
    Value promise = make_promise();
    settle_promise(promise.object, 1, a.empty() ? Value() : a[0]);
    return promise;
  });
  promise_ctor.object->properties["reject"] = make_native([this](Values &a) {
    Value promise = make_promise();
    settle_promise(promise.object, 2, a.empty() ? Value() : a[0]);
    return promise;
  });
  promise_ctor.object->properties["all"] = make_native([this](Values &a) {
    Value result = make_promise();
    Values items;
    if (!a.empty())
      append_spread(items, a[0]);
    auto collected = std::make_shared<Values>(items.size());
    auto remaining = std::make_shared<size_t>(items.size());
    auto failed = std::make_shared<bool>(false);
    Runtime *runtime = this;
    for (size_t i = 0; i < items.size(); ++i) {
      Value item = items[i];
      if (!(item.type == Value::Type::Object && item.object &&
            item.object->host_type == "Promise")) {
        (*collected)[i] = item;
        --*remaining;
        continue;
      }
      Value on_ok = make_native([runtime, result, collected, remaining, failed, i](Values &args) {
        if (*failed)
          return Value();
        (*collected)[i] = args.empty() ? Value() : args[0];
        if (--*remaining == 0)
          runtime->settle_promise(result.object, 1, runtime->make_array(*collected));
        return Value();
      });
      Value on_err = make_native([runtime, result, failed](Values &args) {
        if (*failed)
          return Value();
        *failed = true;
        runtime->settle_promise(result.object, 2, args.empty() ? Value() : args[0]);
        return Value();
      });
      runtime->promise_then(item, on_ok, on_err);
    }
    if (*remaining == 0)
      settle_promise(result.object, 1, make_array(*collected));
    return result;
  });
  promise_ctor.object->properties["allSettled"] = make_native([this](Values &a) {
    Value result = make_promise();
    Values items;
    if (!a.empty())
      append_spread(items, a[0]);
    auto collected = std::make_shared<Values>(items.size());
    auto remaining = std::make_shared<size_t>(items.size());
    Runtime *runtime = this;
    if (items.empty()) {
      settle_promise(result.object, 1, make_array({}));
      return result;
    }
    for (size_t i = 0; i < items.size(); ++i) {
      Value item = items[i];
      auto finish = make_native([runtime, result, collected, remaining](Values &) {
        if (--*remaining == 0)
          runtime->settle_promise(result.object, 1, runtime->make_array(*collected));
        return Value();
      });
      Value on_ok = make_native([this, collected, finish, i](Values &args) {
        Value entry = make_object();
        entry.object->properties["status"] = Value("fulfilled");
        entry.object->properties["value"] = args.empty() ? Value() : args[0];
        (*collected)[i] = entry;
        Values none;
        return call(finish, none, Value());
      });
      Value on_err = make_native([this, collected, finish, i](Values &args) {
        Value entry = make_object();
        entry.object->properties["status"] = Value("rejected");
        entry.object->properties["reason"] = args.empty() ? Value() : args[0];
        (*collected)[i] = entry;
        Values none;
        return call(finish, none, Value());
      });
      if (item.type == Value::Type::Object && item.object && item.object->host_type == "Promise")
        runtime->promise_then(item, on_ok, on_err);
      else
        runtime->promise_then(([&] {
          Value settled_promise = runtime->make_promise();
          runtime->settle_promise(settled_promise.object, 1, item);
          return settled_promise;
        })(), on_ok, on_err);
    }
    return result;
  });
  promise_ctor.object->properties["race"] = make_native([this](Values &a) {
    Value result = make_promise();
    Values items;
    if (!a.empty())
      append_spread(items, a[0]);
    Runtime *runtime = this;
    for (const auto &item : items) {
      Value on_ok = make_native([runtime, result](Values &args) {
        runtime->settle_promise(result.object, 1, args.empty() ? Value() : args[0]);
        return Value();
      });
      Value on_err = make_native([runtime, result](Values &args) {
        runtime->settle_promise(result.object, 2, args.empty() ? Value() : args[0]);
        return Value();
      });
      if (item.type == Value::Type::Object && item.object && item.object->host_type == "Promise")
        runtime->promise_then(item, on_ok, on_err);
      else
        settle_promise(result.object, 1, item);
    }
    return result;
  });
  g.properties["Promise"] = promise_ctor;
  // I queue a function to run after the current script finishes.
  g.properties["queueMicrotask"] = make_native([this](Values &a) {
    if (a.empty() || !callable(a[0]))
      throw JsError{Value("TypeError: queueMicrotask needs a function")};
    Value callback = a[0];
    enqueue_microtask(
        [this, callback]() {
          Values none;
          call(callback, none, Value());
        },
        {callback});
    return Value();
  });

}
} // namespace pv::js
