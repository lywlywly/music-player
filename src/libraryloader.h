#ifndef LIBRARYLOADER_H
#define LIBRARYLOADER_H

#include <functional>

class ColumnRegistry;
class DatabaseManager;
class QObject;
class SongLibrary;

// Loads the custom columns into columnRegistry and the songs into library
// from the database at startup, then calls onLoaded. Call it on context's
// thread; columnRegistry, databaseManager and library must stay valid until
// onLoaded runs or context is destroyed.
//
// A file-backed database loads on a worker thread with its own connection,
// and the result is applied from the event loop; if context is destroyed
// first, nothing is applied and onLoaded is not called. A ":memory:" database
// cannot be opened by a second connection, so it loads, and onLoaded runs,
// before loadLibrary returns.
void loadLibrary(ColumnRegistry &columnRegistry,
                 DatabaseManager &databaseManager, SongLibrary &library,
                 QObject *context, std::function<void()> onLoaded);

#endif // LIBRARYLOADER_H
