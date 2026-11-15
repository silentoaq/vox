#include "setup.h"

#include <commctrl.h>
#include <shlobj.h>
#include <propkey.h>
#include <propvarutil.h>
#include <wrl/client.h>

#include <algorithm>
#include <filesystem>
#include <functional>
#include <thread>
#include <vector>

#include <winrt/Windows.Data.Json.h>
#include <winrt/base.h>

#include "../appIcon.h"
#include "../files.h"
#include "../install.h"
#include "../text.h"

using Microsoft::WRL::ComPtr;
using winrt::Windows::Data::Json::JsonObject;

namespace {

constexpr char kPayloadMagic[8] = {'V', 'O', 'X', 'S', 'E', 'T', 'U', 'P'};
constexpr uint64_t kFooterBytes = sizeof(uint64_t) + sizeof kPayloadMagic;
constexpr DWORD kCopyChunk = 1 << 20;
constexpr int kRemoveAttempts = 20;
constexpr DWORD kRemoveRetryMs = 500;
constexpr wchar_t kLinkKey[] = L"Software\\Classes\\" APP_ID;
constexpr wchar_t kShortcutName[] = L"\\" APP_NAME L".lnk";
constexpr wchar_t kExeName[] = L"\\" APP_NAME L".exe";
constexpr wchar_t kStagingName[] = L"\\app-setup.partial";
constexpr int kChangeButton = 100;

std::wstring lastError(Text id, const std::wstring& subject) {
  const DWORD code = GetLastError();
  return tr(id, {subject, std::to_wstring(code)});
}

std::wstring selfPath() {
  wchar_t path[MAX_PATH];
  GetModuleFileNameW(nullptr, path, MAX_PATH);
  return path;
}

std::wstring tempPath(const std::wstring& name) {
  wchar_t dir[MAX_PATH];
  GetTempPathW(MAX_PATH, dir);
  return std::wstring(dir) + APP_ID L"-" + name + L"-" + std::to_wstring(GetCurrentProcessId());
}

std::wstring knownFolder(REFKNOWNFOLDERID id) {
  PWSTR path = nullptr;
  SHGetKnownFolderPath(id, KF_FLAG_CREATE, nullptr, &path);
  std::wstring out(path);
  CoTaskMemFree(path);
  return out;
}

std::wstring copyRange(const std::wstring& from, uint64_t offset, uint64_t size, const std::wstring& to) {
  HANDLE in = CreateFileW(from.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
  if (in == INVALID_HANDLE_VALUE) return lastError(Text::CantRead, from);
  HANDLE out = CreateFileW(to.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
  if (out == INVALID_HANDLE_VALUE) {
    std::wstring error = lastError(Text::CantWrite, to);
    CloseHandle(in);
    return error;
  }
  std::wstring error;
  LARGE_INTEGER start;
  start.QuadPart = LONGLONG(offset);
  if (!SetFilePointerEx(in, start, nullptr, FILE_BEGIN)) error = lastError(Text::CantRead, from);
  std::vector<char> buffer(kCopyChunk);
  for (uint64_t left = size; error.empty() && left > 0;) {
    const DWORD want = DWORD(std::min<uint64_t>(left, kCopyChunk));
    DWORD read = 0;
    DWORD written = 0;
    if (!ReadFile(in, buffer.data(), want, &read, nullptr) || read != want) error = lastError(Text::CantRead, from);
    else if (!WriteFile(out, buffer.data(), read, &written, nullptr) || written != read) error = lastError(Text::CantWrite, to);
    left -= read;
  }
  CloseHandle(out);
  CloseHandle(in);
  if (!error.empty()) DeleteFileW(to.c_str());
  return error;
}

std::wstring setValue(const std::wstring& path, const wchar_t* name, DWORD type, const void* data, DWORD bytes) {
  HKEY key = nullptr;
  LSTATUS status = RegCreateKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
  if (status == ERROR_SUCCESS) {
    status = RegSetValueExW(key, name, 0, type, static_cast<const BYTE*>(data), bytes);
    RegCloseKey(key);
  }
  if (status == ERROR_SUCCESS) return {};
  return tr(Text::CantWriteRegistry, {path, std::to_wstring(status)});
}

std::wstring setString(const std::wstring& path, const wchar_t* name, const std::wstring& value) {
  return setValue(path, name, REG_SZ, value.c_str(), DWORD((value.size() + 1) * sizeof(wchar_t)));
}

std::wstring setNumber(const std::wstring& path, const wchar_t* name, DWORD value) {
  return setValue(path, name, REG_DWORD, &value, sizeof value);
}

std::wstring removeKey(const wchar_t* path) {
  LSTATUS status = RegDeleteTreeW(HKEY_CURRENT_USER, path);
  if (status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND) return {};
  return tr(Text::CantDeleteRegistry, {path, std::to_wstring(status)});
}

std::wstring createShortcut(const std::wstring& path, const std::wstring& target, const std::wstring& dir) {
  ComPtr<IShellLinkW> link;
  HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link));
  if (SUCCEEDED(hr)) hr = link->SetPath(target.c_str());
  if (SUCCEEDED(hr)) hr = link->SetWorkingDirectory(dir.c_str());
  ComPtr<IPropertyStore> store;
  if (SUCCEEDED(hr)) hr = link.As(&store);
  PROPVARIANT id;
  if (SUCCEEDED(hr)) hr = InitPropVariantFromString(kAppUserModelId, &id);
  if (SUCCEEDED(hr)) {
    hr = store->SetValue(PKEY_AppUserModel_ID, id);
    PropVariantClear(&id);
  }
  if (SUCCEEDED(hr)) hr = store->Commit();
  ComPtr<IPersistFile> file;
  if (SUCCEEDED(hr)) hr = link.As(&file);
  if (SUCCEEDED(hr)) hr = file->Save(path.c_str(), TRUE);
  if (SUCCEEDED(hr)) return {};
  return tr(Text::CantCreateShortcut, {path, hexText(hr)});
}

DWORD folderKilobytes(const std::wstring& dir) {
  uintmax_t bytes = 0;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(dir))
    if (entry.is_regular_file()) bytes += entry.file_size();
  return DWORD(bytes / 1024);
}

std::wstring registerApp(const std::wstring& root, const std::wstring& version) {
  const std::wstring launcher = root + kExeName;
  const std::wstring quoted = L"\"" + launcher + L"\"";
  const std::wstring linkKey = kLinkKey;
  const std::wstring uninstallKey = kUninstallKey;
  const std::vector<std::function<std::wstring()>> steps = {
    [&] { return setString(linkKey, nullptr, L"URL:" APP_NAME); },
    [&] { return setString(linkKey, L"URL Protocol", L""); },
    [&] { return setString(linkKey + L"\\DefaultIcon", nullptr, quoted + L",0"); },
    [&] { return setString(linkKey + L"\\shell\\open\\command", nullptr, quoted + L" \"%1\""); },
    [&] { return setString(uninstallKey, L"DisplayName", APP_NAME); },
    [&] { return recordInstalledVersion(version); },
    [&] { return setString(uninstallKey, L"DisplayIcon", quoted + L",0"); },
    [&] { return setString(uninstallKey, L"UninstallString", quoted + L" --uninstall"); },
    [&] { return setString(uninstallKey, L"InstallLocation", root); },
    [&] { return setNumber(uninstallKey, L"NoModify", 1); },
    [&] { return setNumber(uninstallKey, L"NoRepair", 1); },
    [&] { return setNumber(uninstallKey, L"EstimatedSize", folderKilobytes(versionDir(root, version))); },
    [&] { return createShortcut(knownFolder(FOLDERID_Programs) + kShortcutName, launcher, root); },
    [&] { return createShortcut(knownFolder(FOLDERID_Desktop) + kShortcutName, launcher, root); },
  };
  for (const auto& step : steps)
    if (std::wstring error = step(); !error.empty()) return error;
  SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
  return {};
}

std::wstring unregisterApp() {
  if (std::wstring error = removeKey(kLinkKey); !error.empty()) return error;
  if (std::wstring error = removeKey(kUninstallKey); !error.empty()) return error;
  DeleteFileW((knownFolder(FOLDERID_Programs) + kShortcutName).c_str());
  DeleteFileW((knownFolder(FOLDERID_Desktop) + kShortcutName).c_str());
  SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
  return {};
}

std::wstring unpack(const std::wstring& root, const std::wstring& archive) {
  const std::wstring staging = root + kStagingName;
  if (std::wstring error = removeTree(staging); !error.empty()) return error;
  if (!CreateDirectoryW(staging.c_str(), nullptr)) return lastError(Text::CantCreate, staging);
  if (std::wstring error = unzip(archive, staging); !error.empty()) return error;
  auto manifest = readUtf8File(staging + L"\\" + kManifestFile);
  if (!manifest) return tr(Text::SetupNoVersion);
  const std::wstring version(JsonObject::Parse(*manifest).GetNamedString(L"version"));
  if (!isVersionText(version)) return tr(Text::SetupBadVersion, {version});

  const std::wstring target = versionDir(root, version);
  if (std::wstring error = removeTree(target); !error.empty()) return error;
  if (!MoveFileExW(staging.c_str(), target.c_str(), 0)) return lastError(Text::CantMove, staging);

  if (std::wstring error = installLauncher(root, version); !error.empty()) return error;
  writeMarker(root, kCurrentMarker, version);
  removeMarker(root, kPendingMarker);
  removeMarker(root, kTryingMarker);
  removeMarker(root, kRejectedMarker);
  if (std::wstring error = removeVersionsExcept(root, version); !error.empty()) return error;
  return registerApp(root, version);
}

bool samePath(const std::wstring& a, const std::wstring& b) {
  return CompareStringOrdinal(a.c_str(), int(a.size()), b.c_str(), int(b.size()), TRUE) == CSTR_EQUAL;
}

std::wstring install(const Payload& payload, const std::wstring& root) {
  const std::wstring previous = installRoot();
  const int created = SHCreateDirectoryExW(nullptr, root.c_str(), nullptr);
  if (created == ERROR_ACCESS_DENIED) return tr(Text::NoPermission, {root});
  if (created != ERROR_SUCCESS && created != ERROR_ALREADY_EXISTS && created != ERROR_FILE_EXISTS)
    return tr(Text::CantCreate, {root, std::to_wstring(created)});
  if (std::wstring error = closeRunningApp(); !error.empty()) return error;
  const std::wstring archive = tempPath(L"setup") + L".zip";
  if (std::wstring error = copyRange(payload.self, payload.offset, payload.size, archive); !error.empty()) return error;
  std::wstring error = unpack(root, archive);
  DeleteFileW(archive.c_str());
  if (error.empty() && !samePath(previous, root) && readMarker(previous, kCurrentMarker)) error = removeInstall(previous);
  return error;
}

struct RootChoice {
  std::wstring root;
  std::wstring text;
};

std::wstring rootText(const std::wstring& root) { return tr(Text::InstallLocation, {root}); }

std::optional<std::wstring> pickFolder(HWND owner, const std::wstring& start) {
  ComPtr<IFileOpenDialog> dialog;
  if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) return std::nullopt;
  FILEOPENDIALOGOPTIONS options = 0;
  dialog->GetOptions(&options);
  dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
  dialog->SetTitle(tr(Text::PickFolderTitle, {APP_NAME}).c_str());
  ComPtr<IShellItem> folder;
  std::filesystem::path existing(start);
  while (!existing.empty() && !std::filesystem::exists(existing) && existing != existing.root_path()) existing = existing.parent_path();
  if (SUCCEEDED(SHCreateItemFromParsingName(existing.c_str(), nullptr, IID_PPV_ARGS(&folder)))) dialog->SetFolder(folder.Get());
  if (FAILED(dialog->Show(owner))) return std::nullopt;
  ComPtr<IShellItem> picked;
  PWSTR path = nullptr;
  if (FAILED(dialog->GetResult(&picked)) || FAILED(picked->GetDisplayName(SIGDN_FILESYSPATH, &path))) return std::nullopt;
  std::filesystem::path chosen(path);
  CoTaskMemFree(path);
  if (!samePath(chosen.filename().wstring(), APP_NAME)) chosen /= APP_NAME;
  return chosen.wstring();
}

HRESULT CALLBACK chooseCallback(HWND hwnd, UINT note, WPARAM wparam, LPARAM, LONG_PTR data) {
  auto* choice = reinterpret_cast<RootChoice*>(data);
  if (note != TDN_BUTTON_CLICKED || int(wparam) != kChangeButton) return S_OK;
  if (auto picked = pickFolder(hwnd, choice->root)) {
    choice->root = *picked;
    choice->text = rootText(choice->root);
    SendMessageW(hwnd, TDM_SET_ELEMENT_TEXT, TDE_CONTENT, reinterpret_cast<LPARAM>(choice->text.c_str()));
  }
  return S_FALSE;
}

std::wstring guarded(const std::function<std::wstring()>& work) {
  try {
    return work();
  } catch (const winrt::hresult_error& e) {
    return std::wstring(e.message());
  } catch (const std::filesystem::filesystem_error& e) {
    return tr(Text::FileOpFailed, {std::to_wstring(e.code().value())});
  }
}

struct Progress {
  std::function<void()> work;
  std::thread thread;
};

HRESULT CALLBACK progressCallback(HWND hwnd, UINT note, WPARAM, LPARAM, LONG_PTR data) {
  auto* progress = reinterpret_cast<Progress*>(data);
  if (note == TDN_CREATED) {
    SendMessageW(hwnd, TDM_SET_PROGRESS_BAR_MARQUEE, TRUE, 0);
    SendMessageW(hwnd, TDM_ENABLE_BUTTON, IDOK, FALSE);
    progress->thread = std::thread([progress, hwnd] {
      winrt::init_apartment(winrt::apartment_type::single_threaded);
      progress->work();
      winrt::uninit_apartment();
      PostMessageW(hwnd, TDM_ENABLE_BUTTON, IDOK, TRUE);
      PostMessageW(hwnd, TDM_CLICK_BUTTON, IDOK, 0);
    });
  }
  return S_OK;
}

TASKDIALOGCONFIG dialogConfig(HINSTANCE instance, const std::wstring& title, const std::wstring& message) {
  TASKDIALOGCONFIG config{sizeof config};
  config.hInstance = instance;
  config.dwFlags = TDF_USE_HICON_MAIN | TDF_POSITION_RELATIVE_TO_WINDOW;
  config.hMainIcon = appIcon(instance, SM_CXICON);
  config.pszWindowTitle = APP_NAME;
  config.pszMainInstruction = title.c_str();
  config.pszContent = message.c_str();
  return config;
}

void runWithProgress(HINSTANCE instance, const std::wstring& title, std::function<void()> work) {
  Progress progress{std::move(work), {}};
  const std::wstring message;
  TASKDIALOGCONFIG config = dialogConfig(instance, title, message);
  config.dwFlags |= TDF_SHOW_MARQUEE_PROGRESS_BAR;
  config.pfCallback = progressCallback;
  config.lpCallbackData = reinterpret_cast<LONG_PTR>(&progress);
  TaskDialogIndirect(&config, nullptr, nullptr, nullptr);
  if (progress.thread.joinable()) progress.thread.join();
}

void showInfo(HINSTANCE instance, const std::wstring& title, const std::wstring& message) {
  TASKDIALOGCONFIG config = dialogConfig(instance, title, message);
  TaskDialogIndirect(&config, nullptr, nullptr, nullptr);
}

std::optional<std::wstring> chooseRoot(HINSTANCE instance) {
  RootChoice choice{installRoot(), {}};
  choice.text = rootText(choice.root);
  const std::wstring title = tr(Text::SetupTitle, {APP_NAME});
  const TASKDIALOG_BUTTON buttons[] = {{kChangeButton, tr(Text::ChangeLocation)}, {IDOK, tr(Text::Install)}};
  TASKDIALOGCONFIG config = dialogConfig(instance, title, choice.text);
  config.pButtons = buttons;
  config.cButtons = ARRAYSIZE(buttons);
  config.dwCommonButtons = TDCBF_CANCEL_BUTTON;
  config.nDefaultButton = IDOK;
  config.pfCallback = chooseCallback;
  config.lpCallbackData = reinterpret_cast<LONG_PTR>(&choice);
  int pressed = 0;
  TaskDialogIndirect(&config, &pressed, nullptr, nullptr);
  if (pressed != IDOK) return std::nullopt;
  return choice.root;
}

enum class Removal { Cancel, AppOnly, Everything };

Removal confirmUninstall(HINSTANCE instance) {
  const std::wstring title = tr(Text::UninstallAsk, {APP_NAME});
  const std::wstring message = tr(Text::UninstallKeepNote);
  const TASKDIALOG_BUTTON buttons[] = {{IDOK, tr(Text::Uninstall)}};
  TASKDIALOGCONFIG config = dialogConfig(instance, title, message);
  config.pButtons = buttons;
  config.cButtons = ARRAYSIZE(buttons);
  config.dwCommonButtons = TDCBF_CANCEL_BUTTON;
  config.nDefaultButton = IDCANCEL;
  config.pszVerificationText = tr(Text::UninstallEverything);
  int pressed = 0;
  BOOL everything = FALSE;
  TaskDialogIndirect(&config, &pressed, nullptr, &everything);
  if (pressed != IDOK) return Removal::Cancel;
  return everything ? Removal::Everything : Removal::AppOnly;
}

std::wstring startProcess(const std::wstring& exe, const std::wstring& arguments) {
  std::wstring commandLine = L"\"" + exe + L"\" " + arguments;
  STARTUPINFOW startup{sizeof startup};
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(exe.c_str(), commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process))
    return lastError(Text::CantStart, exe);
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  return {};
}

void deleteSelfLater() {
  wchar_t system[MAX_PATH];
  GetSystemDirectoryW(system, MAX_PATH);
  std::wstring commandLine = L"\"" + std::wstring(system) + L"\\cmd.exe\" /c ping -n 3 127.0.0.1 >nul & del /f /q \"" +
                             selfPath() + L"\"";
  STARTUPINFOW startup{sizeof startup};
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup,
                      &process))
    return;
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
}

}

std::optional<Payload> findPayload() {
  const std::wstring self = selfPath();
  HANDLE file = CreateFileW(self.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
  if (file == INVALID_HANDLE_VALUE) return std::nullopt;
  LARGE_INTEGER size{};
  GetFileSizeEx(file, &size);
  uint64_t offset = 0;
  char magic[sizeof kPayloadMagic] = {};
  LARGE_INTEGER footer;
  footer.QuadPart = size.QuadPart - LONGLONG(kFooterBytes);
  DWORD read = 0;
  const bool ok = size.QuadPart > LONGLONG(kFooterBytes) && SetFilePointerEx(file, footer, nullptr, FILE_BEGIN) &&
                  ReadFile(file, &offset, sizeof offset, &read, nullptr) && read == sizeof offset &&
                  ReadFile(file, magic, sizeof magic, &read, nullptr) && read == sizeof magic;
  CloseHandle(file);
  if (!ok || memcmp(magic, kPayloadMagic, sizeof magic) != 0 || offset >= uint64_t(footer.QuadPart)) return std::nullopt;
  return Payload{self, offset, uint64_t(footer.QuadPart) - offset};
}

int runSetup(HINSTANCE instance, const Payload& payload) {
  winrt::init_apartment(winrt::apartment_type::single_threaded);
  auto root = chooseRoot(instance);
  if (!root) return 0;
  std::wstring error;
  runWithProgress(instance, tr(Text::Installing, {APP_NAME}), [&] { error = guarded([&] { return install(payload, *root); }); });
  if (error.empty()) error = startProcess(*root + kExeName, L"");
  if (error.empty()) return 0;
  showError(instance, tr(Text::InstallFailed, {APP_NAME}), error);
  return 1;
}

int runUninstall(HINSTANCE instance) {
  const Removal removal = confirmUninstall(instance);
  if (removal == Removal::Cancel) return 0;
  std::wstring error = closeRunningApp();
  if (error.empty()) error = unregisterApp();
  if (error.empty()) {
    const std::wstring remover = tempPath(L"uninstall") + L".exe";
    std::wstring arguments = L"--remove " + std::to_wstring(GetCurrentProcessId()) + L" \"" + exeDir() + L"\"";
    if (removal == Removal::Everything) arguments += L" " + std::wstring(kRemoveEverything);
    if (!CopyFileW(selfPath().c_str(), remover.c_str(), FALSE)) error = lastError(Text::CantCopy, remover);
    else error = startProcess(remover, arguments);
  }
  if (error.empty()) return 0;
  showError(instance, tr(Text::UninstallFailed, {APP_NAME}), error);
  return 1;
}

int runRemove(HINSTANCE instance, DWORD parentPid, const std::wstring& root, bool everything) {
  if (HANDLE parent = OpenProcess(SYNCHRONIZE, FALSE, parentPid)) {
    WaitForSingleObject(parent, INFINITE);
    CloseHandle(parent);
  }
  if (!readMarker(root, kCurrentMarker)) {
    deleteSelfLater();
    showError(instance, tr(Text::NotRemoved, {APP_NAME}), tr(Text::NotInstallFolder, {root, APP_NAME}));
    return 1;
  }
  std::wstring error;
  for (int attempt = 0; attempt < kRemoveAttempts; ++attempt) {
    error = removeInstall(root);
    if (error.empty() && everything) error = removeTree(appDataDir());
    if (error.empty()) break;
    Sleep(kRemoveRetryMs);
  }
  deleteSelfLater();
  if (!error.empty()) {
    showError(instance, tr(Text::PartlyRemoved, {APP_NAME}), error);
    return 1;
  }
  showInfo(instance, tr(Text::Uninstalled, {APP_NAME}), tr(everything ? Text::UninstalledEverything : Text::UninstalledKept));
  return 0;
}

void showError(HINSTANCE instance, const std::wstring& title, const std::wstring& message) {
  TASKDIALOGCONFIG config = dialogConfig(instance, title, message);
  config.dwFlags &= ~TDF_USE_HICON_MAIN;
  config.pszMainIcon = TD_ERROR_ICON;
  TaskDialogIndirect(&config, nullptr, nullptr, nullptr);
}
