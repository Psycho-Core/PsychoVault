#include "../engine/page.hpp"
#include <cassert>
#include <iostream>
using namespace pv;
static int passed = 0;
#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x)) {                                                                \
      std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << " " << #x         \
                << "\n";                                                       \
      return 1;                                                                \
    }                                                                          \
    ++passed;                                                                  \
  } while (0)
static std::string js_eval(const std::string &s) {
  js::Runtime r;
  return r.run(s).to_string();
}
int main() {
  CHECK(parse_url("https://example.com/a/../b?x=1#f")->href() ==
        "https://example.com/b?x=1#f");
  CHECK(parse_url("http://example.com:80/")->href() == "http://example.com/");
  CHECK(!parse_url("https://user:pass@example.com/"));
  CHECK(!parse_url("file:///C:/secret"));
  CHECK(resolve_url(*parse_url("https://example.com/a/b"), "..//c")->href() ==
        "https://example.com/c");
  CHECK(navigate_input("hello world", "https://duckduckgo.com/?q=%s")->href() ==
        "https://duckduckgo.com/?q=hello+world");
  CHECK(Json::parse("{\"a\":[1,true,null,\"x\"]}")->at("a").array.size() == 4);
  CHECK(!Json::parse("{\"a\":}"));
  CHECK(entity_decode("&amp;&#65;&#x1F600;") == "&A\xf0\x9f\x98\x80");
  Document d;
  d.parse(
      "<!doctype html><html><head><title>T</title></head><body><p id=a "
      "class=x>Hello <b>world</b></p><ul><li>one<li>two</ul></body></html>");
  CHECK(d.title() == "T");
  CHECK(d.by_id("a") >= 0);
  CHECK(d.text_content(d.by_id("a")) == "Hello world");
  CHECK(d.query("li").size() == 2);
  CHECK(d.query("body > p.x").size() == 1);
  CHECK(js_eval("1+2*3") == "7");
  CHECK(js_eval("(function(x){return x*x})(7)") == "49");
  CHECK(js_eval("[1,2,3].map(x=>x*2).join(',')") == "2,4,6");
  CHECK(js_eval("var n=0;for(var i=0;i<10;i++)n+=i;n") == "45");
  CHECK(js_eval("JSON.stringify({a:1,b:[true,null]})") ==
        "{\"a\":1,\"b\":[true,null]}");
  CHECK(js_eval("try{throw new Error('ok')}catch(e){e.message}") == "ok");
  CHECK(js_eval("function f(n){return n<2?1:n*f(n-1)}f(6)") == "720");
  CHECK(js_eval("'abc'.toUpperCase()") == "ABC");
  CHECK(js_eval("'a,b,c'.split(',').length") == "3");
  CHECK(js_eval("Object.keys({a:1,b:2}).join(',')") == "a,b");
  {
    js::Runtime r;
    r.run("var x=0;function inc(){x++;}inc();inc();");
    CHECK(r.run("x").to_number() == 2);
    for (int i = 0; i < 100; ++i)
      r.run("({a:1,b:[1,2,3]})");
    CHECK(r.object_count() < 10000);
  }
  {
    bool threw = false;
    js::Runtime r;
    try {
      r.run("while(true){}");
    } catch (std::exception &) {
      threw = true;
    }
    CHECK(threw);
  }
  Page page;
  page.load(*parse_url("https://local.test/"),
            "<!doctype "
            "html><style>body{background:#000;color:#fff;margin:20px}p{color:#"
            "f00}</style><p "
            "id=x>before</"
            "p><script>document.getElementById('x').textContent='after';var "
            "n=0;document.getElementById('x').addEventListener('click',"
            "function(){n++;this.textContent='clicked '+n;});</script>");
  auto frame = page.render(800, 600, 0);
  bool found = false;
  for (auto &p : frame.at("paint").array)
    if (p.str("text") == "after")
      found = true;
  CHECK(found);
  // Hit the paragraph through its painted text box.
  page.click(21, 40);
  frame = page.render(800, 600, 0);
  found = false;
  for (auto &p : frame.at("paint").array)
    if (p.str("text").find("clicked") != std::string::npos)
      found = true;
  CHECK(found);
  Page form;
  form.load(*parse_url("https://local.test/"),
            "<!doctype html><form action='/search'><input name=q value='hello "
            "world'><button>Go</button></form>");
  form.render(800, 600, 0);
  form.click(10, 10);
  form.key("Enter", "");
  CHECK(!form.navigations.empty());
  if (!form.navigations.empty())
    CHECK(form.navigations[0].find("q=hello+world") != std::string::npos);
  Canvas c(64, 64, 0xff000000);
  c.fill({0, 0, 64, 64}, 0xff112233);
  c.stroke({4, 4, 20, 20}, 0xffffffff);
  c.text(2, 30, "PV", 12, 0xff00ff00);
  auto png = encode_png(64, 64, c.pixels);
  CHECK(png.size() > 50);
  CHECK(png[0] == 137 && png[1] == 80);
  std::cout << passed << " engine checks passed\n";
  return 0;
}
