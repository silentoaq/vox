#include "incoming.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <thread>

namespace {

constexpr DWORD kChunk = 64 * 1024;

uint32_t bigEndian32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }

uint64_t bigEndian64(const uint8_t* p) { return uint64_t(bigEndian32(p)) << 32 | bigEndian32(p + 4); }

uint64_t segmentBytes(const uint8_t* body) {
  const bool wide = body[0] != 0;
  const uint8_t* p = body + 12;
  p += wide ? 8 : 4;
  const uint64_t firstOffset = wide ? bigEndian64(p) : bigEndian32(p);
  p += wide ? 8 : 4;
  p += 2;
  const unsigned references = unsigned(p[0]) << 8 | p[1];
  p += 2;
  uint64_t total = firstOffset;
  for (unsigned i = 0; i < references; ++i, p += 12) total += bigEndian32(p) & 0x7FFFFFFF;
  return total;
}

}

Incoming::Incoming(HANDLE input, std::wstring savePath) {
  std::thread(receive, shared_, input, std::move(savePath)).detach();
}

void Incoming::receive(std::shared_ptr<Shared> shared, HANDLE input, std::wstring savePath) {
  std::ofstream save;
  const std::wstring partial = savePath + L".part";
  if (!savePath.empty()) save.open(partial, std::ios::binary | std::ios::trunc);
  std::vector<uint8_t> buffer(kChunk);
  DWORD read = 0;
  while (ReadFile(input, buffer.data(), kChunk, &read, nullptr) && read > 0) {
    if (save.is_open()) save.write(reinterpret_cast<const char*>(buffer.data()), read);
    std::lock_guard guard(shared->lock);
    shared->data.insert(shared->data.end(), buffer.begin(), buffer.begin() + read);
    shared->arrived.notify_all();
  }
  std::lock_guard guard(shared->lock);
  shared->finished = true;
  if (save.is_open()) {
    save.close();
    const auto length = shared->length();
    if (length && *length == shared->data.size()) MoveFileExW(partial.c_str(), savePath.c_str(), MOVEFILE_REPLACE_EXISTING);
    else DeleteFileW(partial.c_str());
  }
  shared->arrived.notify_all();
}

std::optional<uint64_t> Incoming::Shared::length() const {
  uint64_t offset = 0;
  while (offset + 8 <= data.size()) {
    uint64_t size = bigEndian32(&data[offset]);
    const uint8_t* type = &data[offset + 4];
    if (size == 1) {
      if (offset + 16 > data.size()) break;
      size = bigEndian64(&data[offset + 8]);
    }
    if (size == 0 || std::memcmp(type, "moof", 4) == 0) break;
    if (std::memcmp(type, "mdat", 4) == 0) return offset + size;
    if (std::memcmp(type, "sidx", 4) == 0) {
      if (offset + size > data.size()) break;
      return offset + size + segmentBytes(&data[offset + 8]);
    }
    offset += size;
  }
  if (finished) return data.size();
  return std::nullopt;
}

uint64_t Incoming::waitLength() {
  std::unique_lock guard(shared_->lock);
  std::optional<uint64_t> length;
  shared_->arrived.wait(guard, [&] { return (length = shared_->length()).has_value(); });
  return *length;
}

STDMETHODIMP Incoming::Read(void* buffer, ULONG bytes, ULONG* read) {
  std::unique_lock guard(shared_->lock);
  shared_->arrived.wait(guard, [&] { return shared_->finished || shared_->data.size() >= position_ + bytes; });
  const uint64_t available = shared_->data.size() > position_ ? shared_->data.size() - position_ : 0;
  const ULONG count = ULONG(std::min<uint64_t>(bytes, available));
  std::memcpy(buffer, shared_->data.data() + position_, count);
  position_ += count;
  if (read) *read = count;
  return count == bytes ? S_OK : S_FALSE;
}

STDMETHODIMP Incoming::Seek(LARGE_INTEGER move, DWORD origin, ULARGE_INTEGER* position) {
  const uint64_t end = origin == STREAM_SEEK_END ? waitLength() : 0;
  std::lock_guard guard(shared_->lock);
  const uint64_t base = origin == STREAM_SEEK_SET ? 0 : origin == STREAM_SEEK_CUR ? position_ : end;
  position_ = uint64_t(int64_t(base) + move.QuadPart);
  if (position) position->QuadPart = position_;
  return S_OK;
}

STDMETHODIMP Incoming::Stat(STATSTG* stat, DWORD) {
  *stat = {};
  stat->type = STGTY_STREAM;
  stat->cbSize.QuadPart = waitLength();
  stat->grfMode = STGM_READ;
  return S_OK;
}
