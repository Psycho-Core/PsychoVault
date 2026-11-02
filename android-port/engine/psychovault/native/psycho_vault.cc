#include <stdint.h>
#include <vector>

#include "base/android/jni_array.h"
#include "base/android/jni_string.h"
#include "base/android/scoped_java_ref.h"
#include "openssl/crypto.h"
#include "openssl/evp.h"

#include "psychovault/jni_headers/PsychoVaultStore_jni.h"

namespace {
using base::android::JavaRef;
using base::android::ScopedJavaLocalRef;
// The desktop vault and .pvbackup format use scrypt N=16384, r=8, p=1.
// BoringSSL performs the derivation off Chromium's UI thread; no password is logged.
ScopedJavaLocalRef<jbyteArray> JNI_PsychoVaultStore_DeriveKey(
    JNIEnv* env, const JavaRef<jstring>& password, const JavaRef<jbyteArray>& salt) {
  const std::string pass = base::android::ConvertJavaStringToUTF8(env, password);
  if (salt.is_null()) return nullptr;
  std::vector<uint8_t> salt_bytes;
  base::android::JavaByteArrayToByteVector(env, salt, &salt_bytes);
  if (salt_bytes.size() != 16) return nullptr;
  uint8_t key[32];
  if (EVP_PBE_scrypt(pass.data(), pass.size(), salt_bytes.data(), salt_bytes.size(),
                     16384, 8, 1, 64 * 1024 * 1024, key, sizeof(key)) != 1) {
    OPENSSL_cleanse(key, sizeof(key));
    return nullptr;
  }
  auto result = base::android::ToJavaByteArray(env, key, sizeof(key));
  OPENSSL_cleanse(key, sizeof(key));
  return result;
}
}  // namespace
