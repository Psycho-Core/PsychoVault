#include "base/android/jni_string.h"
#include "base/android/scoped_java_ref.h"
#include "base/files/file_path.h"
#include "base/json/json_writer.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "extensions/browser/disable_reason.h"
#include "extensions/browser/extension_registrar.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/uninstall_reason.h"
#include "extensions/browser/unpacked_installer.h"
#include "extensions/common/extension.h"
#include "extensions/common/mojom/manifest.mojom.h"

// Must come after all headers that specialize FromJniType()/ToJniType().
#include "psychovault/jni_headers/PsychoExtensions_jni.h"

namespace {
constexpr char kProtonId[] = "jplgfhpmjnbigmhklmmbgecoobifkmpa";

Profile* RegularProfile(const JavaRef<jobject>& jprofile) {
  Profile* profile = Profile::FromJavaObject(jprofile);
  if (!profile || profile->IsOffTheRecord()) return nullptr;
  return profile;
}

void AppendExtensions(const extensions::ExtensionSet& set, bool enabled,
                      base::ListValue& out) {
  for (const auto& extension : set) {
    base::DictValue item;
    item.Set("id", extension->id());
    item.Set("name", extension->name());
    item.Set("version", extension->version().GetString());
    item.Set("enabled", enabled);
    item.Set("loaded", enabled);
    item.Set("mv", extension->manifest_version());
    const bool proton = extension->id() == kProtonId;
    const bool dark = extension->name() == "Dark Reader";
    item.Set("bundled", proton || dark);
    item.Set("builtin", proton ? "proton-vpn" : dark ? "dark-reader" : "");
    item.Set("dir", extension->id() == kProtonId ? "proton-vpn"
                    : extension->name() == "Dark Reader" ? "dark-reader" : "");
    item.Set("popup", proton ? "popup.html" : dark ? "ui/popup/index.html" : "");
    out.Append(std::move(item));
  }
}

ScopedJavaLocalRef<jstring> JNI_PsychoExtensions_List(
    JNIEnv* env, const JavaRef<jobject>& jprofile) {
  Profile* profile = RegularProfile(jprofile);
  if (!profile) return nullptr;
  auto* registry = extensions::ExtensionRegistry::Get(profile);
  if (!registry) return nullptr;
  base::ListValue items;
  AppendExtensions(registry->enabled_extensions(), true, items);
  AppendExtensions(registry->disabled_extensions(), false, items);
  std::string json;
  base::WriteJson(items, &json);
  return base::android::ConvertUTF8ToJavaString(env, json);
}

void JNI_PsychoExtensions_SetEnabled(JNIEnv* env,
                                     const JavaRef<jobject>& jprofile,
                                     const JavaRef<jstring>& jid,
                                     jboolean enabled) {
  Profile* profile = RegularProfile(jprofile);
  if (!profile) return;
  auto* registrar = extensions::ExtensionRegistrar::Get(profile);
  if (!registrar) return;
  const std::string id = base::android::ConvertJavaStringToUTF8(env, jid);
  if (enabled) registrar->EnableExtension(id);
  else registrar->DisableExtension(id, {extensions::disable_reason::DISABLE_USER_ACTION});
}

void JNI_PsychoExtensions_Uninstall(JNIEnv* env,
                                    const JavaRef<jobject>& jprofile,
                                    const JavaRef<jstring>& jid) {
  Profile* profile = RegularProfile(jprofile);
  if (!profile) return;
  auto* registrar = extensions::ExtensionRegistrar::Get(profile);
  if (!registrar) return;
  const std::string id = base::android::ConvertJavaStringToUTF8(env, jid);
  std::u16string error;
  registrar->UninstallExtension(id, extensions::UNINSTALL_REASON_USER_INITIATED,
                                &error);
}

void JNI_PsychoExtensions_LoadUnpacked(JNIEnv* env,
                                       const JavaRef<jobject>& jprofile,
                                       const JavaRef<jstring>& jpath) {
  Profile* profile = RegularProfile(jprofile);
  if (!profile) return;
  const std::string path = base::android::ConvertJavaStringToUTF8(env, jpath);
  extensions::UnpackedInstaller::Create(profile)->Load(base::FilePath(path));
}
}  // namespace
