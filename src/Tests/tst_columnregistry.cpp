#include <QObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTest>

#include "../columnregistry.h"
#include "../fieldtypepool.h"

class TestColumnRegistry : public QObject {
  Q_OBJECT

private slots:
  void builtInDefinitions_areAvailable();
  void builtInSongAttributeKey_onlyTrueForBuiltIns();
  void loadDynamicColumns_loadsAndNormalizes();
  void loadDynamicColumns_replacesRemovedDynamicColumns();
  void loadDynamicColumns_loadsComputedDefinitions();
  void fieldTypePool_staysInSyncWithRegistryFields();
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
  QVERIFY(registry.hasColumn("codec"));
  QVERIFY(registry.hasColumn("bitrate"));
  QVERIFY(registry.hasColumn("duration"));
  QVERIFY(registry.hasColumn("sample_rate"));
  QVERIFY(registry.hasColumn("filepath"));
  QVERIFY(registry.hasColumn("channels"));

  const ColumnDefinition *status = registry.findColumn("status");
  QVERIFY(status != nullptr);
  const FieldDefinition *statusField = registry.findField(status->id);
  QVERIFY(statusField != nullptr);
  QCOMPARE(statusField->source, ColumnSource::Computed);
  QVERIFY(!status->sortable);

  const ColumnDefinition *codec = registry.findColumn("codec");
  QVERIFY(codec != nullptr);
  const FieldDefinition *codecField = registry.findField(codec->id);
  QVERIFY(codecField != nullptr);
  QCOMPARE(codecField->source, ColumnSource::SongAttribute);
  QCOMPARE(codecField->valueType, ValueType::Text);
  QVERIFY(!codec->visibleByDefault);

  const ColumnDefinition *bitrate = registry.findColumn("bitrate");
  QVERIFY(bitrate != nullptr);
  QCOMPARE(registry.findField(bitrate->id)->valueType, ValueType::Number);
  QVERIFY(!bitrate->visibleByDefault);

  const ColumnDefinition *duration = registry.findColumn("duration");
  QVERIFY(duration != nullptr);
  const FieldDefinition *durationField = registry.findField(duration->id);
  QVERIFY(durationField != nullptr);
  QCOMPARE(durationField->valueType, ValueType::Number);
  QVERIFY(!duration->visibleByDefault);
  QCOMPARE(durationField->displayKind, DisplayKind::DurationSeconds);

  const ColumnDefinition *sampleRate = registry.findColumn("sample_rate");
  QVERIFY(sampleRate != nullptr);
  QCOMPARE(registry.findField(sampleRate->id)->valueType, ValueType::Number);
  QVERIFY(!sampleRate->visibleByDefault);

  const ColumnDefinition *lastPlayed =
      registry.findColumn("last_played_timestamp");
  QVERIFY(lastPlayed != nullptr);
  QCOMPARE(registry.findField(lastPlayed->id)->displayKind,
           DisplayKind::EpochSecondsDateTime);

  const ColumnDefinition *channels = registry.findColumn("channels");
  QVERIFY(channels != nullptr);
  const FieldDefinition *channelsField = registry.findField(channels->id);
  QVERIFY(channelsField != nullptr);
  QCOMPARE(channelsField->valueType, ValueType::Number);
  QVERIFY(!channels->visibleByDefault);
  QCOMPARE(channelsField->displayKind, DisplayKind::ChannelLayout);
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
    const FieldDefinition *dateAddedField = registry.findField(dateAdded->id);
    QVERIFY(dateAddedField != nullptr);
    QCOMPARE(dateAddedField->source, ColumnSource::SongAttribute);
    QCOMPARE(dateAddedField->valueType, ValueType::DateTime);
    QVERIFY(dateAdded->sortable);
    QVERIFY(!dateAdded->visibleByDefault);
    QCOMPARE(dateAdded->defaultWidth, 200);

    const ColumnDefinition *rate = registry.findColumn("attr:rate");
    QVERIFY(rate != nullptr);
    QCOMPARE(registry.findField(rate->id)->valueType, ValueType::Number);
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
    const ColumnDefinition *era = registry.findColumn("computed:era");
    QVERIFY(era != nullptr);
    const FieldDefinition *eraField = registry.findField(era->id);
    QVERIFY(eraField != nullptr);
    QCOMPARE(eraField->source, ColumnSource::Computed);
    QCOMPARE(eraField->expression,
             QString("IF date < 2000 THEN classic ELSE new"));
    QCOMPARE(eraField->valueType, ValueType::Text);
    QVERIFY(era->visibleByDefault);
  }

  QSqlDatabase::removeDatabase(connectionName);
}

void TestColumnRegistry::fieldTypePool_staysInSyncWithRegistryFields() {
  ColumnRegistry registry;

  for (const FieldDefinition &definition : registry.fieldDefinitions()) {
    const FieldDefinition *pooled =
        FieldTypePool::instance().find(definition.id.toStdString());
    QVERIFY(pooled != nullptr);
    QCOMPARE(pooled->valueType, definition.valueType);
    QCOMPARE(pooled->displayKind, definition.displayKind);
  }
  QVERIFY(FieldTypePool::instance().find("builtin:artist") != nullptr);
  QVERIFY(FieldTypePool::instance().find("computed:status") != nullptr);

  const QString connectionName = "test_columnregistry_pool_sync_connection";
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
    QVERIFY(q.exec("INSERT INTO attribute_definitions"
                   "(key, display_name, value_type, source) "
                   "VALUES ('rating', 'Rating', 'number', 'custom_tag')"));
    QVERIFY(q.exec("INSERT INTO computed_attribute_definitions"
                   "(key, display_name, value_type, expression, updated_at) "
                   "VALUES ('era', 'Era', 'text', 'IF date < 2000 THEN classic "
                   "ELSE new', 1)"));

    QVERIFY(registry.loadDynamicColumns(db));
    QVERIFY(registry.findField("attr:rating") != nullptr);
    QVERIFY(registry.findField("computed:era") != nullptr);
    QVERIFY(FieldTypePool::instance().find("attr:rating") != nullptr);
    QVERIFY(FieldTypePool::instance().find("computed:era") != nullptr);

    QVERIFY(q.exec("DELETE FROM attribute_definitions WHERE key='rating'"));
    QVERIFY(
        q.exec("DELETE FROM computed_attribute_definitions WHERE key='era'"));
    QVERIFY(registry.loadDynamicColumns(db));

    QVERIFY(registry.findField("attr:rating") == nullptr);
    QVERIFY(registry.findField("computed:era") == nullptr);
    QVERIFY(FieldTypePool::instance().find("attr:rating") == nullptr);
    QVERIFY(FieldTypePool::instance().find("computed:era") == nullptr);
  }
  QSqlDatabase::removeDatabase(connectionName);
}

QTEST_MAIN(TestColumnRegistry)
#include "tst_columnregistry.moc"
