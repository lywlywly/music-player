#ifndef REPLAYGAIN_H
#define REPLAYGAIN_H

#include <optional>

// Which ReplayGain tag sets the gain: none, the track's, or the album's.
enum class ReplayGainMode { Off, Track, Album };

// playback/replaygain: off, track or album; Track when none is stored.
ReplayGainMode loadReplayGainModeSetting();
void saveReplayGainModeSetting(ReplayGainMode mode);

// The ReplayGain values of one tag event, as GStreamer's tags hold them:
// gains and the reference level in dB, peaks linear.
struct ReplayGainTagValues {
  std::optional<double> trackGain;
  std::optional<double> trackGainR128;
  std::optional<double> trackPeak;
  std::optional<double> albumGain;
  std::optional<double> albumGainR128;
  std::optional<double> albumPeak;
  std::optional<double> referenceLevel;
};

// One song's ReplayGain tags, and the gain they give in each mode. The rules
// are those of GStreamer's rgvolume (gstrgvolume.c, 1.28.7) with 0 dB pre-amp,
// fallback gain and headroom, plus Off; see docs/playback.md (ReplayGain).
class ReplayGainTags {
public:
  // Adds one tag event's values; a value missing from it keeps the earlier
  // one.
  void merge(const ReplayGainTagValues &values);
  // The gain to apply, at most +20 dB (the volume element's maximum).
  double gainDb(ReplayGainMode mode) const;

private:
  std::optional<double> trackGain_;
  std::optional<double> trackPeak_;
  std::optional<double> albumGain_;
  std::optional<double> albumPeak_;
};

#endif // REPLAYGAIN_H
