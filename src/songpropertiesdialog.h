#ifndef SONGPROPERTIESDIALOG_H
#define SONGPROPERTIESDIALOG_H

#include "columnregistry.h"
#include "songlibrary.h"
#include <QDialog>
#include <QModelIndex>
#include <QStandardItemModel>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Ui {
class SongPropertiesDialog;
}

class SongPropertiesDialog : public QDialog {
  Q_OBJECT

public:
  explicit SongPropertiesDialog(
      int songPk, const std::string &filepath, SongLibrary &songLibrary,
      const MSong &song,
      const std::unordered_map<std::string, std::string> &remainingFields,
      const ColumnRegistry &columnRegistry, QWidget *parent = nullptr);
  ~SongPropertiesDialog();

signals:
  void songUpdated(int songPk);

private:
  enum class RowSource { SongField, RemainingField, ComputedField };

  struct RowData {
    std::string rawKey;
    QString displayField;
    QString valueText;
    QString originalValueText;
    ColumnValueType valueType = ColumnValueType::Text;
    RowSource source = RowSource::SongField;
    bool editable = false;
    bool dirty = false;
  };
  static bool isNonTagInternalField(const std::string &key);
  static bool isBuiltInFieldKey(const std::string &key);
  static QString valueTypeToDisplayText(ColumnValueType valueType);
  static std::string toWritableTagKey(const RowData &row);
  static void showInfoPopup(QWidget *parent, const QString &title,
                            const QString &message);
  bool isRowRemovable(const RowData &row) const;
  void buildRows(
      const MSong &song,
      const std::unordered_map<std::string, std::string> &remainingFields);
  void populateTable();
  void updateRowDisplay(int rowIndex);
  void updateRemoveButtonEnabled();
  void addField();
  void removeSelectedField();
  void saveBufferedChanges();

private slots:
  void handleTableDoubleClicked(const QModelIndex &index);

private:
  Ui::SongPropertiesDialog *ui = nullptr;
  SongLibrary &songLibrary_;
  const ColumnRegistry &columnRegistry_;
  int songPk_ = -1;
  std::string filepath_;
  QStandardItemModel *model_ = nullptr;
  std::vector<RowData> rows_;
  std::unordered_set<std::string> removedTagKeys_;
};

#endif // SONGPROPERTIESDIALOG_H
