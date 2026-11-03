#include "../engine/Psycho_page.hpp"
#include "../platform/Psycho_profile.hpp"
#include "Psycho_image_fixtures.hpp"
#include <filesystem>
#include <cassert>
#include <iostream>
using namespace pv;
static int passed = 0;
#define CHECK(x)                                                                                   \
  do {                                                                                             \
    if (!(x)) {                                                                                    \
      std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << " " << #x << "\n";                    \
      return 1;                                                                                    \
    }                                                                                              \
    ++passed;                                                                                      \
  } while (0)
static std::string js_eval(const std::string &s) {
  js::Runtime r;
  return r.run(s).to_string();
}
int main() {
  CHECK(parse_url("https://example.com/a/../b?x=1#f")->href() == "https://example.com/b?x=1#f");
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
  d.parse("<!doctype html><html><head><title>T</title></head><body><p id=a "
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
  CHECK(js_eval("JSON.stringify({a:1,b:[true,null]})") == "{\"a\":1,\"b\":[true,null]}");
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
  {
    auto decoded = decode_png(Bytes(fixtures::rgba_filters.begin(), fixtures::rgba_filters.end()));
    CHECK(decoded.width == 3 && decoded.height == 5);
    CHECK(decoded.pixels[0] == 0 && decoded.pixels[1] == 0 && decoded.pixels[2] == 255 &&
          decoded.pixels[3] == 255);
    CHECK(decoded.pixels[4] == 0 && decoded.pixels[5] == 255 && decoded.pixels[6] == 0 &&
          decoded.pixels[7] == 128);
    CHECK(decoded.pixels[11] == 0);
    auto palette =
        decode_png(Bytes(fixtures::palette_alpha.begin(), fixtures::palette_alpha.end()));
    CHECK(palette.width == 2 && palette.pixels[2] == 255 && palette.pixels[3] == 255 &&
          palette.pixels[7] == 0);
    auto gray = decode_png(Bytes(fixtures::gray16.begin(), fixtures::gray16.end()));
    CHECK(gray.width == 2 && gray.pixels[0] == 0x12 && gray.pixels[4] == 0xab);
    auto rgb = decode_png(Bytes(fixtures::rgb_fixed.begin(), fixtures::rgb_fixed.end()));
    CHECK(rgb.width == 2 && rgb.pixels[2] == 255 && rgb.pixels[4] == 255);
    bool threw = false;
    try {
      auto bad = Bytes(fixtures::rgba_filters.begin(), fixtures::rgba_filters.end());
      bad.pop_back();
      decode_png(bad);
    } catch (const std::exception &) {
      threw = true;
    }
    CHECK(threw);
    threw = false;
    try {
      auto bad = Bytes(fixtures::rgba_filters.begin(), fixtures::rgba_filters.end());
      bad[20] ^= 0xff;
      decode_png(bad);
    } catch (const std::exception &) {
      threw = true;
    }
    CHECK(threw);
  }
  {
    Bytes bytes = {0, 1, 2, 250, 255, 65, 66};
    auto encoded = base64_encode(bytes);
    CHECK(base64_decode(encoded) == bytes);
    CHECK(base64_encode({}) == "" && base64_decode("").empty());
    bool threw = false;
    try {
      base64_decode("A===");
    } catch (const std::exception &) {
      threw = true;
    }
    CHECK(threw);
    threw = false;
    try {
      base64_decode("AAAA", 2);
    } catch (const std::exception &) {
      threw = true;
    }
    CHECK(threw);
  }
  {
    Page image_page;
    image_page.load(
        *parse_url("https://local.test/"),
        "<!doctype html><body style='margin:0'><img src='Psycho_pixel.png' width='6' height='10'></body>");
    Resource image;
    image.url = "https://local.test/pixel.png";
    image.mime = "image/png";
    image.bytes.assign(fixtures::rgba_filters.begin(), fixtures::rgba_filters.end());
    image_page.resource(image);
    auto image_frame = image_page.render(100, 100, 0);
    bool painted = false;
    for (auto &command : image_frame.at("paint").array)
      if (command.num("kind") == double(PaintCommand::Kind::Image) && command.num("w") == 6 &&
          command.num("h") == 10)
        painted = true;
    CHECK(painted);
    CHECK(image_page.images().count("https://local.test/pixel.png") == 1);
    Canvas surface(20, 20, 0xff000000);
    surface.image(image_page.images().begin()->second, {0, 0, 6, 10});
    CHECK(surface.pixels[2] == 255);
  }
  {
    auto suffix = base64_encode(random_bytes(8));
    for (char &c : suffix)
      if (c == '+' || c == '/')
        c = 'x';
    auto directory = std::filesystem::temp_directory_path() / ("psychovault-test-" + suffix);
    auto path = directory.string();
    Profile profile = Profile::open(path, false);
    CHECK(profile.error.empty());
    CHECK(profile.set_search_engine("wikipedia"));
    CHECK(profile.set_home_page("https://example.com/"));
    CHECK(profile.set_custom_search("https://example.com/search?q=%s"));
    profile.bookmark("Example", "https://example.com/");
    profile.visit("Example", "https://example.com/");
    profile.session_tabs = {"https://example.com/", "psychovault://newtab"};
    profile.session_active = 1;
    CHECK(profile.save());
    Profile restored = Profile::open(path, false);
    CHECK(restored.search_engine == "wikipedia");
    CHECK(restored.home_page == "https://example.com/");
    CHECK(restored.custom_search == "https://example.com/search?q=%s");
    CHECK(restored.bookmarks.size() == 1 && restored.history.size() == 1);
    CHECK(restored.session_tabs.size() == 2 && restored.session_active == 1);
    Profile private_profile = Profile::open(path, true);
    private_profile.visit("Private", "https://private.example/");
    private_profile.session_tabs = {"https://private.example/"};
    CHECK(private_profile.save());
    Profile after_private = Profile::open(path, false);
    CHECK(after_private.history.size() == 1 && after_private.session_tabs.size() == 2);
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
  }
  std::cout << passed << " engine checks passed\n";
  return 0;
}
