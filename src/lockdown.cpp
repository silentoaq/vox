#include "lockdown.h"

#include <wrl.h>

#include <set>
#include <string>

#include <winrt/base.h>

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

namespace {

constexpr bool kDevTools = VOX_DEVTOOLS;
const std::set<std::wstring> kEditItems = {L"cut", L"copy", L"paste", L"selectAll"};

HRESULT keepEditItems(ICoreWebView2*, ICoreWebView2ContextMenuRequestedEventArgs* args) {
  ComPtr<ICoreWebView2ContextMenuItemCollection> items;
  args->get_MenuItems(&items);
  UINT32 count = 0;
  items->get_Count(&count);
  for (UINT32 i = count; i-- > 0;) {
    ComPtr<ICoreWebView2ContextMenuItem> item;
    items->GetValueAtIndex(i, &item);
    LPWSTR name = nullptr;
    item->get_Name(&name);
    const bool keep = kEditItems.contains(name);
    CoTaskMemFree(name);
    if (!keep) items->RemoveValueAtIndex(i);
  }
  items->get_Count(&count);
  if (count == 0) args->put_Handled(TRUE);
  return S_OK;
}

}

void clearDebugArguments() {
  if constexpr (!kDevTools) SetEnvironmentVariableW(L"WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS", nullptr);
}

void lockDown(ICoreWebView2* webview) {
  if constexpr (!kDevTools) {
    ComPtr<ICoreWebView2Settings> settings;
    winrt::check_hresult(webview->get_Settings(&settings));
    winrt::check_hresult(settings->put_AreDevToolsEnabled(FALSE));
    ComPtr<ICoreWebView2Settings3> settings3;
    winrt::check_hresult(settings.As(&settings3));
    winrt::check_hresult(settings3->put_AreBrowserAcceleratorKeysEnabled(FALSE));
    ComPtr<ICoreWebView2_11> webview11;
    winrt::check_hresult(webview->QueryInterface(IID_PPV_ARGS(&webview11)));
    winrt::check_hresult(
      webview11->add_ContextMenuRequested(Callback<ICoreWebView2ContextMenuRequestedEventHandler>(keepEditItems).Get(), nullptr));
  }
}
