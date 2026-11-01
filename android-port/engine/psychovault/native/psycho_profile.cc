#include "base/android/jni_string.h"
#include "base/android/scoped_java_ref.h"
#include "chrome/browser/profiles/profile.h"

// Must come after all headers that specialize FromJniType()/ToJniType().
#include "psychovault/jni_headers/PsychoProfileStore_jni.h"

namespace {
ScopedJavaLocalRef<jstring> JNI_PsychoProfileStore_GetProfileName(
    JNIEnv* env, const JavaRef<jobject>& jprofile) {
  Profile* profile = Profile::FromJavaObject(jprofile);
  if (!profile || profile->IsOffTheRecord()) return nullptr;
  // The directory basename is stable for the life of the profile and distinct
  // between regular profiles. It never leaves the browser process as a path.
  return base::android::ConvertUTF8ToJavaString(env, profile->GetBaseName().value());
}
}  // namespace
