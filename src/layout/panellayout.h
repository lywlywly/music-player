#ifndef PANELLAYOUT_H
#define PANELLAYOUT_H

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <optional>
#include <vector>

// A layout: a tree of splitters, boxes and tabs whose leaves are elements
// (panels and controls, named by an id the app chooses) and placeholders
// (empty slots). Plain data with its JSON form and its edits; PanelLayoutView
// builds the widgets. See docs/layout-editing.md.
//
// A tree is valid by construction: a splitter has two children, a box at least
// one, a tabs node at least one, a current tab among them and a title for
// each, and each element appears at most once. fromJson checks a saved tree;
// the edits keep it valid and do not check their arguments.
class PanelLayout {
public:
  enum class Kind {
    HSplitter,
    VSplitter,
    HBox,
    VBox,
    Tabs,
    Element,
    Placeholder
  };

  struct Node {
    Kind kind = Kind::Placeholder;
    // Element only.
    QString elementId;
    // Splitters only: the two children's sizes, scaled to the splitter.
    std::vector<int> sizes;
    // Tabs only.
    int currentTab = 0;
    // Tabs only: one name per child, kept with the tab when its content is
    // replaced. Empty: the tab is named by the elements in it.
    std::vector<QString> tabTitles;
    std::vector<Node> children;

    bool operator==(const Node &) const = default;
  };

  // A node's child indexes from the root; empty for the root.
  using Path = std::vector<int>;

  // The saved tree, or nullopt when json is not a valid tree. An element that
  // appears again becomes a placeholder.
  static std::optional<PanelLayout> fromJson(const QByteArray &json);
  explicit PanelLayout(Node root);

  QByteArray toJson() const;
  const Node &root() const { return root_; }
  const Node &node(const Path &path) const;
  // The elements in the tree, in tree order.
  QStringList placedElements() const;

  // Edits. replaceWithElement takes an element not in the tree.
  void replaceWithElement(const Path &path, const QString &elementId);
  // A splitter or box (kind) of the node and a placeholder.
  void split(const Path &path, Kind kind);
  // A tabs node holding the node.
  void wrapInTabs(const Path &path);
  // Delete and Cut: returns the node and leaves a placeholder.
  Node take(const Path &path);
  // Replaces the node with subtree. An element of subtree that is in the rest
  // of the tree becomes a placeholder.
  void paste(const Path &path, Node subtree);
  // path is a box or a tabs node: appends a placeholder.
  void addChild(const Path &path);
  // path is a child of a box or a tabs node; the move stays within it. A tab
  // takes its title along, and the current tab stays current.
  void moveChild(const Path &path, int delta);
  // path is a child of a box or a tabs node with more than one.
  void removeChild(const Path &path);
  // path is a tab; an empty title names it by its elements again.
  void setTabTitle(const Path &path, const QString &title);
  void setSizes(const Path &path, std::vector<int> sizes);
  void setCurrentTab(const Path &path, int index);

  bool operator==(const PanelLayout &) const = default;

private:
  Node &at(const Path &path);

  Node root_;
};

#endif // PANELLAYOUT_H
