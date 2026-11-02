#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "base/android/jni_string.h"
#include "base/android/scoped_java_ref.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/no_destructor.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/storage_partition.h"
#include "net/base/net_errors.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/url_loader_factory.mojom.h"
#include "url/gurl.h"
#include "url/origin.h"

#include "psychovault/jni_headers/PsychoVpnCheck_jni.h"

namespace {
using base::android::JavaRef;
using base::android::ScopedJavaGlobalRef;

struct Request {
  ScopedJavaGlobalRef<jobject> owner;
  int id;
  std::unique_ptr<network::SimpleURLLoader> loader;
};
using Key = std::pair<jobject, int>;
std::map<Key, std::unique_ptr<Request>>& Live() {
  static base::NoDestructor<std::map<Key, std::unique_ptr<Request>>> live;
  return *live;
}
void Finish(Request* request, std::optional<std::string> body) {
  auto it = Live().find(Key{request->owner.obj(), request->id});
  if (it == Live().end() || it->second.get() != request) return;
  // Take ownership out of the map before calling Java, but do not destroy the
  // SimpleURLLoader inside its own callback. Delete it on the next UI task.
  std::unique_ptr<Request> owned = std::move(it->second);
  Live().erase(it);

  JNIEnv* env = base::android::AttachCurrentThread();
  std::string json = "{\"ok\":false}";
  if (body && request->loader->NetError() == net::OK) {
    auto parsed = base::JSONReader::Read(*body);
    if (parsed && parsed->is_dict()) {
      const std::string* ip = parsed->GetDict().FindString("ip");
      if (ip && !ip->empty()) {
        base::DictValue out;
        out.Set("ok", true);
        out.Set("ip", *ip);
        for (const char* key : {"city", "region", "country_name"}) {
          const std::string* value = parsed->GetDict().FindString(key);
          if (value) out.Set(key, *value);
        }
        json = base::WriteJson(out).value_or("{\"ok\":false}");
      }
    }
  }
  Java_PsychoVpnCheck_onNativeResult(env, request->owner, request->id,
      base::android::ConvertUTF8ToJavaString(env, json));
  base::SequencedTaskRunner::GetCurrentDefault()->DeleteSoon(FROM_HERE,
                                                             std::move(owned));
}
}  // namespace

jboolean JNI_PsychoVpnCheck_Check(JNIEnv* env,
                                  const JavaRef<jobject>& jprofile,
                                  const JavaRef<jobject>& owner, jint id) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  Profile* profile = Profile::FromJavaObject(jprofile);
  if (!profile) return false;
  auto resource = std::make_unique<network::ResourceRequest>();
  resource->url = GURL("https://ipapi.co/json/");
  resource->method = "GET";
  resource->credentials_mode = network::mojom::CredentialsMode::kOmit;
  resource->request_initiator = url::Origin::Create(GURL("chrome://psychovault-pages"));
  static constexpr net::NetworkTrafficAnnotationTag kAnnotation =
      net::DefineNetworkTrafficAnnotation("psychovault_vpn_ip_check", R"(
        semantics {
          sender: "PsychoVault VPN checks"
          description: "User-requested public IP lookup through the selected browser profile."
          trigger: "The user presses Check before VPN or Check after VPN in VPN checks."
          data: "No user content is sent. The service sees the request IP and returns approximate location."
          destination: OTHER
        }
        policy {
          cookies_allowed: NO
          setting: "The check runs only when the user requests it."
          policy_exception_justification: "Not implemented."
        })");
  auto request = std::make_unique<Request>();
  request->owner = ScopedJavaGlobalRef<jobject>(env, owner);
  request->id = id;
  request->loader = network::SimpleURLLoader::Create(std::move(resource), kAnnotation);
  request->loader->SetTimeoutDuration(base::Seconds(15));
  // Keep the factory alive for the synchronous DownloadToString call. The
  // loader clones the mojo remote it needs; the scoped_refptr itself need not
  // outlive this function, but a temporary .get() is easy to misread.
  scoped_refptr<network::mojom::URLLoaderFactory> factory =
      profile->GetDefaultStoragePartition()->GetURLLoaderFactoryForBrowserProcess();
  network::SimpleURLLoader* loader = request->loader.get();
  Request* raw = request.get();
  // The map owns the request. Replacing or erasing it cancels the loader before
  // the callback can run; Finish erases only this owner/id.
  Live()[Key{raw->owner.obj(), id}] = std::move(request);
  loader->DownloadToString(factory.get(), base::BindOnce(&Finish, base::Unretained(raw)), 64 * 1024);
  return true;
}
void JNI_PsychoVpnCheck_CancelAll(JNIEnv* env, const JavaRef<jobject>& owner) {
  for (auto it = Live().begin(); it != Live().end();) {
    // owner.obj() may be a local ref while the map key is a global ref; pointer
    // equality is not Java object identity.
    if (env->IsSameObject(it->first.first, owner.obj())) it = Live().erase(it); else ++it;
  }
}
