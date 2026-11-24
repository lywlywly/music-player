#include "playstatsstore.h"

#include <QAtomicInteger>
#include <QDir>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>
#include <QVariant>

namespace {
// PRAGMA user_version of the schema this code reads and writes.
constexpr int kSchemaVersion = 1;

// Unique connection names: one main connection per store (a store per
// plugin initialize), one per call off the main thread.
QString newConnectionName(const char *kind) {
  static QAtomicInteger<quint64> counter;
  return QStringLiteral("playstats_%1_%2").arg(kind).arg(++counter);
}

void execOrDie(QSqlQuery &query, const QString &sql) {
  if (!query.exec(sql)) {
    qFatal("PlayStatsStore: %s failed: %s", sql.toUtf8().constData(),
           query.lastError().text().toUtf8().constData());
  }
}

void execOrDie(QSqlQuery &query) {
  if (!query.exec()) {
    qFatal("PlayStatsStore: %s failed: %s",
           query.lastQuery().toUtf8().constData(),
           query.lastError().text().toUtf8().constData());
  }
}

int userVersion(QSqlDatabase &db) {
  QSqlQuery query(db);
  execOrDie(query, QStringLiteral("PRAGMA user_version"));
  query.next();
  return query.value(0).toInt();
}

// Opens the file the one way every connection does: pragmas, then the
// schema created or migrated inside BEGIN IMMEDIATE after checking the
// version. SQLite serializes those transactions, so whichever connection
// opens first does it, and no read sees an older schema than this code's.
QSqlDatabase openDatabase(const QString &filePath,
                          const QString &connectionName) {
  QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connectionName);
  db.setDatabaseName(filePath);
  if (!db.open()) {
    qFatal("PlayStatsStore: cannot open %s: %s", filePath.toUtf8().constData(),
           db.lastError().text().toUtf8().constData());
  }
  QSqlQuery query(db);
  execOrDie(query, QStringLiteral("PRAGMA busy_timeout = 5000"));
  execOrDie(query, QStringLiteral("PRAGMA journal_mode = WAL"));
  execOrDie(query, QStringLiteral("PRAGMA synchronous = NORMAL"));
  if (userVersion(db) == kSchemaVersion) {
    return db;
  }
  execOrDie(query, QStringLiteral("BEGIN IMMEDIATE"));
  const int version = userVersion(db);
  if (version > kSchemaVersion) {
    qFatal("PlayStatsStore: %s has schema version %d, newer than %d",
           filePath.toUtf8().constData(), version, kSchemaVersion);
  }
  if (version == 0) {
    execOrDie(query, QStringLiteral(R"(
        CREATE TABLE IF NOT EXISTS song_play_stats (
            song_identity_key     TEXT PRIMARY KEY,
            play_count            INTEGER NOT NULL DEFAULT 0,
            last_played_timestamp INTEGER NOT NULL DEFAULT 0
        ) WITHOUT ROWID
    )"));
    execOrDie(query,
              QStringLiteral("PRAGMA user_version = %1").arg(kSchemaVersion));
  }
  execOrDie(query, QStringLiteral("COMMIT"));
  return db;
}

void closeDatabase(QSqlDatabase &db) {
  const QString name = db.connectionName();
  db.close();
  db = QSqlDatabase();
  QSqlDatabase::removeDatabase(name);
}

// A row's values in PlayStatsStore::Field order; a last played time of 0
// (a count from the cloud before any local play) is no value.
SongFieldValues rowValues(const QSqlQuery &query, int firstColumn) {
  const qint64 lastPlayed = query.value(firstColumn + 1).toLongLong();
  return {query.value(firstColumn).toLongLong(),
          lastPlayed > 0 ? QVariant(lastPlayed) : QVariant()};
}
} // namespace

PlayStatsStore::PlayStatsStore(QString filePath)
    : filePath_(std::move(filePath)), ownerThread_(QThread::currentThread()),
      mainConnectionName_(newConnectionName("main")) {}

PlayStatsStore::~PlayStatsStore() {
  if (main_.isValid()) {
    closeDatabase(main_);
  }
}

QSqlDatabase &PlayStatsStore::mainDatabase() const {
  if (!main_.isValid()) {
    QDir().mkpath(QFileInfo(filePath_).absolutePath());
    main_ = openDatabase(filePath_, mainConnectionName_);
  }
  return main_;
}

void PlayStatsStore::withReadDatabase(
    const std::function<void(QSqlDatabase &)> &read) const {
  // Opening a missing file would create it.
  const bool exists = QFileInfo::exists(filePath_);
  if (QThread::currentThread() == ownerThread_) {
    if (main_.isValid() || exists) {
      read(mainDatabase());
    }
    return;
  }
  if (!exists) {
    return;
  }
  QSqlDatabase db = openDatabase(filePath_, newConnectionName("read"));
  read(db);
  closeDatabase(db);
}

void PlayStatsStore::markStarted(const QString &key, qint64 unixSeconds) {
  QSqlQuery query(mainDatabase());
  query.prepare(QStringLiteral(R"(
      INSERT INTO song_play_stats(song_identity_key, play_count,
                                  last_played_timestamp)
      VALUES(:key, 0, :now)
      ON CONFLICT(song_identity_key) DO UPDATE SET
          last_played_timestamp = excluded.last_played_timestamp
  )"));
  query.bindValue(":key", key);
  query.bindValue(":now", unixSeconds);
  execOrDie(query);
}

void PlayStatsStore::countPlay(const QString &key) {
  QSqlQuery query(mainDatabase());
  query.prepare(QStringLiteral(R"(
      INSERT INTO song_play_stats(song_identity_key, play_count,
                                  last_played_timestamp)
      VALUES(:key, 1, 0)
      ON CONFLICT(song_identity_key) DO UPDATE SET
          play_count = play_count + 1
  )"));
  query.bindValue(":key", key);
  execOrDie(query);
}

void PlayStatsStore::mergeCloudCounts(
    const std::vector<std::pair<QString, int>> &counts) {
  QSqlDatabase &db = mainDatabase();
  if (!db.transaction()) {
    qFatal("PlayStatsStore: cannot start a transaction: %s",
           db.lastError().text().toUtf8().constData());
  }
  QSqlQuery query(db);
  query.prepare(QStringLiteral(R"(
      INSERT INTO song_play_stats(song_identity_key, play_count,
                                  last_played_timestamp)
      VALUES(:key, :count, 0)
      ON CONFLICT(song_identity_key) DO UPDATE SET
          play_count = MAX(play_count, excluded.play_count)
  )"));
  for (const auto &[key, count] : counts) {
    query.bindValue(":key", key);
    query.bindValue(":count", count);
    execOrDie(query);
  }
  if (!db.commit()) {
    qFatal("PlayStatsStore: commit failed: %s",
           db.lastError().text().toUtf8().constData());
  }
}

SongFieldValuesByKey PlayStatsStore::values(const QStringList &keys) const {
  SongFieldValuesByKey values;
  withReadDatabase([&](QSqlDatabase &db) {
    // One read transaction, so the keys are read from one snapshot. A commit
    // left undone would keep it open on the connection, so the next write's
    // transaction would fail.
    if (!db.transaction()) {
      qFatal("PlayStatsStore: cannot start a read transaction: %s",
             db.lastError().text().toUtf8().constData());
    }
    QSqlQuery query(db);
    query.prepare(QStringLiteral(R"(
        SELECT play_count, last_played_timestamp
        FROM song_play_stats
        WHERE song_identity_key = :key
    )"));
    for (const QString &key : keys) {
      query.bindValue(":key", key);
      execOrDie(query);
      if (query.next()) {
        values.insert(key, rowValues(query, 0));
      }
    }
    // Finished first: Qt refuses to commit while a query is active.
    query.finish();
    if (!db.commit()) {
      qFatal("PlayStatsStore: cannot end a read transaction: %s",
             db.lastError().text().toUtf8().constData());
    }
  });
  return values;
}

SongFieldValuesByKey PlayStatsStore::allValues() const {
  SongFieldValuesByKey values;
  withReadDatabase([&](QSqlDatabase &db) {
    QSqlQuery query(db);
    query.setForwardOnly(true);
    execOrDie(query, QStringLiteral(R"(
        SELECT song_identity_key, play_count, last_played_timestamp
        FROM song_play_stats
    )"));
    while (query.next()) {
      values.insert(query.value(0).toString(), rowValues(query, 1));
    }
  });
  return values;
}

std::unordered_map<std::string, int> PlayStatsStore::playCounts() const {
  std::unordered_map<std::string, int> counts;
  withReadDatabase([&](QSqlDatabase &db) {
    QSqlQuery query(db);
    query.setForwardOnly(true);
    execOrDie(query, QStringLiteral(R"(
        SELECT song_identity_key, play_count FROM song_play_stats
    )"));
    while (query.next()) {
      counts.emplace(query.value(0).toString().toStdString(),
                     query.value(1).toInt());
    }
  });
  return counts;
}
