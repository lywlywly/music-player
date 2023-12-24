#ifndef MYTABLEHEADER_H
#define MYTABLEHEADER_H

#include "qheaderview.h"
class MyTableHeader : public QHeaderView {
 public:
  MyTableHeader(Qt::Orientation orientation, QWidget* parent = nullptr);
 protected:
  void contextMenuEvent(QContextMenuEvent* event) override;

 private:
  std::vector<bool> isShown;
};

#endif  // MYTABLEHEADER_H
