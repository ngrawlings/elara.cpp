#include "ElaraGridLayout.h"

namespace elara {

ElaraGridTrack::ElaraGridTrack()
    : mode(ELARA_GRID_SIZE_EXACT),
      size(0),
      weight(1.0),
      resizable_after(false),
      computed_size(0),
      computed_offset(0) {}

ElaraGridTrack::ElaraGridTrack(double exact_size)
    : mode(ELARA_GRID_SIZE_EXACT),
      size(exact_size),
      weight(1.0),
      resizable_after(false),
      computed_size(0),
      computed_offset(0) {}

ElaraGridTrack ElaraGridTrack::fill(double track_weight) {
    ElaraGridTrack track;
    track.mode = ELARA_GRID_SIZE_FILL;
    track.size = 0;
    track.weight = track_weight > 0 ? track_weight : 1.0;
    return track;
}

ElaraGridCell::ElaraGridCell()
    : column(0),
      row(0),
      column_span(1),
      row_span(1) {}

ElaraGridCell::ElaraGridCell(
    ElaraWidgetHandle handle,
    int cell_column,
    int cell_row,
    int cell_column_span,
    int cell_row_span
) : widget_handle(handle),
    column(cell_column),
    row(cell_row),
    column_span(cell_column_span),
    row_span(cell_row_span) {}

ElaraGridLayout::ElaraGridLayout(
    ElaraWidgetRegister* root_widget,
    ElaraWidgetHandle widget_handle
) : ElaraWidget(root_widget, widget_handle),
    resize_handle_size(12),
    resize_axis(ELARA_GRID_RESIZE_NONE),
    resize_index(-1),
    resize_origin(0),
    resize_primary_initial(0),
    resize_secondary_initial(0) {}

ElaraGridLayout::~ElaraGridLayout() {}

int ElaraGridLayout::addColumn(double width) {
    columns.push(ElaraGridTrack(width));
    return (int)columns.length() - 1;
}

int ElaraGridLayout::addFillColumn() {
    columns.push(ElaraGridTrack::fill());
    return (int)columns.length() - 1;
}

int ElaraGridLayout::addWeightedFillColumn(double weight) {
    columns.push(ElaraGridTrack::fill(weight));
    return (int)columns.length() - 1;
}

void ElaraGridLayout::setColumnBorderResizable(int index, bool enabled) {
    if(index >= 0 && index < (int)columns.length()) {
        columns[index].resizable_after = enabled;
    }
}

int ElaraGridLayout::addRow(double height) {
    rows.push(ElaraGridTrack(height));
    return (int)rows.length() - 1;
}

int ElaraGridLayout::addFillRow() {
    rows.push(ElaraGridTrack::fill());
    return (int)rows.length() - 1;
}

int ElaraGridLayout::addWeightedFillRow(double weight) {
    rows.push(ElaraGridTrack::fill(weight));
    return (int)rows.length() - 1;
}

void ElaraGridLayout::setRowBorderResizable(int index, bool enabled) {
    if(index >= 0 && index < (int)rows.length()) {
        rows[index].resizable_after = enabled;
    }
}

int ElaraGridLayout::columnCount() const {
    return (int)columns.length();
}

int ElaraGridLayout::rowCount() const {
    return (int)rows.length();
}

ElaraGridTrack ElaraGridLayout::columnTrack(int index) const {
    if(index < 0 || index >= (int)columns.length()) {
        return ElaraGridTrack();
    }
    return columns[index];
}

ElaraGridTrack ElaraGridLayout::rowTrack(int index) const {
    if(index < 0 || index >= (int)rows.length()) {
        return ElaraGridTrack();
    }
    return rows[index];
}

void ElaraGridLayout::addWidget(
    ElaraWidgetHandle widget_handle,
    int column,
    int row,
    int column_span,
    int row_span
) {
    cells.push(
        ElaraGridCell(
            widget_handle,
            column,
            row,
            column_span,
            row_span
        )
    );
}

void ElaraGridLayout::clearChildren() {
    cells.clear();
    ElaraWidget::clearChildren();
}

void ElaraGridLayout::clearLayout() {
    columns.clear();
    rows.clear();
    endResize();
    clearChildren();
}

int ElaraGridLayout::columnDividerAt(double px) const {
    for(int i = 0; i + 1 < (int)columns.length(); i++) {
        if(!columns[i].resizable_after) {
            continue;
        }

        double divider = columns[i].computed_offset + columns[i].computed_size;
        if(px >= divider - resize_handle_size && px <= divider + resize_handle_size) {
            return i;
        }
    }

    return -1;
}

int ElaraGridLayout::rowDividerAt(double py) const {
    for(int i = 0; i + 1 < (int)rows.length(); i++) {
        if(!rows[i].resizable_after) {
            continue;
        }

        double divider = rows[i].computed_offset + rows[i].computed_size;
        if(py >= divider - resize_handle_size && py <= divider + resize_handle_size) {
            return i;
        }
    }

    return -1;
}

void ElaraGridLayout::beginColumnResize(int index, double px) {
    if(index < 0 || index + 1 >= (int)columns.length()) {
        return;
    }

    resize_axis = ELARA_GRID_RESIZE_COLUMN;
    resize_index = index;
    resize_origin = px;
    resize_primary_initial = columns[index].computed_size;
    resize_secondary_initial = columns[index + 1].computed_size;
}

void ElaraGridLayout::beginRowResize(int index, double py) {
    if(index < 0 || index + 1 >= (int)rows.length()) {
        return;
    }

    resize_axis = ELARA_GRID_RESIZE_ROW;
    resize_index = index;
    resize_origin = py;
    resize_primary_initial = rows[index].computed_size;
    resize_secondary_initial = rows[index + 1].computed_size;
}

void ElaraGridLayout::updateColumnResize(double px) {
    if(resize_axis != ELARA_GRID_RESIZE_COLUMN ||
       resize_index < 0 ||
       resize_index + 1 >= (int)columns.length()) {
        return;
    }

    double delta = px - resize_origin;
    double minimum = 24;
    double pair_total = resize_primary_initial + resize_secondary_initial;
    double left = resize_primary_initial + delta;

    if(left < minimum) {
        left = minimum;
    }
    if(left > pair_total - minimum) {
        left = pair_total - minimum;
    }

    double right = pair_total - left;
    ElaraGridTrack& primary = columns[resize_index];
    ElaraGridTrack& secondary = columns[resize_index + 1];

    if(primary.mode == ELARA_GRID_SIZE_EXACT && secondary.mode == ELARA_GRID_SIZE_FILL) {
        primary.size = left;
    } else if(primary.mode == ELARA_GRID_SIZE_FILL && secondary.mode == ELARA_GRID_SIZE_EXACT) {
        secondary.size = right;
    } else {
        primary.mode = ELARA_GRID_SIZE_EXACT;
        primary.size = left;
        secondary.mode = ELARA_GRID_SIZE_EXACT;
        secondary.size = right;
    }

    computeTracks(&columns, width);
}

void ElaraGridLayout::updateRowResize(double py) {
    if(resize_axis != ELARA_GRID_RESIZE_ROW ||
       resize_index < 0 ||
       resize_index + 1 >= (int)rows.length()) {
        return;
    }

    double delta = py - resize_origin;
    double minimum = 24;
    double pair_total = resize_primary_initial + resize_secondary_initial;
    double top = resize_primary_initial + delta;

    if(top < minimum) {
        top = minimum;
    }
    if(top > pair_total - minimum) {
        top = pair_total - minimum;
    }

    double bottom = pair_total - top;
    ElaraGridTrack& primary = rows[resize_index];
    ElaraGridTrack& secondary = rows[resize_index + 1];

    if(primary.mode == ELARA_GRID_SIZE_EXACT && secondary.mode == ELARA_GRID_SIZE_FILL) {
        primary.size = top;
    } else if(primary.mode == ELARA_GRID_SIZE_FILL && secondary.mode == ELARA_GRID_SIZE_EXACT) {
        secondary.size = bottom;
    } else {
        primary.mode = ELARA_GRID_SIZE_EXACT;
        primary.size = top;
        secondary.mode = ELARA_GRID_SIZE_EXACT;
        secondary.size = bottom;
    }

    computeTracks(&rows, height);
}

void ElaraGridLayout::endResize() {
    resize_axis = ELARA_GRID_RESIZE_NONE;
    resize_index = -1;
    resize_origin = 0;
    resize_primary_initial = 0;
    resize_secondary_initial = 0;
}

void ElaraGridLayout::computeTracks(
    Array<ElaraGridTrack>* tracks,
    double total_size
) {
    double exact_total = 0;
    double fill_weight_total = 0;

    for(int i = 0; i < (int)tracks->length(); i++) {
        if((*tracks)[i].mode == ELARA_GRID_SIZE_FILL) {
            fill_weight_total += (*tracks)[i].weight > 0 ? (*tracks)[i].weight : 1.0;
        } else {
            exact_total += (*tracks)[i].size;
        }
    }

    double remaining = total_size - exact_total;

    if(remaining < 0) {
        remaining = 0;
    }

    double offset = 0;

    for(int i = 0; i < (int)tracks->length(); i++) {
        (*tracks)[i].computed_offset = offset;

        if((*tracks)[i].mode == ELARA_GRID_SIZE_FILL) {
            double weight = (*tracks)[i].weight > 0 ? (*tracks)[i].weight : 1.0;
            (*tracks)[i].computed_size = fill_weight_total > 0
                ? (remaining * weight) / fill_weight_total
                : 0;
        } else {
            (*tracks)[i].computed_size = (*tracks)[i].size;
        }

        offset += (*tracks)[i].computed_size;
    }
}

double ElaraGridLayout::trackOffset(
    const Array<ElaraGridTrack>& tracks,
    int index
) const {
    if(index < 0 || index >= (int)tracks.length()) {
        return 0;
    }

    return tracks[index].computed_offset;
}

double ElaraGridLayout::trackSpanSize(
    const Array<ElaraGridTrack>& tracks,
    int index,
    int span
) const {
    double result = 0;

    if(span < 1) {
        span = 1;
    }

    for(int i = 0; i < span; i++) {
        int track_index = index + i;

        if(track_index >= 0 && track_index < (int)tracks.length()) {
            result += tracks[track_index].computed_size;
        }
    }

    return result;
}

static bool cellCrossesColumnDivider(const ElaraGridCell& cell, int divider_index, int row_index) {
    if(cell.column < 0 || cell.row < 0) {
        return false;
    }
    if(row_index < cell.row || row_index >= cell.row + cell.row_span) {
        return false;
    }
    return cell.column <= divider_index && divider_index < (cell.column + cell.column_span - 1);
}

static bool cellCrossesRowDivider(const ElaraGridCell& cell, int divider_index, int column_index) {
    if(cell.column < 0 || cell.row < 0) {
        return false;
    }
    if(column_index < cell.column || column_index >= cell.column + cell.column_span) {
        return false;
    }
    return cell.row <= divider_index && divider_index < (cell.row + cell.row_span - 1);
}

void ElaraGridLayout::draw(ElaraDrawContext* ctx) {
    computeTracks(&columns, width);
    computeTracks(&rows, height);

    ElaraPaletteTriplet c = colors("panel", "default");

    ctx->setColor(c.base.r, c.base.g, c.base.b);
    ctx->fillRect(0, 0, width, height);

    for(int i = 0; i < (int)cells.length(); i++) {
        ElaraGridCell cell = cells[i];

        if(cell.column < 0 || cell.row < 0) {
            continue;
        }

        if(cell.column >= (int)columns.length() || cell.row >= (int)rows.length()) {
            continue;
        }

        Ref<ElaraWidget> widget = getChild(i);

        if(!widget) {
            continue;
        }

        if(!widget->isVisible()) {
            continue;
        }

        widget->setParent(this);

        double cx = trackOffset(columns, cell.column);
        double cy = trackOffset(rows, cell.row);

        double cw = trackSpanSize(columns, cell.column, cell.column_span);
        double ch = trackSpanSize(rows, cell.row, cell.row_span);

        widget->setBounds(cx, cy, cw, ch);
        widget->onDraw(ctx, (int)cw, (int)ch);
    }

    ElaraPaletteTriplet accent_colors = colors("panel", "active");

    ctx->setColor(accent_colors.accent.r, accent_colors.accent.g, accent_colors.accent.b);

    for(int i = 0; i + 1 < (int)columns.length(); i++) {
        if(!columns[i].resizable_after) {
            continue;
        }

        double divider = columns[i].computed_offset + columns[i].computed_size;
        for(int row_index = 0; row_index < (int)rows.length(); row_index++) {
            bool blocked = false;
            for(int cell_index = 0; cell_index < (int)cells.length(); cell_index++) {
                Ref<ElaraWidget> widget = getChild(cell_index);
                if(!widget || !widget->isVisible()) {
                    continue;
                }
                if(cellCrossesColumnDivider(cells[cell_index], i, row_index)) {
                    blocked = true;
                    break;
                }
            }
            if(blocked) {
                continue;
            }

            double y0 = rows[row_index].computed_offset;
            double y1 = y0 + rows[row_index].computed_size;
            ctx->line(divider, y0, divider, y1, 1);
        }
    }

    for(int i = 0; i + 1 < (int)rows.length(); i++) {
        if(!rows[i].resizable_after) {
            continue;
        }

        double divider = rows[i].computed_offset + rows[i].computed_size;
        for(int column_index = 0; column_index < (int)columns.length(); column_index++) {
            bool blocked = false;
            for(int cell_index = 0; cell_index < (int)cells.length(); cell_index++) {
                Ref<ElaraWidget> widget = getChild(cell_index);
                if(!widget || !widget->isVisible()) {
                    continue;
                }
                if(cellCrossesRowDivider(cells[cell_index], i, column_index)) {
                    blocked = true;
                    break;
                }
            }
            if(blocked) {
                continue;
            }

            double x0 = columns[column_index].computed_offset;
            double x1 = x0 + columns[column_index].computed_size;
            ctx->line(x0, divider, x1, divider, 1);
        }
    }
}

ElaraMouseCursor ElaraGridLayout::cursorAt(double px, double py) const {
    if(columnDividerAt(px) >= 0) {
        return ELARA_CURSOR_RESIZE_EW;
    }

    if(rowDividerAt(py) >= 0) {
        return ELARA_CURSOR_RESIZE_NS;
    }

    return ElaraWidget::cursorAt(px, py);
}

bool ElaraGridLayout::eventPropagate(ElaraUiEvent event) {
    if(!visible) {
        return false;
    }

    computeTracks(&columns, width);
    computeTracks(&rows, height);

    bool is_mouse =
        event.type == ELARA_UI_MOUSE_MOVE ||
        event.type == ELARA_UI_MOUSE_DOWN ||
        event.type == ELARA_UI_MOUSE_UP;

    if(is_mouse) {
        if(resize_axis != ELARA_GRID_RESIZE_NONE) {
            return handleEvent(event);
        }

        if(event.type == ELARA_UI_MOUSE_DOWN) {
            if(columnDividerAt(event.x) >= 0 || rowDividerAt(event.y) >= 0) {
                return handleEvent(event);
            }
        }
    }

    return ElaraWidget::eventPropagate(event);
}

void ElaraGridLayout::onMouseMove(double px, double py) {
    emitMouseMove(px, py);

    if(resize_axis == ELARA_GRID_RESIZE_COLUMN) {
        updateColumnResize(px);
    } else if(resize_axis == ELARA_GRID_RESIZE_ROW) {
        updateRowResize(py);
    }
}

void ElaraGridLayout::onMouseDown(int button, double px, double py) {
    emitMouseDown(button, px, py);

    if(button != 1) {
        return;
    }

    int column_divider = columnDividerAt(px);
    if(column_divider >= 0) {
        beginColumnResize(column_divider, px);
        return;
    }

    int row_divider = rowDividerAt(py);
    if(row_divider >= 0) {
        beginRowResize(row_divider, py);
    }
}

void ElaraGridLayout::onMouseUp(int button, double px, double py) {
    emitMouseUp(button, px, py);
    (void)px;
    (void)py;

    if(button != 1) {
        return;
    }

    endResize();
}

}
