#include <QObject>
#include <QTest>

#include "../playbackmanager.h"
#include "../playbackqueue.h"
#include "../playlist.h"

class TestPlaybackManager : public QObject {
  Q_OBJECT
public:
  explicit TestPlaybackManager(QObject *parent = nullptr);
  PlaybackQueue queue;
  PlaybackManager manager{queue};
  SongLibrary lb;
  Playlist pl{SongStore{lb}, queue};

signals:
private slots:
  void test1();
};

TestPlaybackManager::TestPlaybackManager(QObject *parent) : QObject{parent} {}
void TestPlaybackManager::test1() {
  Playlist *pl;
  queue.addNext(1, pl);
  queue.addNext(3, pl);
  queue.addLast(2, pl);

  std::deque<int> expected = {3, 1, 2};
  QCOMPARE(queue.getQueue(), expected);
  QVERIFY(queue.pop().first == 3);
  QVERIFY(queue.getQueue()[0] == 1);
}

QTEST_MAIN(TestPlaybackManager)
#include "tst_playbackmanager.moc"
