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
build=(ROOT/'tools/Psycho_build.py').read_text()
check('renderer target excludes broker networking','target("pv-renderer",ENGINE+["platform/Psycho_process.cpp","tools/Psycho_renderer_main.cpp"]' in build)
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
    order=[('build','Psycho_build.py'),('tests','pv-tests.exe'),('sandbox','--sandbox-test'),
           ('package','Psycho_package.py'),('verify','verify_package.py'),('publish','gh release create')]
    positions=[(label,release.find(token)) for label,token in order]
    check('every release stage is present',all(pos>=0 for _,pos in positions),
          ','.join(label for label,pos in positions if pos<0))
    check('I publish only after my build, tests, sandbox and hash verification',
          all(positions[i][1]<positions[i+1][1] for i in range(len(positions)-1)))
    check('release workflow shares my one-heavy-build group','group: psychovault-heavy' in release)
    check('I refuse to overwrite a published release','I do not overwrite a published release' in release)
    check('I refuse a tag that does not match my version file','does not match Psycho_VERSION.txt' in release)
    check('I verify asset hashes before publishing','hash mismatch' in release)
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

# My one-file promise has to be enforced, not just claimed.
build_text=(ROOT/'tools/Psycho_build.py').read_text()
check('I embed my renderer as a resource in my executable','101 RCDATA' in build_text)
check('I compile my payload resource with the SDK tool','rc.exe not found' in build_text)
shell_text=(ROOT/'shell/Psycho_win_main.cpp').read_text()
check('my shell resolves its renderer through my payload loader','renderer_executable(' in shell_text)
check('my shell never assumes a sibling renderer file',
      'executable_dir() + "\\\\pv-renderer.exe"' not in shell_text)
payload=(ROOT/'platform/Psycho_payload.cpp').read_text()
check('I verify my unpacked renderer before running it','failed its own hash check' in payload)
check('I unpack into my own per-user folder','FOLDERID_LocalAppData' in payload)
check('I name my payload folder by hash so versions cannot collide','digest.substr(0, 32)' in payload)
for wf in ('.github/workflows/Psycho_owned-engine.yml','.github/workflows/Psycho_release.yml'):
    body=(ROOT/wf).read_text()
    check('I prove my single exe renders alone: '+wf,
          'psychovault-solo' in body and 'expected 1' in body and '--expect-image' in body)
check('my update payload is the single executable',
      'asset("PsychoVault.exe", out / "PsychoVault.exe", "update")' in (ROOT/'tools/Psycho_package.py').read_text())
instructions=(ROOT/'docs/Psycho_TEST-BUILD.md').read_text()
check('my test instructions need no shell commands','```powershell' not in instructions.lower())
check('my test instructions start with a double-click','double-click' in instructions.lower())

print(f'\n{passed} passed, {len(fail)} failed')
sys.exit(1 if fail else 0)
