#include "pcmtap.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace {

template <typename T> float sampleAt(const void *data, size_t index) {
  T value;
  std::memcpy(&value, static_cast<const char *>(data) + index * sizeof(T),
              sizeof(T));
  return static_cast<float>(value);
}

// Sample index (interleaved) as a float in [-1, 1].
float normalizedSample(const void *data, size_t index,
                       PcmTap::SampleFormat format) {
  switch (format) {
  case PcmTap::SampleFormat::UInt8:
    return (sampleAt<uint8_t>(data, index) - 128.0f) / 128.0f;
  case PcmTap::SampleFormat::Int8:
    return sampleAt<int8_t>(data, index) / 128.0f;
  case PcmTap::SampleFormat::Int16:
    return sampleAt<int16_t>(data, index) / 32768.0f;
  case PcmTap::SampleFormat::Int24: {
    const auto *bytes = static_cast<const uint8_t *>(data) + index * 3;
    // Into the top 24 bits of an int32, so the sign carries.
    const auto value =
        static_cast<int32_t>((static_cast<uint32_t>(bytes[0]) << 8) |
                             (static_cast<uint32_t>(bytes[1]) << 16) |
                             (static_cast<uint32_t>(bytes[2]) << 24));
    return value / 2147483648.0f;
  }
  case PcmTap::SampleFormat::Int32:
    return sampleAt<int32_t>(data, index) / 2147483648.0f;
  case PcmTap::SampleFormat::Float32:
    return sampleAt<float>(data, index);
  case PcmTap::SampleFormat::Float64:
    return sampleAt<double>(data, index);
  }
  return 0.0f;
}

int64_t samplesToUs(int64_t samples, int sampleRate) {
  return samples * 1'000'000 / sampleRate;
}

} // namespace

void PcmTap::push(const void *data, int frames, int channels,
                  SampleFormat format, int sampleRate, int64_t startUs) {
  std::vector<float> mono(frames);
  for (int f = 0; f < frames; ++f) {
    float sum = 0.0f;
    for (int c = 0; c < channels; ++c) {
      sum +=
          normalizedSample(data, static_cast<size_t>(f) * channels + c, format);
    }
    mono[f] = sum / channels;
  }

  std::lock_guard lock(mutex_);
  // Called only when samples_ is not empty and the rate is the same, so
  // sampleRate_ is not 0.
  const auto followsOn = [&]() {
    const int64_t expectedUs =
        firstUs_ +
        samplesToUs(static_cast<int64_t>(samples_.size()), sampleRate_);
    return std::llabs(startUs - expectedUs) <= kMaxGapUs;
  };
  if (samples_.empty() || sampleRate != sampleRate_ || !followsOn()) {
    samples_.clear();
    sampleRate_ = sampleRate;
    firstUs_ = startUs;
  }
  samples_.insert(samples_.end(), mono.begin(), mono.end());
  const size_t kept = static_cast<size_t>(kKeptUs * sampleRate_ / 1'000'000);
  if (samples_.size() > kept) {
    const size_t dropped = samples_.size() - kept;
    samples_.erase(samples_.begin(), samples_.begin() + dropped);
    firstUs_ += samplesToUs(static_cast<int64_t>(dropped), sampleRate_);
  }
}

int PcmTap::read(int64_t endUs, std::vector<float> &out) const {
  std::lock_guard lock(mutex_);
  std::fill(out.begin(), out.end(), 0.0f);
  if (samples_.empty()) {
    return 0;
  }
  // Index (in samples_) just past the last sample wanted, to the nearest
  // sample: times in microseconds are rounded.
  const int64_t end = ((endUs - firstUs_) * sampleRate_ + 500'000) / 1'000'000;
  const int64_t begin = end - static_cast<int64_t>(out.size());
  const int64_t size = static_cast<int64_t>(samples_.size());
  if (end <= 0 || begin >= size) {
    return 0;
  }
  for (int64_t i = std::max<int64_t>(begin, 0); i < std::min(end, size); ++i) {
    out[i - begin] = samples_[i];
  }
  return sampleRate_;
}

void PcmTap::clear() {
  std::lock_guard lock(mutex_);
  samples_.clear();
  sampleRate_ = 0;
  firstUs_ = 0;
}
