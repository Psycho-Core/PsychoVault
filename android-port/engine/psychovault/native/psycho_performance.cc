#include <string>
#include <vector>

#include "base/android/jni_array.h"
#include "base/functional/callback_helpers.h"
#include "base/json/string_escape.h"
#include "base/strings/utf_string_conversions.h"
#include "base/types/expected.h"
#include "base/android/jni_string.h"
#include "base/android/scoped_java_ref.h"
#include "base/functional/bind.h"
#include "base/time/time.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/isolated_world_ids.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"

#include "psychovault/jni_headers/PsychoPerformance_jni.h"

namespace {
using content::BrowserThread;
using base::android::JavaRef;
using base::android::ScopedJavaLocalRef;
using base::android::ScopedJavaGlobalRef;
void RunFixedScript(content::WebContents* contents, const std::string& script) {
  if (!contents || contents->IsBeingDestroyed()) return;
  auto* frame = contents->GetPrimaryMainFrame();
  if (!frame || !frame->IsRenderFrameLive()) return;
  // DOM is shared with the isolated world; no bridge or privileged object is added.
  frame->ExecuteJavaScriptInIsolatedWorld(base::UTF8ToUTF16(script), base::NullCallback(),
                                          content::ISOLATED_WORLD_ID_CONTENT_END + 1);
}
jboolean JNI_PsychoPerformance_IsAudible(JNIEnv*, const JavaRef<jobject>& jcontents) {
  auto* contents = content::WebContents::FromJavaWebContents(jcontents);
  return contents && !contents->IsBeingDestroyed() && contents->IsCurrentlyAudible();
}
void JNI_PsychoPerformance_Media(JNIEnv* env, const JavaRef<jobject>& jcontents,
                                 const JavaRef<jstring>& jcommand) {
  auto* contents = content::WebContents::FromJavaWebContents(jcontents);
  const std::string command = base::android::ConvertJavaStringToUTF8(env, jcommand);
  std::string script;
  if (command == "pause") script = "document.querySelectorAll('audio,video').forEach(m=>m.pause());";
  else if (command == "play") script = "document.querySelectorAll('audio,video').forEach(m=>{try{m.play().catch(()=>{})}catch{}});";
  else if (command == "toggle") script = "(()=>{const m=document.querySelector('audio,video');if(!m)return;if(m.paused){try{m.play().catch(()=>{})}catch{}}else m.pause();})();";
  else if (command == "next") script = "document.querySelectorAll('audio,video').forEach(m=>{try{m.currentTime=m.duration-1}catch{}});";
  else if (command == "prev") script = "document.querySelectorAll('audio,video').forEach(m=>{try{m.currentTime=0}catch{}});";
  if (!script.empty()) RunFixedScript(contents, script);
}
void JNI_PsychoPerformance_Enhance(JNIEnv* env, const JavaRef<jobject>& jcontents,
                                   jboolean video, jboolean images, jboolean reduce) {
  // Fixed, local CSS only. No remote enhancement service or injected credentials.
  std::string css = "html.pv-reduce-motion *,html.pv-reduce-motion *::before,html.pv-reduce-motion *::after{animation-duration:.001ms!important;transition-duration:.001ms!important;scroll-behavior:auto!important}";
  if (images) css += "img{image-rendering:auto}";
  if (video) css += "video{object-fit:contain}";
  std::u16string script = u"(()=>{document.documentElement.classList.toggle('pv-reduce-motion'," + std::string(reduce ? "true" : "false") + ");let s=document.getElementById('psycho-page-enhancements');if(!s){s=document.createElement('style');s.id='psycho-page-enhancements';document.documentElement.appendChild(s);}s.textContent=" + base::EscapeJSONString(base::UTF8ToUTF16(css), false) + ";})();";
  RunFixedScript(content::WebContents::FromJavaWebContents(jcontents), script);
}
void FinishSnapshot(const ScopedJavaGlobalRef<jobject>& owner, int id,
                    const content::CopyFromSurfaceResult& result) {
  std::vector<uint8_t> png;
  if (result.has_value() && !result->bitmap.drawsNothing()) {
    auto encoded = gfx::PNGCodec::EncodeBGRASkBitmap(result->bitmap, true);
    if (encoded) png = std::move(*encoded);
  }
  content::GetUIThreadTaskRunner({})->PostTask(FROM_HERE, base::BindOnce([](ScopedJavaGlobalRef<jobject> owner, int id, std::vector<uint8_t> png) {
    JNIEnv* env = base::android::AttachCurrentThread();
    auto bytes = png.empty() ? ScopedJavaLocalRef<jbyteArray>() : base::android::ToJavaByteArray(env, png.data(), png.size());
    Java_PsychoPerformance_onNativeSnapshot(env, owner, id, bytes);
  }, owner, id, std::move(png)));
}
void JNI_PsychoPerformance_Snapshot(JNIEnv* env, const JavaRef<jobject>& jcontents,
                                    const JavaRef<jobject>& owner, jint id) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  auto* contents = content::WebContents::FromJavaWebContents(jcontents);
  auto* view = contents ? contents->GetRenderWidgetHostView() : nullptr;
  ScopedJavaGlobalRef<jobject> ref(env, owner);
  if (!view || contents->GetSize().IsEmpty()) { FinishSnapshot(ref, id, base::unexpected(content::CopyFromSurfaceError::kFrameGone)); return; }
  view->CopyFromSurface(gfx::Rect(), contents->GetSize(), base::Seconds(5), base::BindOnce(&FinishSnapshot, ref, id));
}
}  // namespace
