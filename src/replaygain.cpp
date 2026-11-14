#include "replaygain.h"
#include <QSettings>
#include <QString>
#include <algorithm>
#include <cmath>

namespace {
constexpr const char *kModeSettingsKey = "playback/replaygain";

// ReplayGain's reference level in dB SPL (rgvolume's RG_REFERENCE_LEVEL).
constexpr double kReferenceLevelDb = 89.0;
// The volume element's maximum factor, 10.
constexpr double kMaxGainDb = 20.0;

bool isValidGain(double db) { return db > -60.0 && db < 60.0; }

// Drops a peak of 0 or below, and clamps one above 1.0: float decoders can
// produce samples a little over full scale.
void normalizePeak(std::optional<double> &peak) {
  if (peak && *peak <= 0.0) {
    peak.reset();
  } else if (peak) {
    peak = std::min(*peak, 1.0);
  }
}
} // namespace

ReplayGainMode loadReplayGainModeSetting() {
  QSettings settings;
  const QString mode =
      settings.value(kModeSettingsKey, QStringLiteral("track")).toString();
  if (mode == QStringLiteral("off")) {
    return ReplayGainMode::Off;
  }
  if (mode == QStringLiteral("album")) {
    return ReplayGainMode::Album;
  }
  return ReplayGainMode::Track;
}

void saveReplayGainModeSetting(ReplayGainMode mode) {
  QSettings settings;
  switch (mode) {
  case ReplayGainMode::Off:
    settings.setValue(kModeSettingsKey, QStringLiteral("off"));
    break;
  case ReplayGainMode::Track:
    settings.setValue(kModeSettingsKey, QStringLiteral("track"));
    break;
  case ReplayGainMode::Album:
    settings.setValue(kModeSettingsKey, QStringLiteral("album"));
    break;
  }
}

void ReplayGainTags::merge(const ReplayGainTagValues &values) {
  std::optional<double> trackGain = values.trackGain;
  std::optional<double> trackPeak = values.trackPeak;
  std::optional<double> albumGain = values.albumGain;
  std::optional<double> albumPeak = values.albumPeak;

  // A positive reference level is in dB SPL: the event's gains are moved to
  // ReplayGain's. One of 0 or below is in LUFS (loudgain), and the gains
  // need no change.
  if (values.referenceLevel && *values.referenceLevel > 0.0) {
    const double shift = kReferenceLevelDb - *values.referenceLevel;
    if (trackGain) {
      *trackGain += shift;
    }
    if (albumGain) {
      *albumGain += shift;
    }
  }
  // R128 gains (Opus) take priority; they are relative to -23 LUFS, 5 dB
  // below ReplayGain's reference, and the event's peaks are not for them.
  if (values.trackGainR128) {
    trackGain = *values.trackGainR128 + 5.0;
    trackPeak.reset();
  }
  if (values.albumGainR128) {
    albumGain = *values.albumGainR128 + 5.0;
    albumPeak.reset();
  }

  if (trackGain && !isValidGain(*trackGain)) {
    trackGain.reset();
  }
  if (albumGain && !isValidGain(*albumGain)) {
    albumGain.reset();
  }
  normalizePeak(trackPeak);
  normalizePeak(albumPeak);

  if (trackGain) {
    trackGain_ = trackGain;
  }
  if (trackPeak) {
    trackPeak_ = trackPeak;
  }
  if (albumGain) {
    albumGain_ = albumGain;
  }
  if (albumPeak) {
    albumPeak_ = albumPeak;
  }
}

double ReplayGainTags::gainDb(ReplayGainMode mode) const {
  if (mode == ReplayGainMode::Off) {
    return 0.0;
  }
  // Each mode falls back to the other gain when its own is missing.
  const bool useAlbum = mode == ReplayGainMode::Album ? albumGain_.has_value()
                                                      : !trackGain_.has_value();
  const std::optional<double> &gain = useAlbum ? albumGain_ : trackGain_;
  if (!gain) {
    return 0.0; // the fallback gain
  }
  // A missing peak is taken as full scale, so a positive gain without a peak
  // is cut to 0 dB.
  const double peakDb =
      20.0 * std::log10((useAlbum ? albumPeak_ : trackPeak_).value_or(1.0));
  // Clipping prevention: the peak may reach full scale, not exceed it.
  const double gainDb = std::min(*gain, -peakDb);
  return std::min(gainDb, kMaxGainDb);
}
