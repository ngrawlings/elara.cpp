#include "ElaraTreeViewWidget.h"

namespace elara {

ElaraTreeViewNode::ElaraTreeViewNode()
    : expanded(false) {
}

ElaraTreeViewNode::ElaraTreeViewNode(const String& node_id, const String& node_text)
    : id(node_id),
      text(node_text),
      expanded(false) {
}

void ElaraTreeViewNode::setId(const String& value) {
    id = value;
}

String ElaraTreeViewNode::getId() const {
    return id;
}

void ElaraTreeViewNode::setText(const String& value) {
    text = value;
}

String ElaraTreeViewNode::getText() const {
    return text;
}

void ElaraTreeViewNode::setExpanded(bool value) {
    expanded = value;
}

bool ElaraTreeViewNode::isExpanded() const {
    return expanded;
}

void ElaraTreeViewNode::addChild(const ElaraTreeViewNode& child) {
    children.push(child);
}

int ElaraTreeViewNode::childCount() const {
    return (int)children.length();
}

ElaraTreeViewNode& ElaraTreeViewNode::getChild(int index) {
    return children[index];
}

const ElaraTreeViewNode& ElaraTreeViewNode::getChild(int index) const {
    return children[index];
}

ElaraTreeViewWidget::ElaraTreeViewWidget(
    ElaraWidgetRegister* root_widget,
    ElaraWidgetHandle widget_handle
) : ElaraWidget(root_widget, widget_handle),
    roots(),
    palette_master("panel"),
    selected_id(""),
    selected_text(""),
    enabled(true),
    hover_index(-1),
    font_size(14),
    row_height(24),
    indent_width(18),
    padding_x(8) {
}

ElaraTreeViewWidget::~ElaraTreeViewWidget() {
}

void ElaraTreeViewWidget::appendVisibleRows(
    Array<VisibleRow>& rows,
    const Array<ElaraTreeViewNode>& nodes,
    const Array<int>& parent_path,
    int depth
) const {
    for(int i = 0; i < (int)nodes.length(); i++) {
        VisibleRow row;
        row.path = parent_path;
        row.path.push(i);
        row.depth = depth;
        row.id = nodes[i].getId();
        row.text = nodes[i].getText();
        row.expanded = nodes[i].isExpanded();
        row.has_children = nodes[i].childCount() > 0;
        rows.push(row);

        if(nodes[i].isExpanded() && nodes[i].childCount() > 0) {
            appendVisibleRows(rows, nodes[i].children, row.path, depth + 1);
        }
    }
}

Array<ElaraTreeViewWidget::VisibleRow> ElaraTreeViewWidget::visibleRows() const {
    Array<VisibleRow> rows;
    Array<int> root_path;
    appendVisibleRows(rows, roots, root_path, 0);
    return rows;
}

int ElaraTreeViewWidget::rowAt(double py) const {
    if(py < 0 || py > height) {
        return -1;
    }

    int index = (int)(py / row_height);
    Array<VisibleRow> rows = visibleRows();

    if(index < 0 || index >= (int)rows.length()) {
        return -1;
    }

    return index;
}

ElaraTreeViewNode* ElaraTreeViewWidget::nodeAtPath(const Array<int>& path) {
    if(path.length() <= 0) {
        return 0;
    }

    Array<ElaraTreeViewNode>* current = &roots;
    ElaraTreeViewNode* node = 0;

    for(int i = 0; i < (int)path.length(); i++) {
        int index = path[i];

        if(index < 0 || index >= (int)current->length()) {
            return 0;
        }

        node = &((*current)[index]);
        current = &(node->children);
    }

    return node;
}

const ElaraTreeViewNode* ElaraTreeViewWidget::nodeAtPath(const Array<int>& path) const {
    return ((ElaraTreeViewWidget*)this)->nodeAtPath(path);
}

int ElaraTreeViewWidget::totalNodeCount(const Array<ElaraTreeViewNode>& nodes) const {
    int total = 0;

    for(int i = 0; i < (int)nodes.length(); i++) {
        total += 1;
        total += totalNodeCount(nodes[i].children);
    }

    return total;
}

int ElaraTreeViewWidget::expandedNodeCount(const Array<ElaraTreeViewNode>& nodes) const {
    int total = 0;

    for(int i = 0; i < (int)nodes.length(); i++) {
        if(nodes[i].isExpanded()) {
            total += 1;
        }

        total += expandedNodeCount(nodes[i].children);
    }

    return total;
}

void ElaraTreeViewWidget::clearNodes() {
    roots.clear();
    selected_id = "";
    selected_text = "";
    hover_index = -1;
}

void ElaraTreeViewWidget::addRootNode(const ElaraTreeViewNode& node) {
    roots.push(node);
}

void ElaraTreeViewWidget::setEnabled(bool value) {
    enabled = value;

    if(!enabled) {
        hover_index = -1;
    }
}

bool ElaraTreeViewWidget::isEnabled() const {
    return enabled;
}

void ElaraTreeViewWidget::setFontSize(double value) {
    font_size = value;
    row_height = font_size + 10;
}

double ElaraTreeViewWidget::getFontSize() const {
    return font_size;
}

String ElaraTreeViewWidget::getSelectedId() const {
    return selected_id;
}

String ElaraTreeViewWidget::getSelectedText() const {
    return selected_text;
}

int ElaraTreeViewWidget::getNodeCount() const {
    return totalNodeCount(roots);
}

int ElaraTreeViewWidget::getExpandedCount() const {
    return expandedNodeCount(roots);
}

ElaraMouseCursor ElaraTreeViewWidget::cursor() const {
    return enabled ? ELARA_CURSOR_POINTER : ELARA_CURSOR_DEFAULT;
}

void ElaraTreeViewWidget::draw(ElaraDrawContext* ctx) {
    String sub = enabled ? String("default") : String("disabled");
    ElaraPaletteTriplet c = colors(palette_master, sub);
    ElaraPaletteTriplet hover = colors("button", "hover");
    ElaraPaletteTriplet selected = colors("button", "pressed");

    ctx->setColor(c.base.r, c.base.g, c.base.b);
    ctx->fillRect(0, 0, width, height);

    ctx->setColor(c.accent.r, c.accent.g, c.accent.b);
    ctx->line(0, 0, width, 0, 1);
    ctx->line(0, height - 1, width, height - 1, 1);
    ctx->line(0, 0, 0, height, 1);
    ctx->line(width - 1, 0, width - 1, height, 1);

    Array<VisibleRow> rows = visibleRows();

    for(int i = 0; i < (int)rows.length(); i++) {
        double y = i * row_height;

        if(y + row_height > height) {
            break;
        }

        if(rows[i].id == selected_id) {
            ctx->setColor(selected.base.r, selected.base.g, selected.base.b);
            ctx->fillRect(2, y + 1, width - 4, row_height - 2);
        } else if(i == hover_index) {
            ctx->setColor(hover.base.r, hover.base.g, hover.base.b);
            ctx->fillRect(2, y + 1, width - 4, row_height - 2);
        }

        double x = padding_x + (rows[i].depth * indent_width);

        ctx->setColor(c.text.r, c.text.g, c.text.b);
        if(rows[i].has_children) {
            if(rows[i].expanded) {
                ctx->line(x, y + 8, x + 8, y + 8, 1);
            } else {
                ctx->line(x, y + 8, x + 8, y + 8, 1);
                ctx->line(x + 4, y + 4, x + 4, y + 12, 1);
            }
        }

        ctx->drawText(x + 14, y + font_size + 5, rows[i].text, font_size);
    }
}

void ElaraTreeViewWidget::onMouseMove(double px, double py) {
    emitMouseMove(px, py);
    hover_index = rowAt(py);
}

void ElaraTreeViewWidget::onMouseDown(int button, double px, double py) {
    emitMouseDown(button, px, py);
}

void ElaraTreeViewWidget::onMouseUp(int button, double px, double py) {
    emitMouseUp(button, px, py);

    if(!enabled || button != 1) {
        return;
    }

    int row_index = rowAt(py);
    Array<VisibleRow> rows = visibleRows();

    if(row_index < 0 || row_index >= (int)rows.length()) {
        return;
    }

    ElaraTreeViewNode* node = nodeAtPath(rows[row_index].path);

    if(!node) {
        return;
    }

    double toggle_x = padding_x + (rows[row_index].depth * indent_width) + 10;

    if(rows[row_index].has_children && px <= toggle_x + 8) {
        node->setExpanded(!node->isExpanded());
    } else {
        selected_id = node->getId();
        selected_text = node->getText();
        emitValueChanged(1.0);
        emitClicked(button, px, py);
    }
}

}
