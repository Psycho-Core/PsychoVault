# My Android signing key

Android only accepts an update when the package ID and signing identity are compatible and the version code is appropriate. My previous preview APKs were debug builds created on separate GitHub runners. The key was not retained between those runs, so the later APK may have a different signer. The package-conflict message is consistent with that, but the screenshot alone does not prove the exact cause.

The old ephemeral key is not available here. I cannot make a new key update an APK signed by a different old key. Do not uninstall the installed preview until I understand that uninstalling can delete its local app data. The preview does not have a full data export yet. If the signing keys differ, I must choose whether to keep the old app or uninstall it and lose its app data before installing the new one. Do not assume a reinstall is an update.

## A stable key for future builds

I can create a key for free on my own Windows computer using `CREATE-SIGNING-KEY.bat` and JDK 21. It writes the key in `%USERPROFILE%\PsychoVault-Android-Keys`, not in the repository. It refuses to overwrite an existing key. I keep a secure offline backup of both the keystore and its password. Losing the key can prevent me updating existing installs.

The Android release build requires these environment variables and stops if they are missing. It never silently uses the debug key:

- `PSYCHO_ANDROID_KEYSTORE` - path to my keystore.
- `PSYCHO_ANDROID_STORE_PASSWORD` - keystore password.
- `PSYCHO_ANDROID_KEY_ALIAS` - `psychovault` if I used the batch file.
- `PSYCHO_ANDROID_KEY_PASSWORD` - key password. With the default PKCS12 keytool setup this is normally the same as the keystore password.
- `PSYCHO_ANDROID_VERSION_CODE` - a whole number greater than the last published version code, at least 2.

Never put a password, keystore or token in chat, a Git commit, release notes or logs. The default version code is only for local testing; before distributing any build I must choose a code greater than the installed release. This signing configuration is source work and has not been tested with a real keystore or phone in this environment.

For GitHub Actions I need to add a backed-up copy of my key and its passwords as repository secrets. `.github/workflows/android-signed-preview.yml` is a manual workflow that expects `PSYCHO_ANDROID_KEYSTORE_BASE64` (the base64-encoded keystore), `PSYCHO_ANDROID_STORE_PASSWORD`, `PSYCHO_ANDROID_KEY_ALIAS` and `PSYCHO_ANDROID_KEY_PASSWORD`. It writes the key temporarily on the runner and removes it after the build. I supply an increasing version code when starting the workflow. I must configure the secrets privately in GitHub, not in chat or in the repository. The workflow and signing process have not yet been tested with a real key or phone. I should not publish another disposable-key debug APK as if it were a stable update.

## Play Protect

Play Protect may ask to scan an unfamiliar APK. I can choose **Scan app** and follow Google's result. A scan request by itself is not proof of malware, and a clean result is not proof the browser is secure. Signing consistently does not guarantee this prompt will disappear. I should not disable Play Protect to bypass it.
