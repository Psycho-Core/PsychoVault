#!/usr/bin/env python3
"""I audit authored source boundaries. This is not a security or conformance proof."""
from pathlib import Path
import re, subprocess, sys
ROOT=Path(__file__).resolve().parents[1]
files=subprocess.check_output(['git','ls-files'],cwd=ROOT,text=True).splitlines()
fail=[]; passed=0
def check(name,ok,detail=''):
    global passed
    if ok: passed+=1; print('PASS',name,('- '+detail) if detail else '')
    else: fail.append(name); print('FAIL',name,('- '+detail) if detail else '')
source=[f for f in files if f.endswith(('.cpp','.hpp','.py','.ps1','.bat','.yml','.html')) and not f.startswith('docs/reference/')]
check('authored source files present',len(source)>20,str(len(source)))
for f in source:
    text=(ROOT/f).read_text(encoding='utf-8',errors='replace')
    low=text.lower()
    banned=['chromium','webkit','gecko','webview2','electron','v8::','cefclient','servo','skia','qtwebengine']
    # Documentation/workflow comments may name rejected engines; source includes must not import them.
    if f.endswith(('.cpp','.hpp')):
        bad=[b for b in banned if re.search(r'#\s*include[^\n]*'+re.escape(b),low)]
        check('no embedded engine include: '+f,not bad,','.join(bad))
        check('no shell escape in engine source: '+f,not re.search(r'\b(system|_wsystem|popen|_popen)\s*\(',text))
for f in ['package.json','package-lock.json','Cargo.toml','vcpkg.json','conanfile.txt']:
    check('no runtime package manifest: '+f,not (ROOT/f).exists())
engine_network=[f for f in source if f.startswith('engine/') and re.search(r'#\s*include[^\n]*(winhttp|winsock|curl|openssl)',(ROOT/f).read_text(errors='replace'),re.I)]
check('engine has no direct network stack include',not engine_network,','.join(engine_network))
workflow=(ROOT/'.github/workflows/Psycho_owned-engine.yml').read_text()
check('one heavy build concurrency group','group: psychovault-heavy' in workflow and 'cancel-in-progress: false' in workflow)
check('hosted build is Windows','runs-on: windows-2022' in workflow)
check('double-click build delegates to GitHub','gh workflow run' in (ROOT/'tools/Psycho_start-build.ps1').read_text())
check('no release publish step in development workflow','gh release create' not in workflow and 'softprops/action-gh-release' not in workflow)
# My release pipeline must never publish an unverified or mislabelled build.
release_path=ROOT/'.github/workflows/Psycho_release.yml'
check('I have a release workflow',release_path.exists())
if release_path.exists():
    release=release_path.read_text()
    order=[('build','Psycho_build.py'),('tests','pv-tests.exe'),('solo','psychovault-solo'),
           ('package','Psycho_package.py'),('verify','verify_package.py'),('publish','gh release create')]
    positions=[(label,release.find(token)) for label,token in order]
    check('every release stage is present',all(pos>=0 for _,pos in positions),
          ','.join(label for label,pos in positions if pos<0))
    check('I publish only after my build, tests, single-exe gate and hash verification',
          all(positions[i][1]<positions[i+1][1] for i in range(len(positions)-1)))
    check('release workflow shares my one-heavy-build group','group: psychovault-heavy' in release)
    check('I refuse to overwrite a published release','I do not overwrite a published release' in release)
    check('I refuse a tag that does not match my version file','does not match Psycho_VERSION.txt' in release)
    check('I verify asset hashes before publishing',
          'hash mismatch' in (ROOT/'tools/Psycho_verify_package.py').read_text() and
          'tools/Psycho_verify_package.py' in release)
    check('I verify published assets after publishing','gh release view' in release)
    check('I mark the preview as a prerelease','--prerelease' in release)
    # My notes now live in my repo, so the workflow and the file must agree.
    notes_text = (ROOT/'docs/Psycho_RELEASE-NOTES.md').read_text() if (ROOT/'docs/Psycho_RELEASE-NOTES.md').exists() else release
    check('release notes state YouTube and Netflix do not play','YouTube and Netflix do not play' in notes_text)
    check('release notes state Proton is not bundled','Proton VPN is not bundled' in notes_text)

# My extension boundary must stay default-deny.
extension=(ROOT/'platform/Psycho_extension.cpp').read_text()
check('my extension host grants permissions explicitly','bool ExtensionHost::grant(' in extension)
check('a denied API raises a catchable JS error','throw_extension_error' in extension)
check('I validate extension script paths against traversal','unsafe script path' in extension)
proxy=(ROOT/'platform/Psycho_proxy.cpp').read_text()
check('my proxy refuses to leak when fail-closed','fail-closed' in proxy)
network=(ROOT/'platform/Psycho_network.cpp').read_text()
check('I refuse a proxy scheme I cannot tunnel','refusing the request rather than connecting directly' in network)
check('I stream downloads to a partial file before committing','psychovault-part' in network)

# My page renders inside my own executable, so one file is the whole browser.
build_text=(ROOT/'tools/Psycho_build.py').read_text()
check('I compile my own page host into my browser','"platform/Psycho_host.cpp"' in build_text)
check('my browser links my engine, my page host and my shell together',
      'target("PsychoVault",ENGINE+PLATFORM+["shell/Psycho_win_main.cpp"]' in build_text)
check('I build no second executable to render pages','pv-renderer' not in build_text)
check('I need no resource compiler to embed a renderer','rc.exe' not in build_text)
shell_text=(ROOT/'shell/Psycho_win_main.cpp').read_text()
check('my shell renders through my own page host','Host host(' in shell_text and 'host.load(' in shell_text)
check('my shell starts no helper process','CreateProcess' not in shell_text)
check('my shell never assumes a sibling renderer file','pv-renderer' not in shell_text)
host_hpp=(ROOT/'platform/Psycho_host.hpp').read_text()
host_cpp=(ROOT/'platform/Psycho_host.cpp').read_text()
check('my page host owns one engine page','std::unique_ptr<Page> page_;' in host_hpp)
check('my page host fetches only through a callback',
      'using Fetcher = std::function<Resource(const std::string &url, ResourceType type)>;'
      in host_hpp)
check('my page host asks for the kind of resource it needs','ResourceType type' in host_hpp)
check('my page host never opens the network itself',
      not re.search(r'winhttp|socket\s*\(|connect\s*\(',host_cpp,re.I))
check('my page host hands images over without base64','base64' not in host_cpp)
check('my page host reports a frame after every change','void Host::emit()' in host_cpp and 'settle();' in host_cpp)
for gone in ('platform/Psycho_ipc.cpp','platform/Psycho_process.cpp','platform/Psycho_payload.cpp'):
    check('I removed the helper process plumbing: '+gone,not (ROOT/gone).exists())
tests_text=(ROOT/'tests/Psycho_test_main.cpp').read_text()
check('my tests drive my page host directly','#include "../platform/Psycho_host.hpp"' in tests_text and 'Host host(' in tests_text)
# My pattern engine, my promise queue and my async support are my own code.
regex_cpp=(ROOT/'engine/Psycho_regex.cpp').read_text()
regex_hpp=(ROOT/'engine/Psycho_regex.hpp').read_text()
check('I wrote my own pattern engine','std::optional<Regex> Regex::compile' in regex_cpp)
check('my pattern engine matches on decoded code points','std::vector<uint32_t>' in regex_hpp)
check('my pattern engine stops at a step limit instead of hanging',
      'kStepLimit' in regex_cpp and 'kNestingLimit' in regex_cpp)
check('my pattern engine reaches the build',"engine/Psycho_regex.cpp" in build_text)
runtime_text=(ROOT/'engine/Psycho_js_runtime.cpp').read_text()
check('my runtime compiles a pattern once per object','Value Runtime::make_regex' in runtime_text)
check('my strings take a pattern in match, search, split and replace',
      all(k in runtime_text for k in ('Value Runtime::string_match','Value Runtime::string_search',
                                      'Value Runtime::string_split','Value Runtime::string_replace')))
check('my promise handlers are queued instead of running inline',
      'Value Runtime::promise_then' in runtime_text and 'enqueue_microtask' in runtime_text)
check('my collector treats queued work as a root','for (auto &task : microtasks_)' in runtime_text)
check('an async function hands back a promise','object->function->async' in runtime_text)
check('await cannot freeze a tab forever','await never settled' in runtime_text)
parser_text=(ROOT/'engine/Psycho_js_parser.cpp').read_text()
check('my lexer tells a pattern literal from a division','regex_allowed' in parser_text)
check('I parse async functions, async arrows and await',
      'async_expression' in parser_text and 'Expr::Kind::Await' in parser_text)
page_text=(ROOT/'engine/Psycho_page.cpp').read_text()
check('my page installs fetch on my own fetcher','void Page::install_fetch()' in page_text)
check('my fetch cannot bypass my network policy','fetcher(href)' in page_text)
check('my await pump still lets due timers run','bool Page::pump_for_await()' in page_text)
check('my page host gives my engine its own fetcher','page_->fetcher = [this]' in host_cpp)
# My CSS and layout handle the boxes real pages are built from.
css_text=(ROOT/'engine/Psycho_css.cpp').read_text()
check('I parse positioning, offsets, flex, grid and transforms',
      all(k in css_text for k in ('"position"','"flex-grow"','"grid-template-columns"','"transform"','"right"')))
layout_text=(ROOT/'engine/Psycho_layout.cpp').read_text()
check('my layout places an absolute box against its containing block','place_positioned(' in layout_text)
check('my layout offsets a relative box without moving the flow','move_box(' in layout_text)
check('my flexbox covers direction, wrapping, growth and alignment',
      all(k in layout_text for k in ('flex_children','justify_content','align_items','flex_wrap')))
check('my grid turns a track list into track widths','track_sizes(' in layout_text)
check('my transforms move what a box painted','apply_transform(' in layout_text)
check('a fixed box ignores the scroll position','if (!c.fixed)' in layout_text)
check('my tests measure my layout cases','node_box(' in tests_text)
# The step 2 audit fixes stay fixed.
check('my iterator walk is shared by spread and for..of','bool Runtime::iterate(' in runtime_text)
check('my fetch rejects only when no response arrived','bool received = res.status >= 100;' in page_text)
check('my strings honour a split limit','size_t stop = limit >=' in runtime_text)
check('my tests cover patterns, promises, async and fetch',
      all(k in tests_text for k in ('/ab+c/.test','Promise.all(','async function',"fetch('/api.json'")))
for wf in ('.github/workflows/Psycho_owned-engine.yml','.github/workflows/Psycho_release.yml'):
    body=(ROOT/wf).read_text()
    check('I prove my single exe renders alone: '+wf,
          'psychovault-solo' in body and 'expected 1' in body and '--expect-image' in body)
    check('my gate proves my lone exe left one file behind: '+wf,'left $after files' in body)
    check('my gate rejects a capture that is not a real PNG: '+wf,'is not a real PNG' in body)
    check('my gates no longer claim a separate renderer: '+wf,
          'pv-renderer' not in body and '--sandbox-test' not in body)
package_text=(ROOT/'tools/Psycho_package.py').read_text()
check('my verifier is a real tool, not an inline copy',(ROOT/'tools/Psycho_verify_package.py').exists())
check('my packager writes my checksums where my manifest names them',
      'published_checksums = out / "SHA256SUMS.txt"' in package_text)
check('my manifest points at those checksums',
      'asset("SHA256SUMS.txt", published_checksums, "checksums")' in package_text)
check('my development build verifies my package too',
      'tools/Psycho_verify_package.py' in (ROOT/'.github/workflows/Psycho_owned-engine.yml').read_text())
check('my release build verifies my package with the same tool',
      'tools/Psycho_verify_package.py' in (ROOT/'.github/workflows/Psycho_release.yml').read_text())
check('my update payload is the single executable',
      'asset("PsychoVault.exe", out / "PsychoVault.exe", "update")' in (ROOT/'tools/Psycho_package.py').read_text())
instructions=(ROOT/'docs/Psycho_TEST-BUILD.md').read_text()
check('my test instructions need no shell commands','```powershell' not in instructions.lower())
check('my test instructions start with a double-click','double-click' in instructions.lower())

# My required gates are only real if my browser actually parses their flags.
for flag in ('--expect-image',):
    check('my shell parses '+flag, 'args[i] == "'+flag+'"' in shell_text)
check('my shell refuses an option it does not understand',
      'I do not understand the option' in shell_text)

# Step 4: my browsing state is mine, and it reaches the network the long way.
cookie_text=(ROOT/'engine/Psycho_cookies.cpp').read_text()
network_text=(ROOT/'platform/Psycho_network.cpp').read_text()
storage_text=(ROOT/'engine/Psycho_storage.cpp').read_text()
check('my cookie jar keeps HttpOnly away from a script',
      'from_document' in cookie_text and 'cookie.http_only' in cookie_text)
check('my cookie jar refuses a cookie for another domain','refused outright' in cookie_text)
check('my cookie jar only sends a Secure cookie over https',
      'cookie.secure && url.scheme != "https"' in cookie_text)
check('my web storage enforces a quota','kQuota' in storage_text and 'return false' in storage_text)
check('my page hands a form post to my host with a content type',
      'post.content_type = "application/x-www-form-urlencoded"' in page_text)
check('my host posts a form through my own network stack','options.method = "POST"' in shell_text)
check('my network layer attaches my cookies to a request','L"Cookie: "' in network_text)
check('my network layer keeps what a response set','set_from_header(url, header' in network_text)
check('my find in page asks my engine to walk the painted text',
      'find_marks' in shell_text and 'find_in_paint' in shell_text)
check('my zoom scales the paint list and the clicks','/ current().zoom' in shell_text)
check('my tests cover cookies, storage, history and a real form post',
      all(k in tests_text for k in ('CookieJar jar;','Storage store;','history.pushState',
                                    'page.posts.size()')))

# Audit of steps 3, 4 and 5: each fix keeps a check so it cannot come back.
layout_text=(ROOT/'engine/Psycho_layout.cpp').read_text()
check('an absolute child measures against the padding box',
      'Rect self{left + s.border[3]' in layout_text)
check('my find walks the paint list in my engine', 'find_in_paint(' in layout_text)
check('my shell highlights from that same walk', 'find_in_paint(' in shell_text)
check('Max-Age beats Expires in either order', 'has_max_age' in cookie_text)
check('a ticked box with no value sends on', 'value = "on"' in page_text)
check('a multiple select sends every chosen option', 'bool multiple = has_attr(input, "multiple")' in page_text)
canvas_text=(ROOT/'engine/Psycho_canvas.cpp').read_text()
page_header=(ROOT/'engine/Psycho_page.hpp').read_text()
check('my canvas keeps the text a script set', 'fill_style = value.to_string()' in page_text)
check('my canvas returns the element that owns it', 'return node(canvas_node)' in page_text)
check('my canvas honours a scaled drawImage', 'Point a = map(x, y), b = map(x + w, y + h);' in canvas_text)
check('my canvas can draw a piece of an image', 'draw_image_rect' in canvas_text)
check('my canvas can dash a stroke', 'set_line_dash' in canvas_text)
check('a WebP tells me it cannot decode instead of going quiet',
      'I cannot decode WebP yet' in page_text)
check('my tests cover the step 3 to 5 audit', all(k in tests_text for k in (
      'find_in_paint(paint, "hello")', 'decode_jpeg(bytes)', 'render_svg(',
      'x.roundRect(10,10,20,10,3)', 'c=on')))
check('my page header owns the canvas map', 'canvases_' in page_header)

# Step 8: my own security policy and filter engine, and no borrowed rule list.
security_cpp=(ROOT/'engine/Psycho_security.cpp').read_text()
security_hpp=(ROOT/'engine/Psycho_security.hpp').read_text()
network_cpp=(ROOT/'platform/Psycho_network.cpp').read_text()
sandbox_cpp=(ROOT/'platform/Psycho_sandbox.cpp').read_text()
check('my HTTPS store is my own code',
      'bool HttpsPolicy::record_header' in security_cpp and 'bool HttpsPolicy::requires_https' in security_cpp)
check('a plain HTTP answer cannot pin a host', 'if (!over_https || host.empty())' in security_cpp)
check('my HTTPS promises are capped', 'kMaxHstsMillis' in security_cpp and 'kMaxHstsMillis' in security_hpp)
check('my filter engine is my own code',
      'bool FilterList::parse' in security_cpp and 'const FilterRule *FilterList::match' in security_cpp)
check('an exception in my list beats a block',
      'stable_sort' in security_cpp and 'a.exception && !b.exception' in security_cpp)
check('I refuse rules I cannot honour instead of weakening them',
      'an option I do not implement' in security_cpp)
check('I do not pretend to run cosmetic rules', 'cosmetic rule, not a request rule' in security_cpp)
check('my starter list is mine and short',
      'written by me' in security_cpp and security_cpp.count('||') >= 20 and security_cpp.count('||') < 80)
check('I bundle no third-party list', not re.search(r'easylist|fanboy|adblock\s*plus\s*list', security_cpp, re.I))
check('every request goes through one shared check',
      network_cpp.count('prepare_request(') >= 3 and 'should_upgrade(target, policy.https_only)' in network_cpp)
check('a refusal is tagged so it is never retried in the clear', 'out.refused = true' in network_cpp)
check('an HTTPS promise never falls back to plain HTTP', 'requires_https(initial.host)' in network_cpp)
check('my job object says it is not isolation',
      'Page code is not isolated from my process' in sandbox_cpp and 'status.isolated = false' in sandbox_cpp)
check('my panel prints that same sentence', 'confinement_summary(confinement)' in shell_text)
check('my shell offers only the two tunnel schemes that protect traffic',
      'wanted == "socks5" || wanted == "https"' in shell_text)
check('my tests cover the step 8 decisions', all(k in tests_text for k in (
      'record_header("shop.test"', 'max-age=0', 'blocks_mixed_content', 'blocks_insecure_submit',
      'list.skipped', 'keep->exception', 'net.allowed_hosts', 'store.fallbacks')))

# Step 9: a conformance number from a run of my own code, not a borrowed engine.
runner_cpp=(ROOT/'tools/Psycho_conformance.cpp').read_text()
conformance=(ROOT/'docs/Psycho_CONFORMANCE.md').read_text()
check('my conformance runner is my own tool',
      'PsychoVault original source' in runner_cpp and 'js::Runtime' in runner_cpp)
check('my runner reads the corpus front matter', 'negative_phase' in runner_cpp and 'includes:' in runner_cpp)
check('my runner records a crash instead of losing the run', 'Outcome::Crash' in runner_cpp)
check('I ship no copy of the corpus', not any(f.startswith(('test262/','corpus/')) for f in files))
check('my conformance page states the pass rate two ways',
      '17.4 percent' in conformance and '9.3 percent' in conformance)
check('my conformance page does not claim WPT',
      'did not run a WPT subset' in conformance)
check('my switch statement is mine', 'case Stmt::Kind::Switch:' in (ROOT/'engine/Psycho_js_runtime.cpp').read_text())
check('my comma expression is mine', 'case Expr::Kind::Sequence:' in (ROOT/'engine/Psycho_js_runtime.cpp').read_text())
check('a thrown engine error becomes an object a page can test',
      'Value Runtime::error_object' in (ROOT/'engine/Psycho_js_runtime.cpp').read_text())
check('my tunnel probe exists and is tested', 'TunnelProbe probe_tunnel' in (ROOT/'platform/Psycho_network.cpp').read_text()
      and 'probe_tunnel(net,' in tests_text)
check('my panel prints what the tunnel test said', 'tunnel_test->text' in shell_text)
check('my conformance job is in my workflows', (ROOT/'.github/workflows/Psycho_conformance.yml').exists()
      and 'pv-conformance' in (ROOT/'.github/workflows/Psycho_conformance.yml').read_text())

check('my release notes come from my repo, not a template',
      'docs/Psycho_RELEASE-NOTES.md' in (ROOT/'.github/workflows/Psycho_release.yml').read_text())
notes=(ROOT/'docs/Psycho_RELEASE-NOTES.md').read_text()
check('my release notes say what I added', 'What I added in this build' in notes)
check('my release notes state what I am not claiming', 'What I am not claiming' in notes
      and 'WebP and progressive JPEG are not decoded' in notes)
check('my release notes carry my version marker', '{{VERSION}}' in notes)

print(f'\n{passed} passed, {len(fail)} failed')
sys.exit(1 if fail else 0)
