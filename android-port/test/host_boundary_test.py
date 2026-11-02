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
        self.assertIn('recovery.html', bridge)
        self.assertIn('vpncheck.html', bridge)
        self.assertIn("spec.indexOf('#')", bridge)
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
        page = host.split('private void pageCommand',1)[1].split('private void chromeCommand',1)[0]
        self.assertIn('navigateTab(tabId', page)
        self.assertNotIn('mController.invoke', page)
        self.assertNotIn('layout(args', page)
        for channel in ('history:clear', 'data:clear', 'data:report', 'app:diagnostics', 'app:diagnosticsCopy', 'proxy:status', 'proxy:selftest', 'update:tokenstate', 'rail:icons', 'catalogue:list', 'catalogue:toggle'):
            self.assertIn(channel, page)
        self.assertIn('library:clear', host)
        self.assertIn('vpncheck:ip', host)
        self.assertIn('data:openErrorLog', host)
        self.assertIn('ACTION_VIEW_DOWNLOADS', host)
        self.assertIn('!profile().isOffTheRecord()', host)
        settings = (ROOT / 'ui/settings.js').read_text()
        for control in ('clear', 'dataRefresh', 'modSave', 'modClear', 'svcAdd', 'svcSave', 'proxySelfTest', 'proxyStatus', 'diagnosticsRefresh', 'diagnosticsCopy'):
            self.assertIn(control, settings)
        self.assertIn("el.hasAttribute('data-clear')", settings)
        self.assertIn('paintCatalogue(); refreshData(); refreshDiagnostics();', settings)
    def test_assets_are_allowlisted_and_uncompressed(self):
        native = (ROOT / 'engine/psychovault/native/psycho_ui.cc').read_text()
        gn = (ROOT / 'engine/psychovault/BUILD.gn').read_text()
        self.assertIn('Explicit allowlist', native)
        self.assertIn("script-src 'self';", native)
        self.assertIn('4 * 1024 * 1024', native)
        self.assertIn('disable_compression = true', gn)
        self.assertIn('recovery.html', native)
        self.assertIn('path != "recovery.html"', native)
        # Every staged asset must be served, and every served asset must be staged.
        import re
        staged = set(re.findall(r'"ui/([^"]+)"', gn))
        served = set(re.findall(r'\{"([^"]+)", "[^"]+"\}', native))
        self.assertEqual(staged, served)
        self.assertIn('chrome-bootstrap.js', served)
        self.assertIn('settings.html', served)
    def test_vpn_check_uses_profile_network_and_cancels(self):
        java = (JAVA / 'psychovault/PsychoVpnCheck.java').read_text()
        native = (ROOT / 'engine/psychovault/native/psycho_vpncheck.cc').read_text()
        self.assertIn('GetURLLoaderFactoryForBrowserProcess', native)
        self.assertIn('CredentialsMode::kOmit', native)
        self.assertIn('SetTimeoutDuration', native)
        self.assertIn('cancelAll', java)
        self.assertNotIn('HttpURLConnection', java)

    def test_error_log_is_bounded_and_redacted(self):
        java = (JAVA / 'psychovault/PsychoErrorLog.java').read_text()
        self.assertIn('LIMIT = 100', java)
        self.assertIn('getSimpleName()', java)
        self.assertNotIn('getMessage()', java)

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

    def test_vault_master_salt_is_reused_and_recovery_is_explicit(self):
        vault = (JAVA / 'psychovault/PsychoVaultStore.java').read_text()
        self.assertIn('final byte[] salt = bytes(16);', vault)
        self.assertIn('derive(password, salt, key ->', vault)
        self.assertNotIn('byte[] salt = bytes(16);\n                JSONObject meta', vault)
        self.assertIn('JSONObject recover()', vault)
        self.assertIn('vault-migration.json', vault)
        self.assertIn('psychovault-backup', vault)
        native = (ROOT / 'engine/psychovault/native/psycho_vault.cc').read_text()
        self.assertIn('JNI_PsychoVaultStore_DeriveKey', native)
        self.assertIn('EVP_PBE_scrypt', native)
    def test_downloads_use_guids_and_real_manager(self):
        native = (ROOT / 'engine/psychovault/native/psycho_downloads.cc').read_text()
        self.assertIn('GetDownloadByGuid', native)
        self.assertIn('GetAsReferrer()', native)
        self.assertNotIn('GetWithoutUsername', native)
        java = (JAVA / 'psychovault/PsychoDownloads.java').read_text()
        self.assertIn('requireGuid', java)
    def test_sessions_strip_credentials_without_placeholder_logic(self):
        sessions = (JAVA / 'psychovault/PsychoSessions.java').read_text()
        self.assertIn('return stripCredentials(value.getSpec());', sessions)
        self.assertNotIn('|| true', sessions)
        self.assertIn('commit()', sessions)
    def test_network_policy_is_profile_scoped_and_private_inherits(self):
        native = (ROOT / 'engine/psychovault/native/psycho_network.cc').read_text()
        self.assertIn('ERR_BLOCKED_BY_CLIENT', native)
        self.assertIn('GetOriginalProfile()', native)
        self.assertIn('CancelWithError', native)
        self.assertIn('kPromptForDownloadAndroid', native)
        self.assertNotIn('modified_request_headers', native)
    def test_native_code_uses_pinned_chromium_156_apis(self):
        native_dir = ROOT / 'engine/psychovault/native'
        for path in native_dir.glob('*.cc'):
            text = path.read_text()
            self.assertNotRegex(text, r'base::WriteJson\([^;]*&\w+\)', f'{path.name}: WriteJson now returns optional<string>')
            self.assertNotIn('RefCountedString::TakeString', text)
            self.assertNotIn('base::EscapeJSONString(', text)
        downloads = (native_dir / 'psycho_downloads.cc').read_text()
        self.assertIn('static_cast<double>(item->GetStartTime().InMillisecondsSinceUnixEpoch())', downloads)
        network_java = (JAVA / 'psychovault/PsychoNetwork.java').read_text()
        self.assertIn('@JNINamespace("psychovault")', network_java)
        for name in ('PsychoChromeHost.java', 'PsychoUpdates.java'):
            self.assertIn('import org.chromium.base.version_info.VersionInfo;', (JAVA / 'psychovault' / name).read_text())
        ui = (native_dir / 'psycho_ui.cc').read_text()
        self.assertIn('base::MakeRefCounted<base::RefCountedString>', ui)
        self.assertIn('base::File asset_file(fd)', ui)
        downloads = (native_dir / 'psycho_downloads.cc').read_text()
        self.assertNotIn('for (auto* item :', downloads)
        self.assertIn('for (auto& item : items) Watch(item.get());', downloads)
        performance = (native_dir / 'psycho_performance.cc').read_text()
        self.assertIn('components/viz/common/frame_sinks/copy_output_result.h', performance)
        installer = (ROOT / 'engine/psychovault/install_chromium_source.py').read_text()
        self.assertIn('//components/viz/common', installer)

    def test_performance_does_not_fabricate_metrics(self):
        java = (JAVA / 'psychovault/PsychoPerformance.java').read_text()
        self.assertIn('JSONObject.NULL', java)
        self.assertIn('"verified", false', java)
        self.assertIn('MediaStore.Images.Media.IS_PENDING', java)
        native = (ROOT / 'engine/psychovault/native/psycho_performance.cc').read_text()
        self.assertIn('ExecuteJavaScriptInIsolatedWorld', native)
        self.assertIn('CopyFromSurface', native)


if __name__ == '__main__': unittest.main()
