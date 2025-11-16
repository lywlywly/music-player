#ifndef DATABASEMANAGER_H
#define DATABASEMANAGER_H

#include <QSqlDatabase>

class DatabaseManager {
public:
  static QSqlDatabase &db();
  static int scalarInt(QSqlDatabase &db, const QString &sql);

private:
  static QSqlDatabase init();
  static void applyPragmas(QSqlDatabase &db);
  static bool createTables(QSqlDatabase &db);
};

#endif // DATABASEMANAGER_H
