#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include "playbackbackendmanager.h"
#include <QDialog>

namespace Ui {
class SettingsDialog;
}

class SettingsDialog : public QDialog {
  Q_OBJECT

public:
  explicit SettingsDialog(QWidget *parent = nullptr);
  ~SettingsDialog();

signals:
  void backendChanged(PlaybackBackendManager::Backend backend);

private:
  void applySettings();
  Ui::SettingsDialog *ui;
};

#endif // SETTINGSDIALOG_H
