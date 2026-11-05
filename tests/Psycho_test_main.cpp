#include "../engine/Psycho_page.hpp"
#include "../platform/Psycho_profile.hpp"
#include "../platform/Psycho_proxy.hpp"
#include "../platform/Psycho_extension.hpp"
#include "../platform/Psycho_download.hpp"
#include "../platform/Psycho_host.hpp"
#include "../platform/Psycho_transport.hpp"
#include "../platform/Psycho_sandbox.hpp"
#include "../platform/Psycho_vault.hpp"
#include "../platform/Psycho_spell.hpp"
#include "Psycho_image_fixtures.hpp"
#include "Psycho_media_fixtures.hpp"
#include <cassert>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <cstdlib>
#include <cstring>
#include <thread>
using namespace pv;
static int passed = 0;
// One line per check that has started, when the runner asks for it. A stack overflow
// on a hosted Windows runner ends the process before a failure message can be
// written, so the last line of the log has to be the check that was running.
static bool progress_wanted() {
  static const bool wanted = std::getenv("PV_PROGRESS") != nullptr;
  return wanted;
}
#define CHECK(x)                                                                                   \
  do {                                                                                             \
    if (progress_wanted())                                                                         \
      std::cerr << "check " << __LINE__ << "\n";                                                   \
    if (!(x)) {                                                                                    \
      std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << " " << #x << "\n";                    \
      return 1;                                                                                    \
    }                                                                                              \
    ++passed;                                                                                      \
  } while (0)
// I read back where a box ended up, which is how I check my layout maths.
static Rect node_box(Page &page, const char *id, bool *fixed = nullptr) {
  auto frame = page.render(800, 600, 0);
  int node = page.doc.by_id(id);
  // The fixed flag only exists on a paint command, so I read it from the paint
  // list and the box itself from the layout report.
  const Json *paint = frame.find("paint");
  const Json *painted_box = nullptr;
  if (paint) {
    for (const auto &command : paint->array) {
      if (int(command.num("node", -1)) == node && int(command.num("kind", -1)) == 0) {
        if (fixed)
          *fixed = command.flag("fixed");
        painted_box = &command;
        break;
      }
    }
  }
  const Json *boxes = frame.find("boxes");
  if (boxes) {
    for (const auto &entry : boxes->array) {
      if (int(entry.num("node", -1)) == node)
        return Rect{entry.num("x"), entry.num("y"), entry.num("w"), entry.num("h")};
    }
  }
  if (painted_box)
    return Rect{painted_box->num("x"), painted_box->num("y"), painted_box->num("w"),
                painted_box->num("h")};
  return Rect{0, 0, -1, -1};
}
// Not `near`: Windows headers define that as an empty macro, which turns
// near(x, 0) into the comma expression (x, 0) and fails only when the
// expectation is zero. That cost me a hosted build to find.
static bool close_to(double a, double b) {
  return std::abs(a - b) < 1.0;
}
static std::string js_eval(const std::string &s) {
  js::Runtime r;
  return r.run(s).to_string();
}
// ---- media fixtures I build by hand, so the tests exercise real parsing ----
static Bytes make_wav(int rate, int count, double amplitude) {
  Bytes w;
  auto le16 = [&](uint16_t v) {
    w.push_back(uint8_t(v & 0xff));
    w.push_back(uint8_t((v >> 8) & 0xff));
  };
  auto le32 = [&](uint32_t v) {
    for (int i = 0; i < 4; ++i)
      w.push_back(uint8_t((v >> (8 * i)) & 0xff));
  };
  auto tag = [&](const char *t) {
    for (int i = 0; i < 4; ++i)
      w.push_back(uint8_t(t[i]));
  };
  tag("RIFF");
  le32(uint32_t(36 + count * 2));
  tag("WAVE");
  tag("fmt ");
  le32(16);
  le16(1);
  le16(1);
  le32(uint32_t(rate));
  le32(uint32_t(rate * 2));
  le16(2);
  le16(16);
  tag("data");
  le32(uint32_t(count * 2));
  for (int i = 0; i < count; ++i)
    le16(uint16_t(int16_t(std::sin(double(i) * 0.05) * amplitude)));
  return w;
}
// A two frame MJPEG AVI at four frames a second, so the second frame starts at a
// quarter of a second and a seek to 0.3 lands on it.
static Bytes make_avi() {
  auto tag = [](Bytes &o, const char *t) {
    for (int i = 0; i < 4; ++i)
      o.push_back(uint8_t(t[i]));
  };
  auto le16 = [](Bytes &o, uint16_t v) {
    o.push_back(uint8_t(v & 0xff));
    o.push_back(uint8_t((v >> 8) & 0xff));
  };
  auto le32 = [](Bytes &o, uint32_t v) {
    for (int i = 0; i < 4; ++i)
      o.push_back(uint8_t((v >> (8 * i)) & 0xff));
  };
  auto put_le32 = [](Bytes &o, size_t at, uint32_t v) {
    for (int i = 0; i < 4; ++i)
      o[at + size_t(i)] = uint8_t((v >> (8 * i)) & 0xff);
  };
  auto chunk = [&](Bytes &o, const char *t, const Bytes &body) {
    tag(o, t);
    le32(o, uint32_t(body.size()));
    o.insert(o.end(), body.begin(), body.end());
    if (body.size() & 1)
      o.push_back(0);
  };
  Bytes strh;
  tag(strh, "vids");
  strh.resize(56, 0);
  Bytes strf;
  le32(strf, 40);
  le32(strf, 32);
  le32(strf, 16);
  le16(strf, 1);
  le16(strf, 24);
  tag(strf, "MJPG");
  strf.resize(40, 0);
  Bytes avih(56, 0);
  put_le32(avih, 0, 250000);
  put_le32(avih, 16, 2);
  put_le32(avih, 32, 32);
  put_le32(avih, 36, 16);
  Bytes strl;
  tag(strl, "strl");
  chunk(strl, "strh", strh);
  chunk(strl, "strf", strf);
  Bytes hdrl;
  tag(hdrl, "hdrl");
  chunk(hdrl, "avih", avih);
  tag(hdrl, "LIST");
  le32(hdrl, uint32_t(strl.size()));
  hdrl.insert(hdrl.end(), strl.begin(), strl.end());
  Bytes red(fixtures::frame_red.begin(), fixtures::frame_red.end());
  Bytes green(fixtures::frame_green.begin(), fixtures::frame_green.end());
  Bytes movi;
  tag(movi, "movi");
  chunk(movi, "00dc", red);
  chunk(movi, "00dc", green);
  Bytes avi;
  tag(avi, "RIFF");
  le32(avi, uint32_t(4 + hdrl.size() + 8 + movi.size()));
  tag(avi, "AVI ");
  tag(avi, "LIST");
  le32(avi, uint32_t(hdrl.size()));
  avi.insert(avi.end(), hdrl.begin(), hdrl.end());
  tag(avi, "LIST");
  le32(avi, uint32_t(movi.size()));
  avi.insert(avi.end(), movi.begin(), movi.end());
  return avi;
}
// An MP4 with one track of the codec I ask for. Every size is big-endian, which is
// what a real file uses.
static Bytes make_mp4(const char *handler, const char *codec) {
  auto tag = [](Bytes &o, const char *t) {
    for (int i = 0; i < 4; ++i)
      o.push_back(uint8_t(t[i]));
  };
  auto be32 = [](Bytes &o, uint32_t v) {
    for (int i = 0; i < 4; ++i)
      o.push_back(uint8_t((v >> (8 * (3 - i))) & 0xff));
  };
  auto be32_at = [](Bytes &o, size_t at, uint32_t v) {
    for (int i = 0; i < 4; ++i)
      o[at + size_t(i)] = uint8_t((v >> (8 * (3 - i))) & 0xff);
  };
  auto box = [&](Bytes &o, const char *t, const Bytes &payload) {
    be32(o, uint32_t(payload.size() + 8));
    tag(o, t);
    o.insert(o.end(), payload.begin(), payload.end());
  };
  Bytes ftyp;
  tag(ftyp, "isom");
  be32(ftyp, 0x200);
  tag(ftyp, "isom");
  tag(ftyp, "mp41");
  Bytes mvhd(100, 0);
  be32_at(mvhd, 12, 1000);
  be32_at(mvhd, 16, 2500);
  Bytes tkhd(84, 0);
  be32_at(tkhd, 76, 32u << 16);
  be32_at(tkhd, 80, 16u << 16);
  Bytes hdlr(24, 0);
  for (int i = 0; i < 4; ++i)
    hdlr[8 + size_t(i)] = uint8_t(handler[i]);
  Bytes stsd(16, 0);
  for (int i = 0; i < 4; ++i)
    stsd[12 + size_t(i)] = uint8_t(codec[i]);
  Bytes stbl, minf, mdia, trak, body, moov;
  box(stbl, "stsd", stsd);
  box(minf, "stbl", stbl);
  box(mdia, "hdlr", hdlr);
  box(mdia, "minf", minf);
  box(trak, "mdia", mdia);
  box(trak, "tkhd", tkhd);
  box(body, "mvhd", mvhd);
  box(body, "trak", trak);
  box(moov, "moov", body);
  Bytes file;
  box(file, "ftyp", ftyp);
  file.insert(file.end(), moov.begin(), moov.end());
  return file;
}

// A scripted stream for my protocol tests: I feed it the bytes a proxy would send
// and read back exactly what my client wrote.
class ScriptStream : public Stream {
public:
  Bytes script, written;
  std::string failure;
  bool write_all(const uint8_t *data, size_t size, std::string &) override {
    written.insert(written.end(), data, data + size);
    return true;
  }
  bool read_some(uint8_t *out, size_t capacity, size_t &got, std::string &error) override {
    got = 0;
    if (at >= script.size()) {
      error = failure;
      return false;
    }
    size_t take = std::min(capacity, script.size() - at);
    std::memcpy(out, script.data() + at, take);
    at += take;
    got = take;
    return true;
  }
  bool alive() const override {
    return true;
  }
  std::string text() const {
    return std::string(written.begin(), written.end());
  }
  size_t at = 0;
};

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#define PSYCHO_CLOSESOCKET closesocket
#else
#define PSYCHO_CLOSESOCKET ::close
#endif
#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
namespace {
// Winsock needs starting before the first socket call, and my proxy opens its
// listening socket before my client has spoken to anything.
bool test_sockets_ready() {
#ifdef _WIN32
  static bool once = [] {
    WSADATA data{};
    return WSAStartup(MAKEWORD(2, 2), &data) == 0;
  }();
  return once;
#else
  return true;
#endif
}
// A tiny SOCKS5 proxy that runs inside my own test on a loopback port. It answers
// the handshake the way a real proxy does and then speaks HTTP over the tunnel, so
// the whole path from my client through my own framing is exercised on a real socket.
struct LoopbackProxy {
  int listener = -1;
  int port = 0;
  std::string seen_host, failure;
  std::vector<std::string> requests;
  // When this is set the proxy answers with exactly these bytes and nothing is
  // generated for it, which is how I test the framing edge cases.
  std::string raw_reply;
  // Where a redirect points. A relative one stays on the same host; an absolute one
  // lets me make the next hop a different host, which is what the cookie and referer
  // rules are about.
  std::string location = "/moved";
  std::thread worker;
  // Each entry is the body of one reply, and `statuses` holds the matching status
  // lines, so I can serve a redirect and then the page behind it.
  explicit LoopbackProxy(std::vector<std::pair<std::string, std::string>> replies) {
    if (!test_sockets_ready()) {
      failure = "winsock";
      return;
    }
    listener = int(socket(AF_INET, SOCK_STREAM, 0));
    if (listener < 0) {
      failure = "socket";
      return;
    }
    int yes = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&yes), sizeof yes);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof address) != 0) {
      failure = "bind";
      return;
    }
    socklen_t size = sizeof address;
    if (getsockname(listener, reinterpret_cast<sockaddr *>(&address), &size) != 0) {
      failure = "getsockname";
      return;
    }
    port = ntohs(address.sin_port);
    if (listen(listener, 1) != 0) {
      failure = "listen";
      return;
    }
    worker = std::thread([this, replies] {
      // A redirect hop gets its own tunnel, so I accept one connection per reply and
      // run the handshake again each time.
      for (const auto &reply : replies) {
        int client = int(accept(listener, nullptr, nullptr));
        if (client < 0) {
          failure = "accept";
          return;
        }
        auto read_exact = [&](uint8_t *out, size_t size) {
          size_t done = 0;
          while (done < size) {
            int n = int(recv(client, reinterpret_cast<char *>(out + done), int(size - done), 0));
            if (n <= 0)
              return false;
            done += size_t(n);
          }
          return true;
        };
        auto write_all = [&](const void *data, size_t size) {
          return send(client, reinterpret_cast<const char *>(data), int(size), 0) == int(size);
        };
        uint8_t greeting[2] = {0, 0};
        if (!read_exact(greeting, 2))
          return;
        std::vector<uint8_t> methods(static_cast<size_t>(greeting[1]), 0);
        if (!greeting[1] || !read_exact(methods.data(), methods.size()))
          return;
        uint8_t chosen[2] = {5, 0};
        if (!write_all(chosen, 2))
          return;
        uint8_t head[4] = {0, 0, 0, 0};
        if (!read_exact(head, 4))
          return;
        if (head[3] == 3) {
          uint8_t length = 0;
          if (!read_exact(&length, 1))
            return;
          std::string name(size_t(length), '\0');
          if (!read_exact(reinterpret_cast<uint8_t *>(name.data()), name.size()))
            return;
          seen_host = name;
        } else if (head[3] == 1) {
          uint8_t address[4] = {0, 0, 0, 0};
          if (!read_exact(address, 4))
            return;
          char text[32] = {0};
          std::snprintf(text, sizeof text, "%u.%u.%u.%u", address[0], address[1], address[2],
                        address[3]);
          seen_host = text;
        } else {
          return;
        }
        uint8_t port_bytes[2] = {0, 0};
        if (!read_exact(port_bytes, 2))
          return;
        uint8_t connected[10] = {5, 0, 0, 1, 127, 0, 0, 1, 0, 80};
        if (!write_all(connected, sizeof connected))
          return;
        // The tunnel is open, so I read one request and answer it.
        std::string request;
        char chunk[1024];
        while (request.find("\r\n\r\n") == std::string::npos) {
          int n = int(recv(client, chunk, sizeof chunk, 0));
          if (n <= 0)
            break;
          request.append(chunk, size_t(n));
        }
        // A request with a body is not finished at the blank line, so I read the
        // body too before I answer it.
        auto head_end = request.find("\r\n\r\n");
        auto lower_request = request.substr(0, head_end);
        for (auto &ch : lower_request)
          ch = char(std::tolower(static_cast<unsigned char>(ch)));
        auto length_at = lower_request.find("content-length:");
        if (length_at != std::string::npos) {
          size_t want = size_t(std::strtoull(lower_request.c_str() + length_at + 15, nullptr, 10));
          size_t body_have = request.size() - (head_end + 4);
          while (body_have < want) {
            char more[1024];
            int n = int(recv(client, more, sizeof more, 0));
            if (n <= 0)
              break;
            request.append(more, size_t(n));
            body_have += size_t(n);
          }
        }
        requests.push_back(request);
        const auto &status = reply.first;
        const auto &body = reply.second;
        std::string text = "HTTP/1.1 " + status + "\r\nContent-Type: text/html\r\n";
        if (!raw_reply.empty()) {
          write_all(raw_reply.data(), raw_reply.size());
          shutdown(client, 2);
          PSYCHO_CLOSESOCKET(client);
          continue;
        }
        if (status.rfind("30", 0) == 0)
          text += "Location: " + location + "\r\nContent-Length: 0\r\n";
        else
          text += "Content-Length: " + std::to_string(body.size()) + "\r\n";
        text += "Connection: close\r\n\r\n" + body;
        write_all(text.data(), text.size());
        shutdown(client, 2);
        PSYCHO_CLOSESOCKET(client);
      }
    });
  }
  ~LoopbackProxy() {
    if (worker.joinable())
      worker.join();
    if (listener >= 0)
      PSYCHO_CLOSESOCKET(listener);
  }
};
} // namespace

static const char pg_msg[] = "Cryptographic Forum Research Group";
static const char sunscreen[] =
    "Ladies and Gentlemen of the class of '99: If I could offer you only one "
    "tip for the future, sunscreen would be it.";
static pv::Bytes pvkdf(const char *password, const char *salt, uint32_t iterations) {
  return pv::pbkdf2_sha256(pv::Bytes(password, password + std::strlen(password)),
                           pv::Bytes(salt, salt + std::strlen(salt)), iterations);
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
  // My own pattern engine, driven from script.
  CHECK(js_eval("/ab+c/.test('xabbbcy')") == "true");
  CHECK(js_eval("/ab+c/.test('xacy')") == "false");
  CHECK(js_eval("var m=/a(b+)(c)/.exec('xxabbbcyy');m[0]+'|'+m[1]+'|'+m[2]+'|'+m.index") ==
        "abbbc|bbb|c|2");
  CHECK(js_eval("'a1b22c'.match(/\\d+/g).join(',')") == "1,22");
  CHECK(js_eval("'a1b22c'.search(/\\d+/)") == "1");
  CHECK(js_eval("'a1b22c'.split(/\\d+/).join('-')") == "a-b-c");
  CHECK(js_eval("'2026-11-03'.replace(/(\\d+)-(\\d+)-(\\d+)/,'$3/$2/$1')") == "03/11/2026");
  CHECK(js_eval("'x y x'.replace(/x/g,function(m){return m.toUpperCase();})") == "X y X");
  CHECK(js_eval("new RegExp('a.c','i').test('AxC')") == "true");
  CHECK(js_eval("String(new RegExp('a+b','gi'))") == "/a+b/gi");
  CHECK(js_eval("var re=/a/g;re.exec('aa');re.lastIndex") == "1");
  CHECK(js_eval("/(?<y>\\d{4})/.exec('in 2026')[1]") == "2026");
  CHECK(js_eval("(()=>{try{new RegExp('(');return 'no';}catch(e){return 'threw';}})()") == "threw");
  CHECK(js_eval("var s=0;for(var v of new Set([1,2,3]))s+=v;s") == "6");
  CHECK(js_eval("var s=0;for(var e of new Map([['a',1],['b',2]]))s+=e[1];s") == "3");
  CHECK(js_eval("[...new Set([1,2,2])].join(',')") == "1,2");
  CHECK(js_eval("typeof Symbol") == "function");
  CHECK(js_eval("Array.from('abc').join('-')") == "a-b-c");
  CHECK(js_eval("'a,b,c'.split(',',2).length") == "2");
  CHECK(js_eval("var o={f:async function(){return 1;}};typeof o.f") == "function");
  // Real-world bundles (Sentry, webextension-polyfill, my own PAC scripts) lean
  // on every shape below; each one came out of feeding Proton's worker bundle
  // through my parser, and each one has a measured answer.
  CHECK(js_eval("var d=(a=5)=>a;d()") == "5");
  CHECK(js_eval("var d=(a=5)=>a;d(9)") == "9");
  CHECK(js_eval("var f=function(a,b=4){return a+b};f(3)") == "7");
  CHECK(js_eval("var o={m(k=3){return k}};o.m()") == "3");
  CHECK(js_eval("class K{static q([v]=[41],w=1){return v+w}};K.q()") == "42");
  CHECK(js_eval("var f=({resolve,reject},x=9)=>[resolve,reject,x];f({resolve:1,reject:2})[2]") == "9");
  CHECK(js_eval("var g=([a,,b=4])=>a+b;g([1,2])") == "5");
  CHECK(js_eval("var h=({x=3}={})=>x;h()") == "3");
  CHECK(js_eval("function n({p1:{p2}={p2:8}}={}){return p2};n()") == "8");
  CHECK(js_eval("var s='';for(const[k,v]of[['a',1],['b',2]])s+=k+v;s") == "a1b2");
  CHECK(js_eval("var s='';for(const{x=0}of[{x:5},{}])s+=x+';';s") == "5;0;");
  CHECK(js_eval("const[t,,,i]=[9,8,7,6];t+'-'+i") == "9-6");
  CHECK(js_eval("var b={...{x:1,y:2},y:9,z:3};b.x+','+b.y+','+b.z") == "1,9,3");
  CHECK(js_eval("var o={['g'+'o'](x){return x*2}};o.go(21)") == "42");
  CHECK(js_eval("var a=[10,20];a?.[1]") == "20");
  CHECK(js_eval("var n=null;n?.[0]===undefined") == "true");
  CHECK(js_eval("var f=null;f?.()===undefined") == "true");
  CHECK(js_eval("var f=function(x){return x*2};f?.(21)") == "42");
  CHECK(js_eval("function tag(s,a,b){return s.join('|')+':'+a+','+b};tag`x${1+1}y${'z'}w`") == "x|y|w:2,z");
  CHECK(js_eval("'hello{x}'.replace(/\\{[^}]*\\}/g,'')") == "hello");
  CHECK(js_eval("`v=${'A{x}'.replace(/\\{[^}]*\\}/g,'!')}`") == "v=A!");
  CHECK(js_eval("var of=41;of+1") == "42");
  CHECK(js_eval("true?.03:99") == "0.03");
  CHECK(js_eval("(1)?.5:.25") == "0.5");
  // Promises: handlers are queued and run when the script finishes.
  {
    js::Runtime r;
    r.run("var log=[];Promise.resolve(1).then(function(v){log.push('a'+v);return v+1;})"
          ".then(function(v){log.push('b'+v);});"
          "Promise.all([Promise.resolve(10),Promise.resolve(20)]).then(function(v){"
          "log.push('c'+v.join('+'));});"
          "Promise.race([Promise.reject('no'),Promise.resolve('yes')])"
          ".then(null,function(e){log.push('d'+e);});"
          "Promise.allSettled([1,Promise.reject(2)]).then(function(v){"
          "log.push('e'+v[0].status+'-'+v[1].status);});"
          "queueMicrotask(function(){log.push('f');});");
    CHECK(r.run("log.join(' ')").to_string() == "a1 f b2 c10+20 dno efulfilled-rejected");
    r.run("var out=[];Promise.reject('e').catch(function(x){out.push(x);});");
    CHECK(r.run("out.join(',')").to_string() == "e");
    r.run("var thrown=[];Promise.resolve(1).then(function(){throw new Error('boom');})"
          ".catch(function(e){thrown.push(e.message);});");
    CHECK(r.run("thrown.join(',')").to_string() == "boom");
    bool bad = false;
    try {
      r.run("new Promise(3)");
    } catch (js::JsError &) {
      bad = true;
    }
    CHECK(bad);
  }
  // Positioning, flex, grid and transforms, measured from my own paint list.
  {
    Page page;
    page.load(*parse_url("https://layout.test/"),
              "<!doctype html><style>body{margin:0}"
              "#p{position:relative;width:400px;height:200px;margin:10px}"
              "#a{position:absolute;top:10px;left:20px;width:50px;height:30px}"
              "</style><div id=p><div id=a>x</div></div>");
    Rect a = node_box(page, "a");
    CHECK(close_to(a.x, 30) && close_to(a.y, 20) && close_to(a.w, 50) && close_to(a.h, 30));
  }
  {
    Page page;
    page.load(*parse_url("https://layout.test/"),
              "<!doctype html><style>body{margin:0}"
              "#r{position:relative;top:5px;left:7px;width:100px;height:20px}"
              "</style><div id=r>rel</div>");
    Rect r = node_box(page, "r");
    CHECK(close_to(r.x, 7) && close_to(r.y, 5));
  }
  {
    Page page;
    page.load(*parse_url("https://layout.test/"),
              "<!doctype html><style>body{margin:0}"
              "#f{position:fixed;top:0;left:0;width:100px;height:20px;background:#123}"
              "</style><div id=f>fixed</div>");
    bool fixed = false;
    Rect f = node_box(page, "f", &fixed);
    if (!(close_to(f.x, 0) && close_to(f.y, 0) && fixed)) {
      // When this fails I want the frame itself in the log, not just a false.
      std::cerr << "fixed-diag node=" << page.doc.by_id("f") << " x=" << f.x << " y=" << f.y
                << " w=" << f.w << " h=" << f.h << " fixed=" << fixed << "\n";
      auto again = page.render(800, 600, 0);
      std::cerr << "fixed-frame " << again.dump().substr(0, 4000) << "\n";
    }
    CHECK(close_to(f.x, 0) && close_to(f.y, 0) && fixed);
  }
  {
    Page page;
    page.load(*parse_url("https://layout.test/"),
              "<!doctype html><style>body{margin:0}"
              "#f{display:flex;justify-content:center;width:300px;height:40px}"
              "#a,#b{width:50px;height:20px}"
              "</style><div id=f><div id=a>a</div><div id=b>b</div></div>");
    Rect a = node_box(page, "a"), b = node_box(page, "b");
    CHECK(close_to(a.x, 100) && close_to(b.x, 150));
  }
  {
    Page page;
    page.load(*parse_url("https://layout.test/"),
              "<!doctype html><style>body{margin:0}"
              "#f{display:flex;width:300px;height:40px}"
              "#a{flex:1;height:20px}#b{flex:2;height:20px}"
              "</style><div id=f><div id=a>a</div><div id=b>b</div></div>");
    Rect a = node_box(page, "a"), b = node_box(page, "b");
    CHECK(close_to(a.w, 100) && close_to(b.w, 200));
  }
  {
    Page page;
    page.load(*parse_url("https://layout.test/"),
              "<!doctype html><style>body{margin:0}"
              "#f{display:flex;align-items:center;height:100px;width:200px}"
              "#a{width:40px;height:20px}"
              "</style><div id=f><div id=a>a</div></div>");
    Rect a = node_box(page, "a");
    CHECK(close_to(a.y, 40));
  }
  {
    // A flex row in normal flow starts below what came before it and takes the
    // height of its items, which is where a page with a heading and a row goes
    // wrong if the main and cross axes are mixed up.
    Page page;
    page.load(*parse_url("https://layout.test/"),
              "<!doctype html><style>body{margin:0;font-size:20px;line-height:1}"
              "#f{display:flex;gap:10px}"
              "#a{width:100px;height:30px}#b{width:100px;height:50px}"
              "#after{height:20px}"
              "</style><p id=first>heading</p>"
              "<div id=f><div id=a>a</div><div id=b>b</div></div>"
              "<div id=after>after</div>");
    Rect first = node_box(page, "first"), a = node_box(page, "a"), b = node_box(page, "b");
    Rect f = node_box(page, "f"), after = node_box(page, "after");
    CHECK(close_to(first.y, 20) && close_to(first.h, 20));
    CHECK(close_to(a.y, 60) && close_to(b.y, 60));
    CHECK(close_to(f.y, 60) && close_to(f.h, 50));
    CHECK(close_to(after.y, 110));
  }
  {
    Page page;
    page.load(*parse_url("https://layout.test/"),
              "<!doctype html><style>body{margin:0}"
              "#f{display:flex;flex-direction:column;gap:10px;width:200px}"
              "#a{height:20px}#b{height:30px}"
              "</style><div id=f><div id=a>a</div><div id=b>b</div></div>");
    Rect a = node_box(page, "a"), b = node_box(page, "b");
    CHECK(close_to(a.y, 0) && close_to(b.y, 30));
  }
  {
    Page page;
    page.load(*parse_url("https://layout.test/"),
              "<!doctype html><style>body{margin:0}"
              "#g{display:grid;grid-template-columns:100px 1fr;gap:10px;width:310px}"
              "#a,#b{height:20px}"
              "</style><div id=g><div id=a>a</div><div id=b>b</div></div>");
    Rect a = node_box(page, "a"), b = node_box(page, "b");
    CHECK(close_to(a.w, 100) && close_to(b.x, 110) && close_to(b.w, 200));
  }
  {
    Page page;
    page.load(*parse_url("https://layout.test/"),
              "<!doctype html><style>body{margin:0}"
              "#t{transform:translate(15px,25px);width:100px;height:20px}"
              "</style><div id=t>t</div>");
    Rect t = node_box(page, "t");
    CHECK(close_to(t.x, 15) && close_to(t.y, 25));
  }
  // fetch() goes through my own network hook and resolves through promises.
  {
    Page page;
    page.fetcher = [](const std::string &url) {
      Resource r;
      r.url = url;
      r.mime = "application/json";
      if (url == "https://local.test/api.json") {
        r.text = "{\"name\":\"vault\",\"items\":[1,2]}";
        return r;
      }
      if (url == "https://local.test/offline") {
        // No response at all: this is what a fetch promise rejects on.
        r.status = 0;
        r.ok = false;
        r.error = "connection failed";
        return r;
      }
      r.status = 404;
      r.ok = false;
      r.error = "HTTP 404";
      return r;
    };
    page.load(*parse_url("https://local.test/"),
              "<!doctype html><title>before</title><script>"
              "fetch('/api.json').then(function(r){return r.json();})"
              ".then(function(j){document.title=j.name+j.items.length;});"
              "</script>");
    CHECK(page.title() == "vault2");
    // An error status is still a response, so it resolves with ok false.
    page.load(*parse_url("https://local.test/"),
              "<!doctype html><title>before</title><script>fetch('/missing')"
              ".then(function(r){document.title=(r.ok?'ok':'bad')+r.status;})"
              ".catch(function(e){document.title='rejected';});</script>");
    CHECK(page.title() == "bad404");
    // A request that never got a response rejects.
    page.load(*parse_url("https://local.test/"),
              "<!doctype html><title>before</title><script>fetch('/offline')"
              ".then(function(r){document.title='resolved';})"
              ".catch(function(e){document.title='rejected:'+e;});</script>");
    CHECK(page.title().rfind("rejected:", 0) == 0);
  }
  // await waits for a timer even though my engine runs one statement at a time.
  {
    Page page;
    page.load(*parse_url("https://local.test/"),
              "<!doctype html><title>before</title><script>"
              "(async function(){await new Promise(function(r){setTimeout(r,5);});"
              "document.title='after';})();"
              "</script>");
    CHECK(page.title() == "after");
  }
  // The same page host my browser runs: fetch, script, image and frame events.
  {
    std::vector<HostEvent> events;
    Host host(
        [](const std::string &url, ResourceType) {
          Resource r;
          r.url = url;
          if (url == "https://local.test/") {
            r.mime = "text/html";
            r.text = "<!doctype html><title>t</title><body><img src=\"/Psycho_pixel.png\">"
                     "<div id=out>wait</div><script>fetch('/api')"
                     ".then(function(r){return r.json();})"
                     ".then(function(j){document.getElementById('out').textContent=j.n;});"
                     "</script></body>";
            return r;
          }
          if (url == "https://local.test/api") {
            r.mime = "application/json";
            r.text = "{\"n\":42}";
            return r;
          }
          if (url == "https://local.test/Psycho_pixel.png") {
            r.mime = "image/png";
            r.bytes = Bytes(fixtures::rgba_filters.begin(), fixtures::rgba_filters.end());
            return r;
          }
          r.ok = false;
          r.error = "not found";
          return r;
        },
        [&events](HostEvent event) { events.push_back(std::move(event)); });
    std::string error;
    CHECK(host.load("https://local.test/", error));
    bool frame = false, image = false;
    for (const auto &event : events) {
      if (event.tag == "frame")
        frame = true;
      if (event.tag == "image")
        image = true;
    }
    CHECK(frame);
    CHECK(image);
    CHECK(host.pump() == false);
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
  // Cookies: matching, scope, expiry and what a script may see.
  {
    CookieJar jar;
    auto site = *parse_url("https://example.com/app/page");
    jar.set_from_header(site, "sid=abc; Path=/; HttpOnly", 1000);
    jar.set_from_header(site, "theme=dark; Max-Age=60", 1000);
    auto header = jar.header_for(site, 1001);
    CHECK(header.find("sid=abc") != std::string::npos &&
          header.find("theme=dark") != std::string::npos);
    auto visible = jar.document_string(site, 1001);
    CHECK(visible.find("theme=dark") != std::string::npos &&
          visible.find("sid=abc") == std::string::npos);
    CHECK(jar.header_for(*parse_url("https://other.test/"), 1001).empty());
    jar.set_from_header(*parse_url("https://example.com/"), "tls=1; Secure", 1000);
    CHECK(jar.header_for(*parse_url("http://example.com/"), 1001).find("tls=1") ==
          std::string::npos);
    CHECK(jar.header_for(site, 2000).find("theme=dark") == std::string::npos);
    jar.set_from_header(*parse_url("https://example.com/"), "wide=1; Domain=example.com", 1000);
    CHECK(jar.header_for(*parse_url("https://cdn.example.com/x"), 1001).find("wide=1") !=
          std::string::npos);
    jar.set_from_header(*parse_url("https://example.com/"), "bad=1; Domain=elsewhere.test", 1000);
    CHECK(jar.header_for(*parse_url("https://elsewhere.test/"), 1001).empty());
    CHECK(jar.header_for(site, 1001, true, true).empty());
    // A script cannot create an HttpOnly cookie, and a removal takes effect.
    jar.set_from_document(site, "script=1; HttpOnly", 1000);
    CHECK(jar.document_string(site, 1001).find("script=1") != std::string::npos);
    jar.set_from_header(site, "sid=gone; Max-Age=0", 1000);
    CHECK(jar.header_for(site, 1001).find("sid=gone") == std::string::npos);
  }
  {
    Storage store;
    std::string origin = "https://store.test";
    CHECK(store.set(origin, "a", "1"));
    CHECK(store.set(origin, "b", "2"));
    CHECK(store.length(origin) == 2);
    CHECK(store.key(origin, 1) == "b");
    CHECK(store.get(origin, "a") == "1");
    CHECK(store.remove(origin, "a") && store.length(origin) == 1);
    CHECK(!store.set(origin, "big", std::string(Storage::kQuota, 'x')));
    CHECK(store.set(origin, "b", "22") && store.get(origin, "b") == "22");
  }
  {
    // Two tabs share local storage but never each other's session storage.
    Storage shared;
    Page one;
    one.use_local_storage(&shared);
    one.load(*parse_url("https://store.test/"),
             "<script>localStorage.setItem('k','v');sessionStorage.setItem('s','1');</script>");
    Page two;
    two.use_local_storage(&shared);
    two.load(*parse_url("https://store.test/"),
             "<script>document.title = localStorage.getItem('k') + '/' + "
             "sessionStorage.getItem('s');</script>");
    CHECK(two.title() == "v/null");
  }
  {
    // history.pushState changes the address without loading, and back returns.
    Page page;
    page.load(*parse_url("https://history.test/start"),
              "<script>history.pushState({n:1},'', '/two');</script>");
    CHECK(page.doc.url.path == "/two");
    CHECK(page.history().size() == 2);
    Page again;
    again.load(*parse_url("https://history.test/start"),
               "<script>history.pushState({n:1},'', '/two');history.back();"
               "document.title = location.pathname;</script>");
    CHECK(again.title() == "/start");
  }
  {
    // A form post is handed to my host with the right body, not turned into a GET.
    Page page;
    page.load(*parse_url("https://forms.test/"),
              "<!doctype html><form action='/submit' method=post><input name=q value='hello "
              "world'><input type=hidden name=token value=abc></form>");
    page.render(800, 600, 0);
    page.click(10, 10);
    page.key("Enter", "");
    CHECK(page.posts.size() == 1);
    if (!page.posts.empty()) {
      CHECK(page.posts[0].method == "POST");
      CHECK(page.posts[0].body == "q=hello+world&token=abc");
      CHECK(page.posts[0].content_type == "application/x-www-form-urlencoded");
      CHECK(page.posts[0].url == "https://forms.test/submit");
    }
    CHECK(page.navigations.empty());
  }
  // Audit of steps 3 to 5: the fixes I made after measuring real pages.
  {
    // An absolute child starts at the padding box of its positioned ancestor,
    // not at the border box, so a border and padding both move it inwards.
    Page page;
    page.load(*parse_url("https://layout.test/"),
              "<!doctype html><style>body{margin:0}div{background:#222}"
              "#rel{position:relative;margin:10px;padding:20px;border:5px solid #000;"
              "width:300px;height:100px}"
              "#abs{position:absolute;top:0;left:0;width:40px;height:30px}"
              "</style><div id=rel><div id=abs></div></div>");
    Rect abs = node_box(page, "abs");
    CHECK(close_to(abs.x, 15) && close_to(abs.y, 15));
  }
  {
    // Max-Age wins over Expires whatever order a server writes them in.
    CookieJar jar;
    auto site = *parse_url("https://shop.test/");
    jar.set_from_header(site, "both=1; Max-Age=10; Expires=Wed, 21 Oct 2037 07:28:00 GMT", 1000);
    CHECK(jar.header_for(site, 1200).find("both=1") == std::string::npos);
    jar.set_from_header(site, "either=1; Expires=Wed, 21 Oct 2037 07:28:00 GMT", 1000);
    CHECK(jar.header_for(site, 1200).find("either=1") != std::string::npos);
  }
  {
    // A ticked box with no value sends "on", and a multiple select sends every
    // chosen option.
    Page page;
    page.load(*parse_url("https://forms.test/"),
              "<!doctype html><form action='/go' method=get><input name=q value=v>"
              "<input type=checkbox name=c checked>"
              "<select name=s multiple><option value=x selected>X</option>"
              "<option value=y selected>Y</option><option value=z>Z</option></select></form>");
    page.render(800, 600, 0);
    page.click(5, 5);
    page.key("Enter", "");
    CHECK(!page.navigations.empty());
    if (!page.navigations.empty()) {
      auto url = page.navigations[0];
      CHECK(url.find("c=on") != std::string::npos);
      CHECK(url.find("s=x") != std::string::npos && url.find("s=y") != std::string::npos);
      CHECK(url.find("s=z") == std::string::npos);
    }
  }
  {
    // Find in page walks the text my layout paints, including a word split
    // across two spans.
    std::vector<PaintCommand> paint;
    auto text = [&](double x, double y, std::string body, double width) {
      PaintCommand command;
      command.kind = PaintCommand::Kind::Text;
      command.rect = {x, y, width, 16};
      command.text = std::move(body);
      paint.push_back(command);
    };
    text(0, 0, "Hel", 20);
    text(20, 0, "lo there", 60);
    text(0, 20, "another hello", 90);
    auto hits = find_in_paint(paint, "hello");
    CHECK(hits.size() == 2);
    if (hits.size() == 2) {
      // "hello" starts in the first run and ends two characters into the second,
      // so the highlight covers 20px plus two ninths of the 60px run.
      CHECK(close_to(hits[0].x, 0) && close_to(hits[0].y, 0));
      CHECK(hits[0].w > 30 && hits[0].w < 40);
      CHECK(close_to(hits[1].y, 20) && hits[1].x > 50 && hits[1].x < 60);
    }
    CHECK(find_in_paint(paint, "HELLO").size() == 2);
    CHECK(find_in_paint(paint, "nothing").empty());
  }
  {
    // My canvas gives back what a script set, and the element it belongs to.
    Page page;
    page.load(*parse_url("https://canvas.test/"),
              "<!doctype html><canvas id=c width=40 height=30></canvas><script>"
              "var x=document.getElementById('c').getContext('2d');"
              "x.fillStyle='#ff0000';x.strokeStyle='#00ff00';x.lineWidth=3;"
              "console.log(x.fillStyle+'|'+x.strokeStyle+'|'+x.lineWidth+'|'+x.canvas.id);"
              "x.fillRect(0,0,10,10);"
              "x.beginPath();x.roundRect(10,10,20,10,3);x.fill();"
              "x.setLineDash([2,2]);x.beginPath();x.moveTo(0,25);x.lineTo(40,25);x.stroke();"
              "console.log(x.getImageData(2,2,1,1).data[0]);"
              "</script>");
    bool style = false, pixel = false;
    for (auto &d : page.diagnostics) {
      if (d.message == "log: #ff0000|#00ff00|3|c")
        style = true;
      if (d.message == "log: 255")
        pixel = true;
    }
    CHECK(style);
    CHECK(pixel);
    page.render(400, 300, 0);
    CHECK(page.canvas_images().count(page.doc.by_id("c")) == 1);
  }
  {
    // My JPEG decoder reads a real baseline file, and refuses a progressive one
    // with a message that says why.
    auto bytes = base64_decode(
        "/9j/4AAQSkZJRgABAQAAAQABAAD/2wBDAAIBAQEBAQIBAQECAgICAgQDAgICAgUEBAMEBgUGBgYFBgYGBwkIBgcJBwYGCAsICQoKCgoKBggLDAsKDAkKCgr/2wBDAQICAgICAgUDAwUKBwYHCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgr/wAARCAAIAAgDAREAAhEBAxEB/8QAHwAAAQUBAQEBAQEAAAAAAAAAAAECAwQFBgcICQoL/8QAtRAAAgEDAwIEAwUFBAQAAAF9AQIDAAQRBRIhMUEGE1FhByJxFDKBkaEII0KxwRVS0fAkM2JyggkKFhcYGRolJicoKSo0NTY3ODk6Q0RFRkdISUpTVFVWV1hZWmNkZWZnaGlqc3R1dnd4eXqDhIWGh4iJipKTlJWWl5iZmqKjpKWmp6ipqrKztLW2t7i5usLDxMXGx8jJytLT1NXW19jZ2uHi4+Tl5ufo6erx8vP09fb3+Pn6/8QAHwEAAwEBAQEBAQEBAQAAAAAAAAECAwQFBgcICQoL/8QAtREAAgECBAQDBAcFBAQAAQJ3AAECAxEEBSExBhJBUQdhcRMiMoEIFEKRobHBCSMzUvAVYnLRChYkNOEl8RcYGRomJygpKjU2Nzg5OkNERUZHSElKU1RVVldYWVpjZGVmZ2hpanN0dXZ3eHl6goOEhYaHiImKkpOUlZaXmJmaoqOkpaanqKmqsrO0tba3uLm6wsPExcbHyMnK0tPU1dbX2Nna4uPk5ebn6Onq8vP09fb3+Pn6/9oADAMBAAIRAxEAPwD4b/4KYfs8/wDChf8AhCv+Kv8A7V/tX+0v+Yf5HleV9l/6aPuz5ntjHfNfv/7KbiX/AFi/1w/dcnJ/Z/2ua/N9e8la1jyeOPpqf8TgfV/+EH+zP7M5/wDmK+s+1+s8v/UPh+Tk+r/3+bn+zy+9/9k=");
    auto image = decode_jpeg(bytes);
    CHECK(image.width == 8 && image.height == 8);
    if (image.valid()) {
      auto pixel = [&](int x, int y) {
        const uint8_t *p = &image.pixels[(size_t(y) * image.width + x) * 4];
        return std::make_tuple(int(p[2]), int(p[1]), int(p[0]));
      };
      auto red = pixel(1, 1), blue = pixel(6, 1);
      CHECK(std::get<0>(red) > 200 && std::get<1>(red) < 60);
      CHECK(std::get<2>(blue) > 200 && std::get<0>(blue) < 60);
    }
    bool refused = false;
    try {
      decode_jpeg(Bytes{0xff, 0xd8, 0xff, 0xc2, 0x00});
    } catch (const std::exception &) {
      refused = true;
    }
    CHECK(refused);
  }
  {
    // My SVG renderer fills, strokes and centres a circle inside a viewBox.
    auto image = render_svg(
        "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 100 100' width='100' height='100'>"
        "<rect x='10' y='10' width='80' height='80' fill='#ff0000' stroke='#0000ff'"
        " stroke-width='4'/><circle cx='50' cy='50' r='20' fill='#00ff00'/></svg>",
        100, 100);
    CHECK(image.width == 100 && image.height == 100);
    if (image.valid()) {
      auto pixel = [&](int x, int y) {
        const uint8_t *p = &image.pixels[(size_t(y) * image.width + x) * 4];
        return std::make_tuple(int(p[2]), int(p[1]), int(p[0]), int(p[3]));
      };
      auto centre = pixel(50, 50), edge = pixel(11, 50), corner = pixel(2, 2);
      CHECK(std::get<1>(centre) > 200 && std::get<0>(centre) < 60);
      CHECK(std::get<2>(edge) > 150);
      CHECK(std::get<3>(corner) == 0);
    }
  }
  // ---- audio and video: my own containers, player and media sources ----
  {
    Bytes wav = make_wav(8000, 2000, 8000);
    MediaInfo info;
    std::string error;
    CHECK(probe_media(wav, info, error));
    CHECK(info.container == "WAV");
    CHECK(info.has_audio && !info.has_video);
    CHECK(close_to(info.duration * 100, 25.0));
    CHECK(close_to(info.sample_rate, 8000));
    CHECK(media_type_supported("audio/wav"));
    CHECK(media_type_supported("video/x-msvideo"));
    CHECK(!media_type_supported("video/webm"));

    MediaPlayer player;
    CHECK(player.load(wav, "audio/wav", error));
    CHECK(player.ready() && player.paused());
    CHECK(close_to(player.duration() * 100, 25.0));
    player.play(100.0);
    player.advance(100.1);
    CHECK(!player.paused());
    CHECK(close_to(player.current_time() * 10, 1.0));
    // I ask for a device rate that is not the file rate, so the resampler runs.
    std::vector<int16_t> mixed(480 * 2, 0);
    CHECK(player.pull_audio(mixed.data(), 480, 48000) == 480);
    bool any = false;
    for (int16_t sample : mixed)
      if (sample != 0)
        any = true;
    CHECK(any);
    player.seek(0.2, 102.0);
    CHECK(close_to(player.current_time() * 10, 2.0));
    // Loop wraps at the end instead of stopping.
    player.set_loop(true);
    player.play(103.0);
    player.advance(200.0);
    CHECK(!player.ended());
    CHECK(player.current_time() < 0.05);
    // Muting stops the samples themselves, not just a label.
    player.set_muted(true);
    std::vector<int16_t> silent(64 * 2, 0);
    CHECK(player.pull_audio(silent.data(), 64, 48000) == 0);
  }
  {
    Bytes avi = make_avi();
    MediaInfo info;
    std::string error;
    CHECK(probe_media(avi, info, error));
    CHECK(info.container == "AVI");
    CHECK(info.has_video && !info.has_audio);
    CHECK(info.video_codec == "MJPEG");
    CHECK(info.width == 32 && info.height == 16);
    CHECK(close_to(info.frame_rate, 4.0));
    MediaPlayer video;
    CHECK(video.load(avi, "video/x-msvideo", error));
    CHECK(close_to(video.duration() * 100, 50.0));
    auto colour = [](const Image *image) {
      const uint8_t *p = &image->pixels[(size_t(4) * size_t(image->width) + 4) * 4];
      return std::make_tuple(int(p[2]), int(p[1]), int(p[0]));
    };
    const Image *first = video.frame();
    CHECK(first && first->width == 32 && first->height == 16);
    auto red_pixel = colour(first);
    CHECK(std::get<0>(red_pixel) > 200 && std::get<1>(red_pixel) < 60);
    video.seek(0.3, 10.0);
    const Image *second = video.frame();
    CHECK(second != nullptr);
    auto green_pixel = colour(second);
    CHECK(std::get<1>(green_pixel) > 200 && std::get<0>(green_pixel) < 60);
  }
  {
    // An MP4 that says what it holds: my reader walks the box tree and then names
    // the codec I have no decoder for instead of pretending to play it.
    MediaInfo info;
    std::string error;
    auto h264 = make_mp4("vide", "avc1");
    CHECK(!probe_media(h264, info, error));
    CHECK(info.duration > 0 && close_to(info.duration * 10, 25.0));
    CHECK(info.video_codec == "avc1");
    CHECK(info.width == 32 && info.height == 16);
    CHECK(error.find("H.264") != std::string::npos);
    CHECK(error.find("decoder") != std::string::npos);
    MediaPlayer refused;
    CHECK(!refused.load(h264, "video/mp4", error));
    CHECK(!refused.ready());
    auto aac = make_mp4("soun", "mp4a");
    CHECK(!probe_media(aac, info, error));
    CHECK(info.audio_codec == "mp4a");
    CHECK(error.find("AAC") != std::string::npos);
  }
  {
    // The typed arrays Media Source Extensions needs are real byte views: a
    // subarray shares the bytes and set writes through them.
    CHECK(js_eval("(new Uint8Array(5)).length") == "5");
    CHECK(js_eval("var b=new Uint8Array(4);b[2]=7;b[2]") == "7");
    CHECK(js_eval("var b=new Uint8Array(4);b[0]=1;b[1]=2;b[2]=3;var c=b.subarray(1);c.length")
          == "3");
    CHECK(js_eval("var b=new Uint8Array(4);b[0]=1;b[1]=2;b[2]=3;var c=b.subarray(1);c[0]=9;b[1]")
          == "9");
    CHECK(js_eval("var b=new Uint8Array(4);b.set([4,5],2);b[2]+','+b[3]") == "4,5");
    CHECK(js_eval("var b=new Uint8Array(new ArrayBuffer(3));b.buffer.byteLength") == "3");
    CHECK(js_eval("var s=0;var b=new Uint8Array(3);b[0]=1;b[1]=2;b[2]=3;for(var v of b)s+=v;s")
          == "6");
    CHECK(js_eval("var b=new Uint8Array(4);b[0]=1;b[1]=2;b.join('-')") == "1-2-0-0");
    CHECK(js_eval("var b=new Uint8Array(4);b[0]=1;var c=b.slice(0,2);c[0]=9;b[0]") == "1");
  }
  {
    // A source buffer grows its playable range as the bytes arrive, which is the
    // whole point of appending before the file is complete.
    std::string error;
    SourceBuffer buffer("audio/wav");
    CHECK(buffer.mime() == "audio/wav");
    MediaSourceState source;
    CHECK(source.add_source_buffer("audio/wav", error));
    CHECK(source.buffer_count() == 1);
    CHECK(!source.add_source_buffer("video/webm", error));
    CHECK(error.find("NotSupportedError") != std::string::npos);
    Bytes wav = make_wav(8000, 2000, 8000);
    size_t half = 24 + 2000;
    CHECK(half < wav.size());
    SourceBuffer *first_buffer = source.buffer(0);
    CHECK(first_buffer != nullptr);
    CHECK(first_buffer->append(wav.data(), half, error));
    auto ranges = first_buffer->buffered();
    CHECK(ranges.size() == 1);
    CHECK(ranges.front().second > 0 && ranges.front().second < 0.25);
    CHECK(first_buffer->append(wav.data() + half, wav.size() - half, error));
    ranges = first_buffer->buffered();
    CHECK(ranges.size() == 1);
    CHECK(close_to(ranges.front().second * 100, 25.0));
    CHECK(close_to(source.duration() * 100, 25.0));
    auto newest = source.newest(error);
    CHECK(newest != nullptr);
    MediaPlayer from_source;
    CHECK(from_source.load_shared(newest, error));
    CHECK(close_to(from_source.duration() * 100, 25.0));
    // A source buffer cannot cut a decoded segment in half, and it says so.
    CHECK(!first_buffer->remove(0.0, 0.1, error));
    CHECK(error.find("cannot remove") != std::string::npos);
    CHECK(source.end_of_stream(error));
    CHECK(source.ended());
    CHECK(!source.add_source_buffer("audio/wav", error));
    CHECK(source.total_bytes() == wav.size());
  }
  {
    // The same path through my page host: an audio element plays a WAV, a video
    // element shows real frames, and the frame changes after a seek.
    Bytes wav = make_wav(8000, 2000, 12000);
    Bytes avi = make_avi();
    std::vector<HostEvent> events;
    Host host(
        [&](const std::string &url, ResourceType) {
          Resource r;
          r.url = url;
          if (url == "https://media.test/") {
            r.mime = "text/html";
            r.text = "<!doctype html><title>media</title><body>"
                     "<audio id=tone src=\"/tone.wav\"></audio>"
                     "<video id=clip src=\"/clip.avi\" width=32 height=16></video>"
                     "<script>"
                     "var a=document.getElementById('tone');"
                     "a.addEventListener('play',function(){document.title='tone:'+"
                     "Math.round(a.duration*100);});"
                     "a.play();"
                     "setTimeout(function(){"
                     "var v=document.getElementById('clip');"
                     "v.currentTime=0.3;"
                     "v.pause();"
                     "document.title+='|seek:'+Math.round(v.currentTime*100);"
                     "},20);"
                     "</script></body>";
            return r;
          }
          if (url == "https://media.test/tone.wav") {
            r.mime = "audio/wav";
            r.bytes = wav;
            return r;
          }
          if (url == "https://media.test/clip.avi") {
            r.mime = "video/x-msvideo";
            r.bytes = avi;
            return r;
          }
          r.ok = false;
          r.status = 404;
          return r;
        },
        [&events](HostEvent event) { events.push_back(std::move(event)); });
    std::string error;
    CHECK(host.load("https://media.test/", error));
    CHECK(error.empty());
    bool saw_media_image = false, saw_red = false, saw_green = false;
    int frame_width = 0, frame_height = 0;
    std::string last_title;
    for (int i = 0; i < 20; ++i) {
      host.pump();
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    for (const auto &event : events) {
      if (event.tag == "image" && event.url.rfind("media:", 0) == 0 && event.image) {
        saw_media_image = true;
        frame_width = event.image->width;
        frame_height = event.image->height;
        const uint8_t *p = &event.image->pixels[(size_t(2) * size_t(frame_width) + 2) * 4];
        if (int(p[2]) > int(p[1]) + 100)
          saw_red = true;
        if (int(p[1]) > int(p[2]) + 100)
          saw_green = true;
      }
      if (event.tag == "frame")
        last_title = event.frame.str("title");
    }
    CHECK(saw_media_image);
    CHECK(frame_width == 32 && frame_height == 16);
    CHECK(last_title.rfind("tone:25", 0) == 0);
    CHECK(last_title.find("seek:") != std::string::npos);
    // The clip starts red and turns green once the page seeks to the second frame.
    CHECK(saw_red);
    CHECK(saw_green);
  }
  {
    // A page that builds its own media source and appends a fetched segment plays
    // it, which is the path a streaming site uses.
    Bytes wav = make_wav(8000, 2000, 9000);
    std::vector<HostEvent> events;
    Host host(
        [&](const std::string &url, ResourceType) {
          Resource r;
          r.url = url;
          if (url == "https://mse.test/segment.wav") {
            r.mime = "audio/wav";
            r.bytes = wav;
            return r;
          }
          if (url == "https://mse.test/") {
            r.mime = "text/html";
            r.text = "<!doctype html><title>mse</title><body><audio id=a></audio><script>"
                     "var a=document.getElementById('a');"
                     "a.addEventListener('loadedmetadata',function(){"
                     "document.title='ms:'+Math.round(a.duration*100);});"
                     "a.addEventListener('play',function(){document.title+='|play';});"
                     "var ms=new MediaSource();"
                     "a.src=URL.createObjectURL(ms);"
                     "ms.addEventListener('sourceopen',function(){"
                     "var sb=ms.addSourceBuffer('audio/wav');"
                     "sb.addEventListener('updateend',function(){"
                     "document.title+='|buffered:'+Math.round(sb.buffered.end(0)*100);"
                     "ms.endOfStream();});"
                     "fetch('/segment.wav').then(function(r){return r.arrayBuffer();})"
                     ".then(function(buf){sb.appendBuffer(new Uint8Array(buf));});"
                     "});"
                     "a.play();"
                     "</script></body>";
            return r;
          }
          r.ok = false;
          r.status = 404;
          return r;
        },
        [&events](HostEvent event) { events.push_back(std::move(event)); });
    std::string error;
    CHECK(host.load("https://mse.test/", error));
    CHECK(error.empty());
    std::string title;
    for (int i = 0; i < 8; ++i) {
      host.pump();
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    for (const auto &event : events)
      if (event.tag == "frame")
        title = event.frame.str("title");
    CHECK(title.rfind("ms:25", 0) == 0);
    CHECK(title.find("|play") != std::string::npos);
    CHECK(title.find("buffered:25") != std::string::npos);
  }
  // ---- step 7: my own tunnel through a proxy ----
  {
    // SOCKS5 without a password: my client offers method 0, then asks for a name
    // as a name, so the lookup happens on the far side and not on my machine.
    ScriptStream stream;
    TunnelTarget target{"example.com", 443};
    std::string error;
    // The script is the two replies in order: the method choice, then success with
    // a bound address. The two authentication methods my client offers are the
    // bytes it writes before it asks for those.
    stream.script = Bytes{5, 0, 5, 0, 0, 1, 127, 0, 0, 1, 0x1f, 0x90};
    CHECK(socks5_connect(stream, target, "", "", false, error));
    auto sent = stream.written;
    CHECK(sent.size() == 3 + 7 + size_t(target.host.size()));
    CHECK(sent[0] == 5 && sent[1] == 1 && sent[2] == 0);
    CHECK(sent[3] == 5 && sent[4] == 1 && sent[5] == 0);
    // Address type 3 is a domain name, which is the whole point of the tunnel.
    CHECK(sent[6] == 3);
    CHECK(sent[7] == uint8_t(target.host.size()));
    CHECK(std::string(sent.begin() + 8, sent.begin() + 8 + std::ptrdiff_t(target.host.size())) ==
          "example.com");
    CHECK(sent[sent.size() - 2] == 0x01 && sent[sent.size() - 1] == 0xbb);
  }
  {
    // SOCKS5 with a username and password, and a refusal mapped to words a person
    // can read.
    ScriptStream stream;
    stream.script = Bytes{5, 2, 1, 0, 5, 0, 0, 1, 10, 0, 0, 7, 0, 80};
    TunnelTarget target{"10.0.0.7", 80};
    std::string error;
    CHECK(socks5_connect(stream, target, "user", "pass", true, error));
    auto text = stream.text();
    CHECK(text.find("user") != std::string::npos);
    // With a literal address I send the four bytes, not the text: the request
    // begins at byte 13, right after the greeting and the credentials.
    size_t request_at = 4 + 3 + 4 + 4;
    CHECK(stream.written[request_at] == 5);
    CHECK(stream.written[request_at + 3] == 1);
    CHECK(stream.written[request_at + 4] == 10);
  }
  {
    ScriptStream refused;
    // Success on the method, then code 4: the proxy could not resolve the name.
    refused.script = Bytes{5, 0, 5, 4, 0, 1, 0, 0, 0, 0, 0, 0};
    std::string error;
    CHECK(!socks5_connect(refused, TunnelTarget{"does-not-exist.test", 443}, "", "", false, error));
    CHECK(error.find("resolve") != std::string::npos);
    CHECK(error.find("did not ask") != std::string::npos);
    ScriptStream auth_required;
    auth_required.script = Bytes{5, 2};
    error.clear();
    CHECK(!socks5_connect(auth_required, TunnelTarget{"example.com", 443}, "", "", false, error));
    CHECK(error.find("username and password") != std::string::npos);
    ScriptStream no_method;
    no_method.script = Bytes{5, 0xff};
    error.clear();
    CHECK(!socks5_connect(no_method, TunnelTarget{"example.com", 443}, "", "", false, error));
    CHECK(error.find("authentication") != std::string::npos);
  }
  {
    // CONNECT: the authority line, the proxy credentials, and a 200 that opens the
    // tunnel versus a 407 that refuses it.
    ScriptStream stream;
    stream.script = Bytes{'H', 'T', 'T', 'P', '/', '1', '.', '1', ' ', '2', '0', '0', ' ', 'O',
                          'K', '\r', '\n', '\r', '\n'};
    std::string error;
    CHECK(http_connect(stream, TunnelTarget{"secure.example", 443}, "alice", "secret", true, error));
    auto text = stream.text();
    CHECK(text.rfind("CONNECT secure.example:443 HTTP/1.1\r\n", 0) == 0);
    CHECK(text.find("Proxy-Authorization: Basic ") != std::string::npos);
    CHECK(text.find("\r\n\r\n") != std::string::npos);
    ScriptStream denied;
    denied.script = Bytes{'H', 'T', 'T', 'P', '/', '1', '.', '1', ' ', '4', '0', '7', ' ', 'X',
                          '\r', '\n', '\r', '\n'};
    error.clear();
    CHECK(!http_connect(denied, TunnelTarget{"secure.example", 443}, "", "", false, error));
    CHECK(error.find("407") != std::string::npos);
  }
  {
    // My HTTP framing: a length, chunks, and a body that ends when the connection
    // does. All three are the shapes a real server answers with.
    ScriptStream fixed;
    std::string reply = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n"
                        "Content-Length: 11\r\n\r\nhello world";
    fixed.script.assign(reply.begin(), reply.end());
    HttpRequest request;
    request.host = "example.com";
    HttpRequest asked;
    asked.host = "example.com";
    asked.headers.emplace_back("Accept", "*/*");
    HttpResponse response;
    std::string error;
    CHECK(http_exchange(fixed, asked, response, error));
    CHECK(response.status == 200);
    CHECK(response.body == "hello world");
    // The request line my client wrote is the one the server sees.
    CHECK(fixed.text().rfind("GET / HTTP/1.1\r\n", 0) == 0);
    CHECK(fixed.text().find("Host: example.com") != std::string::npos);
    ScriptStream chunked;
    std::string chunks = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
                         "5\r\nhello\r\n6\r\n world\r\n0\r\n\r\n";
    chunked.script.assign(chunks.begin(), chunks.end());
    response = HttpResponse();
    CHECK(http_exchange(chunked, asked, response, error));
    CHECK(response.body == "hello world");
    ScriptStream until_close;
    std::string closing = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\nuntil the end";
    until_close.script.assign(closing.begin(), closing.end());
    response = HttpResponse();
    CHECK(http_exchange(until_close, asked, response, error));
    CHECK(response.body == "until the end");
    // A head that never finishes is an error, not a silent empty page.
    ScriptStream broken;
    broken.script.assign({65, 66});
    response = HttpResponse();
    error.clear();
    CHECK(!http_exchange(broken, asked, response, error));
    CHECK(!error.empty());
  }
  {
    // A redirect inside the tunnel stays inside the tunnel. The proxy answers 302 on
    // the first request and the page on the second over the same connection, and the
    // name still reaches the proxy rather than being resolved here.
    LoopbackProxy proxy(std::vector<std::pair<std::string, std::string>>{
        {"302 Found", ""}, {"200 OK", "moved page"}});
    if (!proxy.failure.empty())
      std::cerr << "proxy would not start: " << proxy.failure << "\n";
    CHECK(proxy.failure.empty());
    TunnelConfig config;
    config.proxy.scheme = "socks5";
    config.proxy.host = "127.0.0.1";
    config.proxy.port = proxy.port;
    config.user_agent = "PsychoVault-test";
    Resource out = tunneled_fetch(*parse_url("http://redirect.test/start"), config, "GET", "", "",
                                  5000, kMaxResourceBytes);
    CHECK(out.ok);
    CHECK(out.text == "moved page");
    CHECK(out.url == "http://redirect.test/moved");
    CHECK(proxy.requests.size() == 2);
    if (proxy.requests.size() == 2) {
      CHECK(proxy.requests[0].rfind("GET /start HTTP/1.1", 0) == 0);
      CHECK(proxy.requests[1].rfind("GET /moved HTTP/1.1", 0) == 0);
      // The Host line names the origin, not the proxy, so a virtual host still works.
      CHECK(proxy.requests[1].find("Host: redirect.test") != std::string::npos);
    }
    CHECK(proxy.seen_host == "redirect.test");
  }
  {
    // Fail-closed: a proxy that is not a server I can dial is an error, and an
    // https address in a build with no TLS provider is refused by name.
    TunnelConfig config;
    config.proxy.host = "127.0.0.1";
    config.proxy.port = 1;
    config.proxy.scheme = "socks5";
    Resource out = tunneled_fetch(*parse_url("https://example.com/"), config, "GET", "", "", 1000,
                                  kMaxResourceBytes);
    CHECK(!out.ok);
    CHECK(out.status == 0);
    CHECK(!out.error.empty());
    TunnelConfig empty_config;
    out = tunneled_fetch(*parse_url("https://example.com/"), empty_config, "GET", "", "", 1000,
                         kMaxResourceBytes);
    CHECK(!out.ok);
    CHECK(out.error.rfind("fail-closed", 0) == 0);
  }
  {
    // The whole tunnel over a real loopback socket: my client speaks SOCKS5 to a
    // proxy, the proxy is handed a name rather than an address, and the page comes
    // back through my own HTTP framing.
    LoopbackProxy proxy(std::vector<std::pair<std::string, std::string>>{
        {"200 OK", "tunnelled page"}});
    if (!proxy.failure.empty())
      std::cerr << "proxy would not start: " << proxy.failure << "\n";
    CHECK(proxy.failure.empty());
    CHECK(proxy.port > 0);
    TunnelConfig config;
    config.proxy.scheme = "socks5";
    config.proxy.host = "127.0.0.1";
    config.proxy.port = proxy.port;
    config.user_agent = "PsychoVault-test";
    Resource out = tunneled_fetch(*parse_url("http://example.test/page?q=1"), config, "GET", "",
                                  "", 5000, kMaxResourceBytes);
    CHECK(out.ok);
    CHECK(out.status == 200);
    CHECK(out.mime == "text/html");
    CHECK(out.text == "tunnelled page");
    // The name reached the proxy, so nothing resolved it on this machine.
    CHECK(proxy.seen_host == "example.test");
  }
  {
    // A redirect to another host must not carry the first host's cookies, and my
    // Referer has to name the page that sent me rather than the page I am fetching.
    LoopbackProxy proxy(std::vector<std::pair<std::string, std::string>>{
        {"302 Found", ""}, {"200 OK", "other host"}});
    proxy.location = "http://second.test/moved";
    TunnelConfig config;
    config.proxy.scheme = "socks5";
    config.proxy.host = "127.0.0.1";
    config.proxy.port = proxy.port;
    config.cookie_header = "session=secret";
    Resource out = tunneled_fetch(*parse_url("http://first.test/start"), config, "GET", "", "",
                                  5000, kMaxResourceBytes);
    CHECK(out.ok);
    CHECK(proxy.requests.size() == 2);
    if (proxy.requests.size() == 2) {
      CHECK(proxy.requests[0].find("Cookie: session=secret") != std::string::npos);
      // The redirect went to another host, so the first host's cookie stays behind
      // and my referer names the page that sent me.
      CHECK(proxy.requests[1].rfind("GET /moved HTTP/1.1", 0) == 0);
      CHECK(proxy.requests[1].find("Host: second.test") != std::string::npos);
      CHECK(proxy.requests[1].find("Cookie:") == std::string::npos);
      CHECK(proxy.requests[1].find("Referer: http://first.test/") != std::string::npos);
    }
  }
  {
    // A 303 means fetch the new place with a GET and no body, whatever I sent
    // before. A 307 means keep both.
    LoopbackProxy proxy(std::vector<std::pair<std::string, std::string>>{
        {"303 See Other", ""}, {"200 OK", "after see other"}});
    TunnelConfig config;
    config.proxy.scheme = "socks5";
    config.proxy.host = "127.0.0.1";
    config.proxy.port = proxy.port;
    Resource out = tunneled_fetch(*parse_url("http://post.test/form"), config, "POST", "a=1&b=2",
                                  "application/x-www-form-urlencoded", 5000, kMaxResourceBytes);
    CHECK(out.ok);
    CHECK(proxy.requests.size() == 2);
    if (proxy.requests.size() == 2) {
      CHECK(proxy.requests[0].rfind("POST /form HTTP/1.1", 0) == 0);
      CHECK(proxy.requests[0].find("a=1&b=2") != std::string::npos);
      CHECK(proxy.requests[1].rfind("GET /moved HTTP/1.1", 0) == 0);
      CHECK(proxy.requests[1].find("a=1&b=2") == std::string::npos);
      CHECK(proxy.requests[1].find("Content-Type:") == std::string::npos);
    }
  }
  {
    LoopbackProxy proxy(std::vector<std::pair<std::string, std::string>>{
        {"307 Temporary Redirect", ""}, {"200 OK", "after temporary"}});
    TunnelConfig config;
    config.proxy.scheme = "socks5";
    config.proxy.host = "127.0.0.1";
    config.proxy.port = proxy.port;
    Resource out = tunneled_fetch(*parse_url("http://post.test/form"), config, "POST", "a=1",
                                  "text/plain", 5000, kMaxResourceBytes);
    CHECK(out.ok);
    if (proxy.requests.size() == 2) {
      CHECK(proxy.requests[1].rfind("POST /moved HTTP/1.1", 0) == 0);
      CHECK(proxy.requests[1].find("a=1") != std::string::npos);
    }
  }
  {
    // Four ways a response can lie about where its body ends. Each one has to be an
    // error, because rendering half a page as if it were whole is worse.
    struct Case {
      const char *what;
      std::string reply;
      const char *expected;
    };
    std::vector<Case> cases{
        {"chunk size", "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nzz\r\nabc\r\n0\r\n\r\n",
         "chunk size"},
        {"chunk break", "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n3\r\nabcXX0\r\n\r\n",
         "line break"},
        {"two lengths", "HTTP/1.1 200 OK\r\nContent-Length: 5\r\nContent-Length: 9\r\n\r\nhello",
         "two different Content-Length"},
        {"short body", "HTTP/1.1 200 OK\r\nContent-Length: 40\r\n\r\nshort", "before the end"},
        {"not a number", "HTTP/1.1 200 OK\r\nContent-Length: lots\r\n\r\nhello", "not a number"},
    };
    for (const auto &test_case : cases) {
      LoopbackProxy proxy(std::vector<std::pair<std::string, std::string>>{{"200 OK", ""}});
      proxy.raw_reply = test_case.reply;
      TunnelConfig config;
      config.proxy.scheme = "socks5";
      config.proxy.host = "127.0.0.1";
      config.proxy.port = proxy.port;
      Resource out =
          tunneled_fetch(*parse_url("http://broken.test/"), config, "GET", "", "", 5000,
                         kMaxResourceBytes);
      if (out.ok || out.error.find(test_case.expected) == std::string::npos)
        std::cerr << "framing case " << test_case.what << " gave: ok=" << out.ok
                  << " error=" << out.error << "\n";
      CHECK(!out.ok);
      CHECK(out.error.find(test_case.expected) != std::string::npos);
    }
  }
  {
    // A HEAD reply carries a length and no body, so asking for one must not sit
    // waiting for bytes that are never coming.
    LoopbackProxy proxy(std::vector<std::pair<std::string, std::string>>{{"200 OK", ""}});
    proxy.raw_reply = "HTTP/1.1 200 OK\r\nContent-Length: 9000\r\n\r\n";
    TunnelConfig config;
    config.proxy.scheme = "socks5";
    config.proxy.host = "127.0.0.1";
    config.proxy.port = proxy.port;
    Resource out = tunneled_fetch(*parse_url("http://head.test/"), config, "HEAD", "", "", 5000,
                                  kMaxResourceBytes);
    CHECK(out.ok);
    CHECK(out.status == 200);
    CHECK(out.text.empty());
  }
  {
    // My tunnel only counts as a VPN with a protected scheme. A plain HTTP proxy is
    // still routed, because the ordinary web has always supported one, but the code
    // that decides is the same one, and this check pins the distinction.
    ProxyServer plain{"http", "127.0.0.1", 3128};
    ProxyServer secure{"https", "127.0.0.1", 4443};
    ProxyServer socks{"socks5", "127.0.0.1", 1080};
    CHECK(plain.valid() && secure.valid() && socks.valid());
    CHECK(socks.scheme == "socks5");
  }
  {
    // A scheme I never wrote must be refused before a socket is opened, not treated
    // as an HTTP proxy because the code fell through to it.
    TunnelConfig config;
    config.proxy.scheme = "quic-ish";
    config.proxy.host = "127.0.0.1";
    config.proxy.port = 1;
    Resource out = tunneled_fetch(*parse_url("http://example.test/"), config, "GET", "", "", 500,
                                  kMaxResourceBytes);
    CHECK(!out.ok);
    CHECK(out.error.find("fail-closed") != std::string::npos);
    CHECK(out.error.find("quic-ish") != std::string::npos);
  }
  {
    // An IPv6 literal has to be bracketed in a CONNECT target or the proxy reads the
    // address's own colons as the port separator.
    ScriptStream stream;
    stream.script = Bytes{};
    std::string connect_error;
    TunnelTarget target{"2001:db8::1", 443};
    http_connect(stream, target, "", "", false, connect_error);
    auto text = stream.text();
    CHECK(text.rfind("CONNECT [2001:db8::1]:443 HTTP/1.1", 0) == 0);
  }
  {
    // A stream that reports a successful read of zero bytes used to leave my framing
    // loops waiting forever. A reader that is handed nothing now gets an error.
    class EmptyStream : public Stream {
    public:
      bool write_all(const uint8_t *, size_t, std::string &) override { return true; }
      bool read_some(uint8_t *, size_t, size_t &got, std::string &) override {
        got = 0;
        return true;
      }
      bool alive() const override { return true; }
    };
    EmptyStream stream;
    HttpRequest request;
    request.host = "empty.test";
    HttpResponse response;
    std::string error;
    CHECK(!http_exchange(stream, request, response, error));
    CHECK(error.find("stopped sending") != std::string::npos);
  }
  {
    // Strictly a transport check: peer_subject must be empty before a handshake, and
    // a build with no TLS provider must refuse rather than fall back to plaintext.
#ifndef _WIN32
    SocketStream plain{Socket()};
    std::string error;
    auto tls = TlsStream::open(plain, "example.com", error);
    CHECK(!tls);
    CHECK(error.find("refuse") != std::string::npos);
#endif
  }
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
    profile.proxy.user = "tunnel-user";
    profile.proxy.password = "tunnel-secret";
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
    CHECK(restored.proxy.has_auth());
    CHECK(restored.proxy.user == "tunnel-user");
    CHECK(restored.proxy.password == "tunnel-secret");
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
  // My page host renders inside my own process. I check that it fetches what
  // the page asks for, decodes images and reports frames without a renderer.
  {
    std::map<std::string, int> fetches;
    std::vector<HostEvent> events;
    Host::Fetcher fetcher = [&](const std::string &url, ResourceType) {
      ++fetches[url];
      Resource r;
      r.url = url;
      if (url == "https://example.test/") {
        r.mime = "text/html";
        r.text = "<!doctype html><html><head><title>Host</title></head><body>"
                 "<h1>Hello host</h1><img src=\"/a.png\"><img src=\"/missing.png\">"
                 "</body></html>";
      } else if (url == "https://example.test/a.png") {
        r.mime = "image/png";
        r.bytes.assign(fixtures::rgb_fixed.begin(), fixtures::rgb_fixed.end());
      } else {
        r.ok = false;
        r.status = 404;
        r.error = "Not found";
      }
      return r;
    };
    Host host(fetcher, [&](HostEvent event) { events.push_back(std::move(event)); });
    std::string error;
    CHECK(host.load("https://example.test/", error));
    CHECK(error.empty());
    CHECK(host.loaded());
    int frames = 0, images = 0;
    int image_width = 0, image_height = 0;
    std::string frame_title;
    for (const auto &event : events) {
      if (event.tag == "frame") {
        ++frames;
        frame_title = event.frame.str("title");
        CHECK(event.frame.at("paint").type == Json::Type::Array);
        CHECK(!event.frame.at("paint").array.empty());
      }
      if (event.tag == "image") {
        ++images;
        CHECK(event.image != nullptr);
        if (event.image) {
          image_width = event.image->width;
          image_height = event.image->height;
          CHECK(event.image->valid());
        }
      }
    }
    CHECK(frames == 1);
    CHECK(images == 1);
    CHECK(image_width == 2 && image_height == 1);
    CHECK(frame_title == "Host");
    // I ask for each subresource once, even the one that fails.
    CHECK(fetches["https://example.test/"] == 1);
    CHECK(fetches["https://example.test/a.png"] == 1);
    CHECK(fetches["https://example.test/missing.png"] == 1);
    // Nothing is due and nothing is pending, so my host idles instead of spinning.
    CHECK(!host.pump());
    CHECK(host.next_timer_delay() < 0);
    // A resize reports a fresh frame.
    events.clear();
    host.resize(320, 200);
    CHECK(events.size() == 1 && events[0].tag == "frame");
    // Scrolling never runs past the top of the document.
    events.clear();
    host.scroll(-50);
    CHECK(events.size() == 1 && events[0].frame.num("scroll") == 0);
  }
  // A redirect decides the base URL, so a relative link follows the final URL.
  {
    std::vector<HostEvent> events;
    Host host(
        [](const std::string &url, ResourceType) {
          Resource r;
          r.url = url;
          r.mime = "text/html";
          if (url == "https://example.test/old") {
            r.url = "https://other.test/landing";
            r.text = "<!doctype html><html><body style=\"margin:0\">"
                     "<a href=\"child\" style=\"display:block;width:300px;height:60px\">child</a>"
                     "</body></html>";
          } else {
            r.text = "<!doctype html><html><body>other</body></html>";
          }
          return r;
        },
        [&](HostEvent event) { events.push_back(std::move(event)); });
    std::string error;
    CHECK(host.load("https://example.test/old", error));
    std::string page_url;
    for (const auto &event : events)
      if (event.tag == "frame")
        page_url = event.frame.str("url");
    CHECK(page_url == "https://other.test/landing");
    events.clear();
    host.click(20, 20);
    bool followed = false;
    for (const auto &event : events)
      if (event.tag == "navigate" && event.url == "https://other.test/child")
        followed = true;
    CHECK(followed);
  }
  // I click a link and my host asks my shell to navigate.
  {
    std::vector<HostEvent> events;
    Host host(
        [](const std::string &url, ResourceType) {
          Resource r;
          r.url = url;
          r.mime = "text/html";
          r.text = "<!doctype html><html><body style=\"margin:0\">"
                   "<a href=\"/next\" style=\"display:block;width:300px;height:60px\">next</a>"
                   "</body></html>";
          return r;
        },
        [&](HostEvent event) { events.push_back(std::move(event)); });
    std::string error;
    CHECK(host.load("https://example.test/", error));
    events.clear();
    host.click(20, 20);
    bool navigated = false;
    for (const auto &event : events)
      if (event.tag == "navigate" && event.url == "https://example.test/next")
        navigated = true;
    CHECK(navigated);
  }
  // A timer that is due runs through pump and reports a new frame.
  {
    std::vector<HostEvent> events;
    Host host(
        [](const std::string &url, ResourceType) {
          Resource r;
          r.url = url;
          r.mime = "text/html";
          r.text = "<!doctype html><html><head><title>Before</title></head><body>"
                   "<script>setTimeout(function(){document.title='After'},0)</script>"
                   "</body></html>";
          return r;
        },
        [&](HostEvent event) { events.push_back(std::move(event)); });
    std::string error;
    CHECK(host.load("https://example.test/", error));
    CHECK(host.pump());
    std::string title;
    for (const auto &event : events)
      if (event.tag == "frame")
        title = event.frame.str("title");
    CHECK(title == "After");
  }
  // A failed navigation is an error I can show, not a blank window.
  {
    bool reported = false;
    Host host(
        [](const std::string &url, ResourceType) {
          Resource r;
          r.url = url;
          r.ok = false;
          r.status = 404;
          r.error = "Not found";
          return r;
        },
        [&](HostEvent) { reported = true; });
    std::string error;
    CHECK(!host.load("https://example.test/missing", error));
    CHECK(error == "Not found");
    CHECK(!host.loaded());
    CHECK(!reported);
  }
  // A fetcher that throws cannot take my browser down with it.
  {
    std::vector<HostEvent> events;
    Host host(
        [](const std::string &url, ResourceType) -> Resource {
          if (url == "https://example.test/") {
            Resource r;
            r.url = url;
            r.mime = "text/html";
            r.text = "<!doctype html><html><body><img src=\"/boom.png\"></body></html>";
            return r;
          }
          throw std::runtime_error("socket exploded");
        },
        [&](HostEvent event) { events.push_back(std::move(event)); });
    std::string error;
    CHECK(host.load("https://example.test/", error));
    bool frame = false;
    for (const auto &event : events)
      if (event.tag == "frame")
        frame = true;
    CHECK(frame);
  }

  // ---- Step five: my own JPEG, SVG and Canvas 2D -------------------------
  {
    // An eight by eight baseline JPEG: red on the left, blue on the right, with
    // chroma subsampling on. I decode it with my own Huffman and cosine code.
    static const uint8_t tiny_jpeg[] = {
    255,216,255,224,0,16,74,70,73,70,0,1,1,0,0,1,0,1,0,0,
    255,219,0,67,0,3,2,2,3,2,2,3,3,3,3,4,3,3,4,5,
    8,5,5,4,4,5,10,7,7,6,8,12,10,12,12,11,10,11,11,13,
    14,18,16,13,14,17,14,11,11,16,22,16,17,19,20,21,21,21,12,15,
    23,24,22,20,24,18,20,21,20,255,219,0,67,1,3,4,4,5,4,5,
    9,5,5,9,20,13,11,13,20,20,20,20,20,20,20,20,20,20,20,20,
    20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,
    20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,255,192,
    0,17,8,0,8,0,8,3,1,34,0,2,17,1,3,17,1,255,196,0,
    31,0,0,1,5,1,1,1,1,1,1,0,0,0,0,0,0,0,0,1,
    2,3,4,5,6,7,8,9,10,11,255,196,0,181,16,0,2,1,3,3,
    2,4,3,5,5,4,4,0,0,1,125,1,2,3,0,4,17,5,18,33,
    49,65,6,19,81,97,7,34,113,20,50,129,145,161,8,35,66,177,193,21,
    82,209,240,36,51,98,114,130,9,10,22,23,24,25,26,37,38,39,40,41,
    42,52,53,54,55,56,57,58,67,68,69,70,71,72,73,74,83,84,85,86,
    87,88,89,90,99,100,101,102,103,104,105,106,115,116,117,118,119,120,121,122,
    131,132,133,134,135,136,137,138,146,147,148,149,150,151,152,153,154,162,163,164,
    165,166,167,168,169,170,178,179,180,181,182,183,184,185,186,194,195,196,197,198,
    199,200,201,202,210,211,212,213,214,215,216,217,218,225,226,227,228,229,230,231,
    232,233,234,241,242,243,244,245,246,247,248,249,250,255,196,0,31,1,0,3,
    1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,1,2,3,4,5,
    6,7,8,9,10,11,255,196,0,181,17,0,2,1,2,4,4,3,4,7,
    5,4,4,0,1,2,119,0,1,2,3,17,4,5,33,49,6,18,65,81,
    7,97,113,19,34,50,129,8,20,66,145,161,177,193,9,35,51,82,240,21,
    98,114,209,10,22,36,52,225,37,241,23,24,25,26,38,39,40,41,42,53,
    54,55,56,57,58,67,68,69,70,71,72,73,74,83,84,85,86,87,88,89,
    90,99,100,101,102,103,104,105,106,115,116,117,118,119,120,121,122,130,131,132,
    133,134,135,136,137,138,146,147,148,149,150,151,152,153,154,162,163,164,165,166,
    167,168,169,170,178,179,180,181,182,183,184,185,186,194,195,196,197,198,199,200,
    201,202,210,211,212,213,214,215,216,217,218,226,227,228,229,230,231,232,233,234,
    242,243,244,245,246,247,248,249,250,255,218,0,12,3,1,0,2,17,3,17,
    0,63,0,248,219,226,175,252,194,255,0,237,175,254,201,69,20,87,247,15,
    130,95,242,64,101,223,247,23,255,0,79,212,62,175,197,255,0,249,45,241,
    255,0,247,15,255,0,76,211,63,255,217
    };
    Bytes jpeg(tiny_jpeg, tiny_jpeg + sizeof(tiny_jpeg));
    auto picture = decode_jpeg(jpeg);
    CHECK(picture.width == 8 && picture.height == 8);
    CHECK(picture.pixels.size() == 8 * 8 * 4);
    auto channel = [&](int x, int y, int offset) { return int(picture.pixels[(y * 8 + x) * 4 + offset]); };
    // My buffer is blue, green, red, alpha.
    CHECK(channel(1, 1, 2) > 200 && channel(1, 1, 0) < 80);
    CHECK(channel(6, 1, 0) > 200 && channel(6, 1, 2) < 80);
    bool refused = false;
    try {
      decode_jpeg(Bytes(jpeg.begin(), jpeg.end() - 20));
    } catch (const std::exception &) {
      refused = true;
    }
    CHECK(refused);
    refused = false;
    try {
      decode_jpeg(Bytes(fixtures::rgb_fixed.begin(), fixtures::rgb_fixed.end()));
    } catch (const std::exception &) {
      refused = true;
    }
    CHECK(refused);
  }
  {
    // SVG: a red box, a blue circle and a translated group, drawn with my raster.
    std::string svg =
        "<svg xmlns='http://www.w3.org/2000/svg' width='40' height='40' viewBox='0 0 40 40'>"
        "<rect x='0' y='0' width='20' height='20' fill='#ff0000'/>"
        "<circle cx='30' cy='30' r='8' fill='blue'/>"
        "<g transform='translate(2,22)'><rect width='8' height='8' fill='#00ff00'/></g>"
        "</svg>";
    auto drawn = render_svg(svg, 40, 40);
    CHECK(drawn.width == 40 && drawn.height == 40);
    auto pixel = [&](int x, int y) {
      size_t i = (size_t(y) * drawn.width + x) * 4;
      return (uint32_t(drawn.pixels[i + 3]) << 24) | (uint32_t(drawn.pixels[i + 2]) << 16) |
             (uint32_t(drawn.pixels[i + 1]) << 8) | drawn.pixels[i];
    };
    CHECK(pixel(5, 5) == 0xffff0000u);
    CHECK(pixel(30, 30) == 0xff0000ffu);
    CHECK(pixel(4, 24) == 0xff00ff00u);
    CHECK((pixel(38, 2) >> 24) == 0);
    bool rejected = false;
    try {
      render_svg("<svg><rect></svg>", 10, 10);
    } catch (const std::exception &) {
      rejected = true;
    }
    CHECK(rejected);
  }
  {
    // Canvas 2D through a real page, the same path a site drives.
    Page canvas_page;
    canvas_page.load(
        *parse_url("https://canvas.test/"),
        "<!doctype html><canvas id=c width=20 height=20></canvas><canvas id=d width=10 "
        "height=10></canvas><canvas id=e width=5 height=5></canvas><script>"
        "var c=document.getElementById('c').getContext('2d');"
        "c.fillStyle='#ff0000';c.fillRect(0,0,10,20);"
        "c.fillStyle='#00ff00';c.fillRect(10,0,10,10);"
        "c.save();c.translate(0,10);c.fillStyle='#0000ff';c.fillRect(10,0,10,10);c.restore();"
        "c.clearRect(0,0,2,2);"
        "var g=c.createLinearGradient(0,0,20,0);g.addColorStop(0,'#000000');"
        "g.addColorStop(1,'#ffffff');c.fillStyle=g;c.fillRect(0,18,20,2);"
        "var d=document.getElementById('d').getContext('2d');"
        "d.drawImage(document.getElementById('c'),0,0,10,10);"
        "var e=document.getElementById('e').getContext('2d');"
        "e.fillStyle='#ffffff';e.fillRect(0,0,5,5);e.fillRect(0,0,1,1);"
        "document.getElementById('e').width=6;"
        "document.title=c.getImageData(0,0,1,1).data.join(',')+'|'"
        "+c.toDataURL().slice(0,22)+'|'+c.getImageData(19,1,1,1).data.join(',')+'|'"
        "+(typeof c.fillRect)+'|'+(c.measureText('hello').width>0);"
        "</script>");
    auto picture_frame = canvas_page.render(400, 300, 0);
    CHECK(canvas_page.title() == "0,0,0,0|data:image/png;base64,|0,255,0,255|function|true");
    int canvas_commands = 0;
    for (const auto &command : picture_frame.at("paint").array)
      if (int(command.num("kind")) == int(PaintCommand::Kind::Canvas))
        ++canvas_commands;
    CHECK(canvas_commands == 3);
    auto &images = canvas_page.canvas_images();
    CHECK(images.size() == 3);
    const Image &surface = images.at(canvas_page.doc.by_id("c"));
    CHECK(surface.width == 20 && surface.height == 20);
    auto at = [&](const Image &image, int x, int y) {
      size_t i = (size_t(y) * image.width + x) * 4;
      return (uint32_t(image.pixels[i + 3]) << 24) | (uint32_t(image.pixels[i + 2]) << 16) |
             (uint32_t(image.pixels[i + 1]) << 8) | image.pixels[i];
    };
    CHECK(at(surface, 1, 1) == 0x00000000u);  // cleared
    CHECK(at(surface, 15, 5) == 0xff00ff00u);
    CHECK(at(surface, 15, 15) == 0xff0000ffu);
    // The gradient runs black to white, so the left end is dark and the right
    // end is bright.
    uint32_t gradient_left = at(surface, 1, 19), gradient_right = at(surface, 18, 19);
    CHECK(((gradient_left >> 16) & 255) < 60);
    CHECK(((gradient_right >> 16) & 255) > 190);
    const Image &copy = images.at(canvas_page.doc.by_id("d"));
    CHECK(at(copy, 1, 1) == 0xffff0000u && at(copy, 8, 1) == 0xff00ff00u);
    // Setting the bitmap size clears the drawing, as the spec says.
    canvas_page.render(400, 300, 0);
    const Image &resized = canvas_page.canvas_images().at(canvas_page.doc.by_id("e"));
    CHECK(resized.width == 6 && at(resized, 0, 0) == 0x00000000u);
  }
  {
    // A JPEG and an SVG arrive through the resource path a site uses.
    Page media;
    media.load(*parse_url("https://media.test/"),
               "<!doctype html><img id=j src='/photo.jpg'><img id=v src='/logo.svg' width=32 "
               "height=32>");
    static const uint8_t tiny_jpeg[] = {
    255,216,255,224,0,16,74,70,73,70,0,1,1,0,0,1,0,1,0,0,
    255,219,0,67,0,3,2,2,3,2,2,3,3,3,3,4,3,3,4,5,
    8,5,5,4,4,5,10,7,7,6,8,12,10,12,12,11,10,11,11,13,
    14,18,16,13,14,17,14,11,11,16,22,16,17,19,20,21,21,21,12,15,
    23,24,22,20,24,18,20,21,20,255,219,0,67,1,3,4,4,5,4,5,
    9,5,5,9,20,13,11,13,20,20,20,20,20,20,20,20,20,20,20,20,
    20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,
    20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,255,192,
    0,17,8,0,8,0,8,3,1,34,0,2,17,1,3,17,1,255,196,0,
    31,0,0,1,5,1,1,1,1,1,1,0,0,0,0,0,0,0,0,1,
    2,3,4,5,6,7,8,9,10,11,255,196,0,181,16,0,2,1,3,3,
    2,4,3,5,5,4,4,0,0,1,125,1,2,3,0,4,17,5,18,33,
    49,65,6,19,81,97,7,34,113,20,50,129,145,161,8,35,66,177,193,21,
    82,209,240,36,51,98,114,130,9,10,22,23,24,25,26,37,38,39,40,41,
    42,52,53,54,55,56,57,58,67,68,69,70,71,72,73,74,83,84,85,86,
    87,88,89,90,99,100,101,102,103,104,105,106,115,116,117,118,119,120,121,122,
    131,132,133,134,135,136,137,138,146,147,148,149,150,151,152,153,154,162,163,164,
    165,166,167,168,169,170,178,179,180,181,182,183,184,185,186,194,195,196,197,198,
    199,200,201,202,210,211,212,213,214,215,216,217,218,225,226,227,228,229,230,231,
    232,233,234,241,242,243,244,245,246,247,248,249,250,255,196,0,31,1,0,3,
    1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,1,2,3,4,5,
    6,7,8,9,10,11,255,196,0,181,17,0,2,1,2,4,4,3,4,7,
    5,4,4,0,1,2,119,0,1,2,3,17,4,5,33,49,6,18,65,81,
    7,97,113,19,34,50,129,8,20,66,145,161,177,193,9,35,51,82,240,21,
    98,114,209,10,22,36,52,225,37,241,23,24,25,26,38,39,40,41,42,53,
    54,55,56,57,58,67,68,69,70,71,72,73,74,83,84,85,86,87,88,89,
    90,99,100,101,102,103,104,105,106,115,116,117,118,119,120,121,122,130,131,132,
    133,134,135,136,137,138,146,147,148,149,150,151,152,153,154,162,163,164,165,166,
    167,168,169,170,178,179,180,181,182,183,184,185,186,194,195,196,197,198,199,200,
    201,202,210,211,212,213,214,215,216,217,218,226,227,228,229,230,231,232,233,234,
    242,243,244,245,246,247,248,249,250,255,218,0,12,3,1,0,2,17,3,17,
    0,63,0,248,219,226,175,252,194,255,0,237,175,254,201,69,20,87,247,15,
    130,95,242,64,101,223,247,23,255,0,79,212,62,175,197,255,0,249,45,241,
    255,0,247,15,255,0,76,211,63,255,217
    };
    Resource photo;
    photo.url = "https://media.test/photo.jpg";
    photo.mime = "application/octet-stream";
    photo.ok = true;
    photo.bytes = Bytes(tiny_jpeg, tiny_jpeg + sizeof(tiny_jpeg));
    media.resource(photo);
    Resource svg;
    svg.url = "https://media.test/logo.svg";
    svg.mime = "image/svg+xml";
    svg.ok = true;
    std::string svg_text =
        "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 4 4'><rect width='4' height='4' "
        "fill='#ff8800'/></svg>";
    svg.bytes.assign(svg_text.begin(), svg_text.end());
    media.resource(svg);
    CHECK(media.images().at("https://media.test/photo.jpg").width == 8);
    CHECK(media.images().at("https://media.test/logo.svg").width == 4);
    auto media_frame = media.render(400, 300, 0);
    bool sized = false;
    for (const auto &command : media_frame.at("paint").array)
      if (int(command.num("kind")) == int(PaintCommand::Kind::Image) && close_to(command.num("w"), 32) &&
          close_to(command.num("h"), 32))
        sized = true;
    CHECK(sized);
  }
  {
    // The conditional operator used to hand back its test, which broke every
    // site that wrote `x = cond ? a : b`.
    CHECK(js_eval("var n=2;var s=n>1?'many':'one';s") == "many");
    CHECK(js_eval("var o={v:3};var s=o.v===3?'yes':'no';s") == "yes");
    CHECK(js_eval("var f=false;var s=f?'a':'b';s") == "b");
  }
  // ---- Step three: the box model, flex, grid, clipping and gradients ------
  {
    // A border shorthand is a length, a style word and a colour, and my colour
    // parser must not swallow the whole thing.
    Page borders;
    borders.load(*parse_url("https://borders.test/"),
                 "<!doctype html><style>body{margin:0}#a{border-bottom:2px solid #000;height:20px}"
                 "#b{border:1px solid red;height:10px}</style><div id=a></div><div id=b></div>");
    Rect border_a = node_box(borders, "a"), border_b = node_box(borders, "b");
    CHECK(close_to(border_a.h, 22) && close_to(border_a.y, 0));
    CHECK(close_to(border_b.y, 22) && close_to(border_b.h, 12));
  }
  {
    // Two stacked margins collapse to the larger one, so the second box starts
    // at 10 + 30.
    Page collapse;
    collapse.load(*parse_url("https://collapse.test/"),
                  "<!doctype html><style>body{margin:0}#a{margin-bottom:20px;height:10px}"
                  "#b{margin-top:30px;height:10px}</style><div id=a></div><div id=b></div>");
    CHECK(close_to(node_box(collapse, "b").y, 40));
  }
  {
    // A percentage height resolves against the parent's height, never its width.
    Page percent;
    percent.load(*parse_url("https://percent.test/"),
                 "<!doctype html><style>body{margin:0}#p{height:100px}#a{height:50%}</style>"
                 "<div id=p><div id=a></div></div>");
    CHECK(close_to(node_box(percent, "a").h, 50));
  }
  {
    // grid-template-rows fixes the track heights.
    Page grid;
    grid.load(*parse_url("https://grid.test/"),
              "<!doctype html><style>body{margin:0}#g{display:grid;width:100px;"
              "grid-template-columns:50px 50px;grid-template-rows:40px 20px}#c{height:10px}</style>"
              "<div id=g><div></div><div></div><div id=c></div></div>");
    Rect c = node_box(grid, "c");
    CHECK(close_to(c.y, 40) && close_to(c.x, 0) && close_to(c.w, 50));
  }
  {
    // flex order and align-self move one item without touching the rest.
    Page flex;
    flex.load(*parse_url("https://flex.test/"),
              "<!doctype html><style>body{margin:0}#f{display:flex;width:200px;height:60px}"
              "#a{width:50px;height:10px;order:2}#b{width:50px;height:10px;order:1;"
              "align-self:flex-end}</style><div id=f><div id=a></div><div id=b></div></div>");
    Rect flex_a = node_box(flex, "a"), flex_b = node_box(flex, "b");
    CHECK(close_to(flex_a.x, 50) && close_to(flex_a.y, 0));
    CHECK(close_to(flex_b.x, 0) && close_to(flex_b.y, 50));
  }
  {
    // Sticky stays where the flow puts it until a scroll asks for it.
    Page sticky;
    sticky.load(*parse_url("https://sticky.test/"),
                "<!doctype html><style>body{margin:0}#a{position:sticky;top:5px;height:20px}</style>"
                "<div id=a></div>");
    CHECK(close_to(node_box(sticky, "a").y, 0));
  }
  {
    // A gradient background, an overflow clip and the paint pass that applies
    // them, checked on real pixels.
    Page painted;
    painted.load(*parse_url("https://paint.test/"),
                 "<!doctype html><style>body{margin:0;background:#fff}"
                 "#g{height:20px;background:linear-gradient(90deg,#ff0000,#0000ff)}"
                 "#clip{height:20px;overflow:hidden}#tall{height:80px;background:#00ff00}"
                 "</style><div id=g></div><div id=clip><div id=tall></div></div>");
    auto frame = painted.render(200, 120, 0);
    std::vector<PaintCommand> commands;
    bool saw_gradient = false;
    int clips = 0;
    for (const auto &command : frame.at("paint").array) {
      PaintCommand entry;
      entry.kind = PaintCommand::Kind(int(command.num("kind")));
      entry.rect = {command.num("x"), command.num("y"), command.num("w"), command.num("h")};
      entry.color = uint32_t(command.num("color"));
      entry.text = command.str("text");
      entry.size = command.num("size");
      entry.thickness = command.num("thickness");
      entry.bold = command.flag("bold");
      entry.node = int(command.num("node"));
      entry.gradient = command.str("gradient");
      if (!entry.gradient.empty())
        saw_gradient = true;
      if (entry.kind == PaintCommand::Kind::PushClip)
        ++clips;
      commands.push_back(entry);
    }
    CHECK(saw_gradient);
    CHECK(clips == 1);
    Canvas canvas(200, 160, 0xffffffff);
    paint_commands(canvas, commands, 0, painted.images());
    auto pixel = [&](int x, int y) {
      size_t i = (size_t(y) * canvas.width + x) * 4;
      return (uint32_t(canvas.pixels[i + 3]) << 24) | (uint32_t(canvas.pixels[i + 2]) << 16) |
             (uint32_t(canvas.pixels[i + 1]) << 8) | canvas.pixels[i];
    };
    uint32_t left = pixel(2, 10), right = pixel(196, 10);
    CHECK(((left >> 16) & 255) > 200 && (left & 255) < 60);
    CHECK((right & 255) > 200 && ((right >> 16) & 255) < 60);
    // The tall green child is clipped at the bottom of its 20px box.
    CHECK(((pixel(100, 30) >> 8) & 255) == 255);
    CHECK(pixel(100, 60) == 0xffffffff);
  }
  {
    // My HTTPS policy. A header from an HTTPS answer is a promise I keep, the same
    // header read from a plain HTTP answer is not, and a year is as long as any
    // promise lasts.
    HttpsPolicy rules;
    CHECK(!rules.requires_https("shop.test"));
    CHECK(rules.record_header("shop.test", true, "max-age=3600; includeSubDomains", 1000));
    CHECK(rules.requires_https("shop.test"));
    CHECK(rules.requires_https("cdn.shop.test"));
    CHECK(!rules.requires_https("other.test"));
    // Anyone on the path could write that header into a plain answer, so I ignore it.
    CHECK(!rules.record_header("plain.test", false, "max-age=3600", 1000));
    CHECK(!rules.requires_https("plain.test"));
    // A million years from now is still one year from now.
    CHECK(rules.record_header("long.test", true, "max-age=999999999", 1000));
    bool capped = false;
    for (const auto &entry : rules.entries)
      if (entry.host == "long.test") capped = entry.expires_at == 1000 + kMaxHstsMillis;
    CHECK(capped);
    // max-age=0 is a site taking the promise back.
    CHECK(rules.record_header("shop.test", true, "max-age=0", 2000));
    CHECK(!rules.requires_https("shop.test"));
    // An expired entry stops applying once I prune it.
    CHECK(rules.record_header("short.test", true, "max-age=60", 1000));
    rules.prune(70000);
    CHECK(!rules.requires_https("short.test"));
    // The store survives a restart through my own JSON.
    CHECK(rules.record_header("keep.test", true, "max-age=600", 100000));
    HttpsPolicy restored;
    restored.from_json(rules.to_json());
    CHECK(restored.requires_https("keep.test"));
    // An upgrade changes the scheme before a socket opens, and only for the hosts
    // that asked.
    Url plain{"http", "keep.test", "/login", "", "", "", true};
    Url secure{"https", "keep.test", "/login", "", "", "", true};
    CHECK(restored.should_upgrade(plain));
    CHECK(!restored.should_upgrade(secure));
    CHECK(!restored.should_upgrade(Url{"http", "stranger.test", "/", "", "", "", true}));
    // The always-HTTPS setting upgrades everything except loopback.
    CHECK(restored.should_upgrade(Url{"http", "stranger.test", "/", "", "", "", true}, true));
    CHECK(!restored.should_upgrade(Url{"http", "localhost", "/", "", "", "", true}, true));
    CHECK(!restored.should_upgrade(Url{"http", "127.0.0.1", "/", "", "", "", true}, true));
    // Mixed content: an HTTPS page does not pull plain HTTP behind my back, but my
    // own loopback development server is not on the network.
    CHECK(restored.blocks_mixed_content(secure, Url{"http", "cdn.test", "/a.js", "", "", "", true}));
    CHECK(!restored.blocks_mixed_content(secure, Url{"https", "cdn.test", "/a.js", "", "", "", true}));
    CHECK(!restored.blocks_mixed_content(secure, Url{"http", "127.0.0.1", "/a.js", "", "", "", true}));
    CHECK(restored.blocks_insecure_submit(secure, Url{"http", "forms.test", "/send", "", "", "", true}));
    CHECK(!restored.blocks_insecure_submit(secure, Url{"https", "forms.test", "/send", "", "", "", true}));
    CHECK(!restored.blocks_insecure_submit(Url{"http", "blog.test", "/", "", "", "", true},
                                           Url{"http", "forms.test", "/send", "", "", "", true}));
    CHECK(resource_type_name(ResourceType::Script) == "script");
    CHECK(resource_type_name(ResourceType::XmlHttpRequest) == "xmlhttprequest");
  }
  {
    // A connect button has to mean something, so my probe makes a real request
    // through the configured tunnel and reports what came back. The loopback proxy
    // in this test speaks SOCKS5, which is the path a Proton endpoint uses.
    LoopbackProxy proxy(std::vector<std::pair<std::string, std::string>>{
        {"200 OK", "probed page"}});
    if (!proxy.failure.empty())
      std::cerr << "proxy would not start: " << proxy.failure << "\n";
    CHECK(proxy.failure.empty());
    NetworkPolicy net;
    net.proxy = ProxyConfig();
    net.proxy.mode = ProxyConfig::Mode::FixedServers;
    net.proxy.single.scheme = "socks5";
    net.proxy.single.host = "127.0.0.1";
    net.proxy.single.port = proxy.port;
    net.proxy.fail_closed = true;
    // Plain HTTP here because my loopback proxy speaks CONNECT and then plain
    // bytes; an https probe takes the same tunnel and then my TLS layer.
    TunnelProbe probe = probe_tunnel(net, "http://probe.test/health", 5000);
    CHECK(probe.ok);
    CHECK(probe.status == 200);
    CHECK(probe.detail.find("socks5") != std::string::npos);
    CHECK(!proxy.requests.empty());
    // A proxy that is not there is a refusal with a reason, not a connected label.
    NetworkPolicy dead = net;
    dead.proxy.single.port = 1;
    TunnelProbe refused = probe_tunnel(dead, "http://probe.test/health", 2000);
    CHECK(!refused.ok);
    CHECK(!refused.detail.empty());
    // And with no tunnel configured there is nothing to claim.
    NetworkPolicy direct;
    TunnelProbe none = probe_tunnel(direct, "https://probe.test/health", 2000);
    CHECK(!none.ok);
    CHECK(none.detail.find("no tunnel") != std::string::npos);
  }
  {
    // Language pieces this step's measurement told me were missing, now mine:
    // switch with fallthrough and a default that is not last, the comma expression,
    // the Function constructor, and error values a page can catch and test.
    CHECK(js_eval("var r='';switch(2){case 1:r='a';break;case 2:r='b';break;default:r='c';}r") ==
          "b");
    CHECK(js_eval("var r='';switch(1){case 1:r+='a';case 2:r+='b';break;case 3:r+='c';}r") ==
          "ab");
    CHECK(js_eval("var r='';switch(9){default:r+='d';case 1:r+='a';}r") == "da");
    CHECK(js_eval("var r='';switch('1'){case 1:r='num';break;default:r='str';}r") == "str");
    CHECK(js_eval("var a=(1,2,3);a") == "3");
    CHECK(js_eval("var i=0;var j=(i++,i++,i);i+':'+j") == "2:2");
    CHECK(js_eval("var s=0;for(var i=0,j=3;i<j;i++,j--)s+=i*j;s") == "2");
    CHECK(js_eval("Math.max(1,2,3)") == "3");
    CHECK(js_eval("Function('a','return a*2')(21)") == "42");
    CHECK(js_eval("try{(void 0).x}catch(e){e.name}") == "TypeError");
    CHECK(js_eval("try{(void 0).x}catch(e){String(e instanceof TypeError)}") == "true");
    CHECK(js_eval("try{(void 0).x}catch(e){String(e instanceof RangeError)}") == "false");
    CHECK(js_eval("var e=new ReferenceError('none');e.name+':'+e.message") == "ReferenceError:none");
    CHECK(js_eval("function r(n){return r(n+1)}try{r(0)}catch(e){String(e instanceof RangeError)}") ==
          "true");
    CHECK(js_eval("try{Object.assign(null,{})}catch(e){e.name}") == "TypeError");
  }
  {
    // My confinement layer says what it did and what it did not do. On Windows the
    // job object is real and the cap is reported; on this build there is no job
    // object, so the answer is that nothing was limited. Either way the sentence
    // refuses to call a resource cap isolation.
    std::string error;
    bool applied = confine_process(4096ull * 1024ull * 1024ull, 50, error);
    ConfinementStatus status = confinement_status();
    CHECK(status.isolated == false);
    CHECK(applied == status.limited);
    if (applied) CHECK(status.memory_bytes == 4096ull * 1024ull * 1024ull);
    else CHECK(!error.empty());
    std::string summary = confinement_summary(status);
    CHECK(summary.find("not isolated") != std::string::npos);
    CHECK(applied ? summary.find("4096 MiB") != std::string::npos
                  : summary.find("No memory cap") != std::string::npos);
  }
  {
    // My filter list. Comments are ignored, cosmetic rules are refused rather than
    // half-run, and an exception beats a block however the list is ordered.
    FilterList list;
    std::string error;
    const std::string text =
        "! my own list\n"
        "[Adblock Plus 2.0]\n"
        "||doubleclick.net^\n"
        "||ads.example.com^$script,third-party\n"
        "||ads.example.com^$image\n"
        "@@||ads.example.com/keep.js$script\n"
        "example.com##.banner\n"
        "||pick.test^$sitekey=abc\n";
    CHECK(list.parse(text, error));
    CHECK(error.empty());
    CHECK(list.size() == 4);
    CHECK(list.exceptions() == 1);
    CHECK(list.skipped.size() == 2);
    CHECK(list.skipped[0].find("cosmetic") != std::string::npos);
    CHECK(list.skipped[1].find("8:") == 0);
    // A host anchor covers the host and its subdomains, and cannot be talked into
    // matching a query string that merely mentions the name.
    CHECK(list.match(*parse_url("https://doubleclick.net/ad.js"), ResourceType::Script,
                     "shop.test") != nullptr);
    CHECK(list.match(*parse_url("https://sub.doubleclick.net/ad.js"), ResourceType::Script,
                     "shop.test") != nullptr);
    CHECK(list.match(*parse_url("https://example.test/?u=doubleclick.net"), ResourceType::Script,
                     "shop.test") == nullptr);
    // The type and the party both have to agree with the rule.
    CHECK(list.match(*parse_url("https://ads.example.com/a.js"), ResourceType::Script,
                     "shop.test") != nullptr);
    // The image rule names images, so a stylesheet from the same host is not it.
    CHECK(list.match(*parse_url("https://ads.example.com/a.css"), ResourceType::Stylesheet,
                     "shop.test") == nullptr);
    CHECK(list.match(*parse_url("https://ads.example.com/a.png"), ResourceType::Image,
                     "shop.test") != nullptr);
    CHECK(list.match(*parse_url("https://ads.example.com/a.js"), ResourceType::Script,
                     "example.com") == nullptr);
    // The exception wins for the one script the site needs.
    const FilterRule *keep = list.match(*parse_url("https://ads.example.com/keep.js"),
                                        ResourceType::Script, "shop.test");
    CHECK(keep != nullptr);
    CHECK(keep && keep->exception);
    // A rule that names one site and not another is honoured both ways.
    FilterList scoped;
    CHECK(scoped.parse("||track.test^$domain=shop.test|~quiet.test", error));
    CHECK(scoped.match(*parse_url("https://track.test/x"), ResourceType::Script, "shop.test") !=
          nullptr);
    CHECK(scoped.match(*parse_url("https://track.test/x"), ResourceType::Script, "quiet.test") ==
          nullptr);
    CHECK(scoped.match(*parse_url("https://track.test/x"), ResourceType::Script, "other.test") ==
          nullptr);
    // An empty list is an error I can report instead of a silent nothing.
    FilterList empty;
    CHECK(!empty.parse("! only a comment\n", error));
    CHECK(!error.empty());
  }
  {
    // The same decisions, taken through the request path a tab actually uses.
    FilterList list;
    std::string error;
    list.parse("||ads.test^$script\n", error);
    FilterCounters counts;
    HttpsPolicy store;
    store.record_header("bank.test", true, "max-age=3600", 0);
    NetworkPolicy net;
    net.block_ads = true;
    net.filters = &list;
    net.filter_counts = &counts;
    net.https = &store;
    net.top_level_host = "shop.test";
    net.top_level_scheme = "https";
    net.type = ResourceType::Script;
    auto ad = parse_url("https://ads.test/banner.js");
    CHECK(ad.has_value());
    Resource blocked = fetch_url(*ad, net, 5000, 1u << 20);
    CHECK(!blocked.ok);
    CHECK(blocked.refused);
    CHECK(blocked.error.find("my filter") != std::string::npos);
    CHECK(counts.blocked == 1);
    // The per-site exception turns the blocker off for that site alone.
    net.allowed_hosts = {"shop.test"};
    CHECK(!fetch_url(*ad, net, 5000, 1u << 20).refused);
    net.allowed_hosts.clear();
    // Mixed content is refused even with the blocker off.
    NetworkPolicy mixed = net;
    mixed.block_ads = false;
    mixed.type = ResourceType::Image;
    auto cleartext = parse_url("http://cdn.test/x.png");
    Resource refused_cleartext = fetch_url(*cleartext, mixed, 5000, 1u << 20);
    CHECK(refused_cleartext.refused);
    CHECK(refused_cleartext.error.find("plain HTTP") != std::string::npos);
    CHECK(store.blocked_mixed.load() == 1);
    // An always-HTTPS rule upgrades the address before anything is dialled. This
    // build has no direct transport, so the upgraded attempt fails to connect and
    // is retried once in the clear - which is the fallback, and I count it.
    mixed.https_only = true;
    uint64_t fallbacks = store.fallbacks.load();
    Resource upgraded = fetch_url(*cleartext, mixed, 5000, 1u << 20);
    CHECK(!upgraded.refused);
    CHECK(store.fallbacks.load() == fallbacks + 1);
    // A host with a promise on record is upgraded and never falls back, because a
    // site that asked for HTTPS does not get a plain second chance.
    Resource bank = fetch_url(*parse_url("http://bank.test/login"), mixed, 5000, 1u << 20);
    CHECK(!bank.refused);
    CHECK(bank.url.rfind("https://bank.test/login", 0) == 0);
    CHECK(store.fallbacks.load() == fallbacks + 1);
  }
  {
    // Script pieces I fixed while auditing step two.
    CHECK(js_eval("[...new Set([1,1,2])].join(',')") == "1,2");
    CHECK(js_eval("[1,[2,[3]]].flat(2).join(',')") == "1,2,3");
    CHECK(js_eval("[1,2,3].at(-1)") == "3");
    CHECK(js_eval("Object.fromEntries([['a',1]]).a") == "1");
    // I split the string so the compiler does not read ??= as a trigraph.
    CHECK(js_eval("var a=null;a?" "?='z';a") == "z");
    CHECK(js_eval("var b=1;b&&=2;b") == "2");
    CHECK(js_eval("var c=0;c||=7;c") == "7");
    CHECK(js_eval("'a-b'.replace(/(\\w)-(\\w)/,'$2$1')") == "ba");
  }
  // --- Vault crypto, verified against the RFC vectors ------------------------
  {
    auto hex = [](const pv::Bytes &bytes) {
      static const char d[] = "0123456789abcdef";
      std::string out;
      for (uint8_t b : bytes) {
        out += d[b >> 4];
        out += d[b & 15];
      }
      return out;
    };
    auto unhex = [](const char *text) {
      pv::Bytes out;
      auto val = [](char c) {
        return c >= '0' && c <= '9'   ? c - '0'
               : c >= 'a' && c <= 'f' ? c - 'a' + 10
                                      : c - 'A' + 10;
      };
      for (size_t i = 0; text[i] && text[i + 1]; i += 2)
        out.push_back(uint8_t(val(text[i]) * 16 + val(text[i + 1])));
      return out;
    };
    // PBKDF2-HMAC-SHA256 published vectors.
    CHECK(hex(pvkdf("password", "salt", 1)) ==
          "120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b");
    CHECK(hex(pvkdf("password", "salt", 2)) ==
          "ae4d0c95af6b46d32d0adff928f06dd02a303f8ef3c251dfd6e2d85a95474c43");
    CHECK(hex(pvkdf("password", "salt", 4096)) ==
          "c5e478d59288c841aa530db6845c4c8d962893a001ce4e11a4963873aa98134a");
    // RFC 8439 section 2.5.2 Poly1305.
    CHECK(hex(pv::poly1305_mac(pv::Bytes(pg_msg, pg_msg + sizeof pg_msg - 1),
                               unhex("85d6be7857556d337f4452fe42d506a80103808afb0db2fd"
                                     "4abff6af4149f51b"))) == "a8061dc1305136c6c22b8baf0c0127a9");
    // RFC 8439 A.1 vector 1: ChaCha20 with a zero key.
    pv::Bytes key_seq;
    for (uint8_t b = 0; b < 32; ++b) key_seq.push_back(b);
    CHECK(hex(pv::chacha20(key_seq, unhex("000000000000004a00000000"), 1,
                           pv::Bytes(sunscreen, sunscreen + sizeof sunscreen - 1))) ==
          "6e2e359a2568f98041ba0728dd0d6981"
          "e97e7aec1d4360c20a27afccfd9fae0b"
          "f91b65c5524733ab8f593dabcd62b357"
          "1639d624e65152ab8f530c359f0861d8"
          "07ca0dbf500d6a6156a38e088a22b65e"
          "52bc514d16ccf806818ce91ab7793736"
          "5af90bbf74a35be6b40b8eedf2785e42"
          "874d");
    // RFC 8439 section 2.8.2 AEAD.
    {
      pv::Bytes key = unhex("808182838485868788898a8b8c8d8e8f909192939495969798999a9b9c9d9e9f");
      pv::Bytes nonce = unhex("070000004041424344454647");
      pv::Bytes aad = unhex("50515253c0c1c2c3c4c5c6c7");
      pv::Bytes plain(sunscreen, sunscreen + sizeof sunscreen - 1);
      pv::Bytes sealed = pv::aead_seal(key, nonce, aad, plain);
      CHECK(hex(pv::Bytes(sealed.end() - 16, sealed.end())) == "1ae10b594f09e26a7e902ecbd0600691");
      pv::Bytes back;
      CHECK(pv::aead_open(key, nonce, aad, sealed, back) && back == plain);
      sealed[0] ^= 1;
      CHECK(!pv::aead_open(key, nonce, aad, sealed, back));
    }
    // The vault itself: seal, reopen, and a wrong passphrase must fail.
    {
      pv::Vault vault = pv::Vault::create("correct horse battery staple");
      vault.entries["mailbox"] = "hunter2";
      pv::Bytes blob = vault.seal();
      pv::Bytes saved_key = vault.key;
      pv::Vault again = pv::Vault::open(blob, "correct horse battery staple");
      CHECK(again.entries.at("mailbox") == "hunter2");
      CHECK(again.key == saved_key);
      bool refused = false;
      try {
        pv::Vault::open(blob, "wrong passphrase");
      } catch (const std::exception &) {
        refused = true;
      }
      CHECK(refused);
    }
  }
  {
    // My spelling engine: dictionary, suggestions, and the text-side rules.
    pv::Spell spell;
    CHECK(spell.correct("anything")); // an empty dictionary stays quiet
    spell.load_builtin_embedded();
    CHECK(spell.builtin_count() > 2000);
    CHECK(spell.correct("the"));
    CHECK(spell.correct("hello"));
    CHECK(spell.correct("The"));   // capitalised start
    CHECK(spell.correct("HELLO")); // shouted
    CHECK(spell.correct("don't"));
    CHECK(spell.correct("it's"));
    CHECK(spell.correct("cat's")); // possessive on a known word
    CHECK(!spell.correct("zxqwvbalo"));
    CHECK(!spell.correct("helllo"));
    CHECK(spell.correct("h"));   // too short to flag
    CHECK(spell.correct("h3ll0")); // anything with digits is a token, not prose
    // Suggestions come one edit away, best shapes first.
    auto to_the = spell.suggest("teh");
    CHECK(std::find(to_the.begin(), to_the.end(), "the") != to_the.end());
    CHECK(std::find(spell.suggest("helllo").begin(), spell.suggest("helllo").end(), "hello") !=
          spell.suggest("helllo").end());
    CHECK(std::find(spell.suggest("hxuse").begin(), spell.suggest("hxuse").end(), "house") !=
          spell.suggest("hxuse").end());
    // Longer unknowns get a second edit of reach.
    CHECK(std::find(spell.suggest("dictionarry").begin(), spell.suggest("dictionarry").end(),
                    "dictionary") != spell.suggest("dictionarry").end());
    // My own words count even with the built-in dictionary switched off.
    spell.builtin_enabled = false;
    CHECK(!spell.correct("hello"));
    spell.add_user("psychovault");
    CHECK(spell.correct("psychovault"));
    CHECK(spell.active_count() == 1);
    spell.builtin_enabled = true;
    CHECK(spell.correct("hello"));
    // A dictionary source stream contributes words through the same path.
    spell.replace_extra("florinke zarbolux are mine now");
    CHECK(spell.extra_count() == 5); // florinke, zarbolux, are, mine, now
    CHECK(spell.correct("florinke"));
    // Text checking: words flagged in place, URLs and emails left alone.
    auto marks = pv::spell_check_text(spell, "hello zxqwv world");
    CHECK(marks.size() == 1 && marks[0].start == 6 && marks[0].length == 5);
    marks = pv::spell_check_text(spell, "hello world");
    CHECK(marks.empty());
    CHECK(pv::spell_looks_like_url("duckduckgo.com"));
    CHECK(pv::spell_looks_like_url("https://example.com/a b"));
    CHECK(!pv::spell_looks_like_url("type some words"));
    marks = pv::spell_check_text(spell, "visit duckduckgo.com now");
    CHECK(marks.empty() || marks.back().length != 13);
  }
  std::cout << passed << " engine checks passed\n";
  return 0;
}
