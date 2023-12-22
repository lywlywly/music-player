#include "addentrydialog.h"
#include "ui_addentrydialog.h"

AddEntryDialog::AddEntryDialog(QWidget *parent)
    : QDialog(parent), ui(new Ui::AddEntryDialog) {
  ui->setupUi(this);
  connect(this, &AddEntryDialog::accepted, this,
          &AddEntryDialog::emitEntryString);
}

AddEntryDialog::~AddEntryDialog() { delete ui; }

void AddEntryDialog::emitEntryString() {
  qDebug() << "emitEntryString" << ui->lineEdit->text();
  emit entryStringEntered(ui->lineEdit->text());
}
