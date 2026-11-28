#include "demucs.h"

#include <array>
#include <complex>

#include "onnxruntime_cxx_api.h"
#include "spectrogram.h"

struct Demucs::Runtime {
  Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "vox-separate"};
  Ort::Session session{nullptr};
};

Demucs::Demucs(const std::filesystem::path& modelPath, int threads) : runtime_(std::make_unique<Runtime>()) {
  Ort::SessionOptions options;
  options.SetIntraOpNumThreads(threads);
  options.SetInterOpNumThreads(1);
  options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
  options.DisableCpuMemArena();
  runtime_->session = Ort::Session(runtime_->env, modelPath.c_str(), options);
  segment_ = int(runtime_->session.GetInputTypeInfo(1).GetTensorTypeAndShapeInfo().GetShape()[2]);
}

Demucs::~Demucs() = default;

Stems Demucs::separate(const float* left, const float* right) {
  const int frames = framesFor(segment_);
  const size_t plane = size_t(kBins) * frames;
  const float* channels[2] = {left, right};

  std::vector<float> mag(4 * plane);
  std::vector<float> mix(2 * size_t(segment_));
  std::vector<std::complex<float>> spectrum;
  for (int ch = 0; ch < 2; ++ch) {
    spectrogram(channels[ch], segment_, spectrum);
    for (size_t i = 0; i < plane; ++i) {
      mag[(ch * 2) * plane + i] = spectrum[i].real();
      mag[(ch * 2 + 1) * plane + i] = spectrum[i].imag();
    }
    std::copy(channels[ch], channels[ch] + segment_, mix.begin() + ch * size_t(segment_));
  }

  auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
  const std::array<int64_t, 4> magShape{1, 4, kBins, frames};
  const std::array<int64_t, 3> mixShape{1, 2, segment_};
  std::array<Ort::Value, 2> inputs{
    Ort::Value::CreateTensor<float>(memory, mag.data(), mag.size(), magShape.data(), magShape.size()),
    Ort::Value::CreateTensor<float>(memory, mix.data(), mix.size(), mixShape.data(), mixShape.size()),
  };
  const char* inputNames[] = {"mag", "mix"};
  const char* outputNames[] = {"spec", "wave"};
  auto outputs = runtime_->session.Run(Ort::RunOptions{nullptr}, inputNames, inputs.data(), 2, outputNames, 2);
  const float* spec = outputs[0].GetTensorData<float>();
  const float* wave = outputs[1].GetTensorData<float>();

  Stems stems;
  for (int ch = 0; ch < 2; ++ch) {
    stems.accompaniment[ch].assign(segment_, 0.0f);
    stems.vocals[ch].assign(segment_, 0.0f);
  }
  std::vector<std::complex<float>> accompaniment(plane);
  std::vector<std::complex<float>> vocals(plane);
  std::vector<float> signal(segment_);
  for (int ch = 0; ch < 2; ++ch) {
    std::fill(accompaniment.begin(), accompaniment.end(), std::complex<float>());
    for (int s = 0; s < kSources; ++s) {
      const float* real = spec + (size_t(s) * 4 + ch * 2) * plane;
      const float* imag = real + plane;
      auto& target = s == kVocals ? vocals : accompaniment;
      for (size_t i = 0; i < plane; ++i) {
        if (s == kVocals) target[i] = {real[i], imag[i]};
        else target[i] += std::complex<float>(real[i], imag[i]);
      }
      const float* time = wave + (size_t(s) * 2 + ch) * segment_;
      auto& out = s == kVocals ? stems.vocals[ch] : stems.accompaniment[ch];
      for (int i = 0; i < segment_; ++i) out[i] += time[i];
    }
    inverseSpectrogram(accompaniment.data(), segment_, signal.data());
    for (int i = 0; i < segment_; ++i) stems.accompaniment[ch][i] += signal[i];
    inverseSpectrogram(vocals.data(), segment_, signal.data());
    for (int i = 0; i < segment_; ++i) stems.vocals[ch][i] += signal[i];
  }
  return stems;
}
