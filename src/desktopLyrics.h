#pragma once
#include <windows.h>
#include <d2d1.h>
#include <dwrite_3.h>
#include <wrl.h>

#include <functional>
#include <map>
#include <string>
#include <vector>

struct LyricChar {
  double t;
  double e;
  UINT32 pos;
  UINT32 len;
};

struct LyricLine {
  double start;
  std::wstring text;
  std::vector<LyricChar> chars;
};

struct LyricLook {
  UINT32 sung = 0x3AD38A;
  UINT32 unsung = 0xFFFFFF;
  UINT32 outline = 0x000000;
  float outlineWidth = 0.12f;
  int weight = 700;
  bool backgroundOn = false;
  UINT32 background = 0x101012;
};

class DesktopLyrics {
public:
  using Command = std::function<void(const std::wstring& action)>;

  DesktopLyrics(HINSTANCE instance, Command command);
  ~DesktopLyrics();

  bool visible() const { return settings_.visible; }
  bool locked() const { return settings_.locked; }
  const LyricLook& look() const { return settings_.look; }
  void setLook(const LyricLook& look, bool save);
  void setVisible(bool visible);
  void languageChanged();
  void setLocked(bool locked);
  void setSong(std::wstring title, std::wstring artist, std::vector<LyricLine> lines);
  void setFont(const std::wstring& path);
  void setClock(double ms, bool playing);

private:
  struct Settings {
    int x = INT_MIN;
    int y = INT_MIN;
    int width = 0;
    float fontSize = 40.0f;
    LyricLook look;
    int style = 0;
    bool visible = false;
    bool locked = false;
  };

  struct CharVisual {
    Microsoft::WRL::ComPtr<ID2D1Geometry> geometry;
    D2D1_RECT_F box;
  };

  struct Visual {
    float size = 0;
    float width = 0;
    float height = 0;
    Microsoft::WRL::ComPtr<ID2D1Geometry> outline;
    std::vector<CharVisual> chars;
  };

  struct Button {
    std::wstring action;
    std::wstring label;
    bool icon;
    D2D1_RECT_F rect;
  };

  static LRESULT CALLBACK proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
  LRESULT handle(UINT message, WPARAM wparam, LPARAM lparam);

  void loadSettings();
  void saveSettings();
  void applyLocked();
  void updateLockWatch();
  void pollLockHover();
  D2D1_RECT_F unlockRect() const;
  void placeWindow();
  float scale() const;
  float lineHeight() const;
  float toolbarHeight() const;
  int windowHeight() const;
  void layoutButtons();
  int buttonAt(POINT client) const;
  void press(const std::wstring& action);

  double nowMs() const;
  double wallMs() const;
  void tick();
  double nextChangeAt(double ms) const;
  int lineAt(double ms) const;
  void slotKeysAt(double ms, int keys[2]) const;
  bool render(double ms, bool force);
  const Visual& visualFor(int key, const std::wstring& text, const std::vector<LyricChar>& chars);
  void ensureSurface();

  HWND hwnd_ = nullptr;
  Command command_;
  Settings settings_;
  std::wstring title_;
  std::vector<LyricLine> lines_;
  double clockMs_ = 0;
  LARGE_INTEGER clockAt_{};
  LARGE_INTEGER frequency_{};
  bool playing_ = false;
  bool hasSong_ = false;
  bool hovered_ = false;
  int hoveredButton_ = -1;
  bool lockHover_ = false;
  bool overUnlock_ = false;
  int slotKeys_[2] = {INT_MIN, INT_MIN};
  double slotSince_[2] = {0, 0};
  std::vector<Button> buttons_;
  std::map<int, Visual> visuals_;
  std::wstring lastFrame_;
  std::wstring fontFamily_;
  Microsoft::WRL::ComPtr<IDWriteFontCollection> fontCollection_;

  Microsoft::WRL::ComPtr<ID2D1Factory> d2d_;
  Microsoft::WRL::ComPtr<IDWriteFactory> dwrite_;
  Microsoft::WRL::ComPtr<ID2D1DCRenderTarget> target_;
  Microsoft::WRL::ComPtr<ID2D1StrokeStyle> roundStroke_;
  Microsoft::WRL::ComPtr<IDWriteTextFormat> buttonFormat_;
  Microsoft::WRL::ComPtr<IDWriteTextFormat> iconFormat_;
  HDC memDc_ = nullptr;
  HBITMAP bitmap_ = nullptr;
  HGDIOBJ oldBitmap_ = nullptr;
  SIZE surface_{};
};
