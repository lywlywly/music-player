#include "settingsdialog.h"
#include "ui_settingsdialog.h"
#include <QSettings>

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent), ui(new Ui::SettingsDialog) {
  ui->setupUi(this);

  QSettings settings;
  int backendIndex =
      settings
          .value(
              "playback/backend",
              static_cast<int>(PlaybackBackendManager::Backend::QMediaPlayer))
          .toInt();

  ui->backend_combo_box->setCurrentIndex(backendIndex);

  connect(this, &QDialog::accepted, this, &SettingsDialog::applySettings);
}

SettingsDialog::~SettingsDialog() { delete ui; }

void SettingsDialog::applySettings() {
  int selected = ui->backend_combo_box->currentIndex();
  qDebug() << "ui->backend_combo_box->currentIndex(): accepted:" << selected;
  auto selectedBackend = static_cast<PlaybackBackendManager::Backend>(selected);

  emit backendChanged(selectedBackend);

  QSettings settings;
  settings.setValue("playback/backend", selected);
}
