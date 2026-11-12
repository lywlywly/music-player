#include "pluginmanager.h"

#include "iplugin.h"
#include <QDebug>
#include <QDir>
#include <QJsonObject>
#include <QLibrary>
#include <QPluginLoader>
#include <QSet>

PluginManager::PluginManager(PlaybackController &playback,
                             const QString &folder, QObject *parent)
    : QObject(parent), player_(playback) {
  const QDir dir(folder);
  QSet<QString> ids;
  for (const QString &name : dir.entryList(QDir::Files, QDir::Name)) {
    const QString path = dir.filePath(name);
    if (!QLibrary::isLibrary(path)) {
      continue;
    }
    QPluginLoader loader(path);
    // metaData() reads the IID without loading the plugin's code; it is
    // empty for a library that is not a Qt plugin.
    const QJsonObject metaData = loader.metaData();
    if (metaData.isEmpty()) {
      qWarning() << "Plugin skipped:" << loader.errorString();
      continue;
    }
    const QString iid = metaData.value("IID").toString();
    if (iid != QLatin1String(IPlugin_iid)) {
      qWarning() << "Plugin skipped:" << path << "has IID" << iid
                 << "instead of" << IPlugin_iid;
      continue;
    }
    QObject *root = loader.instance();
    if (root == nullptr) {
      qWarning() << "Plugin failed to load:" << loader.errorString();
      continue;
    }
    IPlugin *plugin = qobject_cast<IPlugin *>(root);
    if (plugin == nullptr) {
      qWarning() << "Plugin skipped:" << path << "does not implement IPlugin";
      continue;
    }
    if (ids.contains(plugin->id())) {
      qWarning() << "Plugin skipped:" << path << "has the id" << plugin->id()
                 << "of a plugin already loaded";
      continue;
    }
    ids.insert(plugin->id());
    plugin->initialize(*this);
    plugins_.append(root);
  }
}

PluginManager::~PluginManager() {
  for (QObject *root : plugins_) {
    qobject_cast<IPlugin *>(root)->shutdown();
  }
}

IPlayerHost &PluginManager::player() { return player_; }

const QList<QObject *> &PluginManager::plugins() const { return plugins_; }
