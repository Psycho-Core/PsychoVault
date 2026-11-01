#!/usr/bin/env python3
"""Install the trusted Android chrome into a pinned, already-patched Chromium tree.

All anchors are checked before any write. This stages source only: no GN, compile,
APK, signing, GitHub Actions or device claim is made here.
"""
from pathlib import Path
import shutil
import sys

HERE = Path(__file__).resolve().parent
JAVA_FILES = [
    'java/org/chromium/chrome/browser/PsychoTabController.java',
    'java/org/chromium/chrome/browser/psychovault/PsychoBridge.java',
    'java/org/chromium/chrome/browser/psychovault/PsychoChromeHost.java',
    'java/org/chromium/chrome/browser/psychovault/PsychoProfileStore.java',
]


def replace_once(text, old, new, label):
    if text.count(old) != 1 or new in text:
        raise RuntimeError(f'Chromium anchor changed or patch already applied: {label}')
    return text.replace(old, new, 1)


def install(src):
    src = Path(src).resolve()
    browser = src / 'chrome/android/java/src/org/chromium/chrome/browser'
    paths = {
        'activity': browser / 'ChromeTabbedActivity.java',
        'gni': src / 'chrome/android/chrome_java_sources.gni',
        'chrome_build': src / 'chrome/android/BUILD.gn',
        'ui_build': src / 'chrome/browser/ui/BUILD.gn',
        'configs': src / 'chrome/browser/ui/webui/chrome_web_ui_configs.cc',
    }
    texts = {name: path.read_text(encoding='utf-8') for name, path in paths.items()}
    if any((HERE / name).is_file() is False for name in JAVA_FILES):
        raise RuntimeError('Missing PsychoVault Java source')
    if not (HERE / 'ui/index.html').is_file() or not (HERE / 'ui/newtab.js').is_file() or not (HERE / 'ui/settings.js').is_file():
        raise RuntimeError('Run prepare-engine.sh so the adapted UI assets are staged first')
    if (src / 'psychovault').exists():
        raise RuntimeError('PsychoVault source already installed; use a fresh staged tree')

    class_anchor = 'public class ChromeTabbedActivity extends ChromeActivity implements PreAttachIntentObserver {'
    hook = '''
    private org.chromium.chrome.browser.psychovault.PsychoChromeHost mPsychoChromeHost;

    private void ensurePsychoChromeHost() {
        if (mPsychoChromeHost != null || isFinishing() || mTabModelSelector == null) return;
        var compositor = getCompositorViewHolderSupplier().get();
        if (compositor == null || getToolbarManager() == null) return;
        mPsychoChromeHost = new org.chromium.chrome.browser.psychovault.PsychoChromeHost(
                this, mTabModelSelector, compositor);
    }

    private void destroyPsychoChromeHost() {
        if (mPsychoChromeHost != null) {
            mPsychoChromeHost.destroy();
            mPsychoChromeHost = null;
        }
    }
'''
    activity = replace_once(texts['activity'], class_anchor, class_anchor + hook, 'activity class')
    activity = replace_once(activity, '        super.onResumeWithNative();\n',
        '        super.onResumeWithNative();\n        ensurePsychoChromeHost();\n        if (mPsychoChromeHost != null) mPsychoChromeHost.resume();\n', 'resume')
    activity = replace_once(activity, '    public void onPauseWithNative() {\n',
        '    public void onPauseWithNative() {\n        if (mPsychoChromeHost != null) mPsychoChromeHost.pause();\n', 'pause')
    activity = replace_once(activity, '    public void onDestroyInternal() {\n',
        '    public void onDestroyInternal() {\n        destroyPsychoChromeHost();\n', 'destroy')

    source_anchor = '  "java/src/org/chromium/chrome/browser/ChromeTabbedActivity.java",'
    additions = '\n'.join('  "java/src/org/chromium/chrome/browser/' + name.split('browser/', 1)[1] + '",' for name in JAVA_FILES)
    gni = replace_once(texts['gni'], source_anchor, source_anchor + '\n' + additions, 'Java sources')

    chrome_build = replace_once(texts['chrome_build'], '  android_library("chrome_java") {\n    deps = [\n',
        '  android_library("chrome_java") {\n    deps = [\n      "//psychovault:ui_assets",\n', 'APK assets')
    chrome_build = replace_once(chrome_build, '    srcjar_deps += [ ":chrome_jni_headers" ]',
        '    srcjar_deps += [ ":chrome_jni_headers", "//psychovault:jni_headers" ]', 'profile JNI')
    ui_build = texts['ui_build']
    ui_build = replace_once(ui_build, '  public_deps = [\n    # WARNING WARNING WARNING',
        '  public_deps = [\n    "//psychovault:native",\n    # WARNING WARNING WARNING', 'native UI dependency')
    configs = replace_once(texts['configs'], '#include "chrome/browser/ui/webui/chrome_web_ui_configs.h"\n',
        '#include "chrome/browser/ui/webui/chrome_web_ui_configs.h"\n#include "psychovault/native/psycho_ui.h"\n', 'WebUI include')
    configs = replace_once(configs, '  auto& map = content::WebUIConfigMap::GetInstance();\n',
        '  auto& map = content::WebUIConfigMap::GetInstance();\n  psychovault::RegisterPsychoWebUIConfigs();\n', 'WebUI registration')

    # No target path is modified until every anchor and source check has succeeded.
    replacements = {'activity': activity, 'gni': gni, 'chrome_build': chrome_build, 'ui_build': ui_build, 'configs': configs}
    for name, text in replacements.items():
        paths[name].write_text(text, encoding='utf-8')
    for name in JAVA_FILES:
        target = browser / name.split('browser/', 1)[1]
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(HERE / name, target)
    shutil.copytree(HERE, src / 'psychovault', ignore=shutil.ignore_patterns('__pycache__', '*.pyc'))


if __name__ == '__main__':
    if len(sys.argv) != 2:
        raise SystemExit('Usage: install_chromium_source.py /absolute/path/to/chromium/src')
    install(sys.argv[1])
