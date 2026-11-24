# The layout module: an editable layout of elements in splitters, boxes and
# tabs, its tree, its widgets and its edit mode. The main window's toolbar
# and panel layout are two. Depends on Qt only, not on the app; the app gives
# it the elements, their ids and the default layout. See
# docs/layout-editing.md.
add_library(mylayout STATIC
  panellayout.h
  panellayout.cpp
  panellayouteditor.h
  panellayouteditor.cpp
  panellayoutview.h
  panellayoutview.cpp
)
target_link_libraries(mylayout PUBLIC Qt6::Widgets)
