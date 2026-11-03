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
print(f'\n{passed} passed, {len(fail)} failed')
sys.exit(1 if fail else 0)
