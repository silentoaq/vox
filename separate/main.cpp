#include <windows.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include <opusenc.h>

#include "audioReader.h"
#include "demucs.h"
#include "incoming.h"

namespace {

constexpr int kMargin = kSampleRate / 2;
constexpr opus_int32 kAccompanimentBitrate = 128000;
constexpr opus_int32 kVocalsBitrate = 96000;

std::vector<float> readFloats(const std::wstring& path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  std::vector<float> values(size_t(file.tellg()) / sizeof(float));
  file.seekg(0);
  file.read(reinterpret_cast<char*>(values.data()), values.size() * sizeof(float));
  return values;
}

void writeAtomically(const std::wstring& path, const void* data, size_t bytes) {
  const std::wstring temp = path + L".tmp";
  {
    std::ofstream file(temp, std::ios::binary | std::ios::trunc);
    file.write(static_cast<const char*>(data), std::streamsize(bytes));
  }
  MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
}

void check(int result, const char* step) {
  if (result != OPE_OK) throw std::runtime_error(std::string(step) + ": " + ope_strerror(result));
}

void writeStem(const std::wstring& path, const std::vector<float>* channels, int from, int count, opus_int32 bitrate) {
  std::vector<float> interleaved(size_t(count) * 2);
  for (int i = 0; i < count; ++i)
    for (int ch = 0; ch < 2; ++ch) interleaved[size_t(i) * 2 + ch] = std::clamp(channels[ch][from + i], -1.0f, 1.0f);
  const std::wstring temp = path + L".tmp";
  std::ofstream file(temp, std::ios::binary | std::ios::trunc);
  OpusEncCallbacks callbacks{
    [](void* user, const unsigned char* data, opus_int32 length) {
      auto* out = static_cast<std::ofstream*>(user);
      out->write(reinterpret_cast<const char*>(data), length);
      return out->good() ? 0 : 1;
    },
    [](void*) { return 0; }};
  OggOpusComments* comments = ope_comments_create();
  int error = OPE_OK;
  OggOpusEnc* encoder = ope_encoder_create_callbacks(&callbacks, &file, comments, kSampleRate, 2, 0, &error);
  check(error, "create opus encoder");
  check(ope_encoder_ctl(encoder, OPUS_SET_BITRATE(bitrate)), "set bitrate");
  check(ope_encoder_write_float(encoder, interleaved.data(), count), "encode");
  check(ope_encoder_drain(encoder), "finish opus stream");
  ope_encoder_destroy(encoder);
  ope_comments_destroy(comments);
  file.close();
  MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
}

std::wstring segmentName(const wchar_t* stem, int index) {
  wchar_t name[32];
  swprintf_s(name, L"%s_%04d.opus", stem, index);
  return name;
}

int parity(const std::wstring& model, const std::wstring& mixPath, const std::wstring& referencePath) {
  auto mix = readFloats(mixPath);
  auto reference = readFloats(referencePath);
  Demucs demucs(model, 2);
  const int segment = demucs.segment();
  Stems stems = demucs.separate(mix.data(), mix.data() + segment);
  double worstAccompaniment = 0;
  double worstVocals = 0;
  for (int ch = 0; ch < 2; ++ch) {
    for (int i = 0; i < segment; ++i) {
      auto at = [&](int source) { return reference[(size_t(source) * 2 + ch) * segment + i]; };
      worstAccompaniment = std::max(worstAccompaniment, double(std::abs(stems.accompaniment[ch][i] - (at(0) + at(1) + at(2)))));
      worstVocals = std::max(worstVocals, double(std::abs(stems.vocals[ch][i] - at(kVocals))));
    }
  }
  printf("accompaniment max diff %.6g\nvocals max diff %.6g\n", worstAccompaniment, worstVocals);
  return 0;
}

int separateSong(const std::wstring& model, const std::wstring& savePath, const std::wstring& outDir, int threads, double fromSeconds,
                 int firstThreads) {
  auto incoming = Microsoft::WRL::Make<Incoming>(GetStdHandle(STD_INPUT_HANDLE), savePath == L"-" ? std::wstring() : savePath);
  auto demucs = std::make_unique<Demucs>(model, firstThreads);
  AudioReader reader(incoming.Get(), kSampleRate);
  const int segment = demucs->segment();
  const int stride = segment - 2 * kMargin;
  int samples = int(reader.estimatedSamples());
  const int estimatedSegments = (samples + stride - 1) / stride;
  printf("total %d %d %d\n", estimatedSegments, stride, samples);
  fflush(stdout);

  Audio audio;
  bool ended = false;
  const auto decodeUntil = [&](int needed) {
    while (!ended && int(audio.left.size()) < needed) ended = !reader.readMore(audio);
    if (ended) samples = int(audio.left.size());
  };
  const int first = std::clamp(int(fromSeconds * kSampleRate) / stride, 0, std::max(estimatedSegments - 1, 0));
  std::vector<int> order;
  for (int n = 0; n < estimatedSegments; ++n) order.push_back((first + n) % estimatedSegments);

  std::vector<bool> written;
  std::vector<float> left(segment);
  std::vector<float> right(segment);
  const auto separateAt = [&](int index) {
    const int start = index * stride - kMargin;
    decodeUntil(start + segment);
    if (index * stride >= samples) return;
    const int decoded = int(audio.left.size());
    for (int i = 0; i < segment; ++i) {
      const int source = start + i;
      const bool inside = source >= 0 && source < decoded;
      left[i] = inside ? audio.left[source] : 0.0f;
      right[i] = inside ? audio.right[source] : 0.0f;
    }
    Stems stems = demucs->separate(left.data(), right.data());
    const int count = std::min(stride, samples - index * stride);
    writeStem(outDir + L"\\" + segmentName(L"acc", index), stems.accompaniment, kMargin, count, kAccompanimentBitrate);
    writeStem(outDir + L"\\" + segmentName(L"voc", index), stems.vocals, kMargin, count, kVocalsBitrate);
    if (int(written.size()) <= index) written.resize(index + 1);
    written[index] = true;
    printf("ready %d\n", index);
    fflush(stdout);
    if (firstThreads != threads) {
      demucs = std::make_unique<Demucs>(model, threads);
      firstThreads = threads;
    }
  };
  for (int index : order) separateAt(index);
  decodeUntil(INT_MAX);
  const int segments = (samples + stride - 1) / stride;
  for (int index = 0; index < segments; ++index)
    if (index >= int(written.size()) || !written[index]) separateAt(index);
  const std::string meta = "{\"segments\":" + std::to_string(segments) + ",\"stride\":" + std::to_string(stride) +
                           ",\"samples\":" + std::to_string(samples) + ",\"sampleRate\":" + std::to_string(kSampleRate) + "}";
  writeAtomically(outDir + L"\\segments.json", meta.data(), meta.size());
  printf("done\n");
  fflush(stdout);
  return 0;
}

}

int wmain(int argc, wchar_t** argv) {
  try {
    if (argc == 5 && std::wstring(argv[1]) == L"parity") return parity(argv[2], argv[3], argv[4]);
    if (argc == 7) return separateSong(argv[1], argv[2], argv[3], _wtoi(argv[4]), _wtof(argv[5]), _wtoi(argv[6]));
    fprintf(stderr, "usage: vox-separate <model> <save-path|-> <out-dir> <threads> <from-seconds> <first-threads> < audio\n");
    return 2;
  } catch (const std::exception& e) {
    fprintf(stderr, "error %s\n", e.what());
    fflush(stderr);
    return 1;
  }
}
