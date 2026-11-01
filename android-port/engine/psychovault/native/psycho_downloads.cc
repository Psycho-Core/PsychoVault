#include <algorithm>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "base/android/jni_string.h"
#include "base/android/scoped_java_ref.h"
#include "base/files/file_path.h"
#include "base/json/json_writer.h"
#include "base/memory/raw_ptr.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "components/download/public/common/download_item.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/download_manager.h"

#include "psychovault/jni_headers/PsychoDownloads_jni.h"

namespace {
using content::BrowserThread;
using base::android::JavaRef;
using base::android::ScopedJavaLocalRef;
using base::android::ScopedJavaGlobalRef;
using download::DownloadItem;
class Observer final : public content::DownloadManager::Observer,
                       public DownloadItem::Observer {
 public:
  Observer(JNIEnv* env, content::DownloadManager* manager,
           const JavaRef<jobject>& owner)
      : manager_(manager), owner_(env, owner) {
    manager_->AddObserver(this);
    std::vector<raw_ptr<DownloadItem, VectorExperimental>> items;
    manager_->GetAllDownloads(&items);
    for (auto* item : items) Watch(item);
  }
  ~Observer() override {
    if (manager_) manager_->RemoveObserver(this);
    for (auto* item : items_) item->RemoveObserver(this);
  }
  std::string List() {
    std::vector<raw_ptr<DownloadItem, VectorExperimental>> downloads;
    if (manager_) manager_->GetAllDownloads(&downloads);
    std::sort(downloads.begin(), downloads.end(), [](const auto& a, const auto& b) { return a->GetStartTime() > b->GetStartTime(); });
    base::ListValue out;
    for (auto* item : downloads) {
      if (out.size() >= 200) break;
      base::DictValue row;
      row.Set("id", item->GetGuid());
      row.Set("name", item->GetFileNameToReportUser().BaseName().value());
      row.Set("file", item->GetTargetFilePath().BaseName().value());
      row.Set("path", item->GetGuid());
      row.Set("url", item->GetOriginalUrl().GetAsReferrer().spec());
      row.Set("received", static_cast<double>(item->GetReceivedBytes()));
      row.Set("total", static_cast<double>(item->GetTotalBytes()));
      row.Set("at", item->GetStartTime().InMillisecondsSinceUnixEpoch());
      row.Set("canResume", item->CanResume());
      row.Set("canOpen", item->CanOpenDownload());
      const char* state = "interrupted";
      if (item->GetState() == DownloadItem::IN_PROGRESS) state = item->IsPaused() ? "paused" : "progress";
      else if (item->GetState() == DownloadItem::COMPLETE) state = item->GetFileExternallyRemoved() ? "missing" : "completed";
      else if (item->GetState() == DownloadItem::CANCELLED) state = "cancelled";
      row.Set("state", state);
      row.Set("danger", static_cast<int>(item->GetDangerType()));
      out.Append(std::move(row));
    }
    std::string json; base::WriteJson(out, &json); return json;
  }
  DownloadItem* Find(const std::string& guid) {
    return manager_ ? manager_->GetDownloadByGuid(guid) : nullptr;
  }
  bool Control(const std::string& guid, const std::string& action) {
    auto* item = Find(guid); if (!item) return false;
    if (action == "pause" && item->GetState() == DownloadItem::IN_PROGRESS && !item->IsPaused()) { item->Pause(); return true; }
    if (action == "resume" && item->CanResume()) { item->Resume(true); return true; }
    if (action == "cancel" && item->GetState() == DownloadItem::IN_PROGRESS) { item->Cancel(true); return true; }
    return false;
  }
  bool Open(const std::string& guid) {
    auto* item = Find(guid);
    if (!item || item->GetState() != DownloadItem::COMPLETE || !item->CanOpenDownload()) return false;
    item->OpenDownload(); return true;
  }
  void Clear() {
    std::vector<raw_ptr<DownloadItem, VectorExperimental>> downloads;
    if (manager_) manager_->GetAllDownloads(&downloads);
    for (auto* item : downloads) if (item->GetState() != DownloadItem::IN_PROGRESS) item->Remove();
  }
 private:
  void Watch(DownloadItem* item) { if (items_.insert(item).second) item->AddObserver(this); }
  void Unwatch(DownloadItem* item) { if (items_.erase(item)) item->RemoveObserver(this); }
  void OnDownloadCreated(content::DownloadManager*, DownloadItem* item) override { Watch(item); Changed(); }
  void ManagerGoingDown(content::DownloadManager*) override {
    for (auto* item : items_) item->RemoveObserver(this);
    items_.clear(); manager_ = nullptr; Changed();
  }
  void OnDownloadUpdated(DownloadItem*) override { Changed(); }
  void OnDownloadRemoved(DownloadItem* item) override { Unwatch(item); Changed(); }
  void OnDownloadDestroyed(DownloadItem* item) override { items_.erase(item); Changed(); }
  void Changed() { Java_PsychoDownloads_onNativeChanged(base::android::AttachCurrentThread(), owner_); }
  raw_ptr<content::DownloadManager> manager_;
  ScopedJavaGlobalRef<jobject> owner_;
  std::set<raw_ptr<DownloadItem, SetExperimental>> items_;
};
Observer* Get(jlong pointer) { return reinterpret_cast<Observer*>(pointer); }
jlong JNI_PsychoDownloads_Attach(JNIEnv* env, const JavaRef<jobject>& jprofile,
                                 const JavaRef<jobject>& owner) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  auto* profile = Profile::FromJavaObject(jprofile);
  auto* manager = profile ? profile->GetDownloadManager() : nullptr;
  if (!manager) return 0;
  return reinterpret_cast<jlong>(new Observer(env, manager, owner));
}
ScopedJavaLocalRef<jstring> JNI_PsychoDownloads_List(JNIEnv* env, jlong pointer) {
  auto* observer = Get(pointer);
  return observer ? base::android::ConvertUTF8ToJavaString(env, observer->List()) : ScopedJavaLocalRef<jstring>();
}
jboolean JNI_PsychoDownloads_Control(JNIEnv* env, jlong pointer, const JavaRef<jstring>& guid,
                                     const JavaRef<jstring>& action) {
  auto* observer = Get(pointer);
  return observer && observer->Control(base::android::ConvertJavaStringToUTF8(env, guid), base::android::ConvertJavaStringToUTF8(env, action));
}
jboolean JNI_PsychoDownloads_Open(JNIEnv* env, jlong pointer, const JavaRef<jstring>& guid) {
  auto* observer = Get(pointer);
  return observer && observer->Open(base::android::ConvertJavaStringToUTF8(env, guid));
}
void JNI_PsychoDownloads_Clear(JNIEnv*, jlong pointer) { if (auto* o = Get(pointer)) o->Clear(); }
void JNI_PsychoDownloads_Detach(JNIEnv*, jlong pointer) { delete Get(pointer); }
}  // namespace
