#include "mainwindow.h"
#include <QApplication>
#include <glib.h>
#include <thread>

// GMainLoop *glib_loop = nullptr;

int main(int argc, char *argv[]) {
  QCoreApplication::setOrganizationName("Example");
  QCoreApplication::setOrganizationDomain("example.com");
  QCoreApplication::setApplicationName("myplayer");
  QCoreApplication::setApplicationVersion("1.0.0");

  QApplication a(argc, argv);

#ifdef Q_OS_WIN
  // Temporary: match the Windows font-link order for CJK fallback and the
  // 10pt size native fallback uses (see docs/architecture.md).
  {
    QFont appFont = QApplication::font();
    appFont.setFamilies({appFont.family(), QStringLiteral("Meiryo UI"),
                         QStringLiteral("Microsoft JhengHei UI"),
                         QStringLiteral("Microsoft YaHei UI")});
    appFont.setPointSizeF(10);
    QApplication::setFont(appFont);
  }
#endif

  MainWindow w;
  w.show();

  // std::thread glib_thread([]() {
  //   glib_loop = g_main_loop_new(nullptr, FALSE); // default global
  //   GMainContext g_main_loop_run(glib_loop);
  // });
  // glib_thread.detach();

  return a.exec();
}
