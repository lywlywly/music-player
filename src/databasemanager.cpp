#include "databasemanager.h"
#include <QDebug>
#include <QSqlError>
#include <QSqlQuery>

QSqlDatabase &DatabaseManager::db() {
  static QSqlDatabase connection = init();
  return connection;
}

QSqlDatabase DatabaseManager::init() {
  QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE");
  db.setDatabaseName("myplayer.db");

  if (!db.open()) {
    qFatal("Could not open SQLite database: %s",
           db.lastError().text().toUtf8().data());
  }

  applyPragmas(db);

  if (!db.tables().contains("songs")) {
    qDebug() << "Database appears to be new. Creating tables...";
    if (!createTables(db)) {
      qFatal("Failed to create database tables.");
    }
  }

  return db;
}

void DatabaseManager::applyPragmas(QSqlDatabase &db) {
  QSqlQuery q(db);
  q.exec("PRAGMA foreign_keys = ON;");
  q.exec("PRAGMA journal_mode = WAL;");
  q.exec("PRAGMA synchronous = NORMAL;");
  q.exec("PRAGMA temp_store = MEMORY;");
}

bool DatabaseManager::createTables(QSqlDatabase &db) {
  QSqlQuery q(db);

  if (!q.exec(R"(
        CREATE TABLE IF NOT EXISTS songs (
            song_id      INTEGER PRIMARY KEY AUTOINCREMENT,
            title        TEXT NOT NULL,
            artist       TEXT,
            album        TEXT,
            filepath     TEXT NOT NULL UNIQUE
        );
    )")) {
    qWarning() << "Error creating songs table:" << q.lastError();
    return false;
  }

  if (!q.exec(R"(
        CREATE TABLE IF NOT EXISTS playlists (
            playlist_id  INTEGER PRIMARY KEY AUTOINCREMENT,
            name         TEXT NOT NULL,
            last_played  INTEGER NOT NULL
        );
    )")) {
    qWarning() << "Error creating playlists table:" << q.lastError();
    return false;
  }

  if (!q.exec(R"(
        CREATE TABLE IF NOT EXISTS playlist_items (
            playlist_item_id INTEGER PRIMARY KEY AUTOINCREMENT,
            playlist_id      INTEGER NOT NULL,
            song_id          INTEGER NOT NULL,
            position         INTEGER NOT NULL,

            FOREIGN KEY (playlist_id) REFERENCES playlists(playlist_id) ON DELETE CASCADE,
            FOREIGN KEY (song_id)     REFERENCES songs(song_id)         ON DELETE CASCADE
        );
    )")) {
    qWarning() << "Error creating playlist_items table:" << q.lastError();
    return false;
  }

  q.exec(
      R"(CREATE INDEX IF NOT EXISTS idx_playlist_items_pid_pos ON playlist_items(playlist_id, position);)");

  q.exec(
      R"(CREATE INDEX IF NOT EXISTS idx_playlist_items_songid ON playlist_items(song_id);)");

  return true;
}

int DatabaseManager::scalarInt(QSqlDatabase &db, const QString &sql) {
  QSqlQuery q(db);
  if (!q.exec(sql) || !q.next())
    return 0;
  return q.value(0).toInt();
}
