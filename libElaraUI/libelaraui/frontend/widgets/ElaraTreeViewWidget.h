#ifndef ELARA_TREE_VIEW_WIDGET_H
#define ELARA_TREE_VIEW_WIDGET_H

#include "ElaraWidget.h"

namespace elara {

class ElaraTreeViewNode {
private:
    String id;
    String text;
    bool expanded;

public:
    Array<ElaraTreeViewNode> children;

    ElaraTreeViewNode();
    ElaraTreeViewNode(const String& node_id, const String& node_text);

    void setId(const String& value);
    String getId() const;

    void setText(const String& value);
    String getText() const;

    void setExpanded(bool value);
    bool isExpanded() const;

    void addChild(const ElaraTreeViewNode& child);
    int childCount() const;
    ElaraTreeViewNode& getChild(int index);
    const ElaraTreeViewNode& getChild(int index) const;
};

class ElaraTreeViewWidget : public ElaraWidget {
private:
    struct VisibleRow {
        Array<int> path;
        int depth;
        String id;
        String text;
        bool expanded;
        bool has_children;

        VisibleRow()
            : path(),
              depth(0),
              expanded(false),
              has_children(false) {
        }
    };

    Array<ElaraTreeViewNode> roots;

    String palette_master;
    String selected_id;
    String selected_text;

    bool enabled;
    int hover_index;
    double font_size;
    double row_height;
    double indent_width;
    double padding_x;

    void appendVisibleRows(
        Array<VisibleRow>& rows,
        const Array<ElaraTreeViewNode>& nodes,
        const Array<int>& parent_path,
        int depth
    ) const;
    Array<VisibleRow> visibleRows() const;
    int rowAt(double py) const;
    ElaraTreeViewNode* nodeAtPath(const Array<int>& path);
    const ElaraTreeViewNode* nodeAtPath(const Array<int>& path) const;
    int totalNodeCount(const Array<ElaraTreeViewNode>& nodes) const;
    int expandedNodeCount(const Array<ElaraTreeViewNode>& nodes) const;

public:
    ElaraTreeViewWidget(
        ElaraWidgetRegister* root_widget,
        ElaraWidgetHandle widget_handle
    );

    virtual ~ElaraTreeViewWidget();

    void clearNodes();
    void addRootNode(const ElaraTreeViewNode& node);

    void setEnabled(bool value);
    bool isEnabled() const;

    void setFontSize(double value);
    double getFontSize() const;

    String getSelectedId() const;
    String getSelectedText() const;
    int getNodeCount() const;
    int getExpandedCount() const;

    ElaraMouseCursor cursor() const;
    void draw(ElaraDrawContext* ctx);
    void onMouseMove(double px, double py);
    void onMouseDown(int button, double px, double py);
    void onMouseUp(int button, double px, double py);
};

}

#endif
