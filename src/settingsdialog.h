#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include "columndefinition.h"
#include "columnregistry.h"
#include "databasemanager.h"
#include "libraryexpression.h"
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
  void cloudUuidChanged(const QString &uuid);

private:
  void refreshCustomFieldsList();
  void refreshComputedFieldsList();
  void addCustomFieldFromForm();
  void addComputedFieldFromForm();
  void removeSelectedCustomField();
  void removeSelectedComputedField();
  void updateCloudUuidStatus();
  void applySettings();
  ColumnDefinition buildCustomFieldDefinitionFromForm() const;
  ColumnDefinition buildComputedFieldDefinitionFromForm() const;
  bool expressionTypeMatchesValueType(const Expr &expr,
                                      ColumnValueType expectedType) const;
  Ui::SettingsDialog *ui;
  ColumnRegistry &columnRegistry_;
  DatabaseManager &databaseManager_;
  QList<ColumnDefinition> customFields_;
  QList<ColumnDefinition> computedFields_;
  QSet<QString> originalCustomFieldIds_;
  QSet<QString> originalComputedFieldIds_;
  QString initialCloudUuid_;
  bool initialCloudDisabledByUser_ = false;
  QString pendingCloudUuid_;
  bool pendingCloudDisabledByUser_ = false;
};

#endif // SETTINGSDIALOG_H
