#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include "columndefinition.h"
#include "columnregistry.h"
#include "databasemanager.h"
#include "playbackbackendmanager.h"
#include <QDialog>
#include <QSet>

namespace Ui {
class SettingsDialog;
}

class SettingsDialog : public QDialog {
  Q_OBJECT

public:
  explicit SettingsDialog(ColumnRegistry &columnRegistry,
                          DatabaseManager &databaseManager,
                          QWidget *parent = nullptr);
  ~SettingsDialog();

signals:
  void backendChanged(PlaybackBackendManager::Backend backend);
  void customFieldsChanged();

private:
  void refreshCustomFieldsList();
  void addCustomFieldFromForm();
  void removeSelectedCustomField();
  void applySettings();
  ColumnDefinition buildCustomFieldDefinitionFromForm() const;
  Ui::SettingsDialog *ui;
  ColumnRegistry &columnRegistry_;
  DatabaseManager &databaseManager_;
  QList<ColumnDefinition> customFields_;
  QSet<QString> originalCustomFieldIds_;
};

#endif // SETTINGSDIALOG_H
