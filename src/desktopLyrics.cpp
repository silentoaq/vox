#include "desktopLyrics.h"

#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <limits>

#include <winrt/Windows.Data.Json.h>

#include "appIcon.h"
#include "files.h"
#include "text.h"

using Microsoft::WRL::ComPtr;
using namespace winrt::Windows::Data::Json;

namespace {

constexpr wchar_t kClass[] = APP_ID L".desktopLyrics";
constexpr UINT_PTR kFrameTimer = 1;
constexpr UINT_PTR kLockTimer = 2;
constexpr UINT kLockPollMs = 150;
constexpr double kFrameMs = 33;
constexpr double kFadeMs = 250;
constexpr double kHopMs = 420;
constexpr double kLongSyllableMs = 700;
constexpr int kIdle = -3;
constexpr int kTitle = -2;
constexpr int kEmpty = -1;
constexpr int kStyleDynamic = 0;
constexpr int kStyleWipe = 1;
constexpr float kPadding = 16;
constexpr float kToolbar = 40;
constexpr float kButtonHeight = 30;
constexpr float kButtonGap = 4;
constexpr float kGroupGap = 14;
constexpr float kEdge = 8;
constexpr float kFontStep = 4;
constexpr float kSmallestFont = 8;
constexpr float kDefaultFont = 40;
constexpr float kDefaultWidth = 1000;
constexpr float kBottomMargin = 48;
constexpr float kDim = 0.45f;
constexpr float kHop = 10;
constexpr float kHopScale = 1.12f;
constexpr float kLongScale = 1.15f;
constexpr float kOutlineAlpha = 0.7f;
constexpr float kGlowAlpha = 0.45f;
constexpr float kBackgroundAlpha = 0.8f;
constexpr float kBackgroundPadX = 0.4f;
constexpr float kBackgroundPadY = 0.06f;
constexpr float kGlowWidth = 0.3f;
constexpr float kFar = 100000;
constexpr const wchar_t* kIconFont = L"Segoe MDL2 Assets";
constexpr wchar_t kCloseGlyph[] = L"";
constexpr UINT32 kColors[] = {0x3AD38A, 0x3BA7FF, 0xFF6FA8, 0xFFD24A, 0xFFFFFF};

const wchar_t* systemLyricFont() { return languageId() == L"zh-CN" ? L"Microsoft YaHei" : L"Microsoft JhengHei"; }

class GlyphCollector : public IDWriteTextRenderer {
public:
  explicit GlyphCollector(ID2D1Factory* factory) : factory_(factory) {}

  ComPtr<ID2D1Geometry> all() { return group(0, std::numeric_limits<UINT32>::max()); }
  ComPtr<ID2D1Geometry> range(UINT32 from, UINT32 to) { return group(from, to); }

  HRESULT STDMETHODCALLTYPE DrawGlyphRun(void*, FLOAT x, FLOAT y, DWRITE_MEASURING_MODE, const DWRITE_GLYPH_RUN* run,
                                         const DWRITE_GLYPH_RUN_DESCRIPTION* description, IUnknown*) override {
    std::vector<UINT32> textOf(run->glyphCount, std::numeric_limits<UINT32>::max());
    for (UINT32 i = 0; i < description->stringLength; ++i) {
      UINT16 glyph = description->clusterMap[i];
      if (glyph < run->glyphCount && textOf[glyph] == std::numeric_limits<UINT32>::max())
        textOf[glyph] = description->textPosition + i;
    }
    UINT32 last = description->textPosition;
    float advance = 0;
    for (UINT32 g = 0; g < run->glyphCount; ++g) {
      if (textOf[g] == std::numeric_limits<UINT32>::max()) textOf[g] = last;
      last = textOf[g];
      ComPtr<ID2D1PathGeometry> path;
      factory_->CreatePathGeometry(&path);
      ComPtr<ID2D1GeometrySink> sink;
      path->Open(&sink);
      run->fontFace->GetGlyphRunOutline(run->fontEmSize, &run->glyphIndices[g], nullptr,
                                        run->glyphOffsets ? &run->glyphOffsets[g] : nullptr, 1, run->isSideways,
                                        run->bidiLevel % 2, sink.Get());
      sink->Close();
      ComPtr<ID2D1TransformedGeometry> moved;
      factory_->CreateTransformedGeometry(path.Get(), D2D1::Matrix3x2F::Translation(x + advance, y), &moved);
      parts_.push_back({textOf[g], moved});
      advance += run->glyphAdvances[g];
    }
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE DrawUnderline(void*, FLOAT, FLOAT, const DWRITE_UNDERLINE*, IUnknown*) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE DrawStrikethrough(void*, FLOAT, FLOAT, const DWRITE_STRIKETHROUGH*, IUnknown*) override {
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE DrawInlineObject(void*, FLOAT, FLOAT, IDWriteInlineObject*, BOOL, BOOL, IUnknown*) override {
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE IsPixelSnappingDisabled(void*, BOOL* disabled) override {
    *disabled = TRUE;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetCurrentTransform(void*, DWRITE_MATRIX* transform) override {
    *transform = {1, 0, 0, 1, 0, 0};
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetPixelsPerDip(void*, FLOAT* pixels) override {
    *pixels = 1;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
    if (id == __uuidof(IUnknown) || id == __uuidof(IDWritePixelSnapping) || id == __uuidof(IDWriteTextRenderer)) {
      *out = this;
      return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
  ULONG STDMETHODCALLTYPE Release() override { return 1; }

private:
  struct Part {
    UINT32 textPosition;
    ComPtr<ID2D1Geometry> geometry;
  };

  ComPtr<ID2D1Geometry> group(UINT32 from, UINT32 to) {
    std::vector<ID2D1Geometry*> raw;
    for (auto& part : parts_)
      if (part.textPosition >= from && part.textPosition < to) raw.push_back(part.geometry.Get());
    if (raw.empty()) return nullptr;
    ComPtr<ID2D1GeometryGroup> result;
    factory_->CreateGeometryGroup(D2D1_FILL_MODE_WINDING, raw.data(), UINT32(raw.size()), &result);
    return result;
  }

  ID2D1Factory* factory_;
  std::vector<Part> parts_;
};

}

DesktopLyrics::DesktopLyrics(HINSTANCE instance, Command command) : command_(std::move(command)), fontFamily_(systemLyricFont()) {
  QueryPerformanceFrequency(&frequency_);
  QueryPerformanceCounter(&clockAt_);
  D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d_.GetAddressOf());
  DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                      reinterpret_cast<IUnknown**>(dwrite_.GetAddressOf()));
  loadSettings();

  WNDCLASSEXW wc{};
  wc.cbSize = sizeof wc;
  wc.lpfnWndProc = proc;
  wc.hInstance = instance;
  wc.lpszClassName = kClass;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hIcon = appIcon(instance, SM_CXICON);
  wc.hIconSm = appIcon(instance, SM_CXSMICON);
  RegisterClassExW(&wc);
  CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kClass, APP_NAME,
                  WS_POPUP | WS_THICKFRAME, 0, 0, 1, 1, nullptr, nullptr, instance, this);
  placeWindow();
  applyLocked();
  updateLockWatch();
  if (settings_.visible) ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
  tick();
}

DesktopLyrics::~DesktopLyrics() {
  if (memDc_) {
    SelectObject(memDc_, oldBitmap_);
    DeleteDC(memDc_);
  }
  if (bitmap_) DeleteObject(bitmap_);
}

#pragma region settings

void DesktopLyrics::loadSettings() {
  auto text = readUtf8File(appDataDir() + L"\\desktopLyrics.json");
  if (!text) return;
  JsonObject json;
  if (!JsonObject::TryParse(*text, json)) return;
  settings_.x = int(json.GetNamedNumber(L"x", settings_.x));
  settings_.y = int(json.GetNamedNumber(L"y", settings_.y));
  settings_.width = int(json.GetNamedNumber(L"width", settings_.width));
  settings_.fontSize = float(json.GetNamedNumber(L"fontSize", settings_.fontSize));
  settings_.look.sung = UINT32(json.GetNamedNumber(L"sung", settings_.look.sung));
  settings_.look.unsung = UINT32(json.GetNamedNumber(L"unsung", settings_.look.unsung));
  settings_.look.outline = UINT32(json.GetNamedNumber(L"outline", settings_.look.outline));
  settings_.look.outlineWidth = float(json.GetNamedNumber(L"outlineWidth", settings_.look.outlineWidth));
  settings_.look.weight = int(json.GetNamedNumber(L"weight", settings_.look.weight));
  settings_.look.backgroundOn = json.GetNamedBoolean(L"backgroundOn", settings_.look.backgroundOn);
  settings_.look.background = UINT32(json.GetNamedNumber(L"background", settings_.look.background));
  settings_.style = int(json.GetNamedNumber(L"style", settings_.style)) == kStyleWipe ? kStyleWipe : kStyleDynamic;
  settings_.visible = json.GetNamedBoolean(L"visible", settings_.visible);
  settings_.locked = json.GetNamedBoolean(L"locked", settings_.locked);
}

void DesktopLyrics::saveSettings() {
  JsonObject json;
  json.SetNamedValue(L"x", JsonValue::CreateNumberValue(settings_.x));
  json.SetNamedValue(L"y", JsonValue::CreateNumberValue(settings_.y));
  json.SetNamedValue(L"width", JsonValue::CreateNumberValue(settings_.width));
  json.SetNamedValue(L"fontSize", JsonValue::CreateNumberValue(settings_.fontSize));
  json.SetNamedValue(L"sung", JsonValue::CreateNumberValue(settings_.look.sung));
  json.SetNamedValue(L"unsung", JsonValue::CreateNumberValue(settings_.look.unsung));
  json.SetNamedValue(L"outline", JsonValue::CreateNumberValue(settings_.look.outline));
  json.SetNamedValue(L"outlineWidth", JsonValue::CreateNumberValue(settings_.look.outlineWidth));
  json.SetNamedValue(L"weight", JsonValue::CreateNumberValue(settings_.look.weight));
  json.SetNamedValue(L"backgroundOn", JsonValue::CreateBooleanValue(settings_.look.backgroundOn));
  json.SetNamedValue(L"background", JsonValue::CreateNumberValue(settings_.look.background));
  json.SetNamedValue(L"style", JsonValue::CreateNumberValue(settings_.style));
  json.SetNamedValue(L"visible", JsonValue::CreateBooleanValue(settings_.visible));
  json.SetNamedValue(L"locked", JsonValue::CreateBooleanValue(settings_.locked));
  writeUtf8File(appDataDir() + L"\\desktopLyrics.json", std::wstring(json.Stringify()));
}

#pragma endregion

#pragma region window

float DesktopLyrics::scale() const { return GetDpiForWindow(hwnd_) / 96.0f; }
float DesktopLyrics::lineHeight() const { return settings_.fontSize * scale() * 1.45f; }
float DesktopLyrics::toolbarHeight() const { return kToolbar * scale(); }
int DesktopLyrics::windowHeight() const { return int(std::ceil(toolbarHeight() + 2 * lineHeight() + kPadding * scale() / 2)); }

void DesktopLyrics::placeWindow() {
  RECT work;
  SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
  float s = scale();
  if (settings_.width <= 0) settings_.width = int(kDefaultWidth * s);
  POINT saved{settings_.x + settings_.width / 2, settings_.y + windowHeight() / 2};
  if (settings_.x == INT_MIN || !MonitorFromPoint(saved, MONITOR_DEFAULTTONULL)) {
    settings_.x = work.left + (work.right - work.left - settings_.width) / 2;
    settings_.y = work.bottom - windowHeight() - int(kBottomMargin * s);
  }
  SetWindowPos(hwnd_, HWND_TOPMOST, settings_.x, settings_.y, settings_.width, windowHeight(), SWP_NOACTIVATE);
  layoutButtons();
}

void DesktopLyrics::layoutButtons() {
  float s = scale();
  buttonFormat_.Reset();
  iconFormat_.Reset();
  const std::wstring locale = languageId();
  const wchar_t* uiFont = locale == L"zh-CN" ? L"Microsoft YaHei UI" : L"Microsoft JhengHei UI";
  dwrite_->CreateTextFormat(uiFont, nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                            14 * s, locale.c_str(), &buttonFormat_);
  dwrite_->CreateTextFormat(kIconFont, nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                            DWRITE_FONT_STRETCH_NORMAL, 14 * s, locale.c_str(), &iconFormat_);
  for (auto* format : {buttonFormat_.Get(), iconFormat_.Get()}) {
    format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
  }

  struct Spec {
    const wchar_t* action;
    const wchar_t* label;
    bool icon;
    bool groupStart;
  };
  const Spec specs[] = {
    {L"restart", tr(Text::LyricsRestart), false, false},
    {L"toggle", tr(playing_ ? Text::LyricsPause : Text::LyricsPlay), false, false},
    {L"skip", tr(Text::LyricsSkip), false, false},
    {L"earlier", tr(Text::LyricsEarlier), false, true},
    {L"later", tr(Text::LyricsLater), false, false},
    {L"smaller", L"A-", false, true},
    {L"bigger", L"A+", false, false},
    {L"color", tr(Text::LyricsColor), false, false},
    {L"style", tr(settings_.style == kStyleDynamic ? Text::LyricsDynamic : Text::LyricsWipe), false, false},
    {L"lock", tr(Text::LyricsLock), false, true},
    {L"close", kCloseGlyph, true, false},
  };
  buttons_.clear();
  float x = 0;
  for (const auto& spec : specs) {
    if (spec.groupStart) x += (kGroupGap - kButtonGap) * s;
    ComPtr<IDWriteTextLayout> layout;
    dwrite_->CreateTextLayout(spec.label, UINT32(wcslen(spec.label)), spec.icon ? iconFormat_.Get() : buttonFormat_.Get(),
                              1000, 100, &layout);
    DWRITE_TEXT_METRICS metrics;
    layout->GetMetrics(&metrics);
    float width = std::max(kButtonHeight * s, metrics.widthIncludingTrailingWhitespace + 20 * s);
    float top = (toolbarHeight() - kButtonHeight * s) / 2;
    buttons_.push_back({spec.action, spec.label, spec.icon, D2D1::RectF(x, top, x + width, top + kButtonHeight * s)});
    x += width + kButtonGap * s;
  }
  RECT client;
  GetClientRect(hwnd_, &client);
  const float toolbar = x - kButtonGap * s;
  const int needed = int(toolbar + 2 * kPadding * s);
  if (client.right > 0 && client.right < needed) {
    RECT window;
    GetWindowRect(hwnd_, &window);
    settings_.x = window.left - (needed - client.right) / 2;
    settings_.y = window.top;
    settings_.width = needed;
    SetWindowPos(hwnd_, nullptr, settings_.x, settings_.y, needed, windowHeight(), SWP_NOZORDER | SWP_NOACTIVATE);
    saveSettings();
    return;
  }
  float shift = (float(client.right) - toolbar) / 2;
  for (auto& button : buttons_) {
    button.rect.left += shift;
    button.rect.right += shift;
  }
}

int DesktopLyrics::buttonAt(POINT client) const {
  for (size_t i = 0; i < buttons_.size(); ++i) {
    const auto& r = buttons_[i].rect;
    if (client.x >= r.left && client.x < r.right && client.y >= r.top && client.y < r.bottom) return int(i);
  }
  return -1;
}

void DesktopLyrics::applyLocked() {
  LONG_PTR style = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
  style = settings_.locked && !overUnlock_ ? style | WS_EX_TRANSPARENT : style & ~WS_EX_TRANSPARENT;
  SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, style);
}

D2D1_RECT_F DesktopLyrics::unlockRect() const {
  for (const auto& button : buttons_)
    if (button.action == L"lock") return button.rect;
  return D2D1::RectF();
}

void DesktopLyrics::updateLockWatch() {
  if (settings_.visible && settings_.locked) SetTimer(hwnd_, kLockTimer, kLockPollMs, nullptr);
  else KillTimer(hwnd_, kLockTimer);
}

void DesktopLyrics::pollLockHover() {
  POINT cursor;
  GetCursorPos(&cursor);
  RECT window;
  GetWindowRect(hwnd_, &window);
  const D2D1_RECT_F button = unlockRect();
  const bool inside = PtInRect(&window, cursor) != FALSE;
  const float x = float(cursor.x - window.left);
  const float y = float(cursor.y - window.top);
  const bool over = inside && x >= button.left && x < button.right && y >= button.top && y < button.bottom;
  if (inside == lockHover_ && over == overUnlock_) return;
  lockHover_ = inside;
  overUnlock_ = over;
  applyLocked();
  render(nowMs(), false);
}

void DesktopLyrics::languageChanged() {
  if (!fontCollection_) fontFamily_ = systemLyricFont();
  visuals_.clear();
  lastFrame_.clear();
  layoutButtons();
  render(nowMs(), true);
}

void DesktopLyrics::setVisible(bool visible) {
  settings_.visible = visible;
  ShowWindow(hwnd_, visible ? SW_SHOWNOACTIVATE : SW_HIDE);
  saveSettings();
  updateLockWatch();
  lastFrame_.clear();
  tick();
}

void DesktopLyrics::setLocked(bool locked) {
  settings_.locked = locked;
  hovered_ = false;
  hoveredButton_ = -1;
  lockHover_ = false;
  overUnlock_ = false;
  applyLocked();
  saveSettings();
  updateLockWatch();
  lastFrame_.clear();
  tick();
}

void DesktopLyrics::press(const std::wstring& action) {
  if (action == L"smaller" || action == L"bigger") {
    settings_.fontSize = std::max(kSmallestFont, settings_.fontSize + (action == L"bigger" ? kFontStep : -kFontStep));
    visuals_.clear();
    SetWindowPos(hwnd_, nullptr, 0, 0, settings_.width, windowHeight(), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    saveSettings();
    return;
  }
  if (action == L"color") {
    const auto* found = std::find(std::begin(kColors), std::end(kColors), settings_.look.sung);
    settings_.look.sung = found == std::end(kColors) || found + 1 == std::end(kColors) ? kColors[0] : *(found + 1);
    saveSettings();
    tick();
    command_(L"state");
    return;
  }
  if (action == L"style") {
    settings_.style = settings_.style == kStyleDynamic ? kStyleWipe : kStyleDynamic;
    saveSettings();
    layoutButtons();
    tick();
    return;
  }
  if (action == L"lock") {
    setLocked(true);
    command_(L"state");
    return;
  }
  if (action == L"close") {
    setVisible(false);
    command_(L"state");
    return;
  }
  command_(action);
}

#pragma endregion

#pragma region frames

void DesktopLyrics::setSong(std::wstring title, std::wstring artist, std::vector<LyricLine> lines) {
  hasSong_ = !title.empty();
  title_ = artist.empty() ? std::move(title) : title + L"　" + artist;
  lines_ = std::move(lines);
  visuals_.clear();
  slotKeys_[0] = slotKeys_[1] = INT_MIN;
  tick();
}

void DesktopLyrics::setLook(const LyricLook& look, bool save) {
  if (look.weight != settings_.look.weight) visuals_.clear();
  settings_.look = look;
  if (save) saveSettings();
  tick();
}

void DesktopLyrics::setFont(const std::wstring& path) {
  std::wstring family = systemLyricFont();
  ComPtr<IDWriteFontCollection1> collection;
  if (!path.empty()) {
    ComPtr<IDWriteFactory5> factory;
    winrt::check_hresult(dwrite_.As(&factory));
    ComPtr<IDWriteFontFile> file;
    winrt::check_hresult(factory->CreateFontFileReference(path.c_str(), nullptr, &file));
    ComPtr<IDWriteFontSetBuilder1> builder;
    winrt::check_hresult(factory->CreateFontSetBuilder(&builder));
    winrt::check_hresult(builder->AddFontFile(file.Get()));
    ComPtr<IDWriteFontSet> set;
    winrt::check_hresult(builder->CreateFontSet(&set));
    winrt::check_hresult(factory->CreateFontCollectionFromFontSet(set.Get(), &collection));
    ComPtr<IDWriteFontFamily> fontFamily;
    winrt::check_hresult(collection->GetFontFamily(0, &fontFamily));
    ComPtr<IDWriteLocalizedStrings> names;
    winrt::check_hresult(fontFamily->GetFamilyNames(&names));
    UINT32 length = 0;
    winrt::check_hresult(names->GetStringLength(0, &length));
    family.assign(length, L'\0');
    winrt::check_hresult(names->GetString(0, family.data(), length + 1));
  }
  fontFamily_ = std::move(family);
  fontCollection_ = collection;
  visuals_.clear();
  lastFrame_.clear();
  tick();
}

void DesktopLyrics::setClock(double ms, bool playing) {
  clockMs_ = ms;
  QueryPerformanceCounter(&clockAt_);
  if (playing != playing_) {
    playing_ = playing;
    layoutButtons();
  }
  tick();
}

double DesktopLyrics::wallMs() const {
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  return double(now.QuadPart) * 1000.0 / double(frequency_.QuadPart);
}

double DesktopLyrics::nowMs() const {
  if (!playing_) return clockMs_;
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  return clockMs_ + double(now.QuadPart - clockAt_.QuadPart) * 1000.0 / double(frequency_.QuadPart);
}

void DesktopLyrics::tick() {
  KillTimer(hwnd_, kFrameTimer);
  if (!settings_.visible) return;
  double ms = nowMs();
  bool moving = render(ms, false);
  if (!playing_ && !moving) return;
  double delay = moving ? kFrameMs : nextChangeAt(ms) - ms;
  if (!std::isfinite(delay)) return;
  SetTimer(hwnd_, kFrameTimer, UINT(std::ceil(std::max(1.0, delay))), nullptr);
}

int DesktopLyrics::lineAt(double ms) const {
  int index = -1;
  for (int i = 0; i < int(lines_.size()); ++i)
    if (lines_[i].start <= ms) index = i;
  return index;
}

double DesktopLyrics::nextChangeAt(double ms) const {
  double next = std::numeric_limits<double>::infinity();
  for (const auto& line : lines_) {
    if (line.start > ms) {
      next = std::min(next, line.start);
      break;
    }
  }
  int current = lineAt(ms);
  if (current >= 0) {
    for (const auto& c : lines_[current].chars) {
      if (c.t > ms) {
        next = std::min(next, c.t);
        break;
      }
    }
  }
  return next;
}

void DesktopLyrics::slotKeysAt(double ms, int keys[2]) const {
  keys[0] = keys[1] = kEmpty;
  if (!hasSong_) {
    keys[0] = kIdle;
    return;
  }
  if (lines_.empty()) {
    keys[0] = kTitle;
    return;
  }
  int current = lineAt(ms);
  if (current < 0) {
    keys[0] = kTitle;
    keys[1] = 0;
    return;
  }
  keys[current % 2] = current;
  keys[(current + 1) % 2] = current + 1 < int(lines_.size()) ? current + 1 : kEmpty;
}

const DesktopLyrics::Visual& DesktopLyrics::visualFor(int key, const std::wstring& text, const std::vector<LyricChar>& chars) {
  auto found = visuals_.find(key);
  if (found != visuals_.end()) return found->second;
  Visual& visual = visuals_[key];
  RECT client;
  GetClientRect(hwnd_, &client);
  float room = float(client.right) - 2 * kPadding * scale();
  if (text.empty() || room <= 0) return visual;

  ComPtr<IDWriteTextLayout> layout;
  auto build = [&](float fontSize) {
    ComPtr<IDWriteTextFormat> format;
    winrt::check_hresult(dwrite_->CreateTextFormat(fontFamily_.c_str(), fontCollection_.Get(), DWRITE_FONT_WEIGHT(settings_.look.weight), DWRITE_FONT_STYLE_NORMAL,
                                                   DWRITE_FONT_STRETCH_NORMAL, fontSize, languageId().c_str(), &format));
    format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    layout.Reset();
    winrt::check_hresult(dwrite_->CreateTextLayout(text.c_str(), UINT32(text.size()), format.Get(), 100000, 1000, &layout));
    DWRITE_TEXT_METRICS metrics;
    layout->GetMetrics(&metrics);
    return metrics;
  };
  float size = settings_.fontSize * scale();
  DWRITE_TEXT_METRICS metrics = build(size);
  if (metrics.widthIncludingTrailingWhitespace > room) {
    size = size * room / metrics.widthIncludingTrailingWhitespace;
    metrics = build(size);
  }
  GlyphCollector collector(d2d_.Get());
  layout->Draw(nullptr, &collector, 0, 0);
  visual.outline = collector.all();
  visual.size = size;
  visual.width = metrics.widthIncludingTrailingWhitespace;
  visual.height = metrics.height;
  for (const auto& c : chars) {
    FLOAT x = 0;
    FLOAT y = 0;
    DWRITE_HIT_TEST_METRICS hit;
    layout->HitTestTextPosition(c.pos, FALSE, &x, &y, &hit);
    visual.chars.push_back({collector.range(c.pos, c.pos + c.len), D2D1::RectF(hit.left, hit.top, hit.left + hit.width, hit.top + hit.height)});
  }
  return visual;
}

void DesktopLyrics::ensureSurface() {
  RECT client;
  GetClientRect(hwnd_, &client);
  SIZE size{client.right, client.bottom};
  if (!memDc_) memDc_ = CreateCompatibleDC(nullptr);
  if (size.cx != surface_.cx || size.cy != surface_.cy) {
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof info.bmiHeader;
    info.bmiHeader.biWidth = size.cx;
    info.bmiHeader.biHeight = -size.cy;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(memDc_, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ previous = SelectObject(memDc_, bitmap);
    if (!oldBitmap_) oldBitmap_ = previous;
    if (bitmap_) DeleteObject(bitmap_);
    bitmap_ = bitmap;
    surface_ = size;
  }
  if (!target_) {
    auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
                                              D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    winrt::check_hresult(d2d_->CreateDCRenderTarget(&props, &target_));
  }
  target_->BindDC(memDc_, &client);
}

bool DesktopLyrics::render(double ms, bool force) {
  if (!settings_.visible) return false;
  double wall = wallMs();
  int keys[2];
  slotKeysAt(ms, keys);
  for (int i = 0; i < 2; ++i) {
    if (keys[i] != slotKeys_[i]) {
      slotSince_[i] = slotKeys_[i] == INT_MIN ? wall - kFadeMs : wall;
      slotKeys_[i] = keys[i];
    }
  }
  int current = lines_.empty() ? -1 : lineAt(ms);
  bool dynamic = settings_.style == kStyleDynamic;

  bool moving = false;
  std::wstring frame;
  for (int i = 0; i < 2; ++i) {
    double fade = std::min(1.0, (wall - slotSince_[i]) / kFadeMs);
    if (fade < 1) moving = true;
    frame += std::to_wstring(keys[i]) + L":" + std::to_wstring(std::lround(fade * 20)) + L"|";
  }
  if (current >= 0) {
    for (const auto& c : lines_[current].chars) {
      double p = std::clamp((ms - c.t) / std::max(1.0, c.e - c.t), 0.0, 1.0);
      double hop = std::clamp((ms - c.t) / kHopMs, 0.0, 1.0);
      if (playing_ && ((p > 0 && p < 1) || (dynamic && hop > 0 && hop < 1))) moving = true;
      frame += std::to_wstring(std::lround(p * 40)) + L"." + std::to_wstring(std::lround(hop * 20)) + L",";
    }
  }
  RECT client;
  GetClientRect(hwnd_, &client);
  frame += L"|" + std::to_wstring(hovered_) + std::to_wstring(hoveredButton_) + std::to_wstring(playing_) +
           std::to_wstring(settings_.look.sung) + L"," + std::to_wstring(settings_.look.unsung) + L"," + std::to_wstring(settings_.look.outline) +
           L"," + std::to_wstring(settings_.look.outlineWidth) + L"," + std::to_wstring(settings_.look.weight) + L"," +
           std::to_wstring(settings_.look.backgroundOn) + L"," + std::to_wstring(settings_.look.background) + std::to_wstring(settings_.style) +
           std::to_wstring(settings_.locked) +
           std::to_wstring(lockHover_) + std::to_wstring(overUnlock_) + L"|" + std::to_wstring(client.right) + L"x" +
           std::to_wstring(client.bottom);
  if (!force && frame == lastFrame_) return moving;
  lastFrame_ = frame;

  ensureSurface();
  float s = scale();
  auto* rt = target_.Get();
  rt->BeginDraw();
  rt->SetTransform(D2D1::Matrix3x2F::Identity());
  rt->Clear(D2D1::ColorF(0, 0, 0, 0));
  ComPtr<ID2D1SolidColorBrush> brush;
  rt->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 0), &brush);
  if (!roundStroke_) {
    d2d_->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                                        D2D1_LINE_JOIN_ROUND),
                            nullptr, 0, &roundStroke_);
  }
  D2D1_RECT_F all = D2D1::RectF(0, 0, float(client.right), float(client.bottom));

  if (!settings_.locked) {
    brush->SetColor(D2D1::ColorF(0, 0, 0, hovered_ ? 0.42f : 1.0f / 255));
    rt->FillRoundedRectangle(D2D1::RoundedRect(all, 4 * s, 4 * s), brush.Get());
  }
  if (settings_.locked && lockHover_) {
    const D2D1_RECT_F button = unlockRect();
    brush->SetColor(D2D1::ColorF(0, 0, 0, overUnlock_ ? 0.7f : 0.5f));
    rt->FillRoundedRectangle(D2D1::RoundedRect(button, 4 * s, 4 * s), brush.Get());
    if (overUnlock_) {
      brush->SetColor(D2D1::ColorF(1, 1, 1, 0.18f));
      rt->FillRoundedRectangle(D2D1::RoundedRect(button, 4 * s, 4 * s), brush.Get());
    }
    brush->SetColor(D2D1::ColorF(1, 1, 1, 1));
    const wchar_t* unlock = tr(Text::LyricsUnlock);
    rt->DrawTextW(unlock, UINT32(wcslen(unlock)), buttonFormat_.Get(), button, brush.Get());
  }
  if (hovered_ && !settings_.locked) {
    for (size_t i = 0; i < buttons_.size(); ++i) {
      const auto& button = buttons_[i];
      if (int(i) == hoveredButton_) {
        brush->SetColor(D2D1::ColorF(1, 1, 1, 0.18f));
        rt->FillRoundedRectangle(D2D1::RoundedRect(button.rect, 4 * s, 4 * s), brush.Get());
      }
      brush->SetColor(D2D1::ColorF(1, 1, 1, 1));
      rt->DrawTextW(button.label.c_str(), UINT32(button.label.size()),
                    button.icon ? iconFormat_.Get() : buttonFormat_.Get(), button.rect, brush.Get());
    }
  }

  D2D1_COLOR_F sung = D2D1::ColorF(settings_.look.sung);
  D2D1_COLOR_F unsung = D2D1::ColorF(settings_.look.unsung);
  D2D1_COLOR_F edge = D2D1::ColorF(settings_.look.outline);
  auto mix = [](D2D1_COLOR_F a, D2D1_COLOR_F b, float t, float alpha) {
    return D2D1::ColorF(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, alpha);
  };
  auto paint = [&](ID2D1Geometry* geometry, float size, D2D1_COLOR_F fill, float glow) {
    if (glow > 0) {
      brush->SetColor(D2D1::ColorF(fill.r, fill.g, fill.b, kGlowAlpha * glow * fill.a));
      rt->DrawGeometry(geometry, brush.Get(), size * kGlowWidth, roundStroke_.Get());
    }
    if (settings_.look.outlineWidth > 0) {
      brush->SetColor(D2D1::ColorF(edge.r, edge.g, edge.b, kOutlineAlpha * std::min(1.0f, fill.a * 1.8f)));
      rt->DrawGeometry(geometry, brush.Get(), size * settings_.look.outlineWidth, roundStroke_.Get());
    }
    brush->SetColor(fill);
    rt->FillGeometry(geometry, brush.Get());
  };

  float top = toolbarHeight();
  float lineH = lineHeight();
  for (int i = 0; i < 2; ++i) {
    int key = keys[i];
    if (key == kEmpty) continue;
    bool isLine = key >= 0;
    const std::vector<LyricChar> none;
    const Visual& visual = isLine ? visualFor(key, lines_[key].text, lines_[key].chars)
                                  : visualFor(key, key == kTitle ? title_ : std::wstring(APP_NAME), none);
    if (!visual.outline) continue;
    float fade = float(std::min(1.0, (wall - slotSince_[i]) / kFadeMs));
    float x = i == 1 ? float(client.right) - kPadding * s - visual.width : kPadding * s;
    float y = top + lineH * i + (lineH - visual.height) / 2;
    D2D1_MATRIX_3X2_F place = D2D1::Matrix3x2F::Translation(x, y);

    if (settings_.look.backgroundOn) {
      const float padX = visual.size * kBackgroundPadX;
      const float padY = visual.size * kBackgroundPadY;
      rt->SetTransform(D2D1::Matrix3x2F::Identity());
      brush->SetColor(D2D1::ColorF(settings_.look.background, kBackgroundAlpha * fade));
      rt->FillRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(x - padX, y - padY, x + visual.width + padX, y + visual.height + padY), 4 * s, 4 * s),
        brush.Get());
    }

    if (!isLine || key != current) {
      rt->SetTransform(place);
      paint(visual.outline.Get(), visual.size, D2D1::ColorF(unsung.r, unsung.g, unsung.b, (dynamic && isLine ? kDim : 1.0f) * fade), 0);
      continue;
    }

    const LyricLine& line = lines_[key];
    if (!dynamic) {
      float fill = 0;
      for (size_t k = 0; k < line.chars.size(); ++k) {
        const LyricChar& c = line.chars[k];
        if (c.t > ms) break;
        float p = float(std::clamp((ms - c.t) / std::max(1.0, c.e - c.t), 0.0, 1.0));
        const D2D1_RECT_F& box = visual.chars[k].box;
        fill = box.left + (box.right - box.left) * p;
      }
      rt->SetTransform(place);
      if (settings_.look.outlineWidth > 0) {
        brush->SetColor(D2D1::ColorF(edge.r, edge.g, edge.b, kOutlineAlpha));
        rt->DrawGeometry(visual.outline.Get(), brush.Get(), visual.size * settings_.look.outlineWidth, roundStroke_.Get());
      }
      rt->PushAxisAlignedClip(D2D1::RectF(-kFar, -kFar, fill, kFar), D2D1_ANTIALIAS_MODE_ALIASED);
      brush->SetColor(sung);
      rt->FillGeometry(visual.outline.Get(), brush.Get());
      rt->PopAxisAlignedClip();
      rt->PushAxisAlignedClip(D2D1::RectF(fill, -kFar, kFar, kFar), D2D1_ANTIALIAS_MODE_ALIASED);
      brush->SetColor(unsung);
      rt->FillGeometry(visual.outline.Get(), brush.Get());
      rt->PopAxisAlignedClip();
      continue;
    }

    for (size_t k = 0; k < line.chars.size(); ++k) {
      const LyricChar& c = line.chars[k];
      const auto& part = visual.chars[k];
      if (!part.geometry) continue;
      double duration = std::max(1.0, c.e - c.t);
      float p = float(std::clamp((ms - c.t) / duration, 0.0, 1.0));
      float hop = float(std::clamp((ms - c.t) / kHopMs, 0.0, 1.0));
      float bounce = hop > 0 && hop < 1 ? std::sin(3.14159265f * hop) : 0;
      float emphasis = duration >= kLongSyllableMs && p > 0 && p < 1 ? std::sin(3.14159265f * p) : 0;
      float grow = 1 + (kHopScale - 1) * bounce + (kLongScale - 1) * emphasis;
      D2D1_POINT_2F center = D2D1::Point2F((part.box.left + part.box.right) / 2, part.box.bottom);
      rt->SetTransform(D2D1::Matrix3x2F::Scale(grow, grow, center) *
                       D2D1::Matrix3x2F::Translation(0, -kHop * visual.size / kDefaultFont * bounce) * place);
      paint(part.geometry.Get(), visual.size, mix(unsung, sung, p, kDim + (1 - kDim) * p), emphasis);
    }
  }
  rt->SetTransform(D2D1::Matrix3x2F::Identity());

  if (rt->EndDraw() == HRESULT(D2DERR_RECREATE_TARGET)) {
    target_.Reset();
    roundStroke_.Reset();
    lastFrame_.clear();
    return moving;
  }

  RECT window;
  GetWindowRect(hwnd_, &window);
  POINT position{window.left, window.top};
  POINT origin{0, 0};
  BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
  UpdateLayeredWindow(hwnd_, nullptr, &position, &surface_, memDc_, &origin, 0, &blend, ULW_ALPHA);
  return moving;
}

#pragma endregion

#pragma region messages

LRESULT CALLBACK DesktopLyrics::proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  if (message == WM_NCCREATE) {
    auto* self = static_cast<DesktopLyrics*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
    self->hwnd_ = hwnd;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  }
  auto* self = reinterpret_cast<DesktopLyrics*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  return self ? self->handle(message, wparam, lparam) : DefWindowProcW(hwnd, message, wparam, lparam);
}

LRESULT DesktopLyrics::handle(UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
    case WM_NCCALCSIZE:
      if (wparam) return 0;
      break;
    case WM_NCHITTEST: {
      if (settings_.locked) return overUnlock_ ? HTCLIENT : HTTRANSPARENT;
      POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
      ScreenToClient(hwnd_, &point);
      RECT client;
      GetClientRect(hwnd_, &client);
      float edge = kEdge * scale();
      if (point.x < edge) return HTLEFT;
      if (point.x >= client.right - edge) return HTRIGHT;
      if (hovered_ && buttonAt(point) >= 0) return HTCLIENT;
      return HTCAPTION;
    }
    case WM_MOUSEACTIVATE:
      return MA_NOACTIVATE;
    case WM_MOUSEMOVE:
    case WM_NCMOUSEMOVE: {
      if (settings_.locked) return 0;
      TRACKMOUSEEVENT track{sizeof track, DWORD(TME_LEAVE | (message == WM_NCMOUSEMOVE ? TME_NONCLIENT : 0)), hwnd_, 0};
      TrackMouseEvent(&track);
      POINT point;
      GetCursorPos(&point);
      ScreenToClient(hwnd_, &point);
      hovered_ = true;
      hoveredButton_ = message == WM_MOUSEMOVE ? buttonAt(point) : -1;
      render(nowMs(), false);
      return 0;
    }
    case WM_MOUSELEAVE:
    case WM_NCMOUSELEAVE: {
      POINT point;
      GetCursorPos(&point);
      RECT window;
      GetWindowRect(hwnd_, &window);
      if (PtInRect(&window, point)) return 0;
      hovered_ = false;
      hoveredButton_ = -1;
      render(nowMs(), false);
      return 0;
    }
    case WM_LBUTTONUP: {
      if (settings_.locked) {
        if (overUnlock_) {
          setLocked(false);
          command_(L"state");
        }
        return 0;
      }
      POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
      int index = buttonAt(point);
      if (index >= 0) press(buttons_[index].action);
      return 0;
    }
    case WM_NCLBUTTONDBLCLK:
    case WM_NCRBUTTONUP:
    case WM_CONTEXTMENU:
      return 0;
    case WM_GETMINMAXINFO: {
      auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
      if (!buttons_.empty())
        info->ptMinTrackSize.x = LONG(buttons_.back().rect.right - buttons_.front().rect.left + 2 * kPadding * scale());
      return 0;
    }
    case WM_WINDOWPOSCHANGING: {
      auto* pos = reinterpret_cast<WINDOWPOS*>(lparam);
      if (!(pos->flags & SWP_NOSIZE)) pos->cy = windowHeight();
      return 0;
    }
    case WM_SIZE:
      layoutButtons();
      visuals_.clear();
      render(nowMs(), true);
      return 0;
    case WM_MOVE:
      if (settings_.visible) render(nowMs(), true);
      return 0;
    case WM_EXITSIZEMOVE: {
      RECT window;
      GetWindowRect(hwnd_, &window);
      settings_.x = window.left;
      settings_.y = window.top;
      settings_.width = window.right - window.left;
      saveSettings();
      return 0;
    }
    case WM_DPICHANGED: {
      auto* rect = reinterpret_cast<RECT*>(lparam);
      settings_.width = rect->right - rect->left;
      SetWindowPos(hwnd_, nullptr, rect->left, rect->top, settings_.width, windowHeight(), SWP_NOZORDER | SWP_NOACTIVATE);
      return 0;
    }
    case WM_TIMER:
      if (wparam == kFrameTimer) tick();
      if (wparam == kLockTimer) pollLockHover();
      return 0;
  }
  return DefWindowProcW(hwnd_, message, wparam, lparam);
}

#pragma endregion
