#include "ElaraGridLayout.h"

#include "../ElaraWidgetRegistry.h"

namespace elara {

ElaraGridTrack::ElaraGridTrack()
    : mode(ELARA_GRID_SIZE_EXACT),
      size(0),
      computed_size(0),
      computed_offset(0) {}

ElaraGridTrack::ElaraGridTrack(double exact_size)
    : mode(ELARA_GRID_SIZE_EXACT),
      size(exact_size),
      computed_size(0),
      computed_offset(0) {}

ElaraGridTrack ElaraGridTrack::fill() {
    ElaraGridTrack track;
    track.mode = ELARA_GRID_SIZE_FILL;
    track.size = 0;
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
) : ElaraWidget(root_widget, widget_handle) {}

ElaraGridLayout::~ElaraGridLayout() {}

int ElaraGridLayout::addColumn(double width) {
    columns.push(ElaraGridTrack(width));
    return (int)columns.length() - 1;
}

int ElaraGridLayout::addFillColumn() {
    columns.push(ElaraGridTrack::fill());
    return (int)columns.length() - 1;
}

int ElaraGridLayout::addRow(double height) {
    rows.push(ElaraGridTrack(height));
    return (int)rows.length() - 1;
}

int ElaraGridLayout::addFillRow() {
    rows.push(ElaraGridTrack::fill());
    return (int)rows.length() - 1;
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
    clearChildren();
}

void ElaraGridLayout::computeTracks(
    Array<ElaraGridTrack>* tracks,
    double total_size
) {
    double exact_total = 0;
    int fill_count = 0;

    for(int i = 0; i < (int)tracks->length(); i++) {
        if((*tracks)[i].mode == ELARA_GRID_SIZE_FILL) {
            fill_count++;
        } else {
            exact_total += (*tracks)[i].size;
        }
    }

    double remaining = total_size - exact_total;

    if(remaining < 0) {
        remaining = 0;
    }

    double fill_size = 0;

    if(fill_count > 0) {
        fill_size = remaining / fill_count;
    }

    double offset = 0;

    for(int i = 0; i < (int)tracks->length(); i++) {
        (*tracks)[i].computed_offset = offset;

        if((*tracks)[i].mode == ELARA_GRID_SIZE_FILL) {
            (*tracks)[i].computed_size = fill_size;
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

        Ref<ElaraWidget> widget = ElaraWidgetRegistry::getInstance()->getWidget(cell.widget_handle);

        if(!widget) {
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
}

}
