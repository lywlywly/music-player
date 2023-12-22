#ifndef ADDENTRYDIALOG_H
#define ADDENTRYDIALOG_H

#include <QDialog>

namespace Ui {
class AddEntryDialog;
}

class AddEntryDialog : public QDialog {
  Q_OBJECT

public:
  explicit AddEntryDialog(QWidget *parent = nullptr);
  ~AddEntryDialog();

signals:
  void entryStringEntered(const QString &text);

private:
  Ui::AddEntryDialog *ui;
  void emitEntryString();
};

#endif // ADDENTRYDIALOG_H
