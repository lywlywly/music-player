#include <QObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTest>

#include "../columnregistry.h"

class TestColumnRegistry : public QObject {
  Q_OBJECT

private slots:
  void builtInDefinitions_areAvailable();
  void builtInSongAttributeKey_onlyTrueForBuiltIns();
  void loadDynamicColumns_loadsAndNormalizes();
  void loadDynamicColumns_replacesRemovedDynamicColumns();
  void loadDynamicColumns_loadsComputedDefinitions();
};

void TestColumnRegistry::builtInDefinitions_areAvailable() {
  ColumnRegistry registry;

  QVERIFY(registry.hasColumn("status"));
  QVERIFY(registry.hasColumn("artist"));
  QVERIFY(registry.hasColumn("album"));
  QVERIFY(registry.hasColumn("discnumber"));
  QVERIFY(registry.hasColumn("tracknumber"));
  QVERIFY(registry.hasColumn("title"));
  QVERIFY(registry.hasColumn("date"));
  QVERIFY(registry.hasColumn("genre"));
  QVERIFY(registry.hasColumn("filepath"));

  const ColumnDefinition *status = registry.findColumn("status");
  QVERIFY(status != nullptr);
  QCOMPARE(status->source, ColumnSource::Computed);
  QVERIFY(!status->sortable);
}

void TestColumnRegistry::builtInSongAttributeKey_onlyTrueForBuiltIns() {
  ColumnRegistry registry;

  QVERIFY(registry.isBuiltInSongAttributeKey("title"));
  QVERIFY(registry.isBuiltInSongAttributeKey("filepath"));
  QVERIFY(!registry.isBuiltInSongAttributeKey("status"));
  QVERIFY(!registry.isBuiltInSongAttributeKey("attr:rating"));
  QVERIFY(!registry.isBuiltInSongAttributeKey("not_exists"));
}

void TestColumnRegistry::loadDynamicColumns_loadsAndNormalizes() {
  ColumnRegistry registry;
  const QString connectionName = "test_columnregistry_connection";
  {
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connectionName);
    db.setDatabaseName(":memory:");
    QVERIFY2(db.open(), "failed to open in-memory sqlite database");

    QSqlQuery q(db);
    QVERIFY(q.exec("CREATE TABLE attribute_definitions ("
                   "key TEXT PRIMARY KEY,"
                   "display_name TEXT NOT NULL,"
                   "value_type TEXT NOT NULL,"
                   "source TEXT NOT NULL,"
                   "sortable INTEGER NOT NULL DEFAULT 1,"
                   "filterable INTEGER NOT NULL DEFAULT 1,"
                   "visible_default INTEGER NOT NULL DEFAULT 0,"
                   "width_default INTEGER NOT NULL DEFAULT 140,"
                   "enum_values_json TEXT"
                   ")"));

    QVERIFY(q.exec(
        "INSERT INTO attribute_definitions"
        "(key, display_name, value_type, source, sortable, visible_default, "
        "width_default) "
        "VALUES ('date_added', 'Date Added', 'date', 'user', 1, 0, 200)"));
    QVERIFY(q.exec(
        "INSERT INTO attribute_definitions"
        "(key, display_name, value_type, source, sortable, visible_default, "
        "width_default) "
        "VALUES ('rate', 'Rate', 'number', 'user', 1, 1, 0)"));
    QVERIFY(q.exec(
        "INSERT INTO attribute_definitions"
        "(key, display_name, value_type, source, sortable, visible_default, "
        "width_default) "
        "VALUES ('computed_score', 'Computed Score', 'number', 'computed', 1, "
        "1, 100)"));

    QVERIFY(registry.loadDynamicColumns(db));

    const ColumnDefinition *dateAdded = registry.findColumn("attr:date_added");
    QVERIFY(dateAdded != nullptr);
    QCOMPARE(dateAdded->title, QString("Date Added"));
    QCOMPARE(dateAdded->source, ColumnSource::SongAttribute);
    QCOMPARE(dateAdded->valueType, ColumnValueType::DateTime);
    QVERIFY(dateAdded->sortable);
    QVERIFY(!dateAdded->visibleByDefault);
    QCOMPARE(dateAdded->defaultWidth, 200);

    const ColumnDefinition *rate = registry.findColumn("attr:rate");
    QVERIFY(rate != nullptr);
    QCOMPARE(rate->valueType, ColumnValueType::Number);
    QVERIFY(rate->visibleByDefault);
    QCOMPARE(rate->defaultWidth, 140);

    QVERIFY(registry.findColumn("attr:computed_score") == nullptr);
  }

  QSqlDatabase::removeDatabase(connectionName);
}

void TestColumnRegistry::loadDynamicColumns_replacesRemovedDynamicColumns() {
  ColumnRegistry registry;
  const QString connectionName = "test_columnregistry_reload_connection";
  {
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connectionName);
    db.setDatabaseName(":memory:");
    QVERIFY2(db.open(), "failed to open in-memory sqlite database");

    QSqlQuery q(db);
    QVERIFY(q.exec("CREATE TABLE attribute_definitions ("
                   "key TEXT PRIMARY KEY,"
                   "display_name TEXT NOT NULL,"
                   "value_type TEXT NOT NULL,"
                   "source TEXT NOT NULL,"
                   "sortable INTEGER NOT NULL DEFAULT 1,"
                   "filterable INTEGER NOT NULL DEFAULT 1,"
                   "visible_default INTEGER NOT NULL DEFAULT 0,"
                   "width_default INTEGER NOT NULL DEFAULT 140,"
                   "enum_values_json TEXT"
                   ")"));
    QVERIFY(q.exec("INSERT INTO attribute_definitions"
                   "(key, display_name, value_type, source) "
                   "VALUES ('rating', 'Rating', 'number', 'custom_tag')"));

    QVERIFY(registry.loadDynamicColumns(db));
    QVERIFY(registry.findColumn("attr:rating") != nullptr);

    QVERIFY(q.exec("DELETE FROM attribute_definitions WHERE key='rating'"));
    QVERIFY(registry.loadDynamicColumns(db));
    QVERIFY(registry.findColumn("attr:rating") == nullptr);
  }

  QSqlDatabase::removeDatabase(connectionName);
}

void TestColumnRegistry::loadDynamicColumns_loadsComputedDefinitions() {
  ColumnRegistry registry;
  const QString connectionName = "test_columnregistry_computed_connection";
  {
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connectionName);
    db.setDatabaseName(":memory:");
    QVERIFY2(db.open(), "failed to open in-memory sqlite database");

    QSqlQuery q(db);
    QVERIFY(q.exec("CREATE TABLE attribute_definitions ("
                   "key TEXT PRIMARY KEY,"
                   "display_name TEXT NOT NULL,"
                   "value_type TEXT NOT NULL,"
                   "source TEXT NOT NULL,"
                   "sortable INTEGER NOT NULL DEFAULT 1,"
                   "filterable INTEGER NOT NULL DEFAULT 1,"
                   "visible_default INTEGER NOT NULL DEFAULT 0,"
                   "width_default INTEGER NOT NULL DEFAULT 140,"
                   "enum_values_json TEXT"
                   ")"));
    QVERIFY(q.exec("CREATE TABLE computed_attribute_definitions ("
                   "key TEXT PRIMARY KEY,"
                   "display_name TEXT NOT NULL,"
                   "value_type TEXT NOT NULL,"
                   "expression TEXT NOT NULL,"
                   "sortable INTEGER NOT NULL DEFAULT 1,"
                   "visible_default INTEGER NOT NULL DEFAULT 0,"
                   "width_default INTEGER NOT NULL DEFAULT 140,"
                   "updated_at INTEGER NOT NULL"
                   ")"));
    QVERIFY(q.exec("INSERT INTO computed_attribute_definitions"
                   "(key, display_name, value_type, expression, sortable, "
                   "visible_default, width_default, updated_at) "
                   "VALUES ('era', 'Era', 'text', 'IF date < 2000 THEN classic "
                   "ELSE new', 1, 1, 150, 1)"));

    QVERIFY(registry.loadDynamicColumns(db));
    const ColumnDefinition *era = registry.findColumn("era");
    QVERIFY(era != nullptr);
    QCOMPARE(era->source, ColumnSource::Computed);
    QCOMPARE(era->expression, QString("IF date < 2000 THEN classic ELSE new"));
    QCOMPARE(era->valueType, ColumnValueType::Text);
    QVERIFY(era->visibleByDefault);
  }

  QSqlDatabase::removeDatabase(connectionName);
}

QTEST_MAIN(TestColumnRegistry)
#include "tst_columnregistry.moc"
