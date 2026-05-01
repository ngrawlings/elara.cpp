#ifndef ELARA_TAB_WIDGET_H
#define ELARA_TAB_WIDGET_H

#include <libelaracore/memory/String.h>
#include <libelaracore/memory/Array.h>
#include <libelaracore/memory/Ref.h>

#include "../ElaraGui.h"
#include "ElaraWidget.h"

namespace elara {

class ElaraTabPage {
private:
    String title;
    Ref<ElaraWidget> widget;

public:
    ElaraTabPage();
    ElaraTabPage(const String& tab_title, Ref<ElaraWidget> tab_widget);

    String getTitle() const;
    Ref<ElaraWidget> getWidget() const;
};

class ElaraTabWidget : public ElaraDrawSurface {
private:
    Array< Ref<ElaraTabPage> > pages;

    int active_index;
    int hover_index;

    int width;
    int height;

    double tab_height;

    int tabAt(double px, double py) const;
    double tabX(int index) const;
    double tabWidth(int index) const;

    Ref<ElaraTabPage> activePage() const;

public:
    ElaraTabWidget();

    int addTab(const String& title, Ref<ElaraWidget> widget);

    void setActiveTab(int index);
    int getActiveTab() const;
    int tabCount() const;

    void onDraw(ElaraDrawContext* ctx, int draw_width, int draw_height);

    void onMouseMove(double px, double py);
    void onMouseDown(int button, double px, double py);
    void onMouseUp(int button, double px, double py);

    void onKeyDown(unsigned int keyval);
    void onKeyUp(unsigned int keyval);
};

}

#endif
