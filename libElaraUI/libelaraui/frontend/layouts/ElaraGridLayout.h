#ifndef ELARA_GRID_LAYOUT_H
#define ELARA_GRID_LAYOUT_H

#include "../widgets/ElaraWidget.h"

namespace elara {

enum ElaraGridSizeMode {
    ELARA_GRID_SIZE_EXACT,
    ELARA_GRID_SIZE_FILL
};

class ElaraGridTrack {
public:
    ElaraGridSizeMode mode;
    double size;
    double weight;
    bool resizable_after;
    double computed_size;
    double computed_offset;

    ElaraGridTrack();
    ElaraGridTrack(double exact_size);
    static ElaraGridTrack fill(double weight = 1.0);
};

class ElaraGridCell {
public:
    ElaraWidgetHandle widget_handle;

    int column;
    int row;
    int column_span;
    int row_span;

    ElaraGridCell();
    ElaraGridCell(
        ElaraWidgetHandle handle,
        int cell_column,
        int cell_row,
        int cell_column_span = 1,
        int cell_row_span = 1
    );
};

class ElaraGridLayout : public ElaraWidget {
private:
    enum ResizeAxis {
        ELARA_GRID_RESIZE_NONE,
        ELARA_GRID_RESIZE_COLUMN,
        ELARA_GRID_RESIZE_ROW
    };

    Array<ElaraGridTrack> columns;
    Array<ElaraGridTrack> rows;
    Array<ElaraGridCell> cells;
    double resize_handle_size;
    ResizeAxis resize_axis;
    int resize_index;
    double resize_origin;
    double resize_primary_initial;
    double resize_secondary_initial;

    void computeTracks(Array<ElaraGridTrack>* tracks, double total_size);
    double trackOffset(const Array<ElaraGridTrack>& tracks, int index) const;
    double trackSpanSize(const Array<ElaraGridTrack>& tracks, int index, int span) const;
    int columnDividerAt(double px) const;
    int rowDividerAt(double py) const;
    void beginColumnResize(int index, double px);
    void beginRowResize(int index, double py);
    void updateColumnResize(double px);
    void updateRowResize(double py);
    void endResize();

public:
    ElaraGridLayout(ElaraWidgetRegister* root_widget, ElaraWidgetHandle widget_handle);
    virtual ~ElaraGridLayout();

    int addColumn(double width);
    int addFillColumn();
    int addWeightedFillColumn(double weight);
    void setColumnExactSize(int index, double width);
    void setColumnBorderResizable(int index, bool enabled);

    int addRow(double height);
    int addFillRow();
    int addWeightedFillRow(double weight);
    void setRowExactSize(int index, double height);
    void setRowBorderResizable(int index, bool enabled);
    int columnCount() const;
    int rowCount() const;
    ElaraGridTrack columnTrack(int index) const;
    ElaraGridTrack rowTrack(int index) const;

    void addWidget(
        ElaraWidgetHandle widget_handle,
        int column,
        int row,
        int column_span = 1,
        int row_span = 1
    );

    void clearChildren();
    void clearLayout();

    void draw(ElaraDrawContext* ctx);
    ElaraMouseCursor cursorAt(double px, double py) const;
    bool eventPropagate(ElaraUiEvent event);
    void onMouseMove(double px, double py);
    void onMouseDown(int button, double px, double py);
    void onMouseUp(int button, double px, double py);
};

}

#endif
