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
    'java/org/chromium/chrome/browser/psychovault/PsychoExtensions.java',
    'java/org/chromium/chrome/browser/psychovault/PsychoDownloads.java',
    'java/org/chromium/chrome/browser/psychovault/PsychoNetwork.java',
    'java/org/chromium/chrome/browser/psychovault/PsychoPerformance.java',
    'java/org/chromium/chrome/browser/psychovault/PsychoSessions.java',
    'java/org/chromium/chrome/browser/psychovault/PsychoVaultStore.java',
    'java/org/chromium/chrome/browser/psychovault/PsychoWorkspaces.java',
    'java/org/chromium/chrome/browser/psychovault/PsychoContainers.java',
    'java/org/chromium/chrome/browser/psychovault/PsychoMedia.java',
    'java/org/chromium/chrome/browser/psychovault/PsychoUpdates.java',
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
        'content_client': src / 'chrome/browser/chrome_content_browser_client.cc',
        'spell_h': src / 'chrome/browser/spellchecker/spellcheck_hunspell_dictionary.h',
        'spell_cc': src / 'chrome/browser/spellchecker/spellcheck_hunspell_dictionary.cc',
    }
    texts = {name: path.read_text(encoding='utf-8') for name, path in paths.items()}
    if any((HERE / name).is_file() is False for name in JAVA_FILES):
        raise RuntimeError('Missing PsychoVault Java source')
    if not (HERE / 'ui/index.html').is_file() or not (HERE / 'ui/newtab.js').is_file() or not (HERE / 'ui/settings.js').is_file() or not (HERE / 'bundled/proton-vpn.zip').is_file() or not (HERE / 'bundled/dark-reader.zip').is_file() or not (HERE / 'bundled/blocklist.json').is_file():
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
    activity = replace_once(activity, '            super.onActivityResult(requestCode, resultCode, data);\n',
        '            super.onActivityResult(requestCode, resultCode, data);\n            if (mPsychoChromeHost != null) mPsychoChromeHost.handleActivityResult(requestCode, resultCode, data);\n', 'activity result')

    source_anchor = '  "java/src/org/chromium/chrome/browser/ChromeTabbedActivity.java",'
    additions = '\n'.join('  "java/src/org/chromium/chrome/browser/' + name.split('browser/', 1)[1] + '",' for name in JAVA_FILES)
    gni = replace_once(texts['gni'], source_anchor, source_anchor + '\n' + additions, 'Java sources')

    chrome_build = replace_once(texts['chrome_build'],
        '  chrome_public_apk_or_module_tmpl("chrome_public_apk") {\n    target_type = "android_apk"',
        '  chrome_public_apk_or_module_tmpl("chrome_public_apk") {\n    target_type = "android_apk"\n'
        '    asset_deps = [\n      "//psychovault:ui_assets",\n      "//psychovault:bundled_extensions_assets",\n    ]',
        'APK assets')
    chrome_build = replace_once(chrome_build, '    srcjar_deps += [ ":chrome_jni_headers" ]',
        '    srcjar_deps += [ ":chrome_jni_headers", "//psychovault:jni_headers_java" ]', 'profile JNI')
    ui_build = texts['ui_build']
    # Compile the host inside Chromium's existing browser/UI target. A separate
    # native target would create dependency cycles with factories implemented by
    # that target. Generated JNI headers remain a separate, acyclic target.
    native_sources = '\n'.join(
        f'    "//psychovault/native/{name}",'
        for name in (
            "psycho_ui.cc", "psycho_ui.h", "psycho_profile.cc",
            "psycho_extensions.cc", "psycho_network.cc", "psycho_network.h",
            "psycho_downloads.cc", "psycho_vault.cc", "psycho_performance.cc",
        )
    )
    ui_build = replace_once(ui_build, 'static_library("ui") {',
        'static_library("ui") {\n  sources = [\n' + native_sources + '\n  ]',
        'native UI sources')
    ui_build = replace_once(ui_build, '  deps = [\n    ":browser_element_identifiers",',
        '  deps = [\n    "//psychovault:jni_headers",\n'
        '    "//chrome/browser/spellchecker",\n'
        '    "//components/download/public/common",\n'
        '    "//components/proxy_config",\n'
        '    "//components/spellcheck/browser",\n'
        '    "//extensions/browser",\n'
        '    "//extensions/common",\n'
        '    "//services/network/public/cpp",\n'
        '    "//third_party/boringssl",\n'
        '    "//ui/gfx/codec",\n'
        '    ":browser_element_identifiers",',
        'native UI JNI dependency')
    configs = replace_once(texts['configs'], '#include "chrome/browser/ui/webui/chrome_web_ui_configs.h"\n',
        '#include "chrome/browser/ui/webui/chrome_web_ui_configs.h"\n#include "psychovault/native/psycho_ui.h"\n', 'WebUI include')
    content_client = replace_once(texts['content_client'], '#include "chrome/browser/chrome_content_browser_client.h"\n',
        '#include "chrome/browser/chrome_content_browser_client.h"\n#include "psychovault/native/psycho_network.h"\n', 'network include')
    content_client = replace_once(content_client,
        '  DCHECK(profile);\n\n  ChromeNavigationUIData* chrome_navigation_ui_data =\n',
        '  DCHECK(profile);\n\n'
        '  content::WebContents* psycho_contents = wc_getter.is_null() ? nullptr : wc_getter.Run();\n'
        '  if (!psycho_contents) psycho_contents = content::WebContents::FromFrameTreeNodeId(frame_tree_node_id);\n'
        '  result.push_back(psychovault::CreateNetworkThrottle(profile, psycho_contents));\n\n'
        '  ChromeNavigationUIData* chrome_navigation_ui_data =\n', 'network throttle')
    # The keep-alive throttle factory has the same Profile DCHECK but no
    # ChromeNavigationUIData; the anchor above intentionally targets only the
    # main-frame factory where wc_getter/frame_tree_node_id are available.
    configs = replace_once(configs, '  auto& map = content::WebUIConfigMap::GetInstance();\n',
        '  auto& map = content::WebUIConfigMap::GetInstance();\n  psychovault::RegisterPsychoWebUIConfigs();\n', 'WebUI registration')

    spell_h = replace_once(texts['spell_h'],
        '  // Tests use this method to set a custom URL for downloading dictionaries.\n  static void SetDownloadURLForTesting(const GURL url);',
        '  // PsychoVault: production dictionary mirror base URL (folder of <lang>.bdic files).\n'
        '  static void SetDownloadURLMirror(const GURL url);\n\n'
        '  // Tests use this method to set a custom URL for downloading dictionaries.\n  static void SetDownloadURLForTesting(const GURL url);',
        'spellcheck mirror header')
    spell_cc = replace_once(texts['spell_cc'],
        'GURL& GetDownloadUrlForTesting() {\n  static base::NoDestructor<GURL> download_url_for_testing;\n  return *download_url_for_testing;\n}',
        'GURL& GetDownloadUrlForTesting() {\n  static base::NoDestructor<GURL> download_url_for_testing;\n  return *download_url_for_testing;\n}\n\n'
        'GURL& GetDownloadUrlMirror() {\n  static base::NoDestructor<GURL> download_url_mirror;\n  return *download_url_mirror;\n}',
        'spellcheck mirror state')
    spell_cc = replace_once(spell_cc,
        'void SpellcheckHunspellDictionary::SetDownloadURLForTesting(const GURL url) {\n  GetDownloadUrlForTesting() = url;\n}',
        'void SpellcheckHunspellDictionary::SetDownloadURLMirror(const GURL url) {\n  GetDownloadUrlMirror() = url;\n}\n\n'
        'void SpellcheckHunspellDictionary::SetDownloadURLForTesting(const GURL url) {\n  GetDownloadUrlForTesting() = url;\n}',
        'spellcheck mirror setter')
    spell_cc = replace_once(spell_cc,
        'GURL SpellcheckHunspellDictionary::GetDictionaryURL() {\n  if (GetDownloadUrlForTesting() != GURL()) {',
        'GURL SpellcheckHunspellDictionary::GetDictionaryURL() {\n'
        '  if (GetDownloadUrlMirror().is_valid()) {\n'
        '    std::string bdict_file = dictionary_file_.path.BaseName().MaybeAsASCII();\n'
        '    DCHECK(!bdict_file.empty());\n'
        '    std::string base = GetDownloadUrlMirror().spec();\n'
        '    if (!base.empty() && base.back() != \'/\') base += \'/\';\n'
        '    return GURL(base + base::ToLowerASCII(bdict_file));\n'
        '  }\n'
        '  if (GetDownloadUrlForTesting() != GURL()) {',
        'spellcheck mirror URL')
    # No target path is modified until every anchor and source check has succeeded.
    replacements = {'activity': activity, 'gni': gni, 'chrome_build': chrome_build, 'ui_build': ui_build, 'configs': configs, 'content_client': content_client, 'spell_h': spell_h, 'spell_cc': spell_cc}
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
