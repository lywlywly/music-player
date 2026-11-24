#ifndef PLAYSTATSSTORE_H
#define PLAYSTATSSTORE_H

#include "isongfieldprovider.h"
#include <QSqlDatabase>
#include <QString>
#include <QStringList>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

class QThread;

// The play stats file (docs/plugins/playstats.md, "The plugin's database"):
// play_count and last_played_timestamp by song identity key, in SQLite. The
// file is the only store: each operation is one statement.
//
// Constructing it does no I/O. The thread that constructs it (the main
// thread) has one connection, opened on first use and closed by the
// destructor; it makes every write. A read on another thread opens a
// connection of its own for that call: WAL lets it run beside a write and see
// a consistent snapshot. Every connection opens the file the same way, and a
// read of a missing file returns nothing without creating it.
class PlayStatsStore {
public:
  // The values' order: SongFieldValues hold them in this order.
  enum Field { PlayCount, LastPlayed };

  explicit PlayStatsStore(QString filePath);
  ~PlayStatsStore();
  PlayStatsStore(const PlayStatsStore &) = delete;
  PlayStatsStore &operator=(const PlayStatsStore &) = delete;

  // Writes, on the constructing thread.
  // A song of key started at unixSeconds.
  void markStarted(const QString &key, qint64 unixSeconds);
  // A play of key counted.
  void countPlay(const QString &key);
  // A page of cloud counts, merged as the larger of local and cloud, in one
  // transaction.
  void mergeCloudCounts(const std::vector<std::pair<QString, int>> &counts);

  // Reads, on any thread.
  SongFieldValuesByKey values(const QStringList &keys) const;
  SongFieldValuesByKey allValues() const;
  // Every key's count, for a rebase.
  std::unordered_map<std::string, int> playCounts() const;

private:
  // Runs read on the constructing thread's connection, or on a connection of
  // its own on another thread; does not run it when the file does not exist.
  void withReadDatabase(const std::function<void(QSqlDatabase &)> &read) const;
  // The constructing thread's connection, opened (and the file created) on
  // first use.
  QSqlDatabase &mainDatabase() const;

  const QString filePath_;
  QThread *const ownerThread_;
  const QString mainConnectionName_;
  mutable QSqlDatabase main_;
};

#endif // PLAYSTATSSTORE_H
