#!/usr/bin/env python3
"""Apply PsychoVault's Android source additions after upstream patches, before GN.

Fails closed if pinned Chromium source no longer has the expected anchors. This
script does not build Chromium and does not install a JS/native bridge.
"""
from pathlib import Path
import shutil
import sys

HERE = Path(__file__).resolve().parent


def install(src):
    src = Path(src).resolve()
    java_dir = src / 'chrome/android/java/src/org/chromium/chrome/browser'
    activity = java_dir / 'ChromeTabbedActivity.java'
    sources = src / 'chrome/android/chrome_java_sources.gni'
    java = java_dir / 'PsychoTabController.java'
    activity_text = activity.read_text(encoding='utf-8')
    source_text = sources.read_text(encoding='utf-8')
    activity_anchor = 'public class ChromeTabbedActivity extends ChromeActivity implements PreAttachIntentObserver {'
    source_anchor = '  "java/src/org/chromium/chrome/browser/ChromeTabbedActivity.java",'
    if activity_text.count(activity_anchor) != 1 or source_text.count(source_anchor) != 1 or java.exists():
        raise RuntimeError('Chromium Android source anchors changed or patch already applied')
    hook = '''
    // This is an internal browser controller, not a JavaScript interface.
    public PsychoTabController createPsychoTabController() {
        if (mTabModelSelector == null) throw new IllegalStateException("Tab model is not initialized");
        return new PsychoTabController(mTabModelSelector, this::getCurrentTabCreator);
    }
'''
    new_activity = activity_text.replace(activity_anchor, activity_anchor + '\n' + hook)
    new_sources = source_text.replace(source_anchor, source_anchor + '\n  "java/src/org/chromium/chrome/browser/PsychoTabController.java",')
    shutil.copyfile(HERE / 'java/org/chromium/chrome/browser/PsychoTabController.java', java)
    activity.write_text(new_activity, encoding='utf-8')
    sources.write_text(new_sources, encoding='utf-8')


if __name__ == '__main__':
    if len(sys.argv) != 2:
        raise SystemExit('Usage: install_chromium_source.py /absolute/path/to/chromium/src')
    install(sys.argv[1])
