#include <set>

#include "base/android/jni_string.h"
#include "base/android/scoped_java_ref.h"
#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/json/json_writer.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/web_contents.h"
#include "extensions/browser/crx_installer.h"
#include "extensions/browser/disable_reason.h"
#include "extensions/browser/extension_registrar.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/install/crx_install_error.h"
#include "extensions/browser/extension_install_prompt_client.h"
#include "extensions/browser/uninstall_reason.h"
#include "extensions/browser/unpacked_installer.h"
#include "extensions/common/extension.h"
#include "extensions/common/mojom/manifest.mojom.h"
#include "extensions/common/permissions/permission_message.h"
#include "extensions/common/permissions/permission_message_provider.h"
#include "extensions/common/permissions/permissions_data.h"

#include "psychovault/jni_headers/PsychoExtensions_jni.h"

namespace {
using base::android::JavaRef;
using base::android::ScopedJavaGlobalRef;
using base::android::ScopedJavaLocalRef;
std::set<class PsychoInstallPrompt*>& LivePrompts() { static std::set<PsychoInstallPrompt*> prompts; return prompts; }
class PsychoInstallPrompt final : public extensions::ExtensionInstallPromptClient {
 public:
  PsychoInstallPrompt(JNIEnv* env, const JavaRef<jobject>& owner) : owner_(env, owner) { LivePrompts().insert(this); }
  ~PsychoInstallPrompt() override { LivePrompts().erase(this); if (callback_) std::move(callback_).Run(DoneCallbackPayload(Result::ABORTED)); }
  void ConfirmInstall(DoneCallback callback, const extensions::Extension* extension) override {
    if (!extension) { std::move(callback).Run(DoneCallbackPayload(Result::ABORTED)); return; }
    callback_ = std::move(callback);
    base::ListValue permissions;
    for (const auto& message : extensions::PermissionMessageProvider::Get()->GetPermissionMessages(extensions::PermissionMessageProvider::Get()->GetAllPermissionIDs(extension->permissions_data()->active_permissions(), extension->GetType()))) permissions.Append(message.message());
    std::string json; base::WriteJson(permissions, &json);
    JNIEnv* env = base::android::AttachCurrentThread();
    Java_PsychoExtensions_onInstallPrompt(env, owner_, reinterpret_cast<jlong>(this),
        base::android::ConvertUTF8ToJavaString(env, extension->name()), base::android::ConvertUTF8ToJavaString(env, json));
  }
  void ConfirmReEnable(DoneCallback callback, const extensions::Extension*, content::BrowserContext*) override { std::move(callback).Run(DoneCallbackPayload(Result::USER_CANCELED)); }
  void ConfirmPermissions(DoneCallback callback, const extensions::Extension*, std::unique_ptr<const extensions::PermissionSet>) override { std::move(callback).Run(DoneCallbackPayload(Result::USER_CANCELED)); }
  void ShowInstallDialog(DoneCallback callback, const extensions::Extension* extension, const SkBitmap*) override { ConfirmInstall(std::move(callback), extension); }
  void OnInstallSuccess(scoped_refptr<const extensions::Extension>, SkBitmap*) override {}
  void OnInstallFailure(const extensions::CrxInstallError&) override {}
  void SetUseAppInstalledBubble(bool) override {}
  void SetSkipPostInstallUI(bool) override {}
  void Resolve(bool accepted) { if (callback_) std::move(callback_).Run(DoneCallbackPayload(accepted ? Result::ACCEPTED : Result::USER_CANCELED)); }
 private:
  ScopedJavaGlobalRef<jobject> owner_;
  DoneCallback callback_;
};
void JNI_PsychoExtensions_ResolveInstallPrompt(JNIEnv*, jlong pointer, jboolean accepted) {
  auto* prompt = reinterpret_cast<PsychoInstallPrompt*>(pointer);
  if (prompt && LivePrompts().count(prompt)) prompt->Resolve(accepted);
}
constexpr char kProtonId[] = "jplgfhpmjnbigmhklmmbgecoobifkmpa";
Profile* RegularProfile(const JavaRef<jobject>& jprofile) {
  Profile* profile = Profile::FromJavaObject(jprofile);
  return profile && !profile->IsOffTheRecord() ? profile : nullptr;
}
void AppendExtensions(const extensions::ExtensionSet& set, const char* state, base::ListValue& out) {
  for (const auto& extension : set) {
    base::DictValue item;
    item.Set("id", extension->id()); item.Set("name", extension->name());
    item.Set("version", extension->version().GetString());
    item.Set("enabled", std::string(state) == "enabled"); item.Set("loaded", std::string(state) == "enabled");
    item.Set("state", state); item.Set("mv", extension->manifest_version());
    item.Set("path", extension->path().value());
    const bool proton = extension->id() == kProtonId, dark = extension->name() == "Dark Reader";
    item.Set("bundled", proton || dark); item.Set("builtin", proton ? "proton-vpn" : dark ? "dark-reader" : "");
    item.Set("dir", proton ? "proton-vpn" : dark ? "dark-reader" : extension->path().BaseName().value());
    item.Set("popup", proton ? "popup.html" : dark ? "ui/popup/index.html" : "");
    out.Append(std::move(item));
  }
}
ScopedJavaLocalRef<jstring> JNI_PsychoExtensions_List(JNIEnv* env, const JavaRef<jobject>& jprofile) {
  auto* profile = RegularProfile(jprofile); auto* registry = profile ? extensions::ExtensionRegistry::Get(profile) : nullptr;
  if (!registry) return nullptr;
  base::ListValue items;
  AppendExtensions(registry->enabled_extensions(), "enabled", items);
  AppendExtensions(registry->disabled_extensions(), "disabled", items);
  AppendExtensions(registry->terminated_extensions(), "terminated", items);
  AppendExtensions(registry->blocked_extensions(), "blocked", items);
  std::string json; base::WriteJson(items, &json);
  return base::android::ConvertUTF8ToJavaString(env, json);
}
jboolean JNI_PsychoExtensions_SetEnabled(JNIEnv* env, const JavaRef<jobject>& jprofile,
                                         const JavaRef<jstring>& jid, jboolean enabled) {
  auto* profile = RegularProfile(jprofile); auto* registrar = profile ? extensions::ExtensionRegistrar::Get(profile) : nullptr;
  auto* registry = profile ? extensions::ExtensionRegistry::Get(profile) : nullptr;
  if (!registrar || !registry) return false;
  const std::string id = base::android::ConvertJavaStringToUTF8(env, jid);
  if (!registry->GetExtensionById(id, extensions::ExtensionRegistry::EVERYTHING)) return false;
  if (enabled) registrar->EnableExtension(id);
  else registrar->DisableExtension(id, {extensions::disable_reason::DISABLE_USER_ACTION});
  return enabled ? registry->enabled_extensions().Contains(id) : !registry->enabled_extensions().Contains(id);
}
void UninstallDone(const ScopedJavaGlobalRef<jobject>& owner, int request) {
  Java_PsychoExtensions_onNativeResult(base::android::AttachCurrentThread(), owner, request, true,
      base::android::ConvertUTF8ToJavaString(base::android::AttachCurrentThread(), ""));
}
jboolean JNI_PsychoExtensions_Uninstall(JNIEnv* env, const JavaRef<jobject>& jprofile,
                                        const JavaRef<jstring>& jid, const JavaRef<jobject>& owner, jint request) {
  auto* profile = RegularProfile(jprofile); auto* registrar = profile ? extensions::ExtensionRegistrar::Get(profile) : nullptr;
  if (!registrar) return false;
  const std::string id = base::android::ConvertJavaStringToUTF8(env, jid);
  std::u16string error; ScopedJavaGlobalRef<jobject> ref(env, owner);
  return registrar->UninstallExtension(id, extensions::UNINSTALL_REASON_USER_INITIATED, &error,
      base::BindOnce(&UninstallDone, ref, request));
}
void UnpackedDone(const ScopedJavaGlobalRef<jobject>& owner, int request,
                  const extensions::Extension* extension, const base::FilePath&, const std::u16string& error) {
  JNIEnv* env = base::android::AttachCurrentThread();
  Java_PsychoExtensions_onNativeResult(env, owner, request, extension != nullptr,
      base::android::ConvertUTF16ToJavaString(env, extension ? extension->id() : error));
}
jboolean JNI_PsychoExtensions_LoadUnpacked(JNIEnv* env, const JavaRef<jobject>& jprofile,
                                           const JavaRef<jstring>& jpath, const JavaRef<jobject>& owner, jint request) {
  auto* profile = RegularProfile(jprofile); if (!profile) return false;
  auto installer = extensions::UnpackedInstaller::Create(profile);
  ScopedJavaGlobalRef<jobject> ref(env, owner);
  installer->set_completion_callback(base::BindOnce(&UnpackedDone, ref, request));
  installer->Load(base::FilePath(base::android::ConvertJavaStringToUTF8(env, jpath)));
  return true;
}
void CrxDone(const ScopedJavaGlobalRef<jobject>& owner, int request,
             const std::optional<extensions::CrxInstallError>& error) {
  JNIEnv* env = base::android::AttachCurrentThread();
  Java_PsychoExtensions_onNativeResult(env, owner, request, !error.has_value(),
      base::android::ConvertUTF16ToJavaString(env, error ? error->message() : u""));
}
jboolean JNI_PsychoExtensions_InstallCrx(JNIEnv* env, const JavaRef<jobject>& jprofile,
                                         const JavaRef<jobject>& jcontents, const JavaRef<jstring>& jpath,
                                         const JavaRef<jobject>& owner, jint request) {
  auto* profile = RegularProfile(jprofile); if (!profile) return false;
  auto prompt = std::make_unique<PsychoInstallPrompt>(env, owner);
  auto installer = extensions::CrxInstaller::Create(profile, std::move(prompt));
  installer->set_off_store_install_allow_reason(extensions::CrxInstaller::OffStoreInstallAllowedFromSettingsPage);
  installer->set_delete_source(true);
  ScopedJavaGlobalRef<jobject> ref(env, owner);
  installer->AddInstallerCallback(base::BindOnce(&CrxDone, ref, request));
  installer->InstallCrx(base::FilePath(base::android::ConvertJavaStringToUTF8(env, jpath)));
  return true;
}
}  // namespace
