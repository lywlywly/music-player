#ifndef ADDFIELDDIALOG_H
#define ADDFIELDDIALOG_H

#include <QDialog>

namespace Ui {
class AddFieldDialog;
}

class AddFieldDialog : public QDialog {
  Q_OBJECT

public:
  explicit AddFieldDialog(QWidget *parent = nullptr);
  ~AddFieldDialog();

  QString fieldName() const;
  QString fieldValue() const;

private:
  Ui::AddFieldDialog *ui = nullptr;
};

#endif // ADDFIELDDIALOG_H
