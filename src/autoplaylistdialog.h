#ifndef AUTOPLAYLISTDIALOG_H
#define AUTOPLAYLISTDIALOG_H

#include <QDialog>

class MonitoredLibrary;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class SongLibrary;

// Edits an autoplaylist's name and expression. As the expression is typed,
// it is checked with parseSongFilter, and the status label shows the error
// or how many monitored songs match. OK needs a name and a valid expression.
class AutoPlaylistDialog : public QDialog {
  Q_OBJECT

public:
  AutoPlaylistDialog(const SongLibrary &library,
                     const MonitoredLibrary &monitoredLibrary,
                     const QString &name, const QString &expression,
                     QWidget *parent = nullptr);

  // Trimmed.
  QString name() const;
  QString expression() const;

private:
  void updateStatus();

  const SongLibrary &library_;
  const MonitoredLibrary &monitoredLibrary_;
  QLineEdit *nameEdit_ = nullptr;
  QLineEdit *expressionEdit_ = nullptr;
  QLabel *statusLabel_ = nullptr;
  QDialogButtonBox *buttons_ = nullptr;
};

#endif // AUTOPLAYLISTDIALOG_H
