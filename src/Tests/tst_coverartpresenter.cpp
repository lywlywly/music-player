#include <QBuffer>
#include <QImage>
#include <QLabel>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "../coverartpresenter.h"
#include "testaudiofiles.h"

namespace {
MSong songAt(const QString &filepath) {
  MSong song;
  song.insert_or_assign("filepath",
                        FieldValue(filepath.toStdString(), "filepath"));
  return song;
}

QByteArray pngBytes(const QSize &size) {
  QImage image(size, QImage::Format_RGB32);
  image.fill(Qt::red);
  QByteArray bytes;
  QBuffer buffer(&bytes);
  buffer.open(QIODevice::WriteOnly);
  image.save(&buffer, "PNG");
  return bytes;
}
} // namespace

class TestCoverArtPresenter : public QObject {
  Q_OBJECT

private slots:
  void showSong_withCover_showsScaledCoverAndReportsArtwork();
  void showSong_withoutCover_showsNoCoverText();
};

void TestCoverArtPresenter::showSong_withCover_showsScaledCoverAndReportsArtwork() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString wav = dir.filePath("cover.wav");
  const QByteArray png = pngBytes(QSize(200, 100));
  QVERIFY(writeWavWithCover(wav, png, "image/png"));
  QLabel label;
  label.resize(100, 100);
  CoverArtPresenter presenter(&label);
  QSignalSpy spy(&presenter, &CoverArtPresenter::artworkChanged);

  presenter.showSong(songAt(wav));

  QCOMPARE(spy.size(), 1);
  QCOMPARE(spy.at(0).at(0).toByteArray(), png);
  // Scaled to fit the label, keeping the 2:1 aspect ratio.
  const QPixmap shown = label.pixmap();
  QCOMPARE(shown.deviceIndependentSize().toSize(), QSize(100, 50));

  // A resize rescales the cover.
  label.resize(50, 50);
  presenter.updateCoverSize();
  QCOMPARE(label.pixmap().deviceIndependentSize().toSize(), QSize(50, 25));

  presenter.clear();
  QVERIFY(label.pixmap().isNull());
  // With no cover, a resize shows nothing.
  presenter.updateCoverSize();
  QVERIFY(label.pixmap().isNull());
}

void TestCoverArtPresenter::showSong_withoutCover_showsNoCoverText() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString wav = dir.filePath("untagged.wav");
  QVERIFY(writeSilentWav(wav));
  QLabel label;
  CoverArtPresenter presenter(&label);
  QSignalSpy spy(&presenter, &CoverArtPresenter::artworkChanged);

  presenter.showSong(songAt(wav));

  QCOMPARE(label.text(), QStringLiteral("No cover"));
  QVERIFY(label.pixmap().isNull());
  QCOMPARE(spy.size(), 0);
}

QTEST_MAIN(TestCoverArtPresenter)
#include "tst_coverartpresenter.moc"
