#pragma once
#include <windows.h>
#include <objidl.h>
#include <wrl.h>

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

class Incoming : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, IStream> {
public:
  Incoming(HANDLE input, std::wstring savePath);

  STDMETHODIMP Read(void* buffer, ULONG bytes, ULONG* read) override;
  STDMETHODIMP Write(const void*, ULONG, ULONG*) override { return STG_E_ACCESSDENIED; }
  STDMETHODIMP Seek(LARGE_INTEGER move, DWORD origin, ULARGE_INTEGER* position) override;
  STDMETHODIMP SetSize(ULARGE_INTEGER) override { return E_NOTIMPL; }
  STDMETHODIMP CopyTo(IStream*, ULARGE_INTEGER, ULARGE_INTEGER*, ULARGE_INTEGER*) override { return E_NOTIMPL; }
  STDMETHODIMP Commit(DWORD) override { return E_NOTIMPL; }
  STDMETHODIMP Revert() override { return E_NOTIMPL; }
  STDMETHODIMP LockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return E_NOTIMPL; }
  STDMETHODIMP UnlockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return E_NOTIMPL; }
  STDMETHODIMP Stat(STATSTG* stat, DWORD flags) override;
  STDMETHODIMP Clone(IStream**) override { return E_NOTIMPL; }

private:
  struct Shared {
    std::mutex lock;
    std::condition_variable arrived;
    std::vector<uint8_t> data;
    bool finished = false;
    std::optional<uint64_t> length() const;
  };

  static void receive(std::shared_ptr<Shared> shared, HANDLE input, std::wstring savePath);
  uint64_t waitLength();

  std::shared_ptr<Shared> shared_ = std::make_shared<Shared>();
  uint64_t position_ = 0;
};
