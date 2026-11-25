#include "pluginmanager.h"

#include "columnregistry.h"
#include "iaudiohost.h"
#include "iplugin.h"
#include "ipluginhost.h"
#include "isongfieldhost.h"
#include "isongfieldprovider.h"
#include "songlibrary.h"
#include <QDebug>
#include <QDir>
#include <QHash>
#include <QJsonObject>
#include <QLibrary>
#include <QPluginLoader>
#include <QSet>
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

ValueType valueTypeOf(SongFieldType type) {
  switch (type) {
  case SongFieldType::Number:
    return ValueType::Number;
  case SongFieldType::DateTime:
    return ValueType::DateTime;
  }
  Q_UNREACHABLE();
}

DisplayKind displayKindOf(SongFieldType type) {
  switch (type) {
  case SongFieldType::Number:
    return DisplayKind::Raw;
  case SongFieldType::DateTime:
    return DisplayKind::EpochSecondsDateTime;
  }
  Q_UNREACHABLE();
}

// Why the provider's fields cannot be registered, or empty when they can.
QString songFieldsRefusal(const ISongFieldProvider &provider,
                          const ColumnRegistry &registry) {
  QSet<QString> ids;
  for (const SongField &field : provider.songFields()) {
    if (field.id.isEmpty() || field.id != field.id.toLower()) {
      return QStringLiteral("field id \"%1\" is not lowercase").arg(field.id);
    }
    if (registry.isNameInUse(field.id) || ids.contains(field.id)) {
      return QStringLiteral("field id \"%1\" is already used").arg(field.id);
    }
    ids.insert(field.id);
  }
  for (const QString &keyField : provider.keyFields()) {
    if (!registry.isBuiltInSongAttributeKey(keyField)) {
      return QStringLiteral("key field \"%1\" is not a built-in tag field")
          .arg(keyField);
    }
  }
  return {};
}
} // namespace

// The host a plugin gets: its own, so the services that belong to one plugin
// (its data folder, its fields' changes, whether it wants audio) know which
// plugin calls. player() and control() are the manager's shared ones; audio
// reads go to the shared PluginAudioHost.
class PluginHost : public QObject,
                   public IPluginHost,
                   public ISongFieldHost,
                   public IAudioHost {
public:
  PluginHost(PluginManager &manager, PluginPlayerHost &player,
             PluginAudioHost &audio, QString id, QString dataFolder)
      : manager_(manager), player_(player), audio_(audio), id_(std::move(id)),
        dataFolder_(std::move(dataFolder)) {}

  IPlayerHost &player() override { return player_; }
  IPlayerControl &control() override { return player_; }
  QString dataFolder() const override { return dataFolder_; }
  ISongFieldHost &songFieldHost() override { return *this; }
  IAudioHost &audio() override { return *this; }

  // A state, not a count: the plugin counts once however often it says so.
  void setWanted(bool wanted) override {
    if (wanted == audioWanted_) {
      return;
    }
    audioWanted_ = wanted;
    if (wanted) {
      audio_.addUser();
    } else {
      audio_.removeUser();
    }
  }
  int read(std::span<float> out) const override { return audio_.read(out); }

  void valuesChanged(const QStringList &keys) override {
    // Queued, so a call inside a listener call (a song start) refreshes the
    // rows after playback has started the song, not nested inside it, and a
    // call from another thread runs on the main thread
    // (docs/event-ordering.md, "Who queues each edge"). This host is the
    // context: a call still queued when it is destroyed is dropped.
    QMetaObject::invokeMethod(
        this,
        [this, keys]() { emit manager_.providedValuesChanged(id_, keys); },
        Qt::QueuedConnection);
  }

private:
  PluginManager &manager_;
  PluginPlayerHost &player_;
  PluginAudioHost &audio_;
  const QString id_;
  const QString dataFolder_;
  bool audioWanted_ = false;
};

QStringList loadDisabledPluginIds() {
  return QSettings().value(kDisabledPluginsSettingsKey).toStringList();
}

void saveDisabledPluginIds(const QStringList &ids) {
  QSettings().setValue(kDisabledPluginsSettingsKey, ids);
}

PluginManager::PluginManager(PlaybackController &playback,
                             const QString &folder, const QString &dataRoot,
                             QObject *parent)
    : QObject(parent), player_(playback), audio_(playback) {
  const QStringList disabled = loadDisabledPluginIds();
  const QDir dir(folder);
  const QDir dataDir(dataRoot);
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
    PluginHost &host = *hosts_.emplace_back(std::make_unique<PluginHost>(
        *this, player_, audio_, entry.id, dataDir.filePath(entry.id)));
    plugin->initialize(host);
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

const QList<PluginEntry> &PluginManager::plugins() const { return plugins_; }

void PluginManager::registerSongFields(ColumnRegistry &registry,
                                       SongLibrary &library) {
  // Only a Loaded entry has a root object; qobject_cast of the others' null
  // root is null.
  for (const PluginEntry &entry : std::as_const(plugins_)) {
    const auto *provider = qobject_cast<ISongFieldProvider *>(entry.root);
    if (provider == nullptr) {
      continue;
    }
    if (const QString refusal = songFieldsRefusal(*provider, registry);
        !refusal.isEmpty()) {
      qWarning().noquote() << "Plugin song fields refused:" << entry.id << "-"
                           << refusal;
      continue;
    }
    ProvidedFields provided{.sourceId = entry.id};
    for (const SongField &field : provider->songFields()) {
      registry.addPluginField({.id = field.id,
                               .title = field.title,
                               .sortable = true,
                               .visibleByDefault = field.visibleByDefault,
                               .defaultWidth = field.defaultWidth},
                              {.id = field.id,
                               .source = ColumnSource::Plugin,
                               .valueType = valueTypeOf(field.type),
                               .displayKind = displayKindOf(field.type),
                               .searchable = true,
                               .writable = false});
      provided.fields.push_back(
          {.id = field.id.toStdString(), .type = valueTypeOf(field.type)});
    }
    for (const QString &keyField : provider->keyFields()) {
      provided.keyFields.push_back(keyField.toStdString());
    }
    // The provider lives until shutdown, after every call SongLibrary makes
    // (docs/plugins/playstats.md, "Lifetime").
    provided.songKey = [provider](const QStringList &keyFieldValues) {
      return provider->songKey(keyFieldValues);
    };
    provided.values = [provider](const QStringList &keys) {
      return provider->values(keys);
    };
    provided.allValues = [provider]() { return provider->allValues(); };
    library.addProvidedSource(std::move(provided));
  }
}
