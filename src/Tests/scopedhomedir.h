#ifndef SCOPEDHOMEDIR_H
#define SCOPEDHOMEDIR_H

#include <QByteArray>
#include <QDir>
#include <QString>
#include <QtGlobal>

// Points the home directory (USERPROFILE on Windows, HOME elsewhere) at path
// for the lifetime of this object, e.g. for LyricsLoader::getLyrics, which
// reads "<home>/Music/<artist> - <title>.lrc".
class ScopedHomeDir {
public:
  explicit ScopedHomeDir(const QString &path)
      : hadValue_(qEnvironmentVariableIsSet(kHomeEnvVar)),
        oldValue_(qgetenv(kHomeEnvVar)) {
    qputenv(kHomeEnvVar, QDir::toNativeSeparators(path).toLocal8Bit());
  }
  ~ScopedHomeDir() {
    if (hadValue_) {
      qputenv(kHomeEnvVar, oldValue_);
    } else {
      qunsetenv(kHomeEnvVar);
    }
  }
  ScopedHomeDir(const ScopedHomeDir &) = delete;
  ScopedHomeDir &operator=(const ScopedHomeDir &) = delete;

private:
#ifdef Q_OS_WIN
  static constexpr const char *kHomeEnvVar = "USERPROFILE";
#else
  static constexpr const char *kHomeEnvVar = "HOME";
#endif
  bool hadValue_;
  QByteArray oldValue_;
};

#endif // SCOPEDHOMEDIR_H
