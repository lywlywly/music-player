#include <QDir>
#include <QtGlobal>

namespace {
// Gives the tests their own GStreamer registry (the cache of what each
// plugin provides), set before any test calls gst_init, so the tests do not
// write to the developer's profile. When the tests and the app have
// different build types, they load different plugins (Debug and Release on
// Windows), and with one shared registry each switch between them would
// force a full plugin rescan on the next start (docs/architecture.md,
// "GStreamer plugin registry and scanner"). Kept between runs: it is a cache.
class TestGStreamerBootstrap {
public:
  TestGStreamerBootstrap() {
    const QString dir = QDir(QDir::tempPath()).filePath("music-player-tests");
    QDir().mkpath(dir);
    qputenv("GST_REGISTRY", QDir::toNativeSeparators(
                                QDir(dir).filePath("gstreamer-registry.bin"))
                                .toLocal8Bit());
  }
};

TestGStreamerBootstrap g_testGStreamerBootstrap;
} // namespace
