#ifndef TRACKINFO_H
#define TRACKINFO_H

#include <QHash>
#include <QString>

// A song as plugins see it: plain text the host copies from the app's song,
// so plugins need no app type (docs/plugins.md, "The boundary").
struct TrackInfo {
  // Column fields by id (e.g. "title", "artist", "attr:lyrics"), as text.
  QHash<QString, QString> fields;
  // Tag fields with no column, from the song's tag refresh on play.
  QHash<QString, QString> tagFields;
};

#endif // TRACKINFO_H
