#include "updater.h"

#include <windows.h>
#include <bcrypt.h>
#include <wincrypt.h>

#include <filesystem>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/base.h>

#include "files.h"
#include "http.h"
#include "install.h"
#include "version.h"

using winrt::Windows::Data::Json::JsonObject;

namespace {

constexpr wchar_t kVersion[] = L"" VOX_VERSION_STRING;
constexpr wchar_t kVersionFolder[] = L"app-" VOX_VERSION_STRING;
constexpr DWORD kHashChunk = 1 << 20;

UpdateStatus failed(const std::wstring& message) { return {L"error", {}, message}; }

std::wstring lastError(const std::wstring& step) { return step + L"，錯誤碼 " + std::to_wstring(GetLastError()); }

std::optional<std::wstring> installedRoot() {
  const std::filesystem::path dir(exeDir());
  if (dir.filename() != kVersionFolder) return std::nullopt;
  const std::wstring root = dir.parent_path().wstring();
  if (!readMarker(root, kCurrentMarker)) return std::nullopt;
  return root;
}

std::vector<int> versionParts(const std::wstring& text) {
  std::vector<int> parts;
  size_t start = 0;
  while (start <= text.size()) {
    size_t dot = text.find(L'.', start);
    if (dot == std::wstring::npos) dot = text.size();
    parts.push_back(std::stoi(text.substr(start, dot - start)));
    start = dot + 1;
  }
  return parts;
}

bool newer(const std::wstring& candidate, const std::wstring& base) { return versionParts(candidate) > versionParts(base); }

class Sha256 {
public:
  Sha256() {
    BCryptOpenAlgorithmProvider(&algorithm_, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    BCryptCreateHash(algorithm_, &hash_, nullptr, 0, nullptr, 0, 0);
  }
  ~Sha256() {
    BCryptDestroyHash(hash_);
    BCryptCloseAlgorithmProvider(algorithm_, 0);
  }
  Sha256(const Sha256&) = delete;
  Sha256& operator=(const Sha256&) = delete;
  void add(const void* data, size_t size) { BCryptHashData(hash_, PUCHAR(data), ULONG(size), 0); }
  std::vector<BYTE> digest() {
    std::vector<BYTE> out(32);
    BCryptFinishHash(hash_, out.data(), ULONG(out.size()), 0);
    return out;
  }
  std::string hex() {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string out;
    for (BYTE b : digest()) {
      out += kDigits[b >> 4];
      out += kDigits[b & 15];
    }
    return out;
  }

private:
  BCRYPT_ALG_HANDLE algorithm_ = nullptr;
  BCRYPT_HASH_HANDLE hash_ = nullptr;
};

std::optional<std::string> fileSha256(const std::wstring& path) {
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
  if (file == INVALID_HANDLE_VALUE) return std::nullopt;
  Sha256 hash;
  std::vector<char> buffer(kHashChunk);
  DWORD read = 0;
  bool ok = true;
  while ((ok = ReadFile(file, buffer.data(), kHashChunk, &read, nullptr)) && read > 0) hash.add(buffer.data(), read);
  CloseHandle(file);
  if (!ok) return std::nullopt;
  return hash.hex();
}

std::string bytesSha256(const std::string& bytes) {
  Sha256 hash;
  hash.add(bytes.data(), bytes.size());
  return hash.hex();
}

bool signatureValid(const std::string& message, const std::string& signature) {
  DWORD size = 0;
  CryptStringToBinaryW(VOX_UPDATE_KEY, 0, CRYPT_STRING_BASE64, nullptr, &size, nullptr, nullptr);
  std::vector<BYTE> key(size);
  if (!CryptStringToBinaryW(VOX_UPDATE_KEY, 0, CRYPT_STRING_BASE64, key.data(), &size, nullptr, nullptr)) return false;
  Sha256 hash;
  hash.add(message.data(), message.size());
  std::vector<BYTE> digest = hash.digest();
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  BCRYPT_KEY_HANDLE handle = nullptr;
  bool valid = BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0)) &&
               BCRYPT_SUCCESS(BCryptImportKeyPair(algorithm, nullptr, BCRYPT_ECCPUBLIC_BLOB, &handle, key.data(), ULONG(key.size()), 0)) &&
               BCRYPT_SUCCESS(BCryptVerifySignature(handle, nullptr, digest.data(), ULONG(digest.size()), PUCHAR(signature.data()),
                                                    ULONG(signature.size()), 0));
  if (handle) BCryptDestroyKey(handle);
  if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
  return valid;
}

bool safeRelativePath(const std::wstring& path) {
  if (path.empty() || path.find_first_of(L"\\:") != std::wstring::npos) return false;
  size_t start = 0;
  while (start <= path.size()) {
    size_t slash = path.find(L'/', start);
    if (slash == std::wstring::npos) slash = path.size();
    const std::wstring part = path.substr(start, slash - start);
    if (part.empty() || part == L"." || part == L"..") return false;
    start = slash + 1;
  }
  return true;
}

std::wstring nativePath(std::wstring path) {
  for (wchar_t& c : path)
    if (c == L'/') c = L'\\';
  return path;
}

std::wstring writeFile(const std::wstring& path, const std::string& bytes) {
  HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
  if (file == INVALID_HANDLE_VALUE) return lastError(L"寫不了 " + path);
  DWORD written = 0;
  const bool ok = WriteFile(file, bytes.data(), DWORD(bytes.size()), &written, nullptr) && written == bytes.size();
  std::wstring error = ok ? std::wstring() : lastError(L"寫不了 " + path);
  CloseHandle(file);
  return error;
}

std::map<std::wstring, std::string> ownHashes() {
  std::map<std::wstring, std::string> hashes;
  auto text = readUtf8File(exeDir() + L"\\" + kManifestFile);
  if (!text) return hashes;
  for (const auto& value : JsonObject::Parse(*text).GetNamedArray(L"files")) {
    JsonObject file = value.GetObject();
    hashes[std::wstring(file.GetNamedString(L"path"))] = winrt::to_string(file.GetNamedString(L"sha256"));
  }
  return hashes;
}

std::optional<std::string> download(const std::wstring& url, std::wstring& error) {
  HttpResponse response = httpFetch({L"GET", url, {}, {}});
  if (!response.error.empty()) error = response.error;
  else if (response.status != 200) error = L"伺服器回 " + std::to_wstring(response.status);
  else return std::move(response.body);
  return std::nullopt;
}

UpdateStatus stage(const std::wstring& root, const std::wstring& version, const JsonObject& manifest, const std::string& bytes) {
  const std::wstring staging = versionDir(root, version) + L".partial";
  std::filesystem::create_directories(staging);
  const auto own = ownHashes();
  for (const auto& value : manifest.GetNamedArray(L"files")) {
    JsonObject file = value.GetObject();
    const std::wstring path(file.GetNamedString(L"path"));
    if (!safeRelativePath(path)) return failed(L"更新資訊裡有不合法的檔案路徑：" + path);
    const std::string sha = winrt::to_string(file.GetNamedString(L"sha256"));
    const std::wstring target = staging + L"\\" + nativePath(path);
    std::filesystem::create_directories(std::filesystem::path(target).parent_path());
    if (GetFileAttributesW(target.c_str()) != INVALID_FILE_ATTRIBUTES) {
      if (fileSha256(target) == sha) continue;
      DeleteFileW(target.c_str());
    }
    if (auto found = own.find(path); found != own.end() && found->second == sha) {
      if (!CreateHardLinkW(target.c_str(), (exeDir() + L"\\" + nativePath(path)).c_str(), nullptr))
        return failed(lastError(L"更新失敗：連結不了 " + path));
      continue;
    }
    std::wstring error;
    auto body = download(std::wstring(file.GetNamedString(L"url")), error);
    if (!body) return failed(L"更新失敗：下載 " + path + L" 失敗，" + error);
    if (double(body->size()) != file.GetNamedNumber(L"size") || bytesSha256(*body) != sha)
      return failed(L"更新失敗：" + path + L" 的內容跟更新資訊對不上");
    if (std::wstring failure = writeFile(target, *body); !failure.empty()) return failed(L"更新失敗：" + failure);
  }
  if (std::wstring failure = writeFile(staging + L"\\" + kManifestFile, bytes); !failure.empty())
    return failed(L"更新失敗：" + failure);

  if (auto previous = readMarker(root, kPendingMarker); previous && *previous != version) {
    removeMarker(root, kPendingMarker);
    removeTree(versionDir(root, *previous));
  }
  const std::wstring target = versionDir(root, version);
  if (std::wstring failure = removeTree(target); !failure.empty()) return failed(L"更新失敗：" + failure);
  if (!MoveFileExW(staging.c_str(), target.c_str(), 0)) return failed(lastError(L"更新失敗：搬不動 " + staging));
  writeMarker(root, kPendingMarker, version);
  return {L"ready", version, {}};
}

UpdateStatus check(const std::wstring& root) {
  std::wstring error;
  auto manifest = download(VOX_UPDATE_URL L"/manifest.json", error);
  if (!manifest) return failed(L"檢查更新失敗：" + error);
  auto signature = download(VOX_UPDATE_URL L"/manifest.sig", error);
  if (!signature) return failed(L"檢查更新失敗：" + error);
  if (!signatureValid(*manifest, *signature)) return failed(L"更新資訊的簽章不對，這次不更新");

  JsonObject parsed = JsonObject::Parse(winrt::to_hstring(*manifest));
  const std::wstring version(parsed.GetNamedString(L"version"));
  if (!isVersionText(version)) return failed(L"更新資訊的版本號不對：" + version);
  if (!newer(version, kVersion)) return {};
  if (readMarker(root, kRejectedMarker) == version) return {L"rejected", version, {}};
  if (readMarker(root, kPendingMarker) == version) return {L"ready", version, {}};
  return stage(root, version, parsed, *manifest);
}

}

struct Updater::Shared {
  std::mutex mutex;
  bool closed = false;
  Notify notify;
};

Updater::Updater(Notify notify) : shared_(std::make_shared<Shared>()), root_(installedRoot()) {
  shared_->notify = std::move(notify);
}

Updater::~Updater() {
  std::lock_guard lock(shared_->mutex);
  shared_->closed = true;
}

void Updater::started() {
  if (!root_) return;
  if (readMarker(*root_, kTryingMarker) == kVersion) {
    writeMarker(*root_, kCurrentMarker, kVersion);
    removeMarker(*root_, kTryingMarker);
    for (const std::wstring& error :
         {installLauncher(*root_, kVersion), recordInstalledVersion(kVersion), removeVersionsExcept(*root_, kVersion)})
      if (!error.empty()) OutputDebugStringW((error + L"\n").c_str());
  }
  if (checked_) return;
  checked_ = true;
  std::thread([shared = shared_, root = *root_] {
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    UpdateStatus status;
    try {
      status = check(root);
    } catch (const winrt::hresult_error& e) {
      status = failed(L"更新失敗：" + std::wstring(e.message()));
    } catch (const std::filesystem::filesystem_error& e) {
      status = failed(L"更新失敗：檔案操作錯誤，錯誤碼 " + std::to_wstring(e.code().value()));
    }
    std::lock_guard lock(shared->mutex);
    if (!shared->closed && !status.state.empty()) shared->notify(status);
  }).detach();
}
