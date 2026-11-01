from pathlib import Path
import unittest
ROOT = Path(__file__).resolve().parents[1]
JAVA = ROOT / 'engine/psychovault/java/org/chromium/chrome/browser'
class HostBoundaryTests(unittest.TestCase):
    def test_bridge_has_one_annotated_entry_and_rechecks_document_profile(self):
        bridge = (JAVA / 'psychovault/PsychoBridge.java').read_text()
        self.assertEqual(bridge.count('@JavascriptInterface'), 1)
        self.assertIn('public void post(String json)', bridge)
        self.assertIn('ThreadUtils.postOnUiThread', bridge)
        self.assertIn('current != own', bridge)
        self.assertIn('getLastCommittedUrl()', bridge)
        self.assertIn('mDestroyed', bridge)
    def test_host_uses_exact_origins_and_cleans_up(self):
        host = (JAVA / 'psychovault/PsychoChromeHost.java').read_text()
        self.assertIn('addPossiblyUnsafeInterfaceToOrigins', host)
        self.assertNotIn('addPossiblyUnsafeInterface(', host)
        self.assertIn('removeObserverFromAllModels', host)
        self.assertIn('releasePersistentShowingToken', host)
        self.assertIn('removeInterface("PsychoVaultNative")', host)
        self.assertIn('matcher.destroy()', host)
        self.assertIn('contents.destroy()', host)
    def test_history_requires_committed_regular_navigation_and_remember(self):
        host = (JAVA / 'psychovault/PsychoChromeHost.java').read_text()
        self.assertIn('didFinishNavigationInPrimaryMainFrame', host)
        for gate in ('hasCommitted()', 'isErrorPage()', 'isSameDocument()', 'isInPrimaryMainFrame()', 'isOffTheRecord()', 'optBoolean("remember", true)'):
            self.assertIn(gate, host)
        self.assertIn('library:visit', host)
    def test_settings_store_is_allowlisted_and_profile_scoped(self):
        store = (JAVA / 'psychovault/PsychoProfileStore.java').read_text()
        self.assertIn('getProfileName', store)
        self.assertIn('getOriginalProfile()', store)
        self.assertIn('ALLOWED.contains(key)', store)
        self.assertIn('MODE_PRIVATE', store)
        native = (ROOT / 'engine/psychovault/native/psycho_profile.cc').read_text()
        self.assertIn('GetBaseName()', native)
        self.assertIn('IsOffTheRecord()', native)
    def test_internal_page_cannot_use_chrome_layout_or_tab_commands(self):
        host = (JAVA / 'psychovault/PsychoChromeHost.java').read_text()
        page = host.split('if (tabId != Tab.INVALID_TAB_ID) {',1)[1].split('if (channel.equals("ui:layout"))',1)[0]
        self.assertIn('navigateTab(tabId', page)
        self.assertNotIn('mController.invoke', page)
        self.assertNotIn('layout(args', page)
    def test_assets_are_allowlisted_and_uncompressed(self):
        native = (ROOT / 'engine/psychovault/native/psycho_ui.cc').read_text()
        gn = (ROOT / 'engine/psychovault/BUILD.gn').read_text()
        self.assertIn('Explicit allowlist', native)
        self.assertIn("script-src 'self';", native)
        self.assertIn('4 * 1024 * 1024', native)
        self.assertIn('disable_compression = true', gn)
        self.assertNotIn('recovery.html', native)
        # Every staged asset must be served, and every served asset must be staged.
        import re
        staged = set(re.findall(r'"ui/([^"]+)"', gn))
        served = set(re.findall(r'\{"([^"]+)", "[^"]+"\}', native))
        self.assertEqual(staged, served)
        self.assertIn('chrome-bootstrap.js', served)
        self.assertIn('settings.html', served)
    def test_extensions_are_regular_profile_and_id_checked(self):
        ext = (JAVA / 'psychovault/PsychoExtensions.java').read_text()
        self.assertIn('PROTON_ID', ext)
        self.assertIn('Unsafe extension archive path', ext)
        self.assertIn('isOffTheRecord()', ext)
        self.assertIn('[a-p]{32}', ext)
        native = (ROOT / 'engine/psychovault/native/psycho_extensions.cc').read_text()
        self.assertIn('RegularProfile', native)
        self.assertIn('UnpackedInstaller', native)
        self.assertIn('UNINSTALL_REASON_USER_INITIATED', native)
    def test_private_settings_are_not_written(self):
        store = (JAVA / 'psychovault/PsychoProfileStore.java').read_text()
        self.assertIn('Private windows do not save settings', store)
        self.assertIn('writeInternal', store)

if __name__ == '__main__': unittest.main()
