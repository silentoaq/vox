#pragma once
#include <windows.h>

#include <cstdint>
#include <optional>
#include <string>

struct Payload {
  std::wstring self;
  uint64_t offset = 0;
  uint64_t size = 0;
};

std::optional<Payload> findPayload();
int runSetup(HINSTANCE instance, const Payload& payload);
int runUninstall(HINSTANCE instance);
inline constexpr wchar_t kRemoveEverything[] = L"everything";

int runRemove(HINSTANCE instance, DWORD parentPid, const std::wstring& root, bool everything);
void showError(HINSTANCE instance, const std::wstring& title, const std::wstring& message);
