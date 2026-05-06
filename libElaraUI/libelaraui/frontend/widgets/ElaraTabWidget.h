#ifndef ELARA_TAB_WIDGET_H
#define ELARA_TAB_WIDGET_H

#include <libelaracore/memory/String.h>
#include <libelaracore/memory/Array.h>
#include <libelaracore/memory/Ref.h>

#include "ElaraWidget.h"

namespace elara {

class ElaraTabPage {
private:
    String title;
    Ref<ElaraWidget> widget;

public:
    ElaraTabPage();
    ElaraTabPage(const String& tab_title, ElaraWidget* tab_widget);

    String getTitle() const;
    Ref<ElaraWidget> getWidget() const;
};

class ElaraTabWidget : public ElaraWidget {
private:
    Array< Ref<ElaraTabPage> > pages;

    int active_index;
    int hover_index;

    double tab_height;

    int tabAt(double px, double py) const;
    double tabX(int index) const;
    double tabWidth(int index) const;

    Ref<ElaraTabPage> activePage() const;

    void applyColor(ElaraDrawContext* ctx, const ElaraColor& color) const;

public:
    ElaraTabWidget(ElaraWidgetRegister* root_widget, ElaraWidgetHandle widget_handle);

    void setPalette(ElaraPalette* widget_palette);

    int addTab(const String& title, ElaraWidget* widget);
    void clearChildren();

    void setActiveTab(int index);
    int getActiveTab() const;
    int tabCount() const;

    ElaraMouseCursor cursor() const;
    void draw(ElaraDrawContext* ctx);

    void onMouseMove(double px, double py);
    void onMouseDown(int button, double px, double py);
    void onMouseUp(int button, double px, double py);

    void onKeyDown(unsigned int keyval);
    void onKeyUp(unsigned int keyval);
};

}

#endif
