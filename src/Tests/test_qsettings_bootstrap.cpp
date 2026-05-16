#include <QDir>
#include <QSettings>

namespace {
class TestQSettingsBootstrap {
public:
  TestQSettingsBootstrap() {
    const QString baseDir =
        QDir(QDir::tempPath()).filePath("music-player-tests/qsettings");
    QDir dir(baseDir);
    if (dir.exists()) {
      dir.removeRecursively();
    }
    QDir().mkpath(baseDir);

    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, baseDir);
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, baseDir);
  }
};

TestQSettingsBootstrap g_testQSettingsBootstrap;
} // namespace
