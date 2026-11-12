#ifndef PCMTAP_H
#define PCMTAP_H

#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>

// The recently decoded audio of the playing song, mixed to mono, with stream
// times, for the spectrum. A player pushes on its decoding thread; the GUI
// thread reads the samples at the play position. Thread-safe.
//
// The audio is one run of samples. A push that does not follow on (more than
// kMaxGapUs off, or a new sample rate) starts a new run: late buffers from
// before a seek are replaced by the first one after it. Samples more than
// kKeptUs before the newest are dropped.
class PcmTap {
public:
  // Integer formats are signed except UInt8; Int24 is packed (3 bytes).
  // Little-endian.
  enum class SampleFormat {
    UInt8,
    Int8,
    Int16,
    Int24,
    Int32,
    Float32,
    Float64
  };

  static constexpr int64_t kMaxGapUs = 50'000;
  static constexpr int64_t kKeptUs = 2'000'000;

  // frames interleaved frames of channels channels (at least 1), at
  // sampleRate (above 0); startUs is the stream time of the first one.
  void push(const void *data, int frames, int channels, SampleFormat format,
            int sampleRate, int64_t startUs);
  // Fills out with the out.size() samples that end at stream time endUs,
  // with zeros where there is no audio. Returns the sample rate, or 0 when no
  // audio overlaps them.
  int read(int64_t endUs, std::vector<float> &out) const;
  void clear();

private:
  mutable std::mutex mutex_;
  std::deque<float> samples_;
  int sampleRate_ = 0;
  // Stream time of samples_.front().
  int64_t firstUs_ = 0;
};

#endif // PCMTAP_H
