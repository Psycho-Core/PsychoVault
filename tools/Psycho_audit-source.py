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
    check('release notes state YouTube and Netflix do not play','YouTube and Netflix do not play' in release)
    check('release notes state Proton is not bundled','Proton VPN is not bundled' in release)

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
check('my page host fetches only through a callback','using Fetcher = std::function<Resource(const std::string &url)>;' in host_hpp)
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

print(f'\n{passed} passed, {len(fail)} failed')
sys.exit(1 if fail else 0)
