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
    double computed_size;
    double computed_offset;

    ElaraGridTrack();
    ElaraGridTrack(double exact_size);
    static ElaraGridTrack fill();
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
    Array<ElaraGridTrack> columns;
    Array<ElaraGridTrack> rows;
    Array<ElaraGridCell> cells;

    void computeTracks(Array<ElaraGridTrack>* tracks, double total_size);
    double trackOffset(const Array<ElaraGridTrack>& tracks, int index) const;
    double trackSpanSize(const Array<ElaraGridTrack>& tracks, int index, int span) const;

public:
    ElaraGridLayout(ElaraWidgetRegister* root_widget, ElaraWidgetHandle widget_handle);
    virtual ~ElaraGridLayout();

    int addColumn(double width);
    int addFillColumn();

    int addRow(double height);
    int addFillRow();

    void addWidget(
        ElaraWidgetHandle widget_handle,
        int column,
        int row,
        int column_span = 1,
        int row_span = 1
    );

    void clearLayout();

    void draw(ElaraDrawContext* ctx);
};

}

#endif