#ifndef PROVIDEDFIELDS_H
#define PROVIDEDFIELDS_H

#include "columndefinition.h"
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <functional>
#include <string>
#include <vector>

// Values by key, one per field in ProvidedFields::fields order; an invalid
// QVariant is no value. The plugin API's SongFieldValuesByKey, without its
// header, so SongLibrary stays free of the plugin API.
using ProvidedValuesByKey = QHash<QString, QList<QVariant>>;

// A source of song fields whose values another component keeps (a plugin's
// ISongFieldProvider, wrapped by PluginManager), as SongLibrary uses it
// (docs/plugins/playstats.md, "What SongLibrary keeps"). songKey, values and
// allValues may be called on the library load's worker threads.
struct ProvidedFields {
  struct Field {
    std::string id;
    ValueType type = ValueType::Number;
  };

  // Names the source, e.g. the plugin's id.
  QString sourceId;
  std::vector<Field> fields;
  // Built-in tag fields (`songs` columns) the key is computed from.
  std::vector<std::string> keyFields;
  // The key of a song from its key fields' text, in keyFields order.
  std::function<QString(const QStringList &)> songKey;
  std::function<ProvidedValuesByKey(const QStringList &)> values;
  std::function<ProvidedValuesByKey()> allValues;
};

#endif // PROVIDEDFIELDS_H
