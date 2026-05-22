#ifndef ELARA_LIST_LAYOUT_H
#define ELARA_LIST_LAYOUT_H

#include "../widgets/ElaraWidget.h"
#include "../widgets/ElaraSliderWidget.h"

namespace elara {

class ElaraListEntry {
public:
    ElaraWidgetHandle widget_handle;
    double row_height;

    ElaraListEntry();
    ElaraListEntry(ElaraWidgetHandle handle, double height);
};

class ElaraListLayout : public ElaraWidget {
private:
    Array<ElaraListEntry> entries;
    double scroll_offset;
    double scrollbar_size;
    ElaraSliderWidget* vscroll;
    Ref<ElaraWidget> vscroll_ref;  /* permanent owner — keeps vscroll alive */

    double totalContentHeight() const;
    void updateBounds();

public:
    ElaraListLayout(ElaraWidgetRegister* root_widget, ElaraWidgetHandle widget_handle);
    virtual ~ElaraListLayout();

    void addEntry(ElaraWidgetHandle widget_handle, double row_height);
    void clearEntries();
    void clearEntryChildren();

    double getScrollOffset() const;
    void setScrollOffset(double value);

    void draw(ElaraDrawContext* ctx);
    bool eventPropagate(ElaraUiEvent event);
    void onMouseScroll(double dx, double dy);
};

}

#endif
