#include "libraryloader.h"

#include "columnregistry.h"
#include "databasemanager.h"
#include "songlibrary.h"
#include <QFutureWatcher>
#include <QObject>
#include <QSqlDatabase>
#include <QThread>
#include <QtConcurrent>
#include <utility>

namespace {
// Runs on the worker thread.
SongLibrary::Snapshot loadSongLibrarySnapshot(const QString &databaseName) {
  // TODO: Extract DB-only library loading so startup does not need to construct
  // temporary full ColumnRegistry/SongLibrary instances on the worker thread.
#ifdef MYPLAYER_TESTING
  if (const int delayMs =
          qEnvironmentVariableIntValue("MYPLAYER_TEST_LIBRARY_LOAD_DELAY_MS");
      delayMs > 0) {
    QThread::msleep(static_cast<unsigned long>(delayMs));
  }
#endif
  ColumnRegistry registry;
  DatabaseManager databaseManager(
      registry, databaseName,
      QStringLiteral("playlist_loader_%1")
          .arg(reinterpret_cast<quintptr>(QThread::currentThread())));
  if (!registry.loadDynamicColumns(databaseManager.db())) {
    qFatal("loadSongLibrarySnapshot: failed to load dynamic columns");
  }
  SongLibrary library(registry, databaseManager);
  library.loadFromDatabase();
  return library.snapshot();
}
} // namespace

void loadLibrary(ColumnRegistry &columnRegistry,
                 DatabaseManager &databaseManager, SongLibrary &library,
                 QObject *context, std::function<void()> onLoaded) {
  QSqlDatabase &db = databaseManager.db();
  if (db.databaseName() == QStringLiteral(":memory:")) {
    if (!columnRegistry.loadDynamicColumns(db)) {
      qFatal("loadLibrary: failed to load dynamic columns");
    }
    library.loadFromDatabase();
    onLoaded();
    return;
  }

  // A child of context, so it is destroyed with it.
  auto *watcher = new QFutureWatcher<SongLibrary::Snapshot>(context);
  QObject::connect(
      watcher, &QFutureWatcher<SongLibrary::Snapshot>::finished, watcher,
      [&columnRegistry, &databaseManager, &library, watcher,
       onLoaded = std::move(onLoaded)]() {
        // The worker loaded the columns into its own registry; load them
        // into the app's registry too before the songs that use them.
        if (!columnRegistry.loadDynamicColumns(databaseManager.db())) {
          qFatal("loadLibrary: failed to load dynamic columns");
        }
        library.replaceWithSnapshot(watcher->result());
        watcher->deleteLater();
        onLoaded();
      });
  watcher->setFuture(
      QtConcurrent::run(loadSongLibrarySnapshot, db.databaseName()));
}
