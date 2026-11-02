#include "psychovault/native/psycho_network.h"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <utility>

#include "base/android/jni_string.h"
#include "base/android/scoped_java_ref.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/memory/raw_ptr.h"
#include "base/no_destructor.h"
#include "base/synchronization/lock.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/threading/thread_restrictions.h"
#include "base/values.h"
#include "chrome/browser/content_settings/host_content_settings_map_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/spellchecker/spellcheck_custom_dictionary.h"
#include "chrome/browser/spellchecker/spellcheck_factory.h"
#include "chrome/browser/spellchecker/spellcheck_hunspell_dictionary.h"
#include "chrome/browser/spellchecker/spellcheck_service.h"
#include "chrome/common/pref_names.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "components/content_settings/core/common/content_settings.h"
#include "components/content_settings/core/common/content_settings_pattern.h"
#include "components/content_settings/core/common/content_settings_types.h"
#include "components/content_settings/core/common/pref_names.h"
#include "components/prefs/pref_service.h"
#include "components/proxy_config/proxy_config_dictionary.h"
#include "components/proxy_config/proxy_config_pref_names.h"
#include "components/spellcheck/browser/pref_names.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/web_contents.h"
#include "net/base/net_errors.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "net/http/http_request_headers.h"
#include "services/network/public/cpp/http_request_headers_update_params.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "url/gurl.h"
#include "url/origin.h"

#include "psychovault/jni_headers/PsychoNetwork_jni.h"

namespace psychovault {
namespace {
using content::BrowserThread;
using base::android::JavaRef;
using base::android::ScopedJavaLocalRef;
struct Policy {
  bool block = false, dnt = true, referrer = true, third_party_cookies = false;
  std::set<std::string> domains, allow;
};
struct Counts { base::Lock lock; std::map<std::string, int> pages; int total = 0; };
struct State {
  std::map<Profile*, std::shared_ptr<const Policy>> policies;
  std::map<Profile*, std::shared_ptr<Counts>> counts;
  std::map<Profile*, std::set<std::string>> permission_origins;
};
State& GetState() { static base::NoDestructor<State> state; return *state; }
std::string Site(const GURL& url) {
  return net::registry_controlled_domains::GetDomainAndRegistry(
      url, net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES);
}
bool DomainMatch(const std::set<std::string>& list, const std::string& host) {
  for (const auto& domain : list) if (host == domain || base::EndsWith(host, "." + domain)) return true;
  return false;
}
std::shared_ptr<const Policy> GetPolicy(Profile* profile) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  auto it = GetState().policies.find(profile);
  return it == GetState().policies.end() ? std::make_shared<Policy>() : it->second;
}
void CountBlock(Profile* profile, content::WebContents* contents) {
  if (!profile) return;
  auto& counts = GetState().counts[profile];
  if (!counts) counts = std::make_shared<Counts>();
  base::AutoLock lock(counts->lock);
  counts->total++;
  if (contents) counts->pages[contents->GetUniqueToken().ToString()]++;
}
class NetworkThrottle : public blink::URLLoaderThrottle {
 public:
  NetworkThrottle(Profile* profile, content::WebContents* contents,
                  std::shared_ptr<const Policy> policy)
      : profile_(profile ? profile->GetWeakPtr() : base::WeakPtr<Profile>()), contents_(contents ? contents->GetWeakPtr() : base::WeakPtr<content::WebContents>()), policy_(std::move(policy)) {}
 private:
  bool Block(const GURL& url) {
    if (!policy_->block || !url.SchemeIsHTTPOrHTTPS() || !profile_) return false;
    GURL top = contents_ ? contents_->GetLastCommittedURL() : GURL();
    if (policy_->allow.count(top.host())) return false;
    return DomainMatch(policy_->domains, url.host());
  }
  void Apply(network::ResourceRequest* request, bool main_frame) {
    if (!request || !request->url.SchemeIsHTTPOrHTTPS()) return;
    if (Block(request->url)) {
      CountBlock(profile_.get(), contents_.get());
      delegate_->CancelWithError(net::ERR_BLOCKED_BY_CLIENT, "PsychoVault domain blocker");
      return;
    }
    if (policy_->dnt) {
      request->headers.SetHeader("DNT", "1");
      request->headers.SetHeader("Sec-GPC", "1");
    }
    const bool cross_origin = !request->request_initiator || request->request_initiator->GetURL().DeprecatedGetOriginAsURL() != request->url.DeprecatedGetOriginAsURL();
    const bool cross_site = !request->request_initiator || Site(request->request_initiator->GetURL()) != Site(request->url);
    if (!main_frame && policy_->referrer && cross_origin) request->headers.RemoveHeader(net::HttpRequestHeaders::kReferer);
    // CookieControlsMode also enforces Set-Cookie; this removes the request side
    // immediately for already-created loaders. Neither is a VPN leak guarantee.
    if (!main_frame && policy_->third_party_cookies && cross_site) request->headers.RemoveHeader(net::HttpRequestHeaders::kCookie);
  }
  void WillStartRequest(network::ResourceRequest* request, bool* defer) override {
    if (request && request->is_outermost_main_frame && contents_ && profile_) {
      auto& counts = GetState().counts[profile_.get()];
      if (counts) { base::AutoLock lock(counts->lock); counts->pages[contents_->GetUniqueToken().ToString()] = 0; }
    }
    Apply(request, request && request->is_outermost_main_frame);
  }
  void WillRedirectRequest(net::RedirectInfo* redirect,
      const network::mojom::URLResponseHead& head, bool* defer,
      network::HttpRequestHeadersUpdateParams* headers) override {
    if (!redirect || !redirect->new_url.SchemeIsHTTPOrHTTPS()) return;
    if (Block(redirect->new_url)) {
      CountBlock(profile_.get(), contents_.get());
      delegate_->CancelWithError(net::ERR_BLOCKED_BY_CLIENT, "PsychoVault domain blocker");
      return;
    }
    if (headers && policy_->dnt) {
      headers->modified_headers.SetHeader("DNT", "1");
      headers->modified_headers.SetHeader("Sec-GPC", "1");
    }
  }
  base::WeakPtr<Profile> profile_;
  base::WeakPtr<content::WebContents> contents_;
  std::shared_ptr<const Policy> policy_;
};

void SetPref(PrefService* prefs, const char* name, bool value) {
  if (prefs->FindPreference(name) && !prefs->IsManagedPreference(name)) prefs->SetBoolean(name, value);
}
void JNI_PsychoNetwork_Update(JNIEnv* env, const JavaRef<jobject>& jprofile,
                              const JavaRef<jstring>& jjson) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  Profile* profile = Profile::FromJavaObject(jprofile);
  if (!profile) return;
  if (profile->IsOffTheRecord()) {
    Profile* original = profile->GetOriginalProfile();
    if (original) GetState().policies[profile] = GetPolicy(original);
    return;  // OTR requests inherit the regular policy; no OTR preference writes.
  }
  auto parsed = base::JSONReader::Read(base::android::ConvertJavaStringToUTF8(env, jjson));
  if (!parsed || !parsed->is_dict()) return;
  const auto& value = parsed->GetDict();
  auto policy = std::make_shared<Policy>();
  policy->block = value.FindBool("blockAds").value_or(false);
  policy->dnt = value.FindBool("sendDNT").value_or(true);
  policy->referrer = value.FindBool("stripReferrer").value_or(true);
  policy->third_party_cookies = value.FindBool("blockThirdPartyCookies").value_or(false);
  if (value.FindBool("blockUseBundled").value_or(true)) {
    if (const auto* list = value.FindList("bundledDomains")) for (const auto& item : *list) if (item.is_string()) policy->domains.insert(item.GetString());
  }
  for (const auto* key : {"blockCustomDomains", "blockAllowSites"}) {
    const std::string* text = value.FindString(key);
    if (!text) continue;
    std::set<std::string>& target = std::string(key) == "blockCustomDomains" ? policy->domains : policy->allow;
    for (std::string_view part : base::SplitStringPiece(*text, "\n,", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
      std::string host = base::ToLowerASCII(part);
      if (!host.empty() && host.size() <= 253 && host.find('/') == std::string::npos) target.insert(host);
    }
  }
  GetState().policies[profile] = std::move(policy);
  PrefService* prefs = profile->GetPrefs();
  SetPref(prefs, prefs::kEnableDoNotTrack, value.FindBool("sendDNT").value_or(true));
  SetPref(prefs, prefs::kBlockAutoplayEnabled, value.FindBool("blockAutoplay").value_or(false));
  if (prefs->FindPreference(prefs::kCookieControlsMode)) prefs->SetInteger(prefs::kCookieControlsMode, value.FindBool("blockThirdPartyCookies").value_or(false) ? 1 : 0);
  const std::string rtc = value.FindString("webRtc") ? *value.FindString("webRtc") : "publiconly";
  const std::string rtc_value = rtc == "default" ? "default" : rtc == "disableudp" ? "disable_non_proximity_udp" : "default_public_interface_only";
  if (prefs->FindPreference(prefs::kWebRTCIPHandlingPolicy)) prefs->SetString(prefs::kWebRTCIPHandlingPolicy, rtc_value);
  const std::string dns = value.FindString("doh") ? *value.FindString("doh") : "off";
  if (prefs->FindPreference(prefs::kDnsOverHttpsMode) && (dns == "off" || dns == "automatic" || dns == "secure")) prefs->SetString(prefs::kDnsOverHttpsMode, dns);
  if (prefs->FindPreference(prefs::kDnsOverHttpsTemplates)) prefs->SetString(prefs::kDnsOverHttpsTemplates, value.FindString("dohTemplate") ? *value.FindString("dohTemplate") : "");
  if (const auto* prompt = value.FindBool("askSaveLocation"); prompt && prefs->FindPreference(prefs::kPromptForDownloadAndroid)) {
    // Values mirror chrome/browser/download/download_prompt_status.h (SHOW_PREFERENCE=1, DONT_SHOW=2).
    prefs->SetInteger(prefs::kPromptForDownloadAndroid, *prompt ? 1 : 2);
  }
  const std::string* dir = value.FindString("downloadDir");
  if (dir && !dir->empty() && dir->starts_with("/") && dir->find("..") == std::string::npos && prefs->FindPreference(prefs::kDownloadDefaultDirectory)) prefs->SetString(prefs::kDownloadDefaultDirectory, *dir);
  SetPref(prefs, spellcheck::prefs::kSpellCheckEnable, value.FindBool("spellCheck").value_or(true));
  SetPref(prefs, spellcheck::prefs::kSpellCheckUseSpellingService, value.FindBool("spellBuiltin").value_or(true));
  const std::string* langs = value.FindString("spellLangs");
  if (langs && prefs->FindPreference(spellcheck::prefs::kSpellCheckDictionaries)) {
    base::ListValue dictionaries;
    for (std::string_view lang : base::SplitStringPiece(*langs, ",", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
      std::string normalized = base::ToLowerASCII(lang);
      std::replace(normalized.begin(), normalized.end(), '_', '-');
      if (normalized.size() <= 35) dictionaries.Append(normalized);
    }
    prefs->SetList(spellcheck::prefs::kSpellCheckDictionaries, std::move(dictionaries));
  }
  auto* map = HostContentSettingsMapFactory::GetForProfile(profile);
  if (map) {
    const std::string denied = value.FindString("denyPerms") ? *value.FindString("denyPerms") : "";
    std::set<std::string> deny;
    for (auto part : base::SplitStringPiece(denied, ",", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) deny.insert(base::ToLowerASCII(part));
    const std::pair<const char*, ContentSettingsType> types[] = {
      {"geolocation", ContentSettingsType::GEOLOCATION}, {"media", ContentSettingsType::MEDIASTREAM_CAMERA},
      {"media", ContentSettingsType::MEDIASTREAM_MIC}, {"notifications", ContentSettingsType::NOTIFICATIONS},
      {"clipboard-read", ContentSettingsType::CLIPBOARD_READ_WRITE}, {"midi", ContentSettingsType::MIDI_SYSEX}};
    for (const auto& [name, type] : types) map->SetDefaultContentSetting(type, deny.count(name) ? CONTENT_SETTING_BLOCK : CONTENT_SETTING_ASK);
    const std::pair<const char*, ContentSettingsType> site_types[] = {
      {"camera", ContentSettingsType::MEDIASTREAM_CAMERA}, {"microphone", ContentSettingsType::MEDIASTREAM_MIC},
      {"geolocation", ContentSettingsType::GEOLOCATION}, {"notifications", ContentSettingsType::NOTIFICATIONS}};
    auto& previous = GetState().permission_origins[profile];
    for (const auto& origin : previous) {
      auto pattern = ContentSettingsPattern::FromString(origin);
      if (pattern.IsValid()) for (const auto& [name, type] : site_types) map->SetContentSettingCustomScope(pattern, ContentSettingsPattern::Wildcard(), type, CONTENT_SETTING_DEFAULT);
    }
    previous.clear();
    if (const auto* sites = value.FindDict("sitePermissions")) {
      for (const auto& [origin, rule] : *sites) {
        if (!rule.is_dict()) continue;
        auto pattern = ContentSettingsPattern::FromString(origin);
        if (!pattern.IsValid()) continue;
        bool touched = false;
        for (const auto& [name, type] : site_types) {
          const std::string* setting = rule.GetDict().FindString(name);
          if (!setting) continue;
          ContentSetting parsed = *setting == "allow" ? CONTENT_SETTING_ALLOW : *setting == "deny" ? CONTENT_SETTING_BLOCK : CONTENT_SETTING_DEFAULT;
          map->SetContentSettingCustomScope(pattern, ContentSettingsPattern::Wildcard(), type, parsed);
          touched = true;
        }
        if (touched) previous.insert(origin);
      }
    }
  }
  if (const auto* mirror = value.FindString("spellDictUrl")) {
    GURL url(*mirror);
    SpellcheckHunspellDictionary::SetDownloadURLMirror(url.is_valid() && url.SchemeIsHTTPOrHTTPS() ? url : GURL());
  }
  if (const auto* words = value.FindList("spellUserWords")) {
    if (auto* service = SpellcheckServiceFactory::GetForContext(profile)) {
      if (auto* dictionary = service->GetCustomDictionary()) {
        std::set<std::string> wanted;
        for (const auto& item : *words) if (item.is_string() && item.GetString().size() <= 99) wanted.insert(item.GetString());
        for (const auto& word : dictionary->GetWords()) if (!wanted.count(word)) dictionary->RemoveWord(word);
        for (const auto& word : wanted) dictionary->AddWord(word);
      }
    }
  }
  prefs->CommitPendingWrite();
}

ScopedJavaLocalRef<jstring> JNI_PsychoNetwork_Status(JNIEnv* env, const JavaRef<jobject>& jprofile,
                                                   const JavaRef<jobject>& jcontents) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  Profile* profile = Profile::FromJavaObject(jprofile);
  if (!profile) return nullptr;
  auto* contents = content::WebContents::FromJavaWebContents(jcontents);
  int page = 0, total = 0;
  auto it = GetState().counts.find(profile);
  if (it != GetState().counts.end()) {
    base::AutoLock lock(it->second->lock); total = it->second->total;
    if (contents) { auto p = it->second->pages.find(contents->GetUniqueToken().ToString()); if (p != it->second->pages.end()) page = p->second; }
  }
  base::DictValue out;
  out.Set("blocked", page); out.Set("blockedTotal", total);
  auto policy = GetPolicy(profile);
  out.Set("active", policy->block); out.Set("rules", static_cast<int>(policy->domains.size()));
  PrefService* prefs = profile->GetPrefs();
  auto stringPref = [&](const char* key) { return prefs->FindPreference(key) ? prefs->GetString(key) : std::string(); };
  out.Set("webrtc", stringPref(prefs::kWebRTCIPHandlingPolicy));
  out.Set("dohMode", stringPref(prefs::kDnsOverHttpsMode));
  out.Set("dohTemplate", stringPref(prefs::kDnsOverHttpsTemplates));
  bool configured = false;
  if (prefs->FindPreference(proxy_config::prefs::kProxy)) {
    auto proxy = prefs->GetDict(proxy_config::prefs::kProxy).Clone();
    proxy_config::ProxyConfigDictionary config(std::move(proxy));
    ProxyPrefs::ProxyMode mode; std::string server;
    configured = config.GetMode(&mode) && mode == ProxyPrefs::MODE_FIXED_SERVERS && config.GetProxyServer(&server) && !server.empty();
  }
  out.Set("proxyConfigured", configured);
  // Deliberately no public proxy host, PAC URL, credentials or connection claim.
  std::string json; base::WriteJson(out, &json);
  return base::android::ConvertUTF8ToJavaString(env, json);
}
void JNI_PsychoNetwork_Forget(JNIEnv*, const JavaRef<jobject>& jprofile) {
  auto* profile = Profile::FromJavaObject(jprofile);
  GetState().policies.erase(profile); GetState().counts.erase(profile); GetState().permission_origins.erase(profile);
}
}  // namespace
std::unique_ptr<blink::URLLoaderThrottle> CreateNetworkThrottle(Profile* profile, content::WebContents* contents) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  return std::make_unique<NetworkThrottle>(profile, contents, GetPolicy(profile));
}
void UpdateNetworkPolicy(Profile* profile, const std::string& json) {
  // Native callers use the same full policy parser as Java; no block-only shortcut.
  if (!profile) return;
  JNIEnv* env = base::android::AttachCurrentThread();
  auto jprofile = profile->GetJavaObject();
  JNI_PsychoNetwork_Update(env, jprofile, base::android::ConvertUTF8ToJavaString(env, json));
}
void ForgetNetworkProfile(Profile* profile) { GetState().policies.erase(profile); GetState().counts.erase(profile); GetState().permission_origins.erase(profile); }
}  // namespace psychovault
