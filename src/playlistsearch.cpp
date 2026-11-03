#include "playlistsearch.h"
#include "playlist.h"
#include <QRegularExpression>
#include <algorithm>
#include <optional>

namespace {
// The text inside the quotes when the whole text is one double-quoted
// string, otherwise nullopt.
std::optional<QString> quotedText(const QString &text) {
  const QString trimmed = text.trimmed();
  if (trimmed.size() < 2 || !trimmed.startsWith('"') ||
      !trimmed.endsWith('"')) {
    return std::nullopt;
  }
  const QString inner = trimmed.mid(1, trimmed.size() - 2);
  if (inner.contains('"')) {
    return std::nullopt;
  }
  return inner;
}

bool containsExpressionSymbol(const QString &text) {
  static const QString symbols = QStringLiteral("=<>()[]`");
  for (QChar c : text) {
    if (symbols.contains(c)) {
      return true;
    }
  }
  return false;
}
} // namespace

SongMatcher::SongMatcher(const SongLibrary &library, const QString &query,
                         const QStringList &fieldIds)
    : library_(library) {
  QString text = query;
  if (const std::optional<QString> quoted = quotedText(query)) {
    text = *quoted;
  } else {
    ExprParseResult parsed = parseSongFilter(library_, query);
    if (parsed.ok()) {
      mode_ = Mode::Expression;
      expr_ = std::move(parsed.expr);
      return;
    }
    if (containsExpressionSymbol(query)) {
      mode_ = Mode::Error;
      error_ = parsed.error.text();
      return;
    }
  }
  words_ = text.split(QRegularExpression(QStringLiteral("\\s+")),
                      Qt::SkipEmptyParts);
  for (const QString &id : fieldIds) {
    fieldIds_.push_back(id.toStdString());
  }
}

SongMatcher::Mode SongMatcher::mode() const { return mode_; }

QString SongMatcher::error() const { return error_; }

bool SongMatcher::matches(int songPk) const {
  switch (mode_) {
  case Mode::Expression:
    return library_.matches(*expr_, songPk);
  case Mode::Error:
    return false;
  case Mode::Text:
    break;
  }
  const MSong &song = library_.getSongByPK(songPk);
  std::vector<QString> values;
  values.reserve(fieldIds_.size());
  for (const std::string &id : fieldIds_) {
    if (auto it = song.find(id); it != song.end()) {
      values.push_back(it->second.display());
    }
  }
  for (const QString &word : words_) {
    const bool found =
        std::any_of(values.begin(), values.end(), [&word](const QString &v) {
          return v.contains(word, Qt::CaseInsensitive);
        });
    if (!found) {
      return false;
    }
  }
  return true;
}

std::vector<int> matchPlaylistRows(const Playlist &playlist,
                                   const SongMatcher &matcher) {
  std::vector<int> pks;
  for (int row = 0; row < playlist.songCount(); ++row) {
    const int pk = playlist.getPkByIndex(row);
    if (matcher.matches(pk)) {
      pks.push_back(pk);
    }
  }
  return pks;
}
