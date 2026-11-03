#include "js.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
namespace pv::js {
Value Value::undef() { return {}; }
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
      return "function " +
             (object->function ? object->function->name : std::string()) +
             "() { [PsychoVault code] }";
    if (object->kind == Object::Kind::Native)
      return "function () { [PsychoVault code] }";
    if (object->kind == Object::Kind::Host)
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
    return object && (object->kind == Object::Kind::Function ||
                      object->kind == Object::Kind::Native)
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
    size_t n = std::min(
        size_t(100000),
        size_t(std::max(0.0, object->properties["length"].to_number())));
    for (size_t i = 0; i < n; ++i) {
      auto it = object->properties.find(std::to_string(i));
      j.array.push_back(it == object->properties.end()
                            ? Json()
                            : it->second.to_json(depth + 1));
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
static int32_t int32(double n) { return int32_t(uint32(n)); }
static Value binary(const std::string &op, const Value &a, const Value &b) {
  if (op == "+") {
    if (a.type == Value::Type::String || b.type == Value::Type::String ||
        (a.type == Value::Type::Object && a.object &&
         a.object->kind != Object::Kind::Array) ||
        (b.type == Value::Type::Object && b.object &&
         b.object->kind != Object::Kind::Array))
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
    bool strings =
        a.type == Value::Type::String && b.type == Value::Type::String;
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
size_t Runtime::object_count() const { return objects_.size() - free_.size(); }
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
  if (o && std::find(extra_roots_.begin(), extra_roots_.end(), o) ==
               extra_roots_.end())
    extra_roots_.push_back(o);
}
void Runtime::unroot(Object *o) { std::erase(extra_roots_, o); }
Value Runtime::make_object(Object::Kind kind) { return Value(allocate(kind)); }
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
void Runtime::reset_fuel() { fuel_ = 2000000; }
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
Value Runtime::assign_identifier(Env &env, const std::string &name,
                                 const Value &value) {
  Env *e = find_env(env, name);
  if (e)
    e->properties[name] = value;
  else
    global_->properties[name] = value;
  return value;
}
Value Runtime::get_member(const Value &base, const std::string &key) {
  if (base.type == Value::Type::Undefined || base.type == Value::Type::Null)
    throw JsError{Value("TypeError: cannot read property " + key)};
  if (base.type == Value::Type::String) {
    if (key == "length")
      return Value(double(utf8_decode(base.string).size()));
    Runtime &r = *this;
    std::string s = base.string;
    if (key == "toUpperCase" || key == "toLowerCase")
      return make_native([&r, s, key](Values &) {
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
    if (key == "charAt" || key == "at" || key == "codePointAt" ||
        key == "charCodeAt")
      return make_native([s, key](Values &a) {
        auto cps = utf8_decode(s);
        int i = a.empty() ? 0 : int(a[0].to_number());
        if (i < 0)
          i += int(cps.size());
        if (i < 0 || i >= int(cps.size()))
          return key == "charAt" ? Value("")
                 : key == "at"
                     ? Value()
                     : Value(std::numeric_limits<double>::quiet_NaN());
        return key == "charAt" || key == "at" ? Value(utf8_encode(cps[i]))
                                              : Value(double(cps[i]));
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
      return make_native([&r, s](Values &a) {
        Values out;
        if (a.empty() || a[0].type == Value::Type::Undefined)
          return r.make_array({Value(s)});
        std::string sep = a[0].to_string();
        if (sep.empty()) {
          for (auto c : utf8_decode(s))
            out.emplace_back(utf8_encode(c));
        } else {
          size_t pos = 0;
          while (true) {
            size_t n = s.find(sep, pos);
            out.emplace_back(
                s.substr(pos, n == std::string::npos ? n : n - pos));
            if (n == std::string::npos)
              break;
            pos = n + sep.size();
            if (out.size() > 100000)
              throw std::runtime_error("string split limit");
          }
        }
        return r.make_array(out);
      });
    if (key == "indexOf" || key == "lastIndexOf" || key == "includes" ||
        key == "startsWith" || key == "endsWith")
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
      return make_native([s, key](Values &a) {
        std::string from = a.empty() ? "undefined" : a[0].to_string(),
                    to = a.size() > 1 ? a[1].to_string() : "undefined";
        if (from.empty())
          return Value(to + s);
        size_t p = s.find(from);
        if (p == std::string::npos)
          return Value(s);
        std::string out = s;
        do {
          out.replace(p, from.size(), to);
          p = key == "replaceAll" ? out.find(from, p + to.size())
                                  : std::string::npos;
        } while (p != std::string::npos);
        return Value(out);
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
  if (o->kind == Object::Kind::Function && key == "name")
    return Value(o->function ? o->function->name : "");
  if (o->kind == Object::Kind::Array) {
    Runtime &r = *this;
    Value arr = base;
    if (key == "push")
      return make_native([&r, arr](Values &a) {
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
          if (strict_equals(it == arr.object->properties.end() ? Value()
                                                               : it->second,
                            needle))
            return key == "includes" ? Value(true) : Value(double(i));
        }
        return key == "includes" ? Value(false) : Value(-1.0);
      });
    if (key == "slice" || key == "concat")
      return make_native([&r, arr, key](Values &a) {
        Values out;
        size_t n = size_t(arr.object->properties["length"].to_number());
        int start = a.empty() || key == "concat" ? 0 : int(a[0].to_number()),
            end =
                a.size() > 1 && key == "slice" ? int(a[1].to_number()) : int(n);
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
      return make_native([&r, arr, key](Values &a) {
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
    if (key == "forEach" || key == "map" || key == "filter" || key == "find" ||
        key == "some" || key == "every" || key == "reduce")
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
        std::stable_sort(values.begin(), values.end(),
                         [&](const Value &x, const Value &y) {
                           if (fn.truthy())
                             return r.call(fn, {x, y}).to_number() < 0;
                           return x.to_string() < y.to_string();
                         });
        for (size_t i = 0; i < n; ++i)
          p[std::to_string(i)] = values[i];
        return arr;
      });
  }
  return Value();
}
bool Runtime::set_member(const Value &base, const std::string &key,
                         const Value &value) {
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
    if (end && !*end && i < 100000 &&
        i + 1 > o->properties["length"].to_number())
      o->properties["length"] = Value(double(i + 1));
  }
  return true;
}
Value Runtime::call(Value fn, const Values &args, Value this_value) {
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
      for (size_t i = 0; i < f->params.size(); ++i)
        env->properties[f->params[i]] = i < args.size() ? args[i] : Value();
      env->properties["arguments"] = make_array(args);
      Value saved = this_;
      this_ = this_value;
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
Value Runtime::construct(const Value &constructor, Values &args) {
  if (constructor.type != Value::Type::Object || !constructor.object)
    throw JsError{Value("TypeError: invalid constructor")};
  if (constructor.object->kind == Object::Kind::Native)
    return constructor.object->native(args);
  Value object = make_object();
  Value result = call(constructor, args, object);
  return result.type == Value::Type::Object ? result : object;
}
void Runtime::hoist(const std::vector<std::unique_ptr<Stmt>> &statements,
                    Env &env) {
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
Runtime::Flow
Runtime::execute_block(const std::vector<std::unique_ptr<Stmt>> &statements,
                       Env &env, Value this_value) {
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
    if (iterable.type == Value::Type::Object && iterable.object) {
      if (iterable.object->kind == Object::Kind::Array) {
        size_t n = size_t(iterable.object->properties["length"].to_number());
        for (size_t i = 0; i < n; ++i)
          keys.push_back(s.text == "of"
                             ? iterable.object->properties[std::to_string(i)]
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
    return {Flow::Kind::Return,
            s.expression ? evaluate(*s.expression, env, this_value) : Value()};
  case Stmt::Kind::Break:
    return {Flow::Kind::Break, {}};
  case Stmt::Kind::Continue:
    return {Flow::Kind::Continue, {}};
  case Stmt::Kind::Throw:
    throw JsError{s.expression ? evaluate(*s.expression, env, this_value)
                               : Value()};
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
    for (auto &item : e.items)
      vs.push_back(evaluate(*item, env, this_value));
    return make_array(vs);
  }
  case Expr::Kind::Object: {
    Value o = make_object();
    for (auto &p : e.properties)
      o.object->properties[p.first] = evaluate(*p.second, env, this_value);
    return o;
  }
  case Expr::Kind::Function: {
    Value fn = make_object(Object::Kind::Function);
    fn.object->function = e.function;
    fn.object->closure = &env;
    return fn;
  }
  case Expr::Kind::Member: {
    Value base = evaluate(*e.left, env, this_value);
    std::string key =
        e.computed ? evaluate(*e.right, env, this_value).to_string() : e.text;
    return get_member(base, key);
  }
  case Expr::Kind::Unary: {
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
  case Expr::Kind::Binary: {
    Value a = evaluate(*e.left, env, this_value),
          b = evaluate(*e.right, env, this_value);
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
        old = get_member(
            evaluate(*e.left->left, env, this_value),
            e.left->computed
                ? evaluate(*e.left->right, env, this_value).to_string()
                : e.left->text);
      v = binary(e.text, old, v);
    }
    if (e.left->kind == Expr::Kind::Identifier)
      assign_identifier(env, e.left->text, v);
    else if (e.left->kind == Expr::Kind::Member) {
      Value base = evaluate(*e.left->left, env, this_value);
      set_member(base,
                 e.left->computed
                     ? evaluate(*e.left->right, env, this_value).to_string()
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
      old =
          get_member(evaluate(*e.left->left, env, this_value),
                     e.left->computed
                         ? evaluate(*e.left->right, env, this_value).to_string()
                         : e.left->text);
    Value next = binary(e.text == "++" ? "+" : "-", old, Value(1));
    if (e.left->kind == Expr::Kind::Identifier)
      assign_identifier(env, e.left->text, next);
    else
      set_member(evaluate(*e.left->left, env, this_value),
                 e.left->computed
                     ? evaluate(*e.left->right, env, this_value).to_string()
                     : e.left->text,
                 next);
    return e.prefix ? next : Value(old.to_number());
  }
  case Expr::Kind::Conditional:
    return evaluate(*e.left, env, this_value).truthy()
               ? evaluate(*e.right, env, this_value)
               : evaluate(*e.third, env, this_value);
  case Expr::Kind::Call: {
    Value fn, this_value_call;
    Values args;
    if (e.left->kind == Expr::Kind::Member) {
      Value base = evaluate(*e.left->left, env, this_value);
      std::string key =
          e.left->computed
              ? evaluate(*e.left->right, env, this_value).to_string()
              : e.left->text;
      fn = get_member(base, key);
      this_value_call = base;
    } else
      fn = evaluate(*e.left, env, this_value);
    for (auto &a : e.items)
      args.push_back(evaluate(*a, env, this_value));
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
    collect();
    throw;
  }
  env_stack_.pop_back();
  collect();
  return last;
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
  nav.object->properties["userAgent"] =
      Value("PsychoVault/0.1 (owned engine; development)");
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
  for (std::string name : {"floor", "ceil", "round", "trunc", "abs", "sqrt",
                           "sin", "cos", "tan", "log", "exp"})
    math.object->properties[name] = make_native([name](Values &a) {
      double x = a.empty() ? std::numeric_limits<double>::quiet_NaN()
                           : a[0].to_number();
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
    return Value(std::pow(a.size() > 0 ? a[0].to_number() : 0,
                          a.size() > 1 ? a[1].to_number() : 0));
  });
  math.object->properties["random"] =
      make_native([](Values &) { return Value(0.5); });
  g.properties["Math"] = math;
  math.object->properties["random"] = make_native([](Values &) -> Value {
    auto bytes = random_bytes(8);
    uint64_t bits = 0;
    for (auto b : bytes)
      bits = (bits << 8) | b;
    return Value(double(bits >> 11) / 9007199254740992.0);
  });
  g.properties["String"] = make_native(
      [](Values &a) { return Value(a.empty() ? "" : a[0].to_string()); });
  g.properties["Number"] = make_native(
      [](Values &a) { return Value(a.empty() ? 0 : a[0].to_number()); });
  g.properties["Boolean"] =
      make_native([](Values &a) { return Value(!a.empty() && a[0].truthy()); });
  g.properties["parseInt"] = make_native([](Values &a) {
    std::string s = trim(a.empty() ? "" : a[0].to_string());
    int base = a.size() > 1 ? int(a[1].to_number()) : starts(s, "0x") ? 16 : 10;
    char *end = nullptr;
    double n = double(std::strtoll(s.c_str(), &end, base));
    return Value(end == s.c_str() ? std::numeric_limits<double>::quiet_NaN()
                                  : n);
  });
  g.properties["parseFloat"] = make_native([](Values &a) {
    std::string s = trim(a.empty() ? "" : a[0].to_string());
    char *end = nullptr;
    double n = std::strtod(s.c_str(), &end);
    return Value(end == s.c_str() ? std::numeric_limits<double>::quiet_NaN()
                                  : n);
  });
  g.properties["isNaN"] = make_native([](Values &a) {
    return Value(std::isnan(a.empty() ? std::numeric_limits<double>::quiet_NaN()
                                      : a[0].to_number()));
  });
  g.properties["Array"] = make_native([this](Values &a) {
    if (a.size() == 1 && a[0].type == Value::Type::Number) {
      size_t n = size_t(a[0].to_number());
      if (n > 100000)
        throw std::runtime_error("array limit");
      return make_array(Values(n));
    }
    return make_array(a);
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
  json.object->properties["stringify"] = make_native([](Values &a) {
    return a.empty() ? Value() : Value(a[0].to_json().dump());
  });
  json.object->properties["parse"] = make_native([this](Values &a) {
    auto parsed =
        Json::parse(a.empty() ? "" : a[0].to_string(), 2u * 1024u * 1024u);
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
      e.object->properties["message"] =
          Value(a.empty() ? "" : a[0].to_string());
      return e;
    });
  g.properties["setTimeout"] = make_native([this](Values &a) {
    if (a.empty() || a[0].type_of() != "function")
      throw JsError{Value("TypeError: timer callback required")};
    double delay =
        std::clamp(a.size() > 1 ? a[1].to_number() : 0, 0.0, 86400000.0);
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
}
} // namespace pv::js
