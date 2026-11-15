#pragma once
#include <windows.h>
#include <wrl.h>

#include "WebView2.h"

void clearDebugArguments();
void lockDown(ICoreWebView2* webview);
