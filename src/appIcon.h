#pragma once
#include <windows.h>

#include "resource.h"

inline HICON appIcon(HINSTANCE instance, int sizeMetric) {
  const int side = GetSystemMetrics(sizeMetric);
  return static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, side, side, LR_SHARED));
}
