#include "audioReader.h"

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>

#include <stdexcept>
#include <string>

using Microsoft::WRL::ComPtr;

namespace {

constexpr DWORD kAudioStream = DWORD(MF_SOURCE_READER_FIRST_AUDIO_STREAM);
constexpr DWORD kAllStreams = DWORD(MF_SOURCE_READER_ALL_STREAMS);
constexpr wchar_t kContentType[] = L"audio/mp4";
constexpr double kHundredNanoseconds = 1e7;

void check(HRESULT hr, const char* step) {
  if (FAILED(hr)) throw std::runtime_error(std::string(step) + " failed: " + std::to_string(unsigned(hr)));
}

}

AudioReader::AudioReader(IStream* stream, int sampleRate) {
  check(MFStartup(MF_VERSION), "MFStartup");
  ComPtr<IMFByteStream> bytes;
  check(MFCreateMFByteStreamOnStream(stream, &bytes), "wrap stream");
  ComPtr<IMFAttributes> attributes;
  check(bytes.As(&attributes), "stream attributes");
  check(attributes->SetString(MF_BYTESTREAM_CONTENT_TYPE, kContentType), "content type");
  check(MFCreateSourceReaderFromByteStream(bytes.Get(), nullptr, &reader_), "open audio");
  check(reader_->SetStreamSelection(kAllStreams, FALSE), "deselect streams");
  check(reader_->SetStreamSelection(kAudioStream, TRUE), "select audio");
  ComPtr<IMFMediaType> type;
  check(MFCreateMediaType(&type), "create media type");
  type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
  type->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float);
  type->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
  type->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, sampleRate);
  type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 32);
  type->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 8);
  type->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, sampleRate * 8);
  check(reader_->SetCurrentMediaType(kAudioStream, nullptr, type.Get()), "set output format");
  PROPVARIANT duration;
  PropVariantInit(&duration);
  check(reader_->GetPresentationAttribute(DWORD(MF_SOURCE_READER_MEDIASOURCE), MF_PD_DURATION, &duration), "read duration");
  estimated_ = int64_t(double(duration.uhVal.QuadPart) * sampleRate / kHundredNanoseconds + 0.5);
  PropVariantClear(&duration);
}

AudioReader::~AudioReader() {
  reader_.Reset();
  MFShutdown();
}

bool AudioReader::readMore(Audio& audio) {
  DWORD flags = 0;
  ComPtr<IMFSample> sample;
  check(reader_->ReadSample(kAudioStream, 0, nullptr, &flags, nullptr, &sample), "read audio");
  if (flags & MF_SOURCE_READERF_ENDOFSTREAM) return false;
  if (!sample) return true;
  ComPtr<IMFMediaBuffer> buffer;
  check(sample->ConvertToContiguousBuffer(&buffer), "contiguous buffer");
  BYTE* data = nullptr;
  DWORD length = 0;
  check(buffer->Lock(&data, nullptr, &length), "lock buffer");
  const float* samples = reinterpret_cast<const float*>(data);
  const size_t frames = length / (2 * sizeof(float));
  for (size_t i = 0; i < frames; ++i) {
    audio.left.push_back(samples[2 * i]);
    audio.right.push_back(samples[2 * i + 1]);
  }
  buffer->Unlock();
  return true;
}
