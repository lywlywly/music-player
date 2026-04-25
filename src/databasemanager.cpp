#include "databasemanager.h"
#include "columnregistry.h"
#include <QDebug>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>

namespace {
QString toSqlType(ColumnValueType valueType) {
  switch (valueType) {
  case ColumnValueType::Number:
    return "REAL";
  case ColumnValueType::Boolean:
    return "TEXT";
  case ColumnValueType::DateTime:
    return "TEXT";
  case ColumnValueType::Text:
  default:
    return "text";
  }
}

QString songsColumnDefinition(const ColumnDefinition &definition) {
  const QString columnName = definition.id;
  const QString columnType = toSqlType(definition.valueType);
  QString sql = QString("%1 %2").arg(columnName, columnType);
  if (columnName == "song_id") {
    return sql;
  }
  if (columnName == "filepath") {
    sql += " NOT NULL UNIQUE";
  } else if (columnName == "title") {
    sql += " NOT NULL";
  }
  return sql;
}

bool execOrWarn(QSqlQuery &query, const QString &sql, const char *context) {
  if (query.exec(sql)) {
    return true;
  }
  qWarning() << context << query.lastError();
  return false;
}
} // namespace

DatabaseManager::DatabaseManager(const ColumnRegistry &columnRegistry,
                                 QString databaseName, QString connectionName)
    : columnRegistry_(columnRegistry),
      db_(QSqlDatabase::addDatabase("QSQLITE", connectionName)) {
  db_.setDatabaseName(databaseName);
  if (!db_.open()) {
    qFatal("Could not open SQLite database: %s",
           db_.lastError().text().toUtf8().data());
  }

  applyPragmas();
  if (!createTables()) {
    qFatal("Failed to create database tables.");
  }
}

DatabaseManager::~DatabaseManager() {
  const QString connectionName = db_.connectionName();
  if (connectionName.isEmpty()) {
    return;
  }
  db_.close();
  db_ = QSqlDatabase();
  QSqlDatabase::removeDatabase(connectionName);
}

QSqlDatabase &DatabaseManager::db() { return db_; }

void DatabaseManager::applyPragmas() {
  QSqlQuery q(db_);
  q.exec("PRAGMA foreign_keys = ON;");
  q.exec("PRAGMA journal_mode = WAL;");
  q.exec("PRAGMA synchronous = NORMAL;");
  q.exec("PRAGMA temp_store = MEMORY;");
}

bool DatabaseManager::createTables() {
  QSqlQuery q(db_);

  return ensureSongsSchema(q) && ensurePlaylistsSchema(q) &&
         ensureDynamicAttributesSchema(q) && ensureComputedAttributesSchema(q);
}

bool DatabaseManager::ensureSongsSchema(QSqlQuery &q) {
  QStringList songsColumns;
  songsColumns.push_back("song_id INTEGER PRIMARY KEY AUTOINCREMENT");
  for (const ColumnDefinition &definition :
       columnRegistry_.songAttributeDefinitions()) {
    songsColumns.push_back(songsColumnDefinition(definition));
  }

  const QString createSongsSql =
      QString("CREATE TABLE IF NOT EXISTS songs (%1);")
          .arg(songsColumns.join(", "));
  if (!q.exec(createSongsSql)) {
    qWarning() << "Error creating songs table:" << q.lastError();
    return false;
  }

  QSet<QString> existingSongsColumns;
  if (!q.exec("PRAGMA table_info(songs)")) {
    qWarning() << "Error reading songs table schema:" << q.lastError();
    return false;
  }
  while (q.next()) {
    existingSongsColumns.insert(q.value(1).toString());
  }

  for (const ColumnDefinition &definition :
       columnRegistry_.songAttributeDefinitions()) {
    const QString songsColumn = definition.id;
    if (existingSongsColumns.contains(songsColumn)) {
      continue;
    }

    const QString sql = QString("ALTER TABLE songs ADD COLUMN %1 %2;")
                            .arg(songsColumn, toSqlType(definition.valueType));
    if (!q.exec(sql)) {
      qWarning() << "Error adding songs column" << songsColumn << ":"
                 << q.lastError();
      return false;
    }
    existingSongsColumns.insert(songsColumn);
  }

  return true;
}

bool DatabaseManager::ensurePlaylistsSchema(QSqlQuery &q) {
  if (!execOrWarn(q,
                  R"(
        CREATE TABLE IF NOT EXISTS playlists (
            playlist_id  INTEGER PRIMARY KEY AUTOINCREMENT,
            name         TEXT NOT NULL,
            last_played  INTEGER NOT NULL
        );
    )",
                  "Error creating playlists table:")) {
    return false;
  }

  if (!execOrWarn(q,
                  R"(
        CREATE TABLE IF NOT EXISTS playlist_items (
            playlist_item_id INTEGER PRIMARY KEY AUTOINCREMENT,
            playlist_id      INTEGER NOT NULL,
            song_id          INTEGER NOT NULL,
            position         INTEGER NOT NULL,

            FOREIGN KEY (playlist_id) REFERENCES playlists(playlist_id) ON DELETE CASCADE,
            FOREIGN KEY (song_id)     REFERENCES songs(song_id)         ON DELETE CASCADE
        );
    )",
                  "Error creating playlist_items table:")) {
    return false;
  }

  q.exec(
      R"(CREATE INDEX IF NOT EXISTS idx_playlist_items_pid_pos ON playlist_items(playlist_id, position);)");

  q.exec(
      R"(CREATE INDEX IF NOT EXISTS idx_playlist_items_songid ON playlist_items(song_id);)");

  return true;
}

bool DatabaseManager::ensureDynamicAttributesSchema(QSqlQuery &q) {
  if (!execOrWarn(q,
                  R"(
        CREATE TABLE IF NOT EXISTS song_attributes (
            song_id     INTEGER NOT NULL,
            key         TEXT NOT NULL,
            value_text  TEXT,
            value_type  TEXT NOT NULL,
            PRIMARY KEY (song_id, key),
            FOREIGN KEY (song_id) REFERENCES songs(song_id) ON DELETE CASCADE
        );
    )",
                  "Error creating song_attributes table:")) {
    return false;
  }

  if (!execOrWarn(q,
                  R"(
        CREATE TABLE IF NOT EXISTS attribute_definitions (
            key              TEXT PRIMARY KEY,
            display_name     TEXT NOT NULL,
            value_type       TEXT NOT NULL,
            source           TEXT NOT NULL,
            sortable         INTEGER NOT NULL DEFAULT 1,
            filterable       INTEGER NOT NULL DEFAULT 1,
            visible_default  INTEGER NOT NULL DEFAULT 0,
            width_default    INTEGER NOT NULL DEFAULT 140,
            enum_values_json TEXT
        );
    )",
                  "Error creating attribute_definitions table:")) {
    return false;
  }

  q.exec(
      R"(CREATE INDEX IF NOT EXISTS idx_song_attrs_key_text ON song_attributes(key, value_text);)");
  q.exec(
      R"(CREATE INDEX IF NOT EXISTS idx_song_attrs_key_type ON song_attributes(key, value_type);)");

  return true;
}

bool DatabaseManager::ensureComputedAttributesSchema(QSqlQuery &q) {
  if (!execOrWarn(q,
                  R"(
        CREATE TABLE IF NOT EXISTS computed_attribute_definitions (
            key              TEXT PRIMARY KEY,
            display_name     TEXT NOT NULL,
            value_type       TEXT NOT NULL,
            expression       TEXT NOT NULL,
            sortable         INTEGER NOT NULL DEFAULT 1,
            visible_default  INTEGER NOT NULL DEFAULT 0,
            width_default    INTEGER NOT NULL DEFAULT 140,
            updated_at       INTEGER NOT NULL
        );
    )",
                  "Error creating computed_attribute_definitions table:")) {
    return false;
  }

  if (!execOrWarn(q,
                  R"(
        CREATE TABLE IF NOT EXISTS song_computed_attributes (
            song_id     INTEGER NOT NULL,
            key         TEXT NOT NULL,
            value_text  TEXT,
            value_type  TEXT NOT NULL,
            PRIMARY KEY (song_id, key),
            FOREIGN KEY (song_id) REFERENCES songs(song_id) ON DELETE CASCADE
        );
    )",
                  "Error creating song_computed_attributes table:")) {
    return false;
  }

  q.exec(R"(
      CREATE INDEX IF NOT EXISTS idx_song_computed_attrs_key_text
      ON song_computed_attributes(key, value_text);
  )");
  q.exec(R"(
      CREATE INDEX IF NOT EXISTS idx_song_computed_attrs_key_type
      ON song_computed_attributes(key, value_type);
  )");

  return true;
}

int DatabaseManager::scalarInt(const QString &sql) {
  QSqlQuery q(db_);
  if (!q.exec(sql) || !q.next())
    return 0;
  return q.value(0).toInt();
}
