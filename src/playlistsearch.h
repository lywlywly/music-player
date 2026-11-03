#ifndef PLAYLISTSEARCH_H
#define PLAYLISTSEARCH_H

#include "libraryexpression.h"
#include "songlibrary.h"
#include <QString>
#include <QStringList>
#include <vector>

class Playlist;

// A Playlist Search query. The mode is decided from the text:
// * Expression: it parses as a condition (parseSongFilter).
// * Text, otherwise, and always when the whole text is in double quotes
//   (which are removed): a song matches when every whitespace-separated word
//   is a case-insensitive substring of the displayed value of at least one of
//   the fields.
// * Error: it fails to parse and contains an expression symbol
//   (= < > ( ) [ ] or a backtick), so a broken expression is not taken as
//   text. Nothing matches.
class SongMatcher {
public:
  enum class Mode { Text, Expression, Error };

  // fieldIds: the fields plain text is matched against (the visible columns);
  // ids that are not song fields are ignored.
  SongMatcher(const SongLibrary &library, const QString &query,
              const QStringList &fieldIds);

  Mode mode() const;
  // The parse error, in Error mode.
  QString error() const;
  bool matches(int songPk) const;

private:
  const SongLibrary &library_;
  Mode mode_ = Mode::Text;
  ExprPtr expr_;
  QString error_;
  QStringList words_;
  std::vector<std::string> fieldIds_;
};

// The pks of the playlist's rows that match, in row order.
std::vector<int> matchPlaylistRows(const Playlist &playlist,
                                   const SongMatcher &matcher);

#endif // PLAYLISTSEARCH_H
