#include "Psycho_payload.hpp"
#include <filesystem>
#include <fstream>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#include <shlobj.h>
#endif

namespace pv {
namespace {
bool g_embedded = false;


#ifdef _WIN32
std::string to_hex(const Bytes &bytes) {
  static const char *digits = "0123456789abcdef";
  std::string out;
  out.reserve(bytes.size() * 2);
  for (auto byte : bytes) {
    out += digits[byte >> 4];
    out += digits[byte & 15];
  }
  return out;
}

std::string narrow(const std::wstring &text) {
  if (text.empty()) return {};
  int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), int(text.size()), nullptr, 0, nullptr,
                                nullptr);
  std::string out(size, 0);
  WideCharToMultiByte(CP_UTF8, 0, text.c_str(), int(text.size()), out.data(), size, nullptr, nullptr);
  return out;
}

// I use the operating system hash rather than inventing one.
std::string sha256_hex(const unsigned char *data, size_t size) {
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  std::string result;
  if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0)
    return {};
  DWORD object_size = 0, queried = 0, digest_size = 0;
  if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&object_size),
                        sizeof(object_size), &queried, 0) == 0 &&
      BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&digest_size),
                        sizeof(digest_size), &queried, 0) == 0 &&
      object_size && digest_size && digest_size <= 64) {
    std::vector<unsigned char> object(object_size), digest(digest_size);
    if (BCryptCreateHash(algorithm, &hash, object.data(), object_size, nullptr, 0, 0) == 0) {
      // I chunk the input so a large payload never needs one huge call.
      size_t offset = 0;
      bool ok = true;
      while (offset < size) {
        auto chunk = ULONG(std::min<size_t>(size - offset, 1u << 20));
        if (BCryptHashData(hash, const_cast<PUCHAR>(data + offset), chunk, 0) != 0) {
          ok = false;
          break;
        }
        offset += chunk;
      }
      if (ok && BCryptFinishHash(hash, digest.data(), digest_size, 0) == 0) {
        static const char *hex = "0123456789abcdef";
        for (DWORD i = 0; i < digest_size; ++i) {
          result += hex[digest[i] >> 4];
          result += hex[digest[i] & 15];
        }
      }
    }
  }
  if (hash) BCryptDestroyHash(hash);
  if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
  return result;
}

std::string self_directory() {
  wchar_t buffer[MAX_PATH * 2] = {0};
  DWORD got = GetModuleFileNameW(nullptr, buffer, DWORD(std::size(buffer)));
  if (!got || got >= std::size(buffer)) return {};
  return narrow(std::wstring(buffer)).empty()
             ? std::string()
             : std::filesystem::path(std::wstring(buffer)).parent_path().string();
}

// My unpack folder lives under my own local app data, so another user cannot
// plant a renderer for me. I name it by payload hash, so a new version never
// fights a running old one for the same file.
std::string payload_root() {
  wchar_t *raw = nullptr;
  std::string base;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &raw)) && raw)
    base = narrow(raw);
  if (raw) CoTaskMemFree(raw);
  if (base.empty()) {
    const char *local = std::getenv("LOCALAPPDATA");
    if (local) base = local;
  }
  if (base.empty()) return {};
  return (std::filesystem::path(base) / "PsychoVault" / "engine").string();
}

bool file_matches(const std::string &path, const std::string &digest, size_t size) {
  std::error_code ec;
  if (!std::filesystem::is_regular_file(path, ec)) return false;
  if (std::filesystem::file_size(path, ec) != size || ec) return false;
  std::ifstream file(std::filesystem::path(path), std::ios::binary);
  if (!file) return false;
  std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)),
                                   std::istreambuf_iterator<char>());
  if (bytes.size() != size) return false;
  return sha256_hex(bytes.data(), bytes.size()) == digest;
}
#endif
} // namespace

bool renderer_is_embedded() { return g_embedded; }

std::string renderer_executable(std::string &error) {
  error.clear();
#ifdef _WIN32
  namespace fs = std::filesystem;
  // I prefer a renderer sitting beside me, which is how my development tree and
  // my full ZIP are laid out. Otherwise I unpack the copy inside me.
  auto directory = self_directory();
  if (!directory.empty()) {
    auto sibling = (fs::path(directory) / "pv-renderer.exe").string();
    std::error_code ec;
    if (fs::is_regular_file(sibling, ec)) {
      g_embedded = false;
      return sibling;
    }
  }

  HRSRC resource = FindResourceW(nullptr, MAKEINTRESOURCEW(101), RT_RCDATA);
  if (!resource) {
    error = "I have no embedded renderer and no pv-renderer.exe beside me. "
            "This executable was not built with my payload.";
    return {};
  }
  HGLOBAL loaded = LoadResource(nullptr, resource);
  DWORD size = SizeofResource(nullptr, resource);
  const auto *data = loaded ? static_cast<const unsigned char *>(LockResource(loaded)) : nullptr;
  if (!data || !size) {
    error = "I could not read my embedded renderer.";
    return {};
  }
  auto digest = sha256_hex(data, size);
  if (digest.empty()) {
    error = "I could not hash my embedded renderer, so I refuse to run it.";
    return {};
  }
  auto root = payload_root();
  if (root.empty()) {
    error = "I could not find my local app data folder to unpack my renderer.";
    return {};
  }
  auto folder = fs::path(root) / digest.substr(0, 32);
  auto target = (folder / "pv-renderer.exe").string();
  if (file_matches(target, digest, size)) {
    g_embedded = true;
    return target;
  }
  std::error_code ec;
  fs::create_directories(folder, ec);
  if (ec) {
    error = "I could not create my renderer folder: " + ec.message();
    return {};
  }
  // I write a private temporary file, verify it, then move it into place so I
  // never execute a half-written renderer.
  auto temporary = (folder / ("pv-renderer." + to_hex(random_bytes(8)) + ".tmp")).string();
  {
    std::ofstream file(fs::path(temporary), std::ios::binary | std::ios::trunc);
    if (!file) {
      error = "I could not write my renderer to " + temporary;
      return {};
    }
    file.write(reinterpret_cast<const char *>(data), std::streamsize(size));
    file.flush();
    if (!file) {
      error = "I could not finish writing my renderer.";
      file.close();
      fs::remove(fs::path(temporary), ec);
      return {};
    }
  }
  if (!file_matches(temporary, digest, size)) {
    fs::remove(fs::path(temporary), ec);
    error = "My unpacked renderer failed its own hash check, so I refuse to run it.";
    return {};
  }
  fs::rename(fs::path(temporary), fs::path(target), ec);
  if (ec) {
    // Another instance may have just unpacked the same payload. That is fine if
    // the file already there is the one I verified.
    fs::remove(fs::path(temporary), ec);
    if (!file_matches(target, digest, size)) {
      error = "I could not put my verified renderer in place.";
      return {};
    }
  }
  g_embedded = true;
  return target;
#else
  error = "My embedded renderer payload is a Windows feature.";
  return {};
#endif
}
} // namespace pv
