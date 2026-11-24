#include <QApplication>
#include <QContextMenuEvent>
#include <QInputDialog>
#include <QMenu>
#include <QPointer>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTest>
#include <QTimer>

#include "../layout/panellayouteditor.h"
#include "../layout/panellayoutview.h"
#include <functional>

namespace {
using Kind = PanelLayout::Kind;
using Node = PanelLayout::Node;

Node element(const QString &id) {
  return {.kind = Kind::Element, .elementId = id};
}

QStringList actionTexts(const QMenu *menu) {
  QStringList texts;
  for (const QAction *action : menu->actions()) {
    if (!action->isSeparator()) {
      texts.append(action->text());
    }
  }
  return texts;
}

QAction *actionNamed(const QMenu *menu, const QString &text) {
  for (QAction *action : menu->actions()) {
    if (action->text() == text) {
      return action;
    }
  }
  return nullptr;
}
} // namespace

class TestPanelLayoutEditor : public QObject {
  Q_OBJECT

private slots:
  void init();
  void cleanup();

  void contextMenu_onlyInEditModeWithOverlay();
  void menu_itemsForEachKindOfNode();
  void menuItems_applyTheirEdit();
  void cutPasteAndRename_throughTheMenu();

private:
  // Sends a context menu request to widget; returns the layout menu it
  // opened, or nullptr.
  QMenu *requestMenu(QWidget *widget);
  void closeMenu(QMenu *menu);
  // Renames the tab holding widget to name through the menu and its dialog;
  // returns the text the dialog showed.
  QString renameTab(QWidget *widget, const QString &name);

  QWidget *window_ = nullptr;
  PanelLayoutView *view_ = nullptr;
  PanelLayoutEditor *editor_ = nullptr;
  QWidget *a_ = nullptr;
  QWidget *b_ = nullptr;
};

void TestPanelLayoutEditor::init() {
  window_ = new QWidget;
  auto *host = new QWidget(window_);
  auto *store = new QWidget(window_);
  store->hide();
  view_ = new PanelLayoutView(host, store, "Empty slot");
  a_ = new QWidget;
  b_ = new QWidget;
  view_->addElement("a", "A", a_);
  view_->addElement("b", "B", b_);
  view_->addElement("c", "C", new QWidget);
  // [a | tabs(b, placeholder)]
  Node tabs{.kind = Kind::Tabs, .tabTitles = {QString(), QString()}};
  tabs.children = {element("b"), Node{}};
  Node root{.kind = Kind::HSplitter, .sizes = {1, 1}};
  root.children = {element("a"), std::move(tabs)};
  view_->setLayout(PanelLayout(std::move(root)));
  editor_ = new PanelLayoutEditor(*view_, {Kind::HSplitter, Kind::VSplitter,
                                           Kind::HBox, Kind::VBox, Kind::Tabs});
  window_->resize(400, 300);
  host->resize(400, 300);
  window_->show();
}

void TestPanelLayoutEditor::cleanup() {
  delete window_;
  window_ = nullptr;
}

QMenu *TestPanelLayoutEditor::requestMenu(QWidget *widget) {
  QContextMenuEvent event(QContextMenuEvent::Mouse, QPoint(5, 5),
                          widget->mapToGlobal(QPoint(5, 5)));
  QApplication::sendEvent(widget, &event);
  return qobject_cast<QMenu *>(QApplication::activePopupWidget());
}

void TestPanelLayoutEditor::closeMenu(QMenu *menu) {
  QPointer<QMenu> closed = menu;
  menu->close();
  // Deleted from the event loop once hidden.
  QTRY_VERIFY(closed.isNull());
}

QString TestPanelLayoutEditor::renameTab(QWidget *widget, const QString &name) {
  QString shown;
  // The dialog opens from the event loop once the menu closed.
  QTimer answer;
  connect(&answer, &QTimer::timeout, this, [&]() {
    if (auto *dialog =
            qobject_cast<QInputDialog *>(QApplication::activeModalWidget())) {
      shown = dialog->textValue();
      dialog->setTextValue(name);
      dialog->accept();
    }
  });
  answer.start(10);
  QMenu *menu = requestMenu(widget);
  actionNamed(menu, "Rename Tab...")->trigger();
  closeMenu(menu);
  QTest::qWaitFor([&shown]() { return !shown.isEmpty(); });
  return shown;
}

void TestPanelLayoutEditor::contextMenu_onlyInEditModeWithOverlay() {
  a_->setContextMenuPolicy(Qt::CustomContextMenu);
  QSignalSpy ownMenu(a_, &QWidget::customContextMenuRequested);

  // Outside edit mode the widget gets the request.
  QCOMPARE(requestMenu(a_), nullptr);
  QCOMPARE(ownMenu.count(), 1);

  editor_->setEnabled(true);
  QMenu *menu = requestMenu(a_);
  QVERIFY(menu != nullptr);
  QCOMPARE(ownMenu.count(), 1);
  // While the menu is open, an overlay that ignores the mouse covers a.
  QPointer<QWidget> overlay;
  for (QWidget *child :
       window_->findChildren<QWidget *>(Qt::FindDirectChildrenOnly)) {
    if (child->testAttribute(Qt::WA_TransparentForMouseEvents)) {
      overlay = child;
    }
  }
  QVERIFY(overlay != nullptr);
  QVERIFY(overlay->isVisible());
  QCOMPARE(overlay->geometry(),
           QRect(a_->mapTo(window_, QPoint(0, 0)), a_->size()));
  closeMenu(menu);
  QVERIFY(overlay.isNull());
  // A request outside the layout passes through.
  QCOMPARE(requestMenu(window_), nullptr);

  editor_->setEnabled(false);
  QCOMPARE(requestMenu(a_), nullptr);
  QCOMPARE(ownMenu.count(), 2);
}

void TestPanelLayoutEditor::menu_itemsForEachKindOfNode() {
  editor_->setEnabled(true);

  // An element: Replace with offers the element not in the tree, then the
  // containers.
  QMenu *menu = requestMenu(a_);
  QVERIFY(menu != nullptr);
  QCOMPARE(actionTexts(menu), (QStringList{"A", "Replace with", "Delete", "Cut",
                                           "Paste", "Parent"}));
  QVERIFY(!actionNamed(menu, "Paste")->isEnabled());
  QCOMPARE(actionTexts(actionNamed(menu, "Replace with")->menu()),
           (QStringList{"C", "Splitter (Left and Right)",
                        "Splitter (Top and Bottom)", "Box (Left to Right)",
                        "Box (Top to Bottom)", "Tabs"}));
  // Parent is the splitter's menu, which has no parent.
  QMenu *parent = actionNamed(menu, "Parent")->menu();
  QCOMPARE(actionTexts(parent),
           (QStringList{"Splitter (Left and Right)", "Replace with", "Delete",
                        "Cut", "Paste"}));
  closeMenu(menu);

  // A tab: the tab items, Move Tab Left disabled on the first tab.
  menu = requestMenu(b_);
  QVERIFY(menu != nullptr);
  QCOMPARE(actionTexts(menu),
           (QStringList{"B", "Replace with", "Delete", "Cut", "Paste",
                        "Move Tab Left", "Move Tab Right", "Rename Tab...",
                        "Remove Tab", "Parent"}));
  QVERIFY(!actionNamed(menu, "Move Tab Left")->isEnabled());
  QVERIFY(actionNamed(menu, "Move Tab Right")->isEnabled());
  QVERIFY(actionNamed(menu, "Remove Tab")->isEnabled());
  // The tabs node adds Add Tab.
  QVERIFY(actionTexts(actionNamed(menu, "Parent")->menu()).contains("Add Tab"));
  closeMenu(menu);

  // A placeholder (the second tab): Insert, no Delete or Cut.
  qobject_cast<QTabWidget *>(view_->widgetAt({1}))->setCurrentIndex(1);
  menu = requestMenu(view_->widgetAt({1, 1}));
  QVERIFY(menu != nullptr);
  QCOMPARE(
      actionTexts(menu),
      (QStringList{"Empty", "Insert", "Paste", "Move Tab Left",
                   "Move Tab Right", "Rename Tab...", "Remove Tab", "Parent"}));
  closeMenu(menu);

  // Children of boxes: vbox(hbox(a, placeholder), b).
  Node row{.kind = Kind::HBox, .children = {element("a"), Node{}}};
  view_->setLayout(PanelLayout(
      {.kind = Kind::VBox, .children = {std::move(row), element("b")}}));
  menu = requestMenu(a_);
  QVERIFY(menu != nullptr);
  QCOMPARE(actionTexts(menu),
           (QStringList{"A", "Replace with", "Delete", "Cut", "Paste",
                        "Move Left", "Move Right", "Remove Slot", "Parent"}));
  QVERIFY(!actionNamed(menu, "Move Left")->isEnabled());
  QVERIFY(actionNamed(menu, "Move Right")->isEnabled());
  // The hbox is the vbox's first child.
  parent = actionNamed(menu, "Parent")->menu();
  QCOMPARE(actionTexts(parent),
           (QStringList{"Box (Left to Right)", "Replace with", "Delete", "Cut",
                        "Paste", "Add Slot", "Move Up", "Move Down",
                        "Remove Slot", "Parent"}));
  QVERIFY(!actionNamed(parent, "Move Up")->isEnabled());
  QCOMPARE(actionTexts(actionNamed(parent, "Parent")->menu()).first(),
           QString("Box (Top to Bottom)"));
  closeMenu(menu);

  // An editor offers only the container kinds it was given.
  delete editor_;
  editor_ = new PanelLayoutEditor(*view_, {Kind::HBox, Kind::VBox});
  editor_->setEnabled(true);
  menu = requestMenu(b_);
  QVERIFY(menu != nullptr);
  QCOMPARE(actionTexts(actionNamed(menu, "Replace with")->menu()),
           (QStringList{"C", "Box (Left to Right)", "Box (Top to Bottom)"}));
  closeMenu(menu);
}

void TestPanelLayoutEditor::menuItems_applyTheirEdit() {
  editor_->setEnabled(true);
  // Each item, from the menu of the node at target, makes the same edit as
  // the PanelLayout call (tested in tst_panellayout) and reports it once. A
  // box's child items are the tab items' actions under other labels, which
  // menu_itemsForEachKindOfNode checks.
  struct Case {
    PanelLayout::Path target;
    QStringList items;
    std::function<void(PanelLayout &)> edit;
  };
  const Case cases[] = {
      {{0},
       {"Replace with", "C"},
       [](PanelLayout &l) { l.replaceWithElement({0}, "c"); }},
      {{0},
       {"Replace with", "Splitter (Left and Right)"},
       [](PanelLayout &l) { l.split({0}, Kind::HSplitter); }},
      {{0},
       {"Replace with", "Splitter (Top and Bottom)"},
       [](PanelLayout &l) { l.split({0}, Kind::VSplitter); }},
      {{0},
       {"Replace with", "Box (Left to Right)"},
       [](PanelLayout &l) { l.split({0}, Kind::HBox); }},
      {{0},
       {"Replace with", "Box (Top to Bottom)"},
       [](PanelLayout &l) { l.split({0}, Kind::VBox); }},
      {{0},
       {"Replace with", "Tabs"},
       [](PanelLayout &l) { l.wrapInTabs({0}); }},
      {{0}, {"Delete"}, [](PanelLayout &l) { l.take({0}); }},
      {{1, 0},
       {"Move Tab Right"},
       [](PanelLayout &l) { l.moveChild({1, 0}, 1); }},
      {{1, 1},
       {"Move Tab Left"},
       [](PanelLayout &l) { l.moveChild({1, 1}, -1); }},
      {{1, 0}, {"Remove Tab"}, [](PanelLayout &l) { l.removeChild({1, 0}); }},
      {{1, 0}, {"Parent", "Add Tab"}, [](PanelLayout &l) { l.addChild({1}); }},
  };
  const PanelLayout start = view_->layout();
  for (const Case &c : cases) {
    view_->setLayout(start);
    QSignalSpy spy(editor_, &PanelLayoutEditor::layoutChanged);
    QMenu *menu = requestMenu(view_->widgetAt(c.target));
    QVERIFY(menu != nullptr);
    const QMenu *items = menu;
    for (qsizetype i = 0; i < c.items.size() - 1; ++i) {
      items = actionNamed(items, c.items[i])->menu();
    }
    actionNamed(items, c.items.last())->trigger();
    closeMenu(menu);
    PanelLayout expected = start;
    c.edit(expected);
    QVERIFY2(view_->layout() == expected, qPrintable(c.items.join(" > ")));
    QCOMPARE(spy.count(), 1);
  }
}

void TestPanelLayoutEditor::cutPasteAndRename_throughTheMenu() {
  editor_->setEnabled(true);

  // Cut b, then paste it over the placeholder left in its tab's place.
  QMenu *menu = requestMenu(b_);
  actionNamed(menu, "Cut")->trigger();
  closeMenu(menu);
  QCOMPARE(view_->layout().node({1, 0}), Node{});
  menu = requestMenu(view_->widgetAt({1, 0}));
  QVERIFY(actionNamed(menu, "Paste")->isEnabled());
  actionNamed(menu, "Paste")->trigger();
  closeMenu(menu);
  QCOMPARE(view_->layout().node({1, 0}), element("b"));
  // The cut subtree is pasted once.
  menu = requestMenu(view_->widgetAt({1, 1}));
  QVERIFY(!actionNamed(menu, "Paste")->isEnabled());
  closeMenu(menu);

  // Rename b's tab. The dialog shows the tab's label; a name equal to it is
  // the panels' names again.
  QCOMPARE(renameTab(b_, "Info"), QString("B"));
  QCOMPARE(view_->layout().node({1}).tabTitles[0], QString("Info"));
  QCOMPARE(qobject_cast<QTabWidget *>(view_->widgetAt({1}))->tabText(0),
           QString("Info"));
  QCOMPARE(renameTab(b_, "B"), QString("Info"));
  QCOMPARE(view_->layout().node({1}).tabTitles[0], QString());
}

QTEST_MAIN(TestPanelLayoutEditor)
#include "tst_panellayouteditor.moc"
