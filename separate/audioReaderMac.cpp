#include "audioReader.h"

#include <AudioToolbox/AudioToolbox.h>
#include <unistd.h>

#include <condition_variable>
#include <cstring>
#include <deque>
#include <functional>
#include <fstream>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

constexpr size_t kChunk = 64 * 1024;
constexpr UInt32 kOutputFrames = 4096;

uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
uint64_t be64(const uint8_t* p) { return uint64_t(be32(p)) << 32 | be32(p + 4); }

void check(OSStatus status, const char* step) {
  if (status != noErr) throw std::runtime_error(std::string(step) + " failed: " + std::to_string(int(status)));
}

uint64_t segmentBytes(const uint8_t* body) {
  const bool wide = body[0] != 0;
  const uint8_t* p = body + 12 + (wide ? 8 : 4);
  const uint64_t firstOffset = wide ? be64(p) : be32(p);
  p += (wide ? 8 : 4) + 2;
  const unsigned references = unsigned(p[0]) << 8 | p[1];
  p += 2;
  uint64_t total = firstOffset;
  for (unsigned i = 0; i < references; ++i, p += 12) total += be32(p) & 0x7FFFFFFF;
  return total;
}

class Incoming {
public:
  explicit Incoming(std::filesystem::path savePath) { std::thread(receive, shared_, std::move(savePath)).detach(); }

  bool waitFor(uint64_t end) {
    std::unique_lock guard(shared_->lock);
    shared_->arrived.wait(guard, [&] { return shared_->finished || shared_->data.size() >= end; });
    return shared_->data.size() >= end;
  }

  std::vector<uint8_t> bytes(uint64_t offset, uint64_t size) {
    std::lock_guard guard(shared_->lock);
    const auto from = shared_->data.begin() + std::ptrdiff_t(offset);
    return {from, from + std::ptrdiff_t(size)};
  }

private:
  struct Shared {
    std::mutex lock;
    std::condition_variable arrived;
    std::vector<uint8_t> data;
    bool finished = false;

    std::optional<uint64_t> length() const {
      uint64_t offset = 0;
      while (offset + 8 <= data.size()) {
        uint64_t size = be32(&data[offset]);
        const uint8_t* type = &data[offset + 4];
        if (size == 1) {
          if (offset + 16 > data.size()) break;
          size = be64(&data[offset + 8]);
        }
        if (size == 0 || std::memcmp(type, "moof", 4) == 0) break;
        if (std::memcmp(type, "mdat", 4) == 0) return offset + size;
        if (std::memcmp(type, "sidx", 4) == 0) {
          if (offset + size > data.size()) break;
          return offset + size + segmentBytes(&data[offset + 8]);
        }
        offset += size;
      }
      return std::nullopt;
    }
  };

  static void receive(std::shared_ptr<Shared> shared, std::filesystem::path savePath) {
    std::ofstream save;
    std::filesystem::path partial = savePath;
    partial += ".part";
    if (!savePath.empty()) save.open(partial, std::ios::binary | std::ios::trunc);
    std::vector<uint8_t> buffer(kChunk);
    for (;;) {
      const ssize_t read = ::read(STDIN_FILENO, buffer.data(), buffer.size());
      if (read <= 0) break;
      if (save.is_open()) save.write(reinterpret_cast<const char*>(buffer.data()), read);
      std::lock_guard guard(shared->lock);
      shared->data.insert(shared->data.end(), buffer.begin(), buffer.begin() + read);
      shared->arrived.notify_all();
    }
    std::lock_guard guard(shared->lock);
    shared->finished = true;
    if (save.is_open()) {
      save.close();
      std::error_code error;
      if (shared->length() == shared->data.size()) std::filesystem::rename(partial, savePath, error);
      else std::filesystem::remove(partial, error);
    }
    shared->arrived.notify_all();
  }

  std::shared_ptr<Shared> shared_ = std::make_shared<Shared>();
};

struct Box {
  uint64_t start;
  uint64_t body;
  uint64_t end;
  char type[5];
};

struct Packet {
  uint64_t offset;
  uint32_t size;
};

class Mp4AacReader final : public AudioReader {
public:
  Mp4AacReader(const std::filesystem::path& savePath, int sampleRate) : incoming_(savePath) {
    readHeader();
    AudioStreamBasicDescription input{};
    input.mFormatID = kAudioFormatMPEG4AAC;
    UInt32 size = sizeof input;
    check(AudioFormatGetProperty(kAudioFormatProperty_FormatInfo, UInt32(cookie_.size()), cookie_.data(), &size, &input), "read AAC format");
    AudioStreamBasicDescription output{};
    output.mSampleRate = sampleRate;
    output.mFormatID = kAudioFormatLinearPCM;
    output.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    output.mChannelsPerFrame = 2;
    output.mFramesPerPacket = 1;
    output.mBitsPerChannel = 32;
    output.mBytesPerFrame = 8;
    output.mBytesPerPacket = 8;
    check(AudioConverterNew(&input, &output, &converter_), "create AAC decoder");
    check(AudioConverterSetProperty(converter_, kAudioConverterDecompressionMagicCookie, UInt32(cookie_.size()), cookie_.data()),
          "set AAC cookie");
    if (timescale_ && duration_) estimated_ = int64_t(double(duration_) * sampleRate / timescale_ + 0.5);
  }

  ~Mp4AacReader() override {
    if (converter_) AudioConverterDispose(converter_);
  }

  int64_t estimatedSamples() const override { return estimated_; }

  bool readMore(Audio& audio) override {
    if (drained_) return false;
    std::vector<float> samples(size_t(kOutputFrames) * 2);
    AudioBufferList list{};
    list.mNumberBuffers = 1;
    list.mBuffers[0].mNumberChannels = 2;
    list.mBuffers[0].mDataByteSize = UInt32(samples.size() * sizeof(float));
    list.mBuffers[0].mData = samples.data();
    UInt32 frames = kOutputFrames;
    const OSStatus status = AudioConverterFillComplexBuffer(converter_, supply, this, &frames, &list, nullptr);
    if (status != noErr && status != kEndOfInput) check(status, "decode AAC");
    for (UInt32 i = 0; i < frames; ++i) {
      audio.left.push_back(samples[2 * i]);
      audio.right.push_back(samples[2 * i + 1]);
    }
    if (frames == 0) drained_ = true;
    return !drained_;
  }

private:
  static constexpr OSStatus kEndOfInput = 'vEnd';

  std::optional<Box> boxAt(uint64_t offset) {
    if (!incoming_.waitFor(offset + 8)) return std::nullopt;
    const auto head = incoming_.bytes(offset, 8);
    uint64_t size = be32(head.data());
    Box box{offset, offset + 8, 0, {}};
    std::memcpy(box.type, head.data() + 4, 4);
    if (size == 1) {
      if (!incoming_.waitFor(offset + 16)) return std::nullopt;
      size = be64(incoming_.bytes(offset + 8, 8).data());
      box.body = offset + 16;
    }
    box.end = size == 0 ? UINT64_MAX : offset + size;
    return box;
  }

  std::vector<uint8_t> body(const Box& box) {
    if (!incoming_.waitFor(box.end)) throw std::runtime_error(std::string("truncated ") + box.type);
    return incoming_.bytes(box.body, box.end - box.body);
  }

  void walk(uint64_t from, uint64_t to, const std::function<void(const Box&)>& visit) {
    for (uint64_t offset = from; offset < to;) {
      auto box = boxAt(offset);
      if (!box) return;
      visit(*box);
      offset = box->end;
    }
  }

  void readEsds(const Box& box) {
    const auto content = body(box);
    cookie_.assign(content.begin() + 4, content.end());
  }

  void readSampleEntry(const Box& stsd) {
    const auto content = body(stsd);
    const uint8_t* p = content.data();
    const uint64_t entry = stsd.body + 8;
    if (std::memcmp(p + 12, "mp4a", 4) != 0) throw std::runtime_error("audio is not AAC");
    const uint32_t entrySize = be32(p + 8);
    walk(entry + 36, entry + entrySize, [&](const Box& child) {
      if (!std::memcmp(child.type, "esds", 4)) readEsds(child);
    });
  }

  void readMoov(const Box& moov) {
    walk(moov.body, moov.end, [&](const Box& box) {
      if (!std::memcmp(box.type, "trak", 4)) {
        walk(box.body, box.end, [&](const Box& mdia) {
          if (std::memcmp(mdia.type, "mdia", 4)) return;
          walk(mdia.body, mdia.end, [&](const Box& part) {
            if (!std::memcmp(part.type, "mdhd", 4)) {
              const auto content = body(part);
              const uint8_t* p = content.data();
              if (p[0] == 1) {
                timescale_ = be32(p + 20);
                if (!duration_) duration_ = be64(p + 24);
              } else {
                timescale_ = be32(p + 12);
                if (!duration_) duration_ = be32(p + 16);
              }
            } else if (!std::memcmp(part.type, "minf", 4)) {
              walk(part.body, part.end, [&](const Box& stbl) {
                if (std::memcmp(stbl.type, "stbl", 4)) return;
                walk(stbl.body, stbl.end, [&](const Box& stsd) {
                  if (!std::memcmp(stsd.type, "stsd", 4)) readSampleEntry(stsd);
                });
              });
            }
          });
        });
      } else if (!std::memcmp(box.type, "mvex", 4)) {
        walk(box.body, box.end, [&](const Box& mehd) {
          if (std::memcmp(mehd.type, "mehd", 4)) return;
          const auto content = body(mehd);
          const uint8_t* p = content.data();
          fragmentDuration_ = p[0] == 1 ? be64(p + 4) : be32(p + 4);
        });
      }
    });
  }

  void readSidx(const Box& box) {
    const auto content = body(box);
    const uint8_t* p = content.data();
    const bool wide = p[0] != 0;
    const uint32_t scale = be32(p + 8);
    const uint8_t* q = p + 12 + (wide ? 16 : 8) + 2;
    const unsigned references = unsigned(q[0]) << 8 | q[1];
    q += 2;
    uint64_t total = 0;
    for (unsigned i = 0; i < references; ++i, q += 12) total += be32(q + 4);
    sidxDuration_ = scale ? total * timescale_ / scale : 0;
  }

  void readHeader() {
    uint64_t offset = 0;
    for (;;) {
      auto box = boxAt(offset);
      if (!box) throw std::runtime_error("audio ended before the first fragment");
      if (!std::memcmp(box->type, "moov", 4)) readMoov(*box);
      else if (!std::memcmp(box->type, "sidx", 4)) readSidx(*box);
      else if (!std::memcmp(box->type, "moof", 4)) break;
      else if (!std::memcmp(box->type, "mdat", 4)) throw std::runtime_error("audio is not fragmented");
      offset = box->end;
    }
    if (cookie_.empty()) throw std::runtime_error("audio has no AAC configuration");
    if (sidxDuration_) duration_ = sidxDuration_;
    else if (fragmentDuration_) duration_ = fragmentDuration_;
    next_ = offset;
  }

  void readTraf(const Box& traf, uint64_t moofStart) {
    uint64_t base = moofStart;
    uint32_t defaultSize = 0;
    walk(traf.body, traf.end, [&](const Box& box) {
      const auto content = body(box);
      const uint8_t* p = content.data();
      const uint32_t flags = be32(p) & 0xFFFFFF;
      if (!std::memcmp(box.type, "tfhd", 4)) {
        const uint8_t* q = p + 8;
        if (flags & 0x1) {
          base = be64(q);
          q += 8;
        }
        if (flags & 0x2) q += 4;
        if (flags & 0x8) q += 4;
        if (flags & 0x10) defaultSize = be32(q);
      } else if (!std::memcmp(box.type, "trun", 4)) {
        const uint32_t count = be32(p + 4);
        const uint8_t* q = p + 8;
        uint64_t offset = base;
        if (flags & 0x1) {
          offset = base + int32_t(be32(q));
          q += 4;
        }
        if (flags & 0x4) q += 4;
        for (uint32_t i = 0; i < count; ++i) {
          if (flags & 0x100) q += 4;
          uint32_t size = defaultSize;
          if (flags & 0x200) {
            size = be32(q);
            q += 4;
          }
          if (flags & 0x400) q += 4;
          if (flags & 0x800) q += 4;
          packets_.push_back({offset, size});
          offset += size;
        }
      }
    });
  }

  bool nextFragment() {
    for (;;) {
      auto box = boxAt(next_);
      if (!box) return false;
      next_ = box->end;
      if (!std::memcmp(box->type, "moof", 4)) {
        walk(box->body, box->end, [&](const Box& traf) {
          if (!std::memcmp(traf.type, "traf", 4)) readTraf(traf, box->start);
        });
        if (!packets_.empty()) return true;
      }
      if (box->end == UINT64_MAX) return false;
    }
  }

  static OSStatus supply(AudioConverterRef, UInt32* count, AudioBufferList* data, AudioStreamPacketDescription** descriptions,
                         void* user) {
    auto* self = static_cast<Mp4AacReader*>(user);
    if (self->packets_.empty() && !self->nextFragment()) {
      *count = 0;
      return kEndOfInput;
    }
    const Packet packet = self->packets_.front();
    self->packets_.pop_front();
    if (!self->incoming_.waitFor(packet.offset + packet.size)) {
      *count = 0;
      return kEndOfInput;
    }
    self->current_ = self->incoming_.bytes(packet.offset, packet.size);
    self->description_ = {0, 0, packet.size};
    data->mNumberBuffers = 1;
    data->mBuffers[0].mData = self->current_.data();
    data->mBuffers[0].mDataByteSize = packet.size;
    data->mBuffers[0].mNumberChannels = 0;
    if (descriptions) *descriptions = &self->description_;
    *count = 1;
    return noErr;
  }

  Incoming incoming_;
  AudioConverterRef converter_ = nullptr;
  std::vector<uint8_t> cookie_;
  uint32_t timescale_ = 0;
  uint64_t duration_ = 0;
  uint64_t fragmentDuration_ = 0;
  uint64_t sidxDuration_ = 0;
  int64_t estimated_ = 0;
  uint64_t next_ = 0;
  std::deque<Packet> packets_;
  std::vector<uint8_t> current_;
  AudioStreamPacketDescription description_{};
  bool drained_ = false;
};

}

std::unique_ptr<AudioReader> openInputAudio(const std::filesystem::path& savePath, int sampleRate) {
  return std::make_unique<Mp4AacReader>(savePath, sampleRate);
}
