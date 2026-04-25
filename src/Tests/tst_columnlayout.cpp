#include <QObject>
#include <QSettings>
#include <QTest>
#include <QUuid>

#include "../columnregistry.h"
#include "../globalcolumnlayoutmanager.h"

class TestColumnLayout : public QObject {
  Q_OBJECT

private slots:
  void init();
  void cleanup();

  void persistsOrderVisibilityAndWidth();
  void refreshFromRegistry_addsNewColumnWithDefaultVisibility();
  void refreshFromRegistry_removesUnknownIdsAndKeepsVisibleColumn();

private:
  QString orgName_;
  QString appName_;
};

void TestColumnLayout::init() {
  orgName_ = QStringLiteral("music-player-tests-%1")
                 .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
  appName_ = QStringLiteral("column-layout-tests-%1")
                 .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
  QCoreApplication::setOrganizationName(orgName_);
  QCoreApplication::setApplicationName(appName_);
  QSettings settings;
  settings.clear();
}

void TestColumnLayout::cleanup() {
  QSettings settings;
  settings.clear();
}

void TestColumnLayout::persistsOrderVisibilityAndWidth() {
  ColumnRegistry registry;
  GlobalColumnLayoutManager manager(registry);

  manager.setColumnVisible("genre", true);
  manager.setColumnWidth("artist", 222);
  manager.setOrder({"title", "artist", "status"});

  GlobalColumnLayoutManager loaded(registry);
  QCOMPARE(loaded.isColumnVisible("genre"), true);
  QCOMPARE(loaded.columnWidth("artist"), 222);
  const QList<QString> ids = loaded.allOrderedColumnIds();
  QVERIFY(ids.size() >= 3);
  QCOMPARE(ids[0], QString("title"));
  QCOMPARE(ids[1], QString("artist"));
  QCOMPARE(ids[2], QString("status"));
}

void TestColumnLayout::
    refreshFromRegistry_addsNewColumnWithDefaultVisibility() {
  ColumnRegistry registry;
  GlobalColumnLayoutManager manager(registry);

  registry.addOrUpdateDynamicColumn(
      {"attr:rating", "Rating", ColumnSource::SongAttribute,
       ColumnValueType::Number, "", true, false, 120});
  manager.refreshFromRegistry();

  QVERIFY(manager.allOrderedColumnIds().contains("attr:rating"));
  QCOMPARE(manager.isColumnVisible("attr:rating"), false);
}

void TestColumnLayout::
    refreshFromRegistry_removesUnknownIdsAndKeepsVisibleColumn() {
  QSettings settings;
  settings.setValue("playlist/columns/order",
                    QStringList{"status", "artist", "attr:ghost"});
  settings.setValue("playlist/columns/hidden", QStringList{"status", "artist"});

  ColumnRegistry registry;
  GlobalColumnLayoutManager manager(registry);
  manager.refreshFromRegistry();

  QVERIFY(!manager.allOrderedColumnIds().contains("attr:ghost"));
  QVERIFY(!manager.visibleColumnIds().isEmpty());
}

QTEST_MAIN(TestColumnLayout)
#include "tst_columnlayout.moc"
