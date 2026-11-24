#ifndef PLAYSTATS_H
#define PLAYSTATS_H

#include "iplayerlistener.h"
#include "playcompletiontracker.h"
#include <QString>
#include <QStringList>
#include <functional>

class ISongFieldHost;
class PlayStatsStore;

// The song identity key the stats are filed under: normalized
// title|artist|album, normalize being trimmed().toLower() and a missing field
// an empty string. keyFieldValues holds them in that order
// (playStatsKeyFields). The cloud stores these keys, so changing the rule needs
// a schema version that re-keys the rows (merging those that collide) and a
// re-key of the cloud (docs/plugins/playstats.md, "The identity key").
QString songIdentityKey(const QStringList &keyFieldValues);
QStringList playStatsKeyFields();

// Records plays from the host's playback events: the start time of each song
// and, once a play meets PlayCompletionTracker's thresholds, one more count.
// Each write reports its key to fieldHost, and a count to onPlayCounted (the
// cloud upload). A play counts for the key the song had when it started.
class PlayStats : public IPlayerListener {
public:
  PlayStats(PlayStatsStore &store, ISongFieldHost &fieldHost,
            std::function<void(const QString &key)> onPlayCounted);

  void trackStarted(const TrackInfo &track) override;
  void stopped() override;
  void paused() override;
  void resumed() override;
  void positionChanged(qint64 positionMs) override;
  void durationChanged(qint64 durationMs) override;
  void seeked(qint64 positionMs) override;
  void finished() override;

private:
  void countIfCompleted();

  PlayStatsStore &store_;
  ISongFieldHost &fieldHost_;
  std::function<void(const QString &key)> onPlayCounted_;
  PlayCompletionTracker tracker_;
  // The playing song's key; empty before the first song.
  QString key_;
  // Listened time counts only while playing.
  bool playing_ = false;
};

#endif // PLAYSTATS_H
