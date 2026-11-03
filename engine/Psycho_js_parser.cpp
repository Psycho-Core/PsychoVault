#include "Psycho_js.hpp"
#include <cstdlib>
namespace pv::js {
static bool ident_start(char c) {
  return std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '$' ||
         static_cast<unsigned char>(c) >= 128;
}
static bool ident_part(char c) {
  return ident_start(c) || std::isdigit(static_cast<unsigned char>(c));
}
static std::vector<Token> lex(std::string_view s) {
  if (s.size() > 2u * 1024u * 1024u)
    throw std::runtime_error("JavaScript source limit");
  std::vector<Token> out;
  size_t p = 0;
  int line = 1;
  bool newline = false;
  while (p < s.size()) {
    char c = s[p];
    if (c == '\n') {
      ++line;
      ++p;
      newline = true;
      continue;
    }
    if (std::isspace(static_cast<unsigned char>(c))) {
      ++p;
      continue;
    }
    if (s.substr(p, 2) == "//") {
      p += 2;
      while (p < s.size() && s[p] != '\n')
        ++p;
      continue;
    }
    if (s.substr(p, 2) == "/*") {
      p += 2;
      bool closed = false;
      while (p + 1 < s.size()) {
        if (s[p] == '\n')
          ++line;
        if (s.substr(p, 2) == "*/") {
          p += 2;
          closed = true;
          break;
        }
        ++p;
      }
      if (!closed)
        throw std::runtime_error("unterminated JavaScript comment");
      newline = true;
      continue;
    }
    Token t;
    t.line = line;
    t.newline = newline;
    newline = false;
    if (c == '"' || c == '\'' || c == '`') {
      char quote = c;
      ++p;
      std::string value;
      while (p < s.size() && s[p] != quote) {
        // Inside a template I copy an interpolation verbatim, including any
        // nested template, so the inner backticks do not end the outer one.
        if (quote == '`' && s[p] == '$' && p + 1 < s.size() && s[p + 1] == '{') {
          size_t start = p;
          p += 2;
          int braces = 1;
          char inner = 0;
          while (p < s.size() && braces > 0) {
            char t = s[p];
            if (inner) {
              if (t == '\\' && p + 1 < s.size()) p += 2;
              else { if (t == inner) inner = 0; ++p; }
              continue;
            }
            if (t == '\'' || t == '"' || t == '`') { inner = t; ++p; continue; }
            if (t == '{') ++braces;
            if (t == '}') --braces;
            ++p;
          }
          if (braces != 0)
            throw std::runtime_error("unterminated template interpolation");
          value += s.substr(start, p - start);
          continue;
        }
        c = s[p++];
        if (c == '\n' && quote != '`')
          throw std::runtime_error("unterminated JavaScript string");
        if (c == '\\' && p < s.size()) {
          c = s[p++];
          switch (c) {
          case 'n':
            c = '\n';
            break;
          case 'r':
            c = '\r';
            break;
          case 't':
            c = '\t';
            break;
          case 'b':
            c = '\b';
            break;
          case 'f':
            c = '\f';
            break;
          case '0':
            c = '\0';
            break;
          case '\n':
            continue;
          case 'u': {
            if (p < s.size() && s[p] == '{') {
              size_t e = s.find('}', p);
              if (e == std::string::npos || e - p > 8)
                throw std::runtime_error("invalid Unicode escape");
              uint32_t cp = std::stoul(std::string(s.substr(p + 1, e - p - 1)), nullptr, 16);
              value += utf8_encode(cp);
              p = e + 1;
              continue;
            }
            if (p + 4 > s.size())
              throw std::runtime_error("invalid Unicode escape");
            uint32_t cp = std::stoul(std::string(s.substr(p, 4)), nullptr, 16);
            p += 4;
            value += utf8_encode(cp);
            continue;
          }
          case 'x': {
            if (p + 2 > s.size())
              throw std::runtime_error("invalid hex escape");
            value += char(std::stoul(std::string(s.substr(p, 2)), nullptr, 16));
            p += 2;
            continue;
          }
          }
        }
        if (c == '\n')
          ++line;
        value += c;
      }
      if (p >= s.size())
        throw std::runtime_error("unterminated JavaScript string");
      ++p;
      if (quote == '`') {
        // I keep the raw template text and parse its interpolations later, so a
        // ${...} can contain any expression including nested strings.
        t.text = "template:" + value;
      } else
        t.text = "string:" + value;
    } else if (std::isdigit(static_cast<unsigned char>(c)) ||
               (c == '.' && p + 1 < s.size() &&
                std::isdigit(static_cast<unsigned char>(s[p + 1])))) {
      size_t a = p;
      if (c == '0' && p + 1 < s.size() && (s[p + 1] == 'x' || s[p + 1] == 'X')) {
        p += 2;
        a = p;
        while (p < s.size() && std::isxdigit(static_cast<unsigned char>(s[p])))
          ++p;
        t.text =
            "number:" + std::to_string(std::stoull(std::string(s.substr(a, p - a)), nullptr, 16));
      } else {
        while (p < s.size() && (std::isdigit(static_cast<unsigned char>(s[p])) || s[p] == '.'))
          ++p;
        if (p < s.size() && (s[p] == 'e' || s[p] == 'E')) {
          ++p;
          if (p < s.size() && (s[p] == '+' || s[p] == '-'))
            ++p;
          while (p < s.size() && std::isdigit(static_cast<unsigned char>(s[p])))
            ++p;
        }
        t.text = "number:" + std::string(s.substr(a, p - a));
      }
    } else if (ident_start(c)) {
      size_t a = p;
      while (p < s.size() && ident_part(s[p]))
        ++p;
      t.text = std::string(s.substr(a, p - a));
    } else {
      static const std::vector<std::string> ops = {">>>=",
                                                   "===",
                                                   "!==",
                                                   "**=",
                                                   "<<=",
                                                   ">>=",
                                                   ">>>",
                                                   "&&=",
                                                   "||=",
                                                   "?\?=",
                                                   "=>",
                                                   "==",
                                                   "!=",
                                                   "<=",
                                                   ">=",
                                                   "&&",
                                                   "||",
                                                   "??",
                                                   "++",
                                                   "--",
                                                   "+=",
                                                   "-=",
                                                   "*=",
                                                   "/=",
                                                   "%=",
                                                   "&=",
                                                   "|=",
                                                   "^=",
                                                   "<<",
                                                   ">>",
                                                   "**",
                                                   "?.",
                                                   "..."};
      bool found = false;
      for (auto &op : ops)
        if (s.substr(p, op.size()) == op) {
          t.text = op;
          p += op.size();
          found = true;
          break;
        }
      if (!found) {
        t.text = std::string(1, c);
        ++p;
      }
    }
    out.push_back(std::move(t));
    if (out.size() > 150000)
      throw std::runtime_error("JavaScript token limit");
  }
  out.push_back({"<eof>", line, true});
  return out;
}
class Parser {
public:
  Program program;
  std::vector<Token> tokens;
  size_t p = 0;
  int depth = 0;
  int temp_counter = 0;
  explicit Parser(std::string_view source) : tokens(lex(source)) {}
  const Token &tok(size_t offset = 0) const {
    return tokens[std::min(p + offset, tokens.size() - 1)];
  }
  bool is(std::string_view s, size_t offset = 0) const {
    return tok(offset).text == s;
  }
  bool take(std::string_view s) {
    if (!is(s))
      return false;
    ++p;
    return true;
  }
  void expect(std::string_view s) {
    if (!take(s))
      error("expected " + std::string(s));
  }
  [[noreturn]] void error(const std::string &message) const {
    throw std::runtime_error("JavaScript line " + std::to_string(tok().line) + ": " + message +
                             " near " + tok().text);
  }
  std::string identifier() {
    auto s = tok().text;
    if (s.empty() || !ident_start(s[0]) || keyword(s))
      error("identifier expected");
    ++p;
    return s;
  }
  bool keyword(const std::string &s) const {
    static const std::set<std::string> k = {
        "var",      "let",     "const",    "if",     "else",      "while",  "for",       "do",
        "return",   "break",   "continue", "true",   "false",     "null",   "undefined", "new",
        "function", "throw",   "try",      "catch",  "finally",   "switch", "case",      "default",
        "class",    "extends", "import",   "export", "async",     "await",  "yield",     "delete",
        "void",     "typeof",  "in",       "of",     "instanceof"};
    return k.count(s);
  }
  void semicolon() {
    if (take(";"))
      return;
    if (is("}") || is("<eof>") || tok().newline)
      return;
    error("semicolon expected");
  }
  std::unique_ptr<Expr> expr(int min = 0) {
    if (++depth > 160)
      error("expression nesting limit");
    auto left = prefix();
    while (true) {
      std::string op = tok().text;
      int prec = precedence(op);
      if (prec < min || prec == 0)
        break;
      ++p;
      auto node = make(Expr::Kind::Assign);
      node->text = op;
      node->left = std::move(left);
      if (op == "&&" || op == "||" || op == "??")
        node->kind = Expr::Kind::Logical;
      else if (op == "=" || compound_assignment(op)) {
        // Only a real compound assignment writes to its left operand. Testing
        // "ends with =" here would turn ===, ==, <= and >= into assignments.
        node->kind = Expr::Kind::Assign;
        if (op != "=")
          node->text = op.substr(0, op.size() - 1);
      } else
        node->kind = Expr::Kind::Binary;
      node->right = expr(prec + (prec == 1 || op == "**" ? 0 : 1));
      left = std::move(node);
    }
    if (min == 0 && take("?")) {
      auto node = make(Expr::Kind::Conditional);
      node->left = std::move(left);
      node->right = expr();
      expect(":");
      node->third = expr();
      left = std::move(node);
    }
    --depth;
    return left;
  }
  static bool compound_assignment(const std::string &op) {
    static const std::set<std::string> forms = {"+=",  "-=",  "*=",  "/=",  "%=",  "**=",
                                              "<<=", ">>=", ">>>=", "&=", "|=",  "^=",
                                              "&&=", "||=", "?\?="};
    return forms.count(op) != 0;
  }
  int precedence(const std::string &s) const {
    if (s == "=" || s == "+=" || s == "-=" || s == "*=" || s == "/=" || s == "%=" || s == "**=" ||
        s == "<<=" || s == ">>=" || s == ">>>=" || s == "&=" || s == "|=" || s == "^=" ||
        s == "&&=" || s == "||=" || s == "?\?=")
      return 1;
    if (s == "||" || s == "??")
      return 2;
    if (s == "&&")
      return 3;
    if (s == "|" || s == "^" || s == "&")
      return 4;
    if (s == "==" || s == "!=" || s == "===" || s == "!==")
      return 5;
    if (s == "<" || s == ">" || s == "<=" || s == ">=" || s == "instanceof" || s == "in")
      return 6;
    if (s == "<<" || s == ">>" || s == ">>>")
      return 7;
    if (s == "+" || s == "-")
      return 8;
    if (s == "*" || s == "/" || s == "%")
      return 9;
    if (s == "**")
      return 10;
    return 0;
  }
  std::unique_ptr<Expr> make(Expr::Kind kind) {
    auto e = std::make_unique<Expr>(kind);
    e->line = tok().line;
    return e;
  }
  std::unique_ptr<Stmt> statement_node(Stmt::Kind kind) {
    auto s = std::make_unique<Stmt>(kind);
    s->line = tok().line;
    return s;
  }
  Function *function_node() {
    program.functions.push_back(std::make_unique<Function>());
    return program.functions.back().get();
  }
  // I turn `a${b}c` into alternating literal and expression parts.
  std::unique_ptr<Expr> template_literal(const std::string &raw) {
    auto node = make(Expr::Kind::Template);
    std::string literal;
    size_t i = 0;
    int templates = 0;
    while (i < raw.size()) {
      if (raw[i] == '$' && i + 1 < raw.size() && raw[i + 1] == '{') {
        auto head = make(Expr::Kind::Literal);
        head->literal = Value(literal);
        literal.clear();
        node->items.push_back(std::move(head));
        i += 2;
        // I balance braces and skip quoted text so a } inside a string does not
        // end my interpolation early.
        std::string source;
        int braces = 1;
        while (i < raw.size() && braces > 0) {
          char c = raw[i];
          if (c == '\\' && i + 1 < raw.size()) {
            source += c;
            source += raw[i + 1];
            i += 2;
            continue;
          }
          if (c == '\'' || c == '"' || c == '`') {
            char quote = c;
            source += c;
            ++i;
            while (i < raw.size() && raw[i] != quote) {
              if (raw[i] == '\\' && i + 1 < raw.size()) {
                source += raw[i];
                ++i;
              }
              source += raw[i++];
            }
            if (i < raw.size()) source += raw[i++];
            continue;
          }
          if (c == '{') ++braces;
          if (c == '}') {
            --braces;
            if (!braces) { ++i; break; }
          }
          source += c;
          ++i;
        }
        if (braces)
          error("unterminated template interpolation");
        if (++templates > 256)
          error("too many template interpolations");
        Parser inner(source);
        auto expression = inner.expr();
        node->items.push_back(std::move(expression));
        continue;
      }
      literal += raw[i++];
    }
    auto tail = make(Expr::Kind::Literal);
    tail->literal = Value(literal);
    node->items.push_back(std::move(tail));
    return node;
  }
  std::unique_ptr<Expr> prefix(bool allow_call = true) {
    if (is("class"))
      return postfix(class_expression(), allow_call);
    // A bare-identifier arrow: x => x * 2
    if (tok(1).text == "=>" && !keyword(tok().text) && ident_start(tok().text.empty() ? ' ' : tok().text[0])) {
      auto fn = function_node();
      fn->arrow = true;
      fn->params.push_back(identifier());
      expect("=>");
      if (take("{"))
        fn->body = block();
      else
        fn->expression = expr();
      auto e = make(Expr::Kind::Function);
      e->function = fn;
      return e;
    }
    if (starts(tok().text, "template:")) {
      auto raw = tok().text.substr(9);
      auto e = template_literal(raw);
      ++p;
      return postfix(std::move(e), allow_call);
    }
    if (is("string") || starts(tok().text, "string:")) {
      auto e = make(Expr::Kind::Literal);
      e->literal = Value(tok().text.substr(7));
      ++p;
      return postfix(std::move(e), allow_call);
    }
    if (starts(tok().text, "number:")) {
      auto e = make(Expr::Kind::Literal);
      e->literal = Value(std::strtod(tok().text.c_str() + 7, nullptr));
      ++p;
      return postfix(std::move(e), allow_call);
    }
    if (take("true") || take("false")) {
      auto e = make(Expr::Kind::Literal);
      e->literal = Value(tokens[p - 1].text == "true");
      return e;
    }
    if (take("null")) {
      auto e = make(Expr::Kind::Literal);
      e->literal = Value::null();
      return e;
    }
    if (take("undefined")) {
      auto e = make(Expr::Kind::Literal);
      return e;
    }
    if (take("this")) {
      auto e = make(Expr::Kind::This);
      return postfix(std::move(e), allow_call);
    }
    if (take("delete")) {
      auto e = make(Expr::Kind::Unary);
      e->text = "delete";
      e->prefix = true;
      e->left = prefix();
      return e;
    }
    if (is("!") || is("-") || is("+") || is("~") || is("typeof") || is("void") || is("++") ||
        is("--")) {
      std::string op = tok().text;
      ++p;
      auto e = make(op == "++" || op == "--" ? Expr::Kind::Update : Expr::Kind::Unary);
      e->text = op;
      e->prefix = true;
      e->left = expr(11);
      return e;
    }
    if (take("(")) {
      bool arrow = is(")");
      if (!arrow) {
        size_t scan = p;
        int nest = 1;
        while (scan < tokens.size() && nest) {
          if (tokens[scan].text == "(")
            ++nest;
          else if (tokens[scan].text == ")")
            --nest;
          ++scan;
        }
        arrow = scan < tokens.size() && tokens[scan].text == "=>";
      }
      if (arrow) {
        auto fn = function_node();
        fn->arrow = true;
        if (!is(")"))
          do {
            if (take("...")) { fn->params.push_back("..." + identifier()); break; }
            fn->params.push_back(identifier());
          } while (take(","));
        expect(")");
        expect("=>");
        if (take("{"))
          fn->body = block();
        else
          fn->expression = expr();
        auto e = make(Expr::Kind::Function);
        e->function = fn;
        return e;
      }
      auto e = expr();
      expect(")");
      return postfix(std::move(e), allow_call);
    }
    if (take("[")) {
      auto e = make(Expr::Kind::Array);
      if (!is("]"))
        do {
          if (is("]"))
            break;
          if (take("...")) {
            auto spread = make(Expr::Kind::Unary);
            spread->text = "spread";
            spread->prefix = true;
            spread->left = expr();
            e->items.push_back(std::move(spread));
          } else
            e->items.push_back(expr());
        } while (take(","));
      expect("]");
      return postfix(std::move(e), allow_call);
    }
    if (take("{")) {
      auto e = make(Expr::Kind::Object);
      if (!is("}"))
        do {
          if (is("}"))
            break;
          std::string key;
          // An accessor declares get x() or set x(v).
          if ((is("get") || is("set")) && tok(1).text != ":" && tok(1).text != "," &&
              tok(1).text != "}" && tok(1).text != "(") {
            auto accessor = tok().text;
            ++p;
            auto name = tok().text;
            if (starts(name, "string:")) name = name.substr(7);
            ++p;
            auto fn = function_node();
            expect("(");
            if (!is(")"))
              do { fn->params.push_back(identifier()); } while (take(","));
            expect(")");
            expect("{");
            fn->body = block();
            auto value = make(Expr::Kind::Function);
            value->function = fn;
            e->properties.emplace_back(accessor + ":" + name, std::move(value));
            if (!is("}")) expect(",");
            continue;
          }
          if (take("[")) {
            // A computed key is evaluated at object creation time.
            auto key_expr = expr();
            expect("]");
            expect(":");
            e->computed_properties.emplace_back(std::move(key_expr), expr());
            if (!is("}")) expect(",");
            continue;
          }
          if (starts(tok().text, "string:")) {
            key = tok().text.substr(7);
            ++p;
          } else if (starts(tok().text, "number:")) {
            key = tok().text.substr(7);
            ++p;
          } else {
            key = tok().text;
            ++p;
          }
          // A method shorthand: { f() {} }
          if (is("(")) {
            auto fn = function_node();
            expect("(");
            if (!is(")"))
              do {
                if (take("...")) { fn->params.push_back("..." + identifier()); break; }
                fn->params.push_back(identifier());
              } while (take(","));
            expect(")");
            expect("{");
            fn->body = block();
            auto value = make(Expr::Kind::Function);
            value->function = fn;
            e->properties.emplace_back(key, std::move(value));
            if (!is("}")) expect(",");
            continue;
          }
          if (take(":"))
            e->properties.emplace_back(key, expr());
          else {
            auto v = make(Expr::Kind::Identifier);
            v->text = key;
            e->properties.emplace_back(key, std::move(v));
          }
        } while (take(","));
      expect("}");
      return postfix(std::move(e), allow_call);
    }
    if (is("function"))
      return postfix(function_expression());
    if (is("new")) {
      ++p;
      auto e = make(Expr::Kind::New);
      e->left = prefix(false);
      if (take("(")) {
        if (!is(")"))
          do {
            e->items.push_back(expr());
          } while (take(","));
        expect(")");
      }
      return postfix(std::move(e), allow_call);
    }
    if (tok().text.empty() || !ident_start(tok().text[0]) || keyword(tok().text))
      error("expression expected");
    std::string name = tok().text;
    ++p;
    if (is("=>")) {
      ++p;
      auto fn = function_node();
      fn->params.push_back(name);
      if (take("{"))
        fn->body = block();
      else
        fn->expression = expr();
      auto e = make(Expr::Kind::Function);
      e->function = fn;
      return e;
    }
    auto e = make(Expr::Kind::Identifier);
    e->text = name;
    return postfix(std::move(e), allow_call);
  }
  std::unique_ptr<Expr> postfix(std::unique_ptr<Expr> e, bool allow_call = true) {
    while (true) {
      if (is(".") || is("?.")) {
        bool optional = tok().text == "?.";
        ++p;
        auto m = make(Expr::Kind::Member);
        m->optional = optional;
        m->left = std::move(e);
        m->text = tok().text;
        if (m->text.empty() || m->text == "<eof>")
          error("property expected");
        ++p;
        e = std::move(m);
      } else if (take("[")) {
        auto m = make(Expr::Kind::Member);
        m->left = std::move(e);
        m->computed = true;
        m->right = expr();
        expect("]");
        e = std::move(m);
      } else if (allow_call && is("(")) {
        ++p;
        auto c = make(Expr::Kind::Call);
        c->left = std::move(e);
        if (!is(")"))
          do {
            if (take("...")) {
              auto spread = make(Expr::Kind::Unary);
              spread->text = "spread";
              spread->prefix = true;
              spread->left = expr();
              c->items.push_back(std::move(spread));
            } else
              c->items.push_back(expr());
          } while (take(","));
        expect(")");
        e = postfix(std::move(c));
      } else if (is("++") || is("--")) {
        auto u = make(Expr::Kind::Update);
        u->text = tok().text;
        ++p;
        u->left = std::move(e);
        e = std::move(u);
      } else
        break;
    }
    return e;
  }
  // A class builds a constructor plus a prototype holding its methods.
  // I desugar a destructuring pattern into ordinary declarations that read from
  // a hidden temporary, so the rest of my interpreter needs no new machinery.
  std::string temp_name() { return "__pv_pattern" + std::to_string(++temp_counter); }
  static std::unique_ptr<Expr> identifier_expr(const std::string &name, int line) {
    auto e = std::make_unique<Expr>(Expr::Kind::Identifier);
    e->text = name;
    e->line = line;
    return e;
  }
  static std::unique_ptr<Expr> member_of(std::unique_ptr<Expr> base, std::unique_ptr<Expr> key,
                                         bool computed) {
    auto e = std::make_unique<Expr>(Expr::Kind::Member);
    e->left = std::move(base);
    e->computed = computed;
    if (computed)
      e->right = std::move(key);
    else
      e->text = key->text;
    return e;
  }
  void pattern(std::vector<std::pair<std::string, std::unique_ptr<Expr>>> &out,
               std::unique_ptr<Expr> base) {
    if (take("[")) {
      std::string holder = temp_name();
      size_t slot = out.size();
      out.emplace_back(holder, std::move(base));
      size_t index = 0;
      while (!is("]") && !is("<eof>")) {
        if (take(",")) { ++index; continue; }
        if (take("...")) {
          auto name = identifier();
          auto call = std::make_unique<Expr>(Expr::Kind::Call);
          call->left = member_of(identifier_expr(holder, tok().line), identifier_expr("slice", tok().line), false);
          auto number = std::make_unique<Expr>(Expr::Kind::Literal);
          number->literal = Value(double(index));
          call->items.push_back(std::move(number));
          out.emplace_back(name, std::move(call));
          break;
        }
        auto key = std::make_unique<Expr>(Expr::Kind::Literal);
        key->literal = Value(double(index));
        auto read = member_of(identifier_expr(holder, tok().line), std::move(key), true);
        if (is("[") || is("{")) {
          pattern(out, std::move(read));
        } else {
          auto name = identifier();
          if (take("=")) {
            auto fallback = expr();
            auto cond = std::make_unique<Expr>(Expr::Kind::Conditional);
            auto test = std::make_unique<Expr>(Expr::Kind::Binary);
            test->text = "===";
            test->left = member_of(identifier_expr(holder, tok().line),
                                   [&] { auto k = std::make_unique<Expr>(Expr::Kind::Literal);
                                         k->literal = Value(double(index)); return k; }(), true);
            test->right = identifier_expr("undefined", tok().line);
            cond->left = std::move(test);
            cond->right = std::move(fallback);
            cond->third = std::move(read);
            out.emplace_back(name, std::move(cond));
          } else
            out.emplace_back(name, std::move(read));
        }
        ++index;
        if (!is("]")) expect(",");
      }
      expect("]");
      if (!out[slot].second) {
        expect("=");
        out[slot].second = expr();
      }
      return;
    }
    expect("{");
    std::string holder = temp_name();
    size_t slot = out.size();
    out.emplace_back(holder, std::move(base));
    std::vector<std::string> taken_keys;
    while (!is("}") && !is("<eof>")) {
      if (take("...")) {
        // Object rest copies the own keys I have not already taken.
        auto name = identifier();
        auto call = std::make_unique<Expr>(Expr::Kind::Call);
        call->left = identifier_expr("__pv_rest", tok().line);
        call->items.push_back(identifier_expr(holder, tok().line));
        for (auto &taken : taken_keys) {
          auto literal = std::make_unique<Expr>(Expr::Kind::Literal);
          literal->literal = Value(taken);
          call->items.push_back(std::move(literal));
        }
        out.emplace_back(name, std::move(call));
        break;
      }
      std::string key = tok().text;
      if (starts(key, "string:")) key = key.substr(7);
      if (key.empty() || key == "<eof>") error("destructuring key expected");
      taken_keys.push_back(key);
      ++p;
      std::string name = key;
      if (take(":")) {
        if (is("[") || is("{")) {
          pattern(out, member_of(identifier_expr(holder, tok().line), identifier_expr(key, tok().line), false));
          if (!is("}")) expect(",");
          continue;
        }
        name = identifier();
      }
      auto read = member_of(identifier_expr(holder, tok().line), identifier_expr(key, tok().line), false);
      if (take("=")) {
        auto fallback = expr();
        auto cond = std::make_unique<Expr>(Expr::Kind::Conditional);
        auto test = std::make_unique<Expr>(Expr::Kind::Binary);
        test->text = "===";
        test->left = member_of(identifier_expr(holder, tok().line), identifier_expr(key, tok().line), false);
        test->right = identifier_expr("undefined", tok().line);
        cond->left = std::move(test);
        cond->right = std::move(fallback);
        cond->third = std::move(read);
        out.emplace_back(name, std::move(cond));
      } else
        out.emplace_back(name, std::move(read));
      if (!is("}")) expect(",");
    }
    expect("}");
    if (!out[slot].second) {
      expect("=");
      out[slot].second = expr();
    }
  }
  std::unique_ptr<Expr> class_expression() {
    expect("class");
    auto e = make(Expr::Kind::Class);
    if (!is("{") && !is("extends"))
      e->text = identifier();
    if (take("extends"))
      e->left = postfix(prefix(false));
    expect("{");
    while (!is("}") && !is("<eof>")) {
      if (take(";"))
        continue;
      bool is_static = false;
      if (is("static") && tok(1).text != "(" && tok(1).text != "=" && tok(1).text != ";") {
        is_static = true;
        ++p;
      }
      std::string accessor;
      if ((is("get") || is("set")) && tok(1).text != "(" && tok(1).text != ";" &&
          tok(1).text != "}") {
        accessor = tok().text;
        ++p;
      }
      std::string name = tok().text;
      if (starts(name, "string:")) name = name.substr(7);
      if (name.empty() || name == "<eof>")
        error("class member name expected");
      ++p;
      if (!is("(")) {
        // A class field: name = value, assigned on every instance.
        std::unique_ptr<Expr> value;
        if (take("="))
          value = expr();
        take(";");
        e->fields.emplace_back(name, std::move(value));
        continue;
      }
      auto fn = function_node();
      expect("(");
      if (!is(")"))
        do {
          if (take("...")) { fn->params.push_back("..." + identifier()); break; }
          auto param = identifier();
          fn->params.push_back(param);
          if (take("=")) {
            auto check = std::make_unique<Stmt>(Stmt::Kind::If);
            auto ident = std::make_unique<Expr>(Expr::Kind::Identifier);
            ident->text = param;
            auto undef = std::make_unique<Expr>(Expr::Kind::Identifier);
            undef->text = "undefined";
            auto same = std::make_unique<Expr>(Expr::Kind::Binary);
            same->text = "===";
            same->left = std::move(ident);
            same->right = std::move(undef);
            check->condition = std::move(same);
            auto assign = std::make_unique<Expr>(Expr::Kind::Assign);
            assign->text = "=";
            auto target = std::make_unique<Expr>(Expr::Kind::Identifier);
            target->text = param;
            assign->left = std::move(target);
            assign->right = expr();
            auto body = std::make_unique<Stmt>(Stmt::Kind::Expression);
            body->expression = std::move(assign);
            check->body.push_back(std::move(body));
            fn->defaults.push_back(std::move(check));
          }
        } while (take(","));
      expect(")");
      expect("{");
      fn->body = block();
      for (auto it = fn->defaults.rbegin(); it != fn->defaults.rend(); ++it)
        fn->body.insert(fn->body.begin(), std::move(*it));
      fn->defaults.clear();
      auto value = make(Expr::Kind::Function);
      value->function = fn;
      if (name == "constructor" && accessor.empty() && !is_static)
        e->function = fn;
      else {
        std::string key = (is_static ? "static:" : "") +
                          (accessor.empty() ? "" : accessor + ":") + name;
        e->properties.emplace_back(key, std::move(value));
      }
      take(";");
    }
    expect("}");
    return e;
  }
  std::unique_ptr<Expr> function_expression() {
    expect("function");
    auto fn = function_node();
    if (!is("("))
      fn->name = identifier();
    expect("(");
    if (!is(")"))
      do {
        if (take("...")) {
          // A rest parameter collects the remaining arguments into an array.
          fn->params.push_back("..." + identifier());
          break;
        }
        auto name = identifier();
        fn->params.push_back(name);
        if (take("=")) {
          // I record a default as an initializer statement executed on entry
          // when the argument was not supplied.
          auto check = std::make_unique<Stmt>(Stmt::Kind::If);
          auto ident = std::make_unique<Expr>(Expr::Kind::Identifier);
          ident->text = name;
          auto undef = std::make_unique<Expr>(Expr::Kind::Identifier);
          undef->text = "undefined";
          auto same = std::make_unique<Expr>(Expr::Kind::Binary);
          same->text = "===";
          same->left = std::move(ident);
          same->right = std::move(undef);
          check->condition = std::move(same);
          auto assign = std::make_unique<Expr>(Expr::Kind::Assign);
          assign->text = "=";
          auto target = std::make_unique<Expr>(Expr::Kind::Identifier);
          target->text = name;
          assign->left = std::move(target);
          assign->right = expr();
          auto body = std::make_unique<Stmt>(Stmt::Kind::Expression);
          body->expression = std::move(assign);
          check->body.push_back(std::move(body));
          fn->defaults.push_back(std::move(check));
        }
      } while (take(","));
    expect(")");
    expect("{");
    fn->body = block();
    // My parameter defaults run before the body.
    for (auto it = fn->defaults.rbegin(); it != fn->defaults.rend(); ++it)
      fn->body.insert(fn->body.begin(), std::move(*it));
    fn->defaults.clear();
    auto e = make(Expr::Kind::Function);
    e->function = fn;
    return e;
  }
  std::vector<std::unique_ptr<Stmt>> block() {
    std::vector<std::unique_ptr<Stmt>> out;
    while (!is("}") && !is("<eof>"))
      out.push_back(statement());
    expect("}");
    return out;
  }
  std::unique_ptr<Stmt> statement() {
    if (++depth > 160)
      error("statement nesting limit");
    std::unique_ptr<Stmt> s;
    if (take(";")) {
      --depth;
      return statement_node(Stmt::Kind::Empty);
    }
    if (take("{")) {
      s = statement_node(Stmt::Kind::Block);
      s->body = block();
      --depth;
      return s;
    }
    if (is("class")) {
      auto value = class_expression();
      s = statement_node(Stmt::Kind::Variables);
      s->text = "let";
      s->declarations.emplace_back(value->text, std::move(value));
      --depth;
      return s;
    }
    if (is("var") || is("let") || is("const")) {
      s = statement_node(Stmt::Kind::Variables);
      s->text = tok().text;
      ++p;
      do {
        if (is("[") || is("{")) {
          pattern(s->declarations, nullptr);
          if (!take(",")) break;
          continue;
        }
        auto name = identifier();
        std::unique_ptr<Expr> value;
        if (take("="))
          value = expr();
        s->declarations.emplace_back(name, std::move(value));
      } while (take(","));
      semicolon();
    } else if (take("if")) {
      s = statement_node(Stmt::Kind::If);
      expect("(");
      s->condition = expr();
      expect(")");
      s->body.push_back(statement());
      if (take("else"))
        s->alternate.push_back(statement());
    } else if (take("while")) {
      s = statement_node(Stmt::Kind::While);
      expect("(");
      s->condition = expr();
      expect(")");
      s->body.push_back(statement());
    } else if (take("do")) {
      s = statement_node(Stmt::Kind::Do);
      s->body.push_back(statement());
      expect("while");
      expect("(");
      s->condition = expr();
      expect(")");
      semicolon();
    } else if (take("for")) {
      expect("(");
      std::unique_ptr<Stmt> init;
      if (is("var") || is("let") || is("const")) {
        init = statement_node(Stmt::Kind::Variables);
        init->text = tok().text;
        ++p;
        init->declarations.emplace_back(identifier(), nullptr);
        if (is("of") || is("in")) {
          auto kind = tok().text;
          ++p;
          auto f = statement_node(kind == "of" ? Stmt::Kind::ForIn : Stmt::Kind::ForIn);
          f->text = kind;
          f->name = init->declarations[0].first;
          f->condition = expr();
          expect(")");
          f->body.push_back(statement());
          --depth;
          return f;
        }
        if (take("="))
          init->declarations[0].second = expr();
        while (take(",")) {
          auto name = identifier();
          std::unique_ptr<Expr> value;
          if (take("="))
            value = expr();
          init->declarations.emplace_back(name, std::move(value));
        }
      } else if (!is(";"))
        init = statement_node(Stmt::Kind::Expression), init->expression = expr();
      expect(";");
      s = statement_node(Stmt::Kind::For);
      s->body.push_back(std::move(init));
      if (!is(";"))
        s->condition = expr();
      expect(";");
      if (!is(")"))
        s->increment = expr();
      expect(")");
      s->body.push_back(statement());
    } else if (take("return")) {
      s = statement_node(Stmt::Kind::Return);
      if (!is(";") && !is("}") && !is("<eof>") && !tok().newline)
        s->expression = expr();
      semicolon();
    } else if (take("break") || take("continue")) {
      s = statement_node(tokens[p - 1].text == "break" ? Stmt::Kind::Break : Stmt::Kind::Continue);
      semicolon();
    } else if (take("throw")) {
      s = statement_node(Stmt::Kind::Throw);
      s->expression = expr();
      semicolon();
    } else if (take("try")) {
      s = statement_node(Stmt::Kind::Try);
      expect("{");
      s->body = block();
      if (take("catch")) {
        if (take("(")) {
          s->name = identifier();
          expect(")");
        }
        expect("{");
        s->alternate = block();
      }
      if (take("finally")) {
        expect("{");
        s->final = block();
      }
    } else if (is("function")) {
      auto e = function_expression();
      s = statement_node(Stmt::Kind::Function);
      s->function = e->function;
      s->name = e->function->name;
    } else {
      s = statement_node(Stmt::Kind::Expression);
      s->expression = expr();
      semicolon();
    }
    --depth;
    return s;
  }
  Program run() {
    while (!is("<eof>"))
      program.statements.push_back(statement());
    return std::move(program);
  }
};
Program parse(std::string_view source) {
  Parser p(source);
  return p.run();
}
} // namespace pv::js
