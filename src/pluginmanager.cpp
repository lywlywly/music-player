#include "pluginmanager.h"

#include "iplugin.h"
#include <QDebug>
#include <QDir>
#include <QHash>
#include <QJsonObject>
#include <QLibrary>
#include <QPluginLoader>
#include <QSettings>
#include <utility>

namespace {
constexpr const char *kDisabledPluginsSettingsKey = "plugins/disabled";

// Marks entry as skipped and logs why. Without details, the reason is the
// details.
void skip(PluginEntry &entry, const QString &path, const QString &reason,
          const QString &details = {}) {
  entry.state = PluginEntry::State::Skipped;
  entry.reason = reason;
  entry.details = details.isEmpty() ? reason : details;
  qWarning().noquote() << "Plugin skipped:" << path << "-" << entry.details;
}
} // namespace

QStringList loadDisabledPluginIds() {
  return QSettings().value(kDisabledPluginsSettingsKey).toStringList();
}

void saveDisabledPluginIds(const QStringList &ids) {
  QSettings().setValue(kDisabledPluginsSettingsKey, ids);
}

PluginManager::PluginManager(PlaybackController &playback,
                             const QString &folder, QObject *parent)
    : QObject(parent), player_(playback) {
  const QStringList disabled = loadDisabledPluginIds();
  const QDir dir(folder);
  // The file of each loaded or disabled entry, by id: the id names the
  // plugin's tab, its settings keys and its place in the disabled list. A
  // plugin that failed to load does not hold its id, so a later copy can
  // load.
  QHash<QString, QString> fileById;
  for (const QString &fileName : dir.entryList(QDir::Files, QDir::Name)) {
    const QString path = dir.filePath(fileName);
    if (!QLibrary::isLibrary(path)) {
      continue;
    }
    PluginEntry &entry = plugins_.emplace_back();
    entry.fileName = fileName;
    QPluginLoader loader(path);
    // metaData() parses the file without running its code; it is empty for a
    // library that is not a Qt plugin, or one of another Qt version or build
    // type.
    const QJsonObject metaData = loader.metaData();
    if (metaData.isEmpty()) {
      skip(entry, path, tr("not a plugin for this app"), loader.errorString());
      continue;
    }
    const QString iid = metaData.value("IID").toString();
    if (iid != QLatin1String(IPlugin_iid)) {
      skip(entry, path, tr("another plugin API"),
           tr("built for plugin API %1, the app has %2").arg(iid, IPlugin_iid));
      continue;
    }
    const QJsonObject declared = metaData.value("MetaData").toObject();
    entry.id = declared.value("id").toString();
    entry.name = declared.value("name").toString();
    if (entry.id.isEmpty() || entry.name.isEmpty()) {
      skip(entry, path, tr("no id or name in its metadata"));
      continue;
    }
    if (fileById.contains(entry.id)) {
      skip(entry, path, tr("same id as %1").arg(fileById.value(entry.id)));
      continue;
    }
    if (disabled.contains(entry.id)) {
      entry.state = PluginEntry::State::Disabled;
      fileById.insert(entry.id, fileName);
      continue;
    }
    QObject *root = loader.instance();
    if (root == nullptr) {
      skip(entry, path, tr("failed to load"), loader.errorString());
      continue;
    }
    IPlugin *plugin = qobject_cast<IPlugin *>(root);
    if (plugin == nullptr) {
      skip(entry, path, tr("not a plugin for this app"),
           tr("does not implement IPlugin"));
      continue;
    }
    plugin->initialize(*this);
    entry.state = PluginEntry::State::Loaded;
    entry.root = root;
    fileById.insert(entry.id, fileName);
  }
}

PluginManager::~PluginManager() {
  for (const PluginEntry &entry : std::as_const(plugins_)) {
    if (entry.state == PluginEntry::State::Loaded) {
      qobject_cast<IPlugin *>(entry.root)->shutdown();
    }
  }
}

IPlayerHost &PluginManager::player() { return player_; }

const QList<PluginEntry> &PluginManager::plugins() const { return plugins_; }
