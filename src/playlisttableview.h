#ifndef PLAYLISTTABLEVIEW_H
#define PLAYLISTTABLEVIEW_H

#include <QKeySequence>
#include <QList>
#include <QTableView>

class ColumnRegistry;
class GlobalColumnLayoutManager;
class PlaybackQueue;
class Playlist;
class PlaylistTableModel;

// Table of one Playlist through its own PlaylistTableModel. The header follows
// GlobalColumnLayoutManager and writes back column moves, resizes and
// visibility; clicking a sortable column sorts the playlist. Rows can be
// dragged to reorder the playlist (both kinds). Frontend class.
class PlaylistTableView : public QTableView {
  Q_OBJECT

public:
  PlaylistTableView(Playlist &playlist, PlaybackQueue &queue,
                    const ColumnRegistry &registry,
                    GlobalColumnLayoutManager &layout,
                    QWidget *parent = nullptr);

  // The platform keys for Song Properties: Command+I on macOS, Alt+Enter
  // elsewhere.
  static QList<QKeySequence> propertiesShortcuts();

  // The smallest selected row, or -1 when no row is selected.
  int firstSelectedRow() const;

signals:
  // A properties shortcut was pressed. row is firstSelectedRow(), or the
  // current row when no row is selected.
  void propertiesRequested(int row);

protected:
  void dropEvent(QDropEvent *event) override;

private:
  void sortByHeaderSection(int section);
  void applyLayout();
  void saveVisualOrder();
  void showColumnsMenu(const QPoint &pos);

  const ColumnRegistry &registry_;
  GlobalColumnLayoutManager &layout_;
  PlaylistTableModel *model_;
};

#endif // PLAYLISTTABLEVIEW_H
