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
SongLibrary::Snapshot
loadSongLibrarySnapshot(const QString &databaseName,
                        const std::vector<ProvidedFields> &sources) {
  // TODO: Extract DB-only library loading so startup does not need to construct
  // temporary full ColumnRegistry/SongLibrary instances on the worker thread.
#ifdef MYPLAYER_TESTING
  if (const int delayMs =
          qEnvironmentVariableIntValue("MYPLAYER_TEST_LIBRARY_LOAD_DELAY_MS");
      delayMs > 0) {
    QThread::msleep(static_cast<unsigned long>(delayMs));
  }
#endif
  // Each source's values are read beside the songs, as they need nothing
  // from them (docs/plugins/playstats.md, "Startup"). waitForFinished runs a
  // task not started yet in this thread, so a pool with one thread cannot
  // deadlock.
  std::vector<QFuture<ProvidedValuesByKey>> reads;
  reads.reserve(sources.size());
  for (const ProvidedFields &source : sources) {
    reads.push_back(QtConcurrent::run(source.allValues));
  }
  ColumnRegistry registry;
  DatabaseManager databaseManager(
      registry, databaseName,
      QStringLiteral("playlist_loader_%1")
          .arg(reinterpret_cast<quintptr>(QThread::currentThread())));
  if (!registry.loadDynamicColumns(databaseManager.db())) {
    qFatal("loadSongLibrarySnapshot: failed to load dynamic columns");
  }
  SongLibrary library(registry, databaseManager);
  for (const ProvidedFields &source : sources) {
    library.addProvidedSource(source);
  }
  library.loadFromDatabase();
  for (size_t i = 0; i < sources.size(); ++i) {
    reads[i].waitForFinished();
    library.fillProvidedValues(sources[i].sourceId, reads[i].result());
  }
  return library.takeSnapshot();
}
} // namespace

QFuture<void> loadLibrary(ColumnRegistry &columnRegistry,
                          DatabaseManager &databaseManager,
                          SongLibrary &library, QObject *context,
                          std::function<void()> onLoaded) {
  QSqlDatabase &db = databaseManager.db();
  if (db.databaseName() == QStringLiteral(":memory:")) {
    if (!columnRegistry.loadDynamicColumns(db)) {
      qFatal("loadLibrary: failed to load dynamic columns");
    }
    library.loadFromDatabase();
    for (const ProvidedFields &source : library.providedSources()) {
      library.fillProvidedValues(source.sourceId, source.allValues());
    }
    onLoaded();
    return {};
  }

  library.beginLoad();
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
        library.replaceWithSnapshot(watcher->future().takeResult());
        watcher->deleteLater();
        onLoaded();
      });
  QFuture<SongLibrary::Snapshot> future = QtConcurrent::run(
      loadSongLibrarySnapshot, db.databaseName(), library.providedSources());
  watcher->setFuture(future);
  return QFuture<void>(future);
}
