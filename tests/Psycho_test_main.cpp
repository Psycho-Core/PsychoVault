#include "../engine/Psycho_page.hpp"
#include "../platform/Psycho_profile.hpp"
#include "../platform/Psycho_proxy.hpp"
#include "../platform/Psycho_extension.hpp"
#include "../platform/Psycho_download.hpp"
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
    image.url = "https://local.test/Psycho_pixel.png";
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
    CHECK(image_page.images().count("https://local.test/Psycho_pixel.png") == 1);
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
  // ---- My proxy routing and fail-closed behaviour -------------------------
  {
    ProxyConfig config;
    std::string error;
    auto json = Json::parse(R"({"mode":"fixed_servers","singleProxy":{"scheme":"http","host":"127.0.0.1","port":8080},"bypassList":["localhost",".internal.test","10.0.0.0/8","*.skip.test"],"failClosed":true})");
    auto parsed = ProxyConfig::from_json(*json, error);
    CHECK(parsed.has_value());
    config = *parsed;
    CHECK(config.mode_name() == "fixed_servers");
    ProxyServer server;
    std::string reason;
    CHECK(config.route(*parse_url("https://example.com/"), server, reason));
    CHECK(server.authority() == "127.0.0.1:8080");
    CHECK(config.route(*parse_url("https://box.internal.test/"), server, reason));
    CHECK(server.empty());
    CHECK(config.route(*parse_url("https://a.skip.test/"), server, reason));
    CHECK(server.empty());
    CHECK(config.route(*parse_url("http://10.1.2.3/"), server, reason));
    CHECK(server.empty());
    CHECK(config.route(*parse_url("http://11.1.2.3/"), server, reason));
    CHECK(!server.empty());
    CHECK(config.route(*parse_url("http://localhost/"), server, reason));
    CHECK(server.empty());
    // A fail-closed config with no usable proxy must refuse, not leak direct.
    auto broken = Json::parse(R"({"mode":"pac_script","pac":"","failClosed":true})");
    auto bad = ProxyConfig::from_json(*broken, error);
    CHECK(!bad.has_value());
    ProxyConfig empty_pac;
    empty_pac.mode = ProxyConfig::Mode::PacScript;
    empty_pac.fail_closed = true;
    CHECK(!empty_pac.route(*parse_url("https://example.com/"), server, reason));
    CHECK(reason.find("fail-closed") != std::string::npos);
    ProxyConfig open_pac;
    open_pac.mode = ProxyConfig::Mode::PacScript;
    CHECK(open_pac.route(*parse_url("https://example.com/"), server, reason));
    // I reject a proxy server I cannot actually tunnel rather than accept it.
    auto socks = Json::parse(R"({"mode":"fixed_servers","singleProxy":{"scheme":"ftp","host":"h","port":1}})");
    CHECK(!ProxyConfig::from_json(*socks, error).has_value());
    auto roundtrip = ProxyConfig::from_json(config.to_json(), error);
    CHECK(roundtrip.has_value() && roundtrip->single.authority() == "127.0.0.1:8080");
  }
  // ---- My PAC evaluator runs in my own interpreter ------------------------
  {
    auto servers = parse_pac_result("PROXY first.test:8080; HTTPS second.test:4443; DIRECT");
    CHECK(servers.size() == 3);
    CHECK(servers[0].scheme == "http" && servers[0].port == 8080);
    CHECK(servers[1].scheme == "https" && servers[1].port == 4443);
    CHECK(servers[2].empty());
    CHECK(parse_pac_result("BOGUS x; PROXY :; PROXY h:0").empty());
    std::string script =
        "function FindProxyForURL(url, host) {"
        "  if (isPlainHostName(host)) return 'DIRECT';"
        "  if (dnsDomainIs(host, '.internal.test')) return 'DIRECT';"
        "  if (shExpMatch(host, '*.vpn.test')) return 'PROXY vpn.test:3128';"
        "  if (isInNet(host, '10.0.0.0', '255.0.0.0')) return 'DIRECT';"
        "  return 'PROXY default.test:8080';"
        "}";
    auto plain = run_pac(script, *parse_url("http://box/"));
    CHECK(plain.ok && plain.raw == "DIRECT");
    auto internal = run_pac(script, *parse_url("http://a.internal.test/"));
    CHECK(internal.ok && internal.raw == "DIRECT");
    auto vpn = run_pac(script, *parse_url("https://site.vpn.test/"));
    CHECK(vpn.ok && vpn.servers.size() == 1 && vpn.servers[0].host == "vpn.test");
    auto fallback = run_pac(script, *parse_url("https://example.com/"));
    CHECK(fallback.ok && fallback.servers[0].port == 8080);
    auto missing = run_pac("var x = 1;", *parse_url("https://example.com/"));
    CHECK(!missing.ok && !missing.error.empty());
    // A hostile script must not hang my routing decision.
    auto loop = run_pac("function FindProxyForURL(){ while(true){} return 'DIRECT'; }",
                        *parse_url("https://example.com/"));
    CHECK(!loop.ok);
  }
  // ---- My extension manifest validation and default-deny permissions ------
  {
    auto manifest = ExtensionManifest::parse(R"({"manifest_version":3,"name":"Test VPN","version":"1.0.0","permissions":["proxy","storage","privacy","webRequest","webRequestAuthProvider"],"background":{"service_worker":"js/service.js"}})");
    CHECK(manifest.valid);
    CHECK(manifest.name == "Test VPN");
    CHECK(manifest.service_worker == "js/service.js");
    CHECK(manifest.has_permission("proxy"));
    CHECK(manifest.has_permission("storage"));
    CHECK(!manifest.has_permission("tabs"));
    CHECK(!manifest.has_permission("debugger"));
    CHECK(!ExtensionManifest::parse("{}").valid);
    CHECK(!ExtensionManifest::parse("not json").valid);
    CHECK(!ExtensionManifest::parse(R"({"manifest_version":1,"name":"x","version":"1"})").valid);
    CHECK(!ExtensionManifest::parse(R"({"manifest_version":3,"name":"x"})").valid);
    auto traversal = ExtensionManifest::parse(R"({"manifest_version":3,"name":"x","version":"1","background":{"service_worker":"../../evil.js"}})");
    CHECK(!traversal.valid);
    auto absolute = ExtensionManifest::parse(R"({"manifest_version":3,"name":"x","version":"1","background":{"service_worker":"C:/evil.js"}})");
    CHECK(!absolute.valid);
    auto hosts = ExtensionManifest::parse(R"({"manifest_version":3,"name":"x","version":"1","host_permissions":["https://*.example.com/*","<all_urls>"]})");
    CHECK(hosts.valid && hosts.matches_host(*parse_url("https://a.example.com/")));
    ExtensionManifest scoped;
    scoped.host_permissions = {"https://*.example.com/*"};
    CHECK(scoped.matches_host(*parse_url("https://sub.example.com/x")));
    CHECK(scoped.matches_host(*parse_url("https://example.com/x")));
    CHECK(!scoped.matches_host(*parse_url("https://evil.test/x")));
    CHECK(!scoped.matches_host(*parse_url("file:///etc/passwd")));
  }
  // ---- My extension host enforces permissions at the API boundary ---------
  {
    auto root = std::filesystem::temp_directory_path() /
                ("psychovault-ext-" + std::to_string(now_ms()));
    std::filesystem::create_directories(root);
    std::string error;
    write_file_atomic((root / "manifest.json").string(),
                      R"({"manifest_version":3,"name":"No Perms","version":"1.0","background":{"service_worker":"w.js"}})", error);
    // This worker tries to take over my proxy without the proxy permission.
    write_file_atomic((root / "w.js").string(),
                      "var denied = false; try { chrome.proxy.settings.set({value:{mode:'direct'}}); }"
                      " catch (e) { denied = true; }", error);
    ExtensionHost host;
    bool proxy_called = false;
    ExtensionEffects effects;
    effects.set_proxy = [&](const std::string &, const ProxyConfig &, std::string &) {
      proxy_called = true; return true;
    };
    host.set_effects(std::move(effects));
    CHECK(host.install_directory(root.string(), error));
    auto records = host.list();
    CHECK(records.size() == 1);
    CHECK(host.start_worker(records[0].manifest.id, error));
    CHECK(!proxy_called);
    auto value = host.list()[0];
    CHECK(value.install_error.empty());
    // A worker with the permission does reach my broker.
    write_file_atomic((root / "manifest.json").string(),
                      R"({"manifest_version":3,"name":"With Perms","version":"1.0","permissions":["proxy","storage"],"background":{"service_worker":"w.js"}})", error);
    write_file_atomic((root / "w.js").string(),
                      "chrome.storage.local.set({token:'abc'});"
                      "chrome.proxy.settings.set({value:{mode:'fixed_servers',singleProxy:{scheme:'http',host:'127.0.0.1',port:0}}});", error);
    ExtensionHost granted;
    bool reached = false;
    ExtensionEffects allowed;
    allowed.set_proxy = [&](const std::string &, const ProxyConfig &, std::string &) {
      reached = true; return true;
    };
    granted.set_effects(std::move(allowed));
    CHECK(granted.install_directory(root.string(), error));
    auto id = granted.list()[0].manifest.id;
    // Port 0 is valid JS but not a usable proxy, so my validation must refuse it
    // before the broker is ever asked to change routing.
    granted.start_worker(id, error);
    CHECK(!reached);
    write_file_atomic((root / "w.js").string(),
                      "chrome.storage.local.set({token:'abc'});"
                      "chrome.proxy.settings.set({value:{mode:'fixed_servers',singleProxy:{scheme:'http',host:'127.0.0.1',port:3128}}});", error);
    CHECK(granted.start_worker(id, error));
    CHECK(reached);
    CHECK(granted.list()[0].storage.str("token") == "abc");
    CHECK(granted.set_enabled(id, false, error));
    CHECK(!granted.remove("does-not-exist", error));
    CHECK(granted.remove(id, error));
    CHECK(granted.list().empty());
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
  }
  // ---- My download naming refuses traversal and device names -------------
  {
    auto url = parse_url("https://example.com/files/report.pdf");
    CHECK(safe_download_name(*url, "") == "report.pdf");
    CHECK(safe_download_name(*url, "attachment; filename=\"clean name.zip\"") == "clean name.zip");
    auto evil = safe_download_name(*url, "attachment; filename=\"../../evil.exe\"");
    CHECK(evil.find('/') == std::string::npos && evil.find("..") == std::string::npos);
    auto stream = safe_download_name(*url, "attachment; filename=\"file.txt:payload\"");
    CHECK(stream.find(':') == std::string::npos);
    CHECK(safe_download_name(*parse_url("https://example.com/CON"), "") == "_CON");
    CHECK(safe_download_name(*parse_url("https://example.com/"), "").size() > 0);
    auto directory = std::filesystem::temp_directory_path() /
                     ("psychovault-dl-" + std::to_string(now_ms()));
    std::filesystem::create_directories(directory);
    auto first = unique_download_path(directory.string(), "same.txt");
    CHECK(first.find("same.txt") != std::string::npos);
    std::string error;
    write_file_atomic(first, "x", error);
    auto second = unique_download_path(directory.string(), "same.txt");
    CHECK(second != first && second.find("same (1).txt") != std::string::npos);
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
    NetworkPolicy policy;
    policy.block_ads = true;
    policy.blocked_hosts = {"ads.example", "tracker.test"};
    std::string reason;
    CHECK(policy_blocks(policy, *parse_url("https://ads.example/banner"), reason));
    CHECK(policy_blocks(policy, *parse_url("https://sub.tracker.test/x"), reason));
    CHECK(!policy_blocks(policy, *parse_url("https://good.test/"), reason));
    policy.allowed_hosts = {"ads.example"};
    CHECK(!policy_blocks(policy, *parse_url("https://ads.example/banner"), reason));
    // parse_url already refuses file://, so I check the policy guard directly
    // rather than dereferencing a rejected URL.
    CHECK(!parse_url("file:///etc/passwd").has_value());
    Url local_scheme;
    local_scheme.scheme = "file";
    local_scheme.host = "";
    local_scheme.path = "/etc/passwd";
    CHECK(policy_blocks(policy, local_scheme, reason));
  }
  // ---- My new settings survive a profile round trip ----------------------
  {
    auto directory = std::filesystem::temp_directory_path() /
                     ("psychovault-settings-" + std::to_string(now_ms()));
    auto path = directory.string();
    Profile profile = Profile::open(path, false);
    profile.block_ads = false;
    profile.send_dnt = false;
    profile.strip_referrer = true;
    profile.clear_on_exit = true;
    profile.web_rtc_policy = "disable_non_proxied_udp";
    profile.blocked_hosts = {"ads.example"};
    profile.cpu_limit = 50;
    profile.sleep_tabs_minutes = 15;
    profile.accent = "purple";
    profile.high_contrast = true;
    profile.show_tab_close_buttons = false;
    profile.add_user_word("Psycho");
    profile.add_user_word("psycho");
    profile.proxy.mode = ProxyConfig::Mode::FixedServers;
    profile.proxy.single = ProxyServer{"http", "127.0.0.1", 3128};
    profile.proxy.fail_closed = true;
    profile.proxy.owner_extension = "vpn-test";
    CHECK(profile.save());
    Profile restored = Profile::open(path, false);
    CHECK(!restored.block_ads);
    CHECK(!restored.send_dnt);
    CHECK(restored.strip_referrer);
    CHECK(restored.clear_on_exit);
    CHECK(restored.web_rtc_policy == "disable_non_proxied_udp");
    CHECK(restored.blocked_hosts.size() == 1);
    CHECK(restored.cpu_limit == 50);
    CHECK(restored.sleep_tabs_minutes == 15);
    CHECK(restored.accent == "purple");
    CHECK(restored.high_contrast);
    CHECK(!restored.show_tab_close_buttons);
    CHECK(restored.user_words.size() == 1);
    CHECK(restored.proxy.fail_closed);
    CHECK(restored.proxy.single.authority() == "127.0.0.1:3128");
    CHECK(restored.proxy.owner_extension == "vpn-test");
    CHECK(profile.remove_user_word("PSYCHO"));
    CHECK(profile.user_words.empty());
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
  }
  std::cout << passed << " engine checks passed\n";
  return 0;
}
