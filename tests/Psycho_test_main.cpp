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
  // ---- My event payloads reach extension listeners intact ----------------
  {
    auto root = std::filesystem::temp_directory_path() /
                ("psychovault-probe-" + std::to_string(now_ms()));
    std::filesystem::create_directories(root);
    std::string error;
    write_file_atomic((root / "manifest.json").string(),
                      R"({"manifest_version":3,"name":"Probe","version":"1.0","background":{"service_worker":"w.js"}})", error);
    write_file_atomic((root / "w.js").string(),
                      "var g = { enabled: false };"
                      "function reassign() { g = { enabled: true }; }"
                      "chrome.runtime.onMessage.addListener(function (m) {"
                      "  console.log('typeof=' + (typeof m) + ' target=' + m.target + ' n=' + m.n);"
                      "  console.log('bool=' + (typeof m.enabled) + ' v=' + m.enabled +"
                      "              ' not=' + (!m.enabled) + ' notnot=' + (!!m.enabled) +"
                      "              ' eq=' + (m.command === 'configure') + ' or=' + (m.host || 'fallback'));"
                      "  reassign();"
                      "  console.log('global=' + g.enabled);"
                      "});", error);
    ExtensionHost host;
    std::vector<std::string> seen;
    ExtensionEffects effects;
    effects.log = [&](const std::string &, const std::string &, const std::string &message) {
      seen.push_back(message);
    };
    host.set_effects(std::move(effects));
    CHECK(host.install_directory(root.string(), error));
    auto id = host.list()[0].manifest.id;
    CHECK(host.start_worker(id, error));
    Json payload = Json::make_object();
    payload.set("target", "probe-target");
    payload.set("n", 42);
    payload.set("enabled", true);
    payload.set("command", "configure");
    payload.set("host", "127.0.0.1");
    host.broadcast("runtime.onMessage", payload);
    CHECK(seen.size() >= 1);
    CHECK(seen[0].find("target=probe-target") != std::string::npos);
    CHECK(seen[0].find("n=42") != std::string::npos);
    CHECK(seen.size() >= 3);
    CHECK(seen[1].find("bool=boolean") != std::string::npos);
    CHECK(seen[1].find("notnot=true") != std::string::npos);
    CHECK(seen[1].find("eq=true") != std::string::npos);
    CHECK(seen[2].find("global=true") != std::string::npos);
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
  }
  // ---- My bundled VPN bridge drives my real proxy path -------------------
  {
    ExtensionHost host;
    ProxyConfig applied = ProxyConfig::direct();
    bool set_called = false, clear_called = false;
    std::string webrtc;
    std::vector<std::string> log;
    ExtensionEffects effects;
    effects.set_proxy = [&](const std::string &, const ProxyConfig &config, std::string &) {
      applied = config; set_called = true; return true;
    };
    effects.get_proxy = [&](const std::string &) { return applied; };
    effects.clear_proxy = [&](const std::string &, std::string &) {
      applied = ProxyConfig::direct(); clear_called = true; return true;
    };
    effects.set_webrtc_policy = [&](const std::string &, const std::string &value, std::string &) {
      webrtc = value; return true;
    };
    effects.log = [&](const std::string &, const std::string &, const std::string &message) {
      log.push_back(message);
    };
    host.set_effects(std::move(effects));
    std::string error;
    auto repo_root = std::filesystem::path(__FILE__).parent_path().parent_path();
    std::string bridge = (repo_root / "extensions" / "PsychoVault-VPN-Bridge").string();
    CHECK(host.install_directory(bridge, error));
    auto records = host.list();
    CHECK(records.size() == 1);
    auto id = records[0].manifest.id;
    CHECK(records[0].manifest.has_permission("proxy"));
    CHECK(records[0].manifest.has_permission("webRequestAuthProvider"));
    // My bridge must start in my own interpreter with no shim.
    bool started = host.start_worker(id, error);
    if (!started) std::cerr << "bridge start error: " << error << "\n";
    CHECK(started);
    CHECK(host.list()[0].install_error.empty());
    // Starting disabled must clear routing rather than leave a stale proxy.
    CHECK(clear_called);
    CHECK(!applied.active());
    // I configure it the way my UI would, through a runtime message.
    Json configure = Json::make_object();
    configure.set("target", "psychovault-vpn");
    configure.set("command", "configure");
    configure.set("scheme", "http");
    configure.set("host", "127.0.0.1");
    configure.set("port", 3128);
    configure.set("enabled", true);
    host.broadcast("runtime.onMessage", configure);
    if (!set_called) std::cerr << "bridge worker error: " << host.worker_error(id) << "\n";
    CHECK(set_called);
    CHECK(applied.mode == ProxyConfig::Mode::FixedServers);
    CHECK(applied.single.authority() == "127.0.0.1:3128");
    CHECK(applied.fail_closed);
    CHECK(webrtc == "disable_non_proxied_udp");
    CHECK(host.list()[0].storage.flag("enabled"));
    CHECK(host.list()[0].storage.num("port") == 3128);
    // The routing it installed must actually route and bypass correctly.
    ProxyServer server;
    std::string reason;
    CHECK(applied.route(*parse_url("https://example.com/"), server, reason));
    CHECK(server.authority() == "127.0.0.1:3128");
    CHECK(applied.route(*parse_url("http://localhost/"), server, reason));
    CHECK(server.empty());
    // Disconnecting must drop back to direct and restore WebRTC policy.
    Json disconnect = Json::make_object();
    disconnect.set("target", "psychovault-vpn");
    disconnect.set("command", "disconnect");
    host.broadcast("runtime.onMessage", disconnect);
    CHECK(!applied.active());
    CHECK(webrtc == "default");
    CHECK(!host.list()[0].storage.flag("enabled"));
    // Enabling with no server must refuse rather than claim a VPN is on.
    Json unconfigured = Json::make_object();
    unconfigured.set("target", "psychovault-vpn");
    unconfigured.set("command", "configure");
    unconfigured.set("host", "");
    unconfigured.set("port", 0);
    unconfigured.set("enabled", true);
    host.broadcast("runtime.onMessage", unconfigured);
    CHECK(!applied.active());
    CHECK(!host.list()[0].storage.flag("enabled"));
    CHECK(!log.empty());
  }
  // ---- Comparison operators must never write to their left operand --------
  // I misclassified every operator ending in '=' as a compound assignment, so
  // ===, ==, <= and >= silently overwrote the value they were comparing.
  {
    CHECK(js_eval("var o={c:'configure'};var r=(o.c==='configure');r+':'+o.c") == "true:configure");
    CHECK(js_eval("var o={c:'configure'};var r=(o.c==='status');r+':'+o.c") == "false:configure");
    CHECK(js_eval("var o={c:'configure'};(o.c==='status')+':'+(o.c==='configure')+':'+(typeof o.c)") ==
          "false:true:string");
    CHECK(js_eval("var a=5;var b=(a==5);b+':'+a") == "true:5");
    CHECK(js_eval("var a=5;var b=(a!=6);b+':'+a") == "true:5");
    CHECK(js_eval("var a=5;var b=(a!==6);b+':'+a") == "true:5");
    CHECK(js_eval("var a=5;var b=(a<=5);b+':'+a") == "true:5");
    CHECK(js_eval("var a=5;var b=(a>=5);b+':'+a") == "true:5");
    CHECK(js_eval("var a=5;var b=(a<6);b+':'+a") == "true:5");
    CHECK(js_eval("var a=5;var b=(a>4);b+':'+a") == "true:5");
    // A real compound assignment must still write.
    CHECK(js_eval("var a=5;a+=3;a") == "8");
    CHECK(js_eval("var a=5;a-=3;a") == "2");
    CHECK(js_eval("var a=5;a*=3;a") == "15");
    CHECK(js_eval("var a=6;a/=3;a") == "2");
    CHECK(js_eval("var a=7;a%=3;a") == "1");
    CHECK(js_eval("var a=2;a**=3;a") == "8");
    CHECK(js_eval("var a=1;a<<=3;a") == "8");
    CHECK(js_eval("var a=8;a>>=2;a") == "2");
    CHECK(js_eval("var a=6;a&=3;a") == "2");
    CHECK(js_eval("var a=6;a|=1;a") == "7");
    CHECK(js_eval("var a=6;a^=3;a") == "5");
    // Branching on a comparison must take the correct arm.
    CHECK(js_eval("var o={c:'configure'};var out='none';"
                  "if(o.c==='status'){out='status';}else if(o.c==='configure'){out='configure';}out") ==
          "configure");
    CHECK(js_eval("var o={c:'configure'};var n=0;"
                  "if(o.c==='configure'){n++;}if(o.c==='configure'){n++;}n") == "2");
  }
  // Modern JavaScript my extension host and real pages rely on.
  {
    CHECK(js_eval("var n='world';`hi ${n} ${1+2}`") == "hi world 3");
    CHECK(js_eval("var a=2;`x${`y${a}`}z`") == "xy2z");
    CHECK(js_eval("Math.max(...[1,5,3])") == "5");
    CHECK(js_eval("[...[1,2],3].join(',')") == "1,2,3");
    CHECK(js_eval("Array.from('ab').join(',')") == "a,b");
    CHECK(js_eval("Array.of(1,2).length") == "2");
    CHECK(js_eval("function f(a=5){return a}f()") == "5");
    CHECK(js_eval("function f(a=5){return a}f(9)") == "9");
    CHECK(js_eval("function f(...a){return a.length}f(1,2,3)") == "3");
    CHECK(js_eval("var o={n:7,f:function(){return (()=>this.n)()}};o.f()") == "7");
    CHECK(js_eval("var f=x=>x*2;f(4)") == "8");
    CHECK(js_eval("var s='';for(var c of 'abc')s+=c;s") == "abc");
    CHECK(js_eval("var s=0;for(var x of [1,2,3])s+=x;s") == "6");
    CHECK(js_eval("var o={get x(){return 5}};o.x") == "5");
    CHECK(js_eval("var k='a';var o={[k]:5};o.a") == "5");
    CHECK(js_eval("var o={f(){return 6}};o.f()") == "6");
    CHECK(js_eval("var o=null;String(o?.x)") == "undefined");
    CHECK(js_eval("var o={a:{b:4}};o?.a?.b") == "4");
    CHECK(js_eval("'a' in {a:1}") == "true");
    CHECK(js_eval("({a:1}).hasOwnProperty('a')") == "true");
    CHECK(js_eval("({a:1}).hasOwnProperty('b')") == "false");
    CHECK(js_eval("var o={a:1};delete o.a;Object.keys(o).length") == "0");
    CHECK(js_eval("var m=new Map();m.set('a',1);m.set('b',2);m.get('b')") == "2");
    CHECK(js_eval("var m=new Map([['a',1],['b',2]]);m.size") == "2");
    CHECK(js_eval("var m=new Map();m.set('a',1);m.has('a')") == "true");
    CHECK(js_eval("var m=new Map();m.set('a',1);m.delete('a');m.size") == "0");
    CHECK(js_eval("var m=new Map();m.set('a',1);m.set('b',2);m.keys().join(',')") == "a,b");
    CHECK(js_eval("var t=0;var m=new Map([['a',1],['b',2]]);m.forEach(function(v){t+=v});t") == "3");
    CHECK(js_eval("var s=new Set([1,1,2]);s.size") == "2");
    CHECK(js_eval("var s=new Set();s.add(1);s.add(1);s.size") == "1");
    CHECK(js_eval("var s=new Set([1,2]);s.has(2)") == "true");
    CHECK(js_eval("var s=new Set([1,2]);s.delete(1);s.size") == "1");
    CHECK(js_eval("Array.from([1,2],x=>x*3).join(',')") == "3,6");
    bool threw = false;
    try {
      js_eval("[...5]");
    } catch (const js::JsError &) {
      threw = true;
    }
    CHECK(threw);
  }
  // Classes, prototypes and explicit receivers.
  {
    CHECK(js_eval("class A{constructor(x){this.x=x}get(){return this.x}}new A(5).get()") == "5");
    CHECK(js_eval("class A{constructor(){this.n=0}inc(){this.n++;return this}}var a=new A();a.inc().inc().n") == "2");
    CHECK(js_eval("class A{static make(){return 42}}A.make()") == "42");
    CHECK(js_eval("class A{get double(){return 21*2}}new A().double") == "42");
    CHECK(js_eval("class A{x=5;constructor(y){this.y=y}}var a=new A(2);a.x+a.y") == "7");
    CHECK(js_eval("class A{f(){return 1}}class B extends A{}new B().f()") == "1");
    CHECK(js_eval("class A{f(){return 1}}class B extends A{f(){return super.f()+10}}new B().f()") == "11");
    CHECK(js_eval("class A{constructor(x){this.x=x}}class B extends A{constructor(x){super(x*2)}}new B(3).x") == "6");
    CHECK(js_eval("class A{constructor(x){this.x=x}}class B extends A{}new B(9).x") == "9");
    CHECK(js_eval("class A{f(){return 1}}class B extends A{f(){return 12}}new B().f()") == "12");
    CHECK(js_eval("class A{}class B extends A{}new B() instanceof A") == "true");
    CHECK(js_eval("class A{}new A() instanceof A") == "true");
    CHECK(js_eval("class A{}class B{}new A() instanceof B") == "false");
    CHECK(js_eval("class A{constructor(){this.v=3}f(){return this.v}}var a=new A();var f=a.f;f.call(a)") == "3");
    CHECK(js_eval("function f(a,b){return this.n+a+b}f.call({n:1},2,3)") == "6");
    CHECK(js_eval("function f(a,b){return this.n+a+b}f.apply({n:1},[2,3])") == "6");
    CHECK(js_eval("function f(a,b){return this.n+a+b}var g=f.bind({n:1},2);g(3)") == "6");
    CHECK(js_eval("class A{f(x=4){return x}}new A().f()") == "4");
  }
  // Destructuring desugars into ordinary declarations.
  {
    CHECK(js_eval("var [a,b]=[1,2];a+b") == "3");
    CHECK(js_eval("var {a,b}={a:1,b:2};a+b") == "3");
    CHECK(js_eval("var {a:x}={a:7};x") == "7");
    CHECK(js_eval("var [a,b=9]=[1];a+b") == "10");
    CHECK(js_eval("var {a,b=9}={a:1};a+b") == "10");
    CHECK(js_eval("var {a=5}={a:1};a") == "1");
    CHECK(js_eval("var {a:{b}}={a:{b:7}};b") == "7");
    CHECK(js_eval("var [[a],b]=[[1],2];a+b") == "3");
    CHECK(js_eval("var [a,...r]=[1,2,3];r.join(',')") == "2,3");
    CHECK(js_eval("var {a,...r}={a:1,b:2,c:3};Object.keys(r).join(',')") == "b,c");
    CHECK(js_eval("var [,b]=[1,2];b") == "2");
    CHECK(js_eval("let {a}={a:4};a") == "4");
    CHECK(js_eval("const [a]=[6];a") == "6");
    CHECK(js_eval("function f(){return {x:3}}var {x}=f();x") == "3");
  }
  std::cout << passed << " engine checks passed\n";
  return 0;
}
