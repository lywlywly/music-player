#ifndef DATABASEMANAGER_H
#define DATABASEMANAGER_H

#include "columnregistry.h"
#include <QSqlDatabase>
#include <QString>
class QSqlQuery;

// Owns the app's SQLite connection and schema setup. It opens the DB, applies
// pragmas, and ensures required tables/indexes exist.
class DatabaseManager {
public:
  explicit DatabaseManager(const ColumnRegistry &columnRegistry,
                           QString databaseName = "myplayer.db",
                           QString connectionName = "myplayer_main");
  ~DatabaseManager();
  DatabaseManager(const DatabaseManager &) = delete;
  DatabaseManager &operator=(const DatabaseManager &) = delete;

  QSqlDatabase &db();
  int scalarInt(const QString &sql);

private:
  void applyPragmas();
  bool createTables();
  bool ensureSongsSchema(QSqlQuery &q);
  bool ensurePlaylistsSchema(QSqlQuery &q);
  bool ensureDynamicAttributesSchema(QSqlQuery &q);

  const ColumnRegistry &columnRegistry_;
  QSqlDatabase db_;
};

#endif // DATABASEMANAGER_H
