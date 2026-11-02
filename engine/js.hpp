#pragma once
#include "base.hpp"
namespace pv::js {
struct Object;
struct Env;
struct Stmt;
struct Expr;
struct Function;
struct Value {
  enum class Type { Undefined, Null, Boolean, Number, String, Object };
  Type type = Type::Undefined;
  bool boolean = false;
  double number = 0;
  std::string string;
  Object *object = nullptr;
  Value() = default;
  static Value undef();
  static Value null();
  Value(bool v) : type(Type::Boolean), boolean(v) {}
  Value(double v) : type(Type::Number), number(v) {}
  Value(int v) : type(Type::Number), number(v) {}
  Value(std::string v) : type(Type::String), string(std::move(v)) {}
  Value(const char *v) : type(Type::String), string(v) {}
  Value(Object *v) : type(Type::Object), object(v) {}
  bool truthy() const;
  double to_number() const;
  std::string to_string() const;
  std::string type_of() const;
  Json to_json(int depth = 0) const;
};
using Values = std::vector<Value>;
struct Object {
  enum class Kind { Plain, Array, Function, Native, Environment, Host };
  Kind kind = Kind::Plain;
  std::map<std::string, Value> properties;
  Object *prototype = nullptr;
  Object *parent = nullptr;
  Function *function = nullptr;
  Env *closure = nullptr;
  std::function<Value(Values &)> native;
  int host = -2;
  std::string host_type;
  bool marked = false;
};
struct Env : Object {
  explicit Env(Env *outer = nullptr) {
    kind = Kind::Environment;
    parent = outer;
  }
};
struct Token {
  std::string text;
  int line = 1;
  bool newline = false;
};
struct Expr {
  enum class Kind {
    Literal,
    Identifier,
    This,
    Array,
    Object,
    Unary,
    Update,
    Binary,
    Logical,
    Assign,
    Conditional,
    Call,
    Member,
    New,
    Function
  };
  Kind kind;
  Value literal;
  std::string text;
  bool computed = false, prefix = false;
  std::unique_ptr<Expr> left, right, third;
  std::vector<std::unique_ptr<Expr>> items;
  std::vector<std::pair<std::string, std::unique_ptr<Expr>>> properties;
  Function *function = nullptr;
  int line = 1;
  explicit Expr(Kind k) : kind(k) {}
};
struct Stmt {
  enum class Kind {
    Expression,
    Variables,
    Block,
    If,
    While,
    Do,
    For,
    ForIn,
    Return,
    Break,
    Continue,
    Throw,
    Try,
    Function,
    Empty
  };
  Kind kind;
  std::string text, name;
  std::unique_ptr<Expr> expression, condition, increment;
  std::vector<std::pair<std::string, std::unique_ptr<Expr>>> declarations;
  std::vector<std::unique_ptr<Stmt>> body, alternate, final;
  Function *function = nullptr;
  int line = 1;
  explicit Stmt(Kind k) : kind(k) {}
};
struct Function {
  std::vector<std::string> params;
  std::vector<std::unique_ptr<Stmt>> body;
  std::unique_ptr<Expr> expression;
  std::string name;
};
struct Program {
  std::vector<std::unique_ptr<Stmt>> statements;
  std::vector<std::unique_ptr<Function>> functions;
};
Program parse(std::string_view source);
struct JsError {
  Value value;
};
class Host {
public:
  virtual ~Host() = default;
  virtual Value get(int id, const std::string &name) = 0;
  virtual bool set(int id, const std::string &name, const Value &value) = 0;
  virtual Value call(int id, const std::string &name, Values &args) = 0;
  virtual void log(const std::string &text) = 0;
};
class Runtime {
public:
  explicit Runtime(Host *host = nullptr);
  ~Runtime();
  Value run(std::string_view source);
  Value call(Value function, const Values &args, Value this_value = Value());
  Value evaluate(Expr &expr, Env &env, Value this_value);
  Value get_member(const Value &base, const std::string &key);
  bool set_member(const Value &base, const std::string &key,
                  const Value &value);
  Value make_object(Object::Kind kind = Object::Kind::Plain);
  Value make_array(const Values &values = {});
  Value make_native(std::function<Value(Values &)> fn);
  Value host_value(int id, const std::string &type = "node");
  Object *allocate(Object::Kind kind);
  Env &global() { return *global_; }
  void collect();
  void root(Object *object);
  void unroot(Object *object);
  Host *host() { return host_; }
  void reset_fuel();
  void tick();
  struct Timer {
    double id;
    double due;
    Value callback;
    Values args;
    bool interval;
  };
  std::vector<Timer> timers;
  double next_timer = 1;
  size_t object_count() const;

private:
  struct Flow {
    enum class Kind { None, Return, Break, Continue } kind = Kind::None;
    Value value;
  };
  Host *host_;
  std::vector<std::unique_ptr<Program>> programs_;
  size_t program_bytes_ = 0;
  std::vector<std::unique_ptr<Object>> objects_;
  std::vector<Object *> free_;
  Env *global_ = nullptr;
  std::vector<Env *> env_stack_;
  std::vector<Object *> extra_roots_;
  std::unordered_map<int, Object *> hosts_;
  Value this_ = Value();
  uint64_t fuel_ = 0;
  int depth_ = 0;
  size_t allocated_since_gc_ = 0;
  Value last_statement_;
  void install_builtins();
  void hoist(const std::vector<std::unique_ptr<Stmt>> &statements, Env &env);
  Flow execute(const Stmt &statement, Env &env, Value this_value);
  Flow execute_block(const std::vector<std::unique_ptr<Stmt>> &statements,
                     Env &env, Value this_value);
  Env *find_env(Env &env, const std::string &name);
  Value declare(Env &env, const std::string &name, const Value &value);
  Value assign_identifier(Env &env, const std::string &name,
                          const Value &value);
  Value construct(const Value &constructor, Values &args);
  void mark(Object *object);
};
bool strict_equals(const Value &a, const Value &b);
std::string number_string(double n);
} // namespace pv::js
