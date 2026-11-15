#include "files.h"

#include "text.h"

#include <windows.h>
#include <shlobj.h>

#include <fstream>
#include <sstream>

std::wstring exeDir() {
  wchar_t path[MAX_PATH];
  GetModuleFileNameW(nullptr, path, MAX_PATH);
  std::wstring dir(path);
  return dir.substr(0, dir.find_last_of(L'\\'));
}

std::wstring appDataDir() {
  PWSTR base = nullptr;
  SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &base);
  std::wstring dir = std::wstring(base) + L"\\" APP_NAME;
  CoTaskMemFree(base);
  CreateDirectoryW(dir.c_str(), nullptr);
  return dir;
}

std::optional<std::wstring> readUtf8File(const std::wstring& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) return std::nullopt;
  std::stringstream buffer;
  buffer << file.rdbuf();
  std::string bytes = buffer.str();
  int n = MultiByteToWideChar(CP_UTF8, 0, bytes.data(), int(bytes.size()), nullptr, 0);
  std::wstring text(n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, bytes.data(), int(bytes.size()), text.data(), n);
  return text;
}

void writeBytesFile(const std::wstring& path, const std::string& bytes) {
  const std::wstring temp = path + L".tmp";
  {
    std::ofstream file(temp, std::ios::binary | std::ios::trunc);
    file << bytes;
  }
  MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}

void writeUtf8File(const std::wstring& path, const std::wstring& text) {
  int n = WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0, nullptr, nullptr);
  std::string bytes(n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), bytes.data(), n, nullptr, nullptr);
  writeBytesFile(path, bytes);
}

std::wstring unzip(const std::wstring& archive, const std::wstring& dir, const std::wstring& member) {
  wchar_t system[MAX_PATH];
  GetSystemDirectoryW(system, MAX_PATH);
  std::wstring commandLine = L"\"" + std::wstring(system) + L"\\tar.exe\" -xf \"" + archive + L"\" -C \"" + dir + L"\"";
  if (!member.empty()) commandLine += L" \"" + member + L"\"";
  STARTUPINFOW startup{sizeof startup};
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process))
    return tr(Text::UnzipStart, {std::to_wstring(GetLastError())});
  WaitForSingleObject(process.hProcess, INFINITE);
  DWORD code = 0;
  GetExitCodeProcess(process.hProcess, &code);
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  return code == 0 ? std::wstring() : tr(Text::UnzipFailed, {std::to_wstring(code)});
}
