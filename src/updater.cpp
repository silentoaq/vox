#include "updater.h"

#include <windows.h>
#include <bcrypt.h>
#include <wincrypt.h>

#include <filesystem>
#include <mutex>
#include <thread>
#include <vector>

#include <winrt/Windows.Data.Json.h>
#include <winrt/base.h>

#include "files.h"
#include "http.h"
#include "install.h"
#include "version.h"

using winrt::Windows::Data::Json::JsonObject;

namespace {

constexpr wchar_t kVersion[] = L"" VOX_VERSION_STRING;
constexpr wchar_t kInstallerFile[] = L"\\VoxSetup.exe";
constexpr wchar_t kManifestFile[] = L"\\update.json";
constexpr wchar_t kSignatureFile[] = L"\\update.sig";
constexpr wchar_t kAttemptedFile[] = L"\\attempted";
constexpr wchar_t kRejectedFile[] = L"\\updateRejected";
constexpr wchar_t kUpdatedFlag[] = L" --updated";
constexpr DWORD kHashChunk = 1 << 20;

struct Release {
  std::wstring version;
  std::wstring url;
  double size = 0;
  std::string sha256;
};

UpdateStatus failed(const std::wstring& message) { return {L"error", {}, message}; }

std::wstring stagingDir() { return appDataDir() + L"\\update"; }

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

std::optional<std::string> readBytes(const std::wstring& path) {
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
  if (file == INVALID_HANDLE_VALUE) return std::nullopt;
  LARGE_INTEGER size{};
  std::string bytes;
  DWORD read = 0;
  bool ok = GetFileSizeEx(file, &size);
  if (ok) {
    bytes.resize(size_t(size.QuadPart));
    ok = ReadFile(file, bytes.data(), DWORD(bytes.size()), &read, nullptr) && read == bytes.size();
  }
  CloseHandle(file);
  if (!ok) return std::nullopt;
  return bytes;
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

std::optional<Release> releaseOf(const std::string& manifest, const std::string& signature) {
  if (!signatureValid(manifest, signature)) return std::nullopt;
  JsonObject parsed = JsonObject::Parse(winrt::to_hstring(manifest));
  JsonObject installer = parsed.GetNamedObject(L"installer");
  Release release{std::wstring(parsed.GetNamedString(L"version")), std::wstring(installer.GetNamedString(L"url")),
                  installer.GetNamedNumber(L"size"), winrt::to_string(installer.GetNamedString(L"sha256"))};
  if (!isVersionText(release.version)) return std::nullopt;
  return release;
}

std::optional<Release> stagedRelease() {
  auto manifest = readBytes(stagingDir() + kManifestFile);
  auto signature = readBytes(stagingDir() + kSignatureFile);
  if (!manifest || !signature) return std::nullopt;
  return releaseOf(*manifest, *signature);
}

bool stagedInstallerValid(const Release& release) { return fileSha256(stagingDir() + kInstallerFile) == release.sha256; }

void removeStaging() {
  std::error_code error;
  std::filesystem::remove_all(stagingDir(), error);
}

std::optional<std::string> download(const std::wstring& url, std::wstring& error) {
  HttpResponse response = httpFetch({L"GET", url, {}, {}});
  if (!response.error.empty()) error = response.error;
  else if (response.status != 200) error = L"伺服器回 " + std::to_wstring(response.status);
  else return std::move(response.body);
  return std::nullopt;
}

UpdateStatus check() {
  std::wstring error;
  auto manifest = download(VOX_UPDATE_URL L"/update.json", error);
  if (!manifest) return failed(L"檢查更新失敗：" + error);
  auto signature = download(VOX_UPDATE_URL L"/update.sig", error);
  if (!signature) return failed(L"檢查更新失敗：" + error);
  auto release = releaseOf(*manifest, *signature);
  if (!release) return failed(L"更新資訊的簽章或版本號不對，這次不更新");
  if (!newer(release->version, kVersion)) {
    removeStaging();
    return {};
  }
  if (readUtf8File(appDataDir() + kRejectedFile) == release->version) return {};
  if (auto staged = stagedRelease(); staged && staged->version == release->version && stagedInstallerValid(*staged))
    return {L"ready", release->version, {}};

  removeStaging();
  std::filesystem::create_directories(stagingDir());
  const std::wstring installer = stagingDir() + kInstallerFile;
  const std::wstring partial = installer + L".partial";
  if (std::wstring failure = httpDownload(release->url, partial); !failure.empty()) return failed(L"下載新版失敗：" + failure);
  if (double(std::filesystem::file_size(partial)) != release->size || fileSha256(partial) != release->sha256)
    return failed(L"下載的新版跟更新資訊對不上");
  if (!MoveFileExW(partial.c_str(), installer.c_str(), MOVEFILE_REPLACE_EXISTING))
    return failed(L"新版存不起來，錯誤碼 " + std::to_wstring(GetLastError()));
  writeBytesFile(stagingDir() + kSignatureFile, *signature);
  writeBytesFile(stagingDir() + kManifestFile, *manifest);
  return {L"ready", release->version, {}};
}

}

bool launchStagedUpdate() {
  auto release = stagedRelease();
  if (!release) return false;
  if (!newer(release->version, kVersion)) {
    removeStaging();
    return false;
  }
  if (readUtf8File(stagingDir() + kAttemptedFile) == release->version || !stagedInstallerValid(*release)) return false;
  writeUtf8File(stagingDir() + kAttemptedFile, release->version);
  const std::wstring installer = stagingDir() + kInstallerFile;
  std::wstring commandLine = L"\"" + installer + L"\"" + kUpdatedFlag;
  STARTUPINFOW startup{sizeof startup};
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(installer.c_str(), commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process))
    return false;
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  return true;
}

struct Updater::Shared {
  std::mutex mutex;
  bool closed = false;
  Notify notify;
};

Updater::Updater(Notify notify) : shared_(std::make_shared<Shared>()), dir_(installedDir()) {
  shared_->notify = std::move(notify);
}

Updater::~Updater() {
  std::lock_guard lock(shared_->mutex);
  shared_->closed = true;
}

void Updater::started() {
  if (!dir_ || checked_) return;
  checked_ = true;
  if (auto staged = stagedRelease(); staged && readUtf8File(stagingDir() + kAttemptedFile) == staged->version) {
    writeUtf8File(appDataDir() + kRejectedFile, staged->version);
    removeStaging();
    shared_->notify({L"rejected", staged->version, {}});
  }
  std::thread([shared = shared_] {
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    UpdateStatus status;
    try {
      status = check();
    } catch (const winrt::hresult_error& e) {
      status = failed(L"更新失敗：" + std::wstring(e.message()));
    } catch (const std::filesystem::filesystem_error& e) {
      status = failed(L"更新失敗：檔案操作錯誤，錯誤碼 " + std::to_wstring(e.code().value()));
    }
    std::lock_guard lock(shared->mutex);
    if (!shared->closed && !status.state.empty()) shared->notify(status);
  }).detach();
}
