#ifndef ISONGFIELDPROVIDER_H
#define ISONGFIELDPROVIDER_H

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>

// The type of a provided field; the host shows, sorts and filters by it.
enum class SongFieldType {
  Number,
  // Epoch seconds, shown as a local date and time.
  DateTime,
};

struct SongField {
  // Lowercase, e.g. "play_count": the column's id and the expression
  // symbol, so no built-in, custom or computed field may use it.
  QString id;
  // The column title, e.g. tr("Play Count").
  QString title;
  SongFieldType type = SongFieldType::Number;
  int defaultWidth = 140;
  bool visibleByDefault = true;
};

// One value per field of songFields(), in that order; an invalid QVariant
// means no value. Both types are a qint64.
using SongFieldValues = QList<QVariant>;
// Values by key; a key with no entry has no values.
using SongFieldValuesByKey = QHash<QString, SongFieldValues>;

// Capability: song fields whose values the plugin keeps, shown as columns
// and usable in expressions like any other field
// (docs/plugins/plugin-system.md, "Provided song fields"). The host keeps a
// copy of the values on each song and reads them again for the keys the
// plugin reports through ISongFieldHost::valuesChanged.
//
// Threads: songFields and keyFields on the main thread; songKey, values and
// allValues on the main thread or the library load's workers. All between
// initialize and shutdown.
class ISongFieldProvider {
public:
  virtual ~ISongFieldProvider() = default;

  virtual QList<SongField> songFields() const = 0;
  // The built-in tag fields the key is computed from, e.g. title, artist,
  // album.
  virtual QStringList keyFields() const = 0;
  // Songs with the same key share values. keyFieldValues holds the fields'
  // text in keyFields() order (an absent field is an empty string).
  // Deterministic and reentrant.
  virtual QString songKey(const QStringList &keyFieldValues) const = 0;
  // The current values of these keys.
  virtual SongFieldValuesByKey values(const QStringList &keys) const = 0;
  // The current values of every key that has any, for the startup load.
  virtual SongFieldValuesByKey allValues() const = 0;
};

#define ISongFieldProvider_iid "myplayer.ISongFieldProvider/1"
Q_DECLARE_INTERFACE(ISongFieldProvider, ISongFieldProvider_iid)

#endif // ISONGFIELDPROVIDER_H
