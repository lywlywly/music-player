#include <QObject>
#include <QSqlDatabase>
#include <QTest>
#include <QUuid>

#include "../columnregistry.h"
#include "../databasemanager.h"
#include "../playlist.h"
#include "../playlistsearch.h"
#include "../songlibrary.h"

namespace {
MSong makeSong(const QString &title, const QString &artist,
               const QString &genre, const QString &path) {
  MSong song;
  song.insert_or_assign("title", FieldValue(title.toStdString(), "title"));
  song.insert_or_assign("artist", FieldValue(artist.toStdString(), "artist"));
  song.insert_or_assign("album", FieldValue("Album", "album"));
  song.insert_or_assign("genre", FieldValue(genre.toStdString(), "genre"));
  song.insert_or_assign("filepath", FieldValue(path.toStdString(), "filepath"));
  return song;
}

const QStringList kTitleAndArtist = {"title", "artist"};
} // namespace

class TestPlaylistSearch : public QObject {
  Q_OBJECT

private slots:
  void init();
  void cleanup();

  void mode_isDecidedByParse_data();
  void mode_isDecidedByParse();
  void textMode_needsEveryWordInSomeField();
  void expressionMode_matchesInRowOrder();
  void errorMode_matchesNothing();

private:
  std::vector<int> search(const QString &query,
                          const QStringList &fieldIds = kTitleAndArtist) const;

  ColumnRegistry *registry_ = nullptr;
  DatabaseManager *databaseManager_ = nullptr;
  SongLibrary *library_ = nullptr;
  Playlist *playlist_ = nullptr;
  // In row order; the paths are in the reverse order.
  int blueInGreen_ = -1;
  int blueTrain_ = -1;
  int soWhat_ = -1;
  QString connectionName_;
};

void TestPlaylistSearch::init() {
  connectionName_ =
      QStringLiteral("test_playlistsearch_%1")
          .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
  registry_ = new ColumnRegistry();
  databaseManager_ =
      new DatabaseManager(*registry_, ":memory:", connectionName_);
  library_ = new SongLibrary(*registry_, *databaseManager_);
  playlist_ = new Playlist(-1, *library_, *databaseManager_);
  for (MSong song :
       {makeSong("Blue in Green", "Miles Davis", "jazz", "/music/c.mp3"),
        makeSong("Blue Train", "John Coltrane", "jazz", "/music/b.mp3"),
        makeSong("So What", "Miles Davis", "modal", "/music/a.mp3")}) {
    library_->prepareParsedSong(song);
    QVERIFY(playlist_->addSong(std::move(song)));
  }
  blueInGreen_ = playlist_->getPkByIndex(0);
  blueTrain_ = playlist_->getPkByIndex(1);
  soWhat_ = playlist_->getPkByIndex(2);
}

void TestPlaylistSearch::cleanup() {
  delete playlist_;
  playlist_ = nullptr;
  delete library_;
  library_ = nullptr;
  delete databaseManager_;
  databaseManager_ = nullptr;
  delete registry_;
  registry_ = nullptr;
  QSqlDatabase::removeDatabase(connectionName_);
}

std::vector<int> TestPlaylistSearch::search(const QString &query,
                                            const QStringList &fieldIds) const {
  return matchPlaylistRows(*playlist_, SongMatcher(*library_, query, fieldIds));
}

void TestPlaylistSearch::mode_isDecidedByParse_data() {
  QTest::addColumn<QString>("query");
  QTest::addColumn<SongMatcher::Mode>("mode");

  QTest::newRow("condition")
      << "genre HAS jazz AND title != x" << SongMatcher::Mode::Expression;
  QTest::newRow("true") << "true" << SongMatcher::Mode::Expression;
  // Parses as text, not a condition.
  QTest::newRow("words") << "davis blue" << SongMatcher::Mode::Text;
  QTest::newRow("field name") << "title" << SongMatcher::Mode::Text;
  // Fails to parse ("AND operands must be boolean"), with no symbol.
  QTest::newRow("keyword") << "simon and garfunkel" << SongMatcher::Mode::Text;
  QTest::newRow("slash") << "ac/dc" << SongMatcher::Mode::Text;
  // Quoted text is text, even with an expression symbol.
  QTest::newRow("quoted condition")
      << "\"title = x\"" << SongMatcher::Mode::Text;
  QTest::newRow("broken condition")
      << "genre IN [pop" << SongMatcher::Mode::Error;
  QTest::newRow("broken comparison") << "title =" << SongMatcher::Mode::Error;
}

void TestPlaylistSearch::mode_isDecidedByParse() {
  QFETCH(QString, query);
  QFETCH(SongMatcher::Mode, mode);
  QCOMPARE(SongMatcher(*library_, query, kTitleAndArtist).mode(), mode);
}

void TestPlaylistSearch::textMode_needsEveryWordInSomeField() {
  // The words may match different fields.
  QCOMPARE(search("davis blue"), std::vector<int>({blueInGreen_}));
  QCOMPARE(search("  BLUE  "), std::vector<int>({blueInGreen_, blueTrain_}));
  QCOMPARE(search("davis train"), std::vector<int>());
  // The quotes are removed.
  QCOMPARE(search("\"so what\""), std::vector<int>({soWhat_}));
  // Only the given fields; ids that are not song fields are ignored.
  QCOMPARE(search("jazz"), std::vector<int>());
  QCOMPARE(search("jazz", {"status", "genre"}),
           std::vector<int>({blueInGreen_, blueTrain_}));
}

void TestPlaylistSearch::expressionMode_matchesInRowOrder() {
  QCOMPARE(search("genre HAS jazz"),
           std::vector<int>({blueInGreen_, blueTrain_}));
  // The fields only limit plain text.
  QCOMPARE(search("genre = modal", {}), std::vector<int>({soWhat_}));
  QCOMPARE(search("true"),
           std::vector<int>({blueInGreen_, blueTrain_, soWhat_}));
}

void TestPlaylistSearch::errorMode_matchesNothing() {
  const SongMatcher matcher(*library_, "genre IN [jazz", kTitleAndArtist);
  QCOMPARE(matcher.mode(), SongMatcher::Mode::Error);
  QVERIFY(!matcher.error().isEmpty());
  QCOMPARE(matchPlaylistRows(*playlist_, matcher), std::vector<int>());
}

QTEST_GUILESS_MAIN(TestPlaylistSearch)
#include "tst_playlistsearch.moc"
