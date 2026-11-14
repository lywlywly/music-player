#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include "columndefinition.h"
#include "columnregistry.h"
#include "databasemanager.h"
#include "displayexpression.h"
#include "libraryexpression.h"
#include "playbackbackendmanager.h"
#include "replaygain.h"
#include "statusruntimesymboltable.h"
#include <QComboBox>
#include <QDialog>
#include <QLabel>
#include <QList>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSet>
#include <unordered_map>

class IPluginSettingsPage;
class MonitoredLibrary;
struct PluginEntry;
class PluginsPage;

namespace Ui {
class SettingsDialog;
}

// Preferences: its pages in a navigation tree (docs/settings-dialog.md). OK
// applies every page but Library, whose actions apply at once.
class SettingsDialog : public QDialog {
  Q_OBJECT

public:
  // plugins are the PluginManager's entries: the Plugins page lists them, and
  // each loaded plugin's settings page is under it.
  explicit SettingsDialog(ColumnRegistry &columnRegistry,
                          DatabaseManager &databaseManager,
                          MonitoredLibrary &monitoredLibrary,
                          const QList<PluginEntry> &plugins,
                          QWidget *parent = nullptr);
  ~SettingsDialog();
  void setDisplayExpressionPreviewContext(
      const std::unordered_map<std::string, FieldValue> *song,
      const StatusRuntimeSymbolTable &runtimeSymbols);

signals:
  void backendChanged(PlaybackBackendManager::Backend backend);
  void replayGainModeChanged(ReplayGainMode mode);
  void customFieldsChanged();
  void cloudUuidChanged(const QString &uuid);
  void statusBarExpressionChanged(const QString &expression);
  void windowTitleExpressionChanged(const QString &expression);
  void displayThemeModeChanged(const QString &mode);

private:
  // Adds the pages built in code to the stack and an item per page to the
  // tree, and sizes the tree to fit its items.
  void setUpPageTree(QWidget *libraryPage, QWidget *displayPage,
                     const QList<PluginEntry> &plugins);
  void refreshCustomFieldsList();
  void refreshComputedFieldsList();
  void addCustomFieldFromForm();
  void addComputedFieldFromForm();
  void removeSelectedCustomField();
  void removeSelectedComputedField();
  void updateCloudUuidStatus();
  void setActiveDisplayExpressionEditor(QPlainTextEdit *editor);
  void updateActiveDisplayExpressionPreview();
  QString evaluateDisplayExpressionPreview(const QString &expressionText,
                                           DisplayExpressionKind kind) const;
  void applySettings();
  ColumnDefinition buildCustomFieldDefinitionFromForm() const;
  ColumnDefinition buildComputedFieldDefinitionFromForm() const;
  FieldDefinition buildCustomFieldSchemaFromForm() const;
  FieldDefinition buildComputedFieldSchemaFromForm() const;
  bool expressionTypeMatchesValueType(const Expr &expr,
                                      ValueType expectedType) const;
  Ui::SettingsDialog *ui;
  ColumnRegistry &columnRegistry_;
  DatabaseManager &databaseManager_;
  QList<ColumnDefinition> customFields_;
  QList<ColumnDefinition> computedFields_;
  QHash<QString, FieldDefinition> customFieldSchemasById_;
  QHash<QString, FieldDefinition> computedFieldSchemasById_;
  QSet<QString> originalCustomFieldIds_;
  QSet<QString> originalComputedFieldIds_;
  QString initialCloudUuid_;
  bool initialCloudDisabledByUser_ = false;
  QString pendingCloudUuid_;
  bool pendingCloudDisabledByUser_ = false;
  // The loaded plugins' pages, under Plugins.
  QList<IPluginSettingsPage *> pluginPages_;
  PluginsPage *pluginsPage_ = nullptr;
  QComboBox *displayThemeModeCombo_ = nullptr;
  QPlainTextEdit *statusExpressionEdit_ = nullptr;
  QPlainTextEdit *windowTitleExpressionEdit_ = nullptr;
  QLabel *activeDisplayExpressionPreviewLabel_ = nullptr;
  QPlainTextEdit *activeDisplayExpressionEdit_ = nullptr;
  QPushButton *resetDisplayExpressionsButton_ = nullptr;
  std::unordered_map<std::string, FieldValue> previewSong_;
  StatusRuntimeSymbolTable previewRuntimeSymbols_;
};

#endif // SETTINGSDIALOG_H
