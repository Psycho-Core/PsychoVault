#pragma once
#include "Psycho_base.hpp"
#include <deque>
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
// A promise keeps the handlers waiting on it and the promise each handler
// settles when it finishes.
struct PromiseReaction {
  Value on_fulfilled, on_rejected, child;
};
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
  // A typed array and its buffer are real bytes, not a list of numbers, so a
  // media segment costs what it is instead of sixty times that.
  std::shared_ptr<std::vector<uint8_t>> bytes;
  size_t byte_offset = 0, length = 0;
  // An arrow function keeps the this value from where it was created.
  bool has_lexical_this = false;
  Value lexical_this;
  // Accessors run instead of returning a stored value.
  Object *getter = nullptr;
  Object *setter = nullptr;
  // A class constructor keeps its declaration so new can assign fields.
  const Expr *class_ast = nullptr;
  // I keep a promise's state, its settled value and everything waiting on it
  // here, so a promise is an ordinary object to the rest of my engine.
  int promise_state = 0; // 0 pending, 1 fulfilled, 2 rejected
  Value promise_result;
  bool promise_handled = false;
  std::vector<PromiseReaction> reactions;
  // An object may own engine state, such as a compiled pattern.
  std::shared_ptr<void> internal;
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
  // Only a pattern literal uses this.
  std::string flags;
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
    // A comma expression evaluates each part and answers the last one.
    Sequence,
    Call,
    Member,
    New,
    Function,
    Template,
    Class,
    Regex,
    Await
  };
  Kind kind;
  Value literal;
  std::string text;
  bool computed = false, prefix = false, optional = false;
  std::unique_ptr<Expr> left, right, third;
  std::vector<std::unique_ptr<Expr>> items;
  std::vector<std::pair<std::string, std::unique_ptr<Expr>>> properties;
  // Computed keys are evaluated when the object is created.
  std::vector<std::pair<std::unique_ptr<Expr>, std::unique_ptr<Expr>>> computed_properties;
  // Class fields are assigned to every new instance.
  std::vector<std::pair<std::string, std::unique_ptr<Expr>>> fields;
  Function *function = nullptr;
  // A pattern literal carries its flags beside its source text.
  std::string flags;
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
    Switch,
    Empty
  };
  Kind kind;
  std::string text, name;
  std::unique_ptr<Expr> expression, condition, increment;
  std::vector<std::pair<std::string, std::unique_ptr<Expr>>> declarations;
  std::vector<std::unique_ptr<Stmt>> body, alternate, final;
  // A switch case: a test, or nothing for default, and the statements under it.
  struct Case {
    std::unique_ptr<Expr> test;
    std::vector<std::unique_ptr<Stmt>> body;
  };
  std::vector<Case> cases;
  Function *function = nullptr;
  int line = 1;
  explicit Stmt(Kind k) : kind(k) {}
};
struct Function {
  bool arrow = false;
  // An async function runs its body straight away but hands back a promise.
  bool async = false;
  std::vector<std::string> params;
  std::vector<std::unique_ptr<Stmt>> defaults;
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
  bool set_member(const Value &base, const std::string &key, const Value &value);
  Value make_object(Object::Kind kind = Object::Kind::Plain);
  Value make_array(const Values &values = {});
  Value make_native(std::function<Value(Values &)> fn);
  Value host_value(int id, const std::string &type = "node");
  // I compile a pattern here so a literal and a constructor behave the same.
  Value make_regex(const std::string &pattern, const std::string &flags);
  Value make_promise();
  void settle_promise(Object *promise, int state, const Value &value);
  Value promise_then(const Value &promise, const Value &on_fulfilled, const Value &on_rejected);
  // I walk whatever a value can be iterated as: text by code point, arrays by
  // index, my Map and Set objects in insertion order, and anything else that
  // supplies its own "@@iterator" method.
  bool iterate(const Value &value, Values &out, size_t limit = 100000);
  // I wait for a promise the same way I would in a browser: keep running
  // queued work and due timers until it settles or my deadline passes.
  Value await_value(const Value &value);
  // My page installs this so awaiting still lets timers fire while I wait.
  void set_pump(std::function<bool()> pump) {
    pump_ = std::move(pump);
  }
  // I run queued promise work. My page calls this after every script and timer.
  void drain_microtasks(size_t limit = 200000);
  bool has_microtasks() const {
    return !microtasks_.empty();
  }
  Object *allocate(Object::Kind kind);
  Env &global() {
    return *global_;
  }
  void collect();
  void root(Object *object);
  void unroot(Object *object);
  Host *host() {
    return host_;
  }
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
  Value call_sync(Value fn, const Values &args, Value this_value);
  // Where my C++ stack was when this runtime started. A recursive script cannot be
  // allowed to run my own stack out, and counting JavaScript frames is not enough
  // when the compiler chooses how much stack a frame takes.
  const char *stack_base_ = nullptr;
  // Turns a thrown message into the error object a page catches.
  Value error_object(const Value &thrown);
  Host *host_;
  std::function<bool()> pump_;
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
  struct Microtask {
    std::function<void()> run;
    // I mark these values while the task is queued, so a collection cannot free
    // a promise that is still waiting its turn.
    std::vector<Value> roots;
  };
  std::deque<Microtask> microtasks_;
  int symbol_counter_ = 0;
  Object *regex_prototype_ = nullptr;
  Object *promise_prototype_ = nullptr;
  void enqueue_microtask(std::function<void()> run, std::vector<Value> roots);
  // Patterns: one entry point each for the string methods that take one.
  Value regex_exec(const Value &self, const Value &input);
  Value string_match(const std::string &subject, const Value &pattern);
  Value string_search(const std::string &subject, const Value &pattern);
  Value string_split(const std::string &subject, const Value &separator, double limit);
  Value string_replace(const std::string &subject, const Value &pattern, const Value &replacement,
                       bool all);
  Value settle_reaction(Object *promise, const PromiseReaction &reaction, int state,
                        const Value &value);
  void install_builtins();
  void hoist(const std::vector<std::unique_ptr<Stmt>> &statements, Env &env);
  Flow execute(const Stmt &statement, Env &env, Value this_value);
  Flow execute_block(const std::vector<std::unique_ptr<Stmt>> &statements, Env &env,
                     Value this_value);
  Env *find_env(Env &env, const std::string &name);
  Value declare(Env &env, const std::string &name, const Value &value);
  Value assign_identifier(Env &env, const std::string &name, const Value &value);
  Value construct(const Value &constructor, Values &args);
  void init_instance(const Value &ctor, Value &object, Values &args);
  Value run_init(const Value &ctor, Value &object, Values &args);
  void mark(Object *object);
};
bool strict_equals(const Value &a, const Value &b);
std::string number_string(double n);
} // namespace pv::js
