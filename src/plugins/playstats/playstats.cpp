#include "playstats.h"

#include "isongfieldhost.h"
#include "playstatsstore.h"
#include <QDateTime>

namespace {
QString normalize(const QString &text) { return text.trimmed().toLower(); }
} // namespace

QString songIdentityKey(const QStringList &keyFieldValues) {
  return normalize(keyFieldValues.value(0)) + QLatin1Char('|') +
         normalize(keyFieldValues.value(1)) + QLatin1Char('|') +
         normalize(keyFieldValues.value(2));
}

QStringList playStatsKeyFields() {
  return {QStringLiteral("title"), QStringLiteral("artist"),
          QStringLiteral("album")};
}

PlayStats::PlayStats(PlayStatsStore &store, ISongFieldHost &fieldHost,
                     std::function<void(const QString &key)> onPlayCounted)
    : store_(store), fieldHost_(fieldHost),
      onPlayCounted_(std::move(onPlayCounted)) {}

void PlayStats::trackStarted(const TrackInfo &track) {
  QStringList keyFieldValues;
  for (const QString &field : playStatsKeyFields()) {
    keyFieldValues.push_back(track.fields.value(field));
  }
  // Kept for the session, so a play counts for the song that played even if
  // its tags change while it plays.
  key_ = songIdentityKey(keyFieldValues);
  // The duration is kept: the backend may have reported it before the start.
  tracker_.startSession();
  playing_ = true;
  store_.markStarted(key_, QDateTime::currentSecsSinceEpoch());
  fieldHost_.valuesChanged({key_});
}

void PlayStats::stopped() { playing_ = false; }

void PlayStats::paused() { playing_ = false; }

void PlayStats::resumed() { playing_ = true; }

void PlayStats::positionChanged(qint64 positionMs) {
  tracker_.recordPositionMs(positionMs, playing_);
  countIfCompleted();
}

void PlayStats::durationChanged(qint64 durationMs) {
  tracker_.setDurationMs(durationMs);
}

void PlayStats::seeked(qint64 /*positionMs*/) { tracker_.seeked(); }

void PlayStats::finished() { countIfCompleted(); }

void PlayStats::countIfCompleted() {
  if (key_.isEmpty() || !tracker_.shouldCount()) {
    return;
  }
  tracker_.markCounted();
  store_.countPlay(key_);
  fieldHost_.valuesChanged({key_});
  onPlayCounted_(key_);
}
