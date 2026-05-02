#include "ElaraTabWidget.h"

namespace elara {

ElaraTabPage::ElaraTabPage() {}

ElaraTabPage::ElaraTabPage(
    const String& tab_title,
    ElaraWidget* tab_widget
) : title(tab_title),
    widget(tab_widget) {}

String ElaraTabPage::getTitle() const {
    return title;
}

Ref<ElaraWidget> ElaraTabPage::getWidget() const {
    return widget;
}

ElaraTabWidget::ElaraTabWidget(ElaraWidgetRegister* root_widget, ElaraWidgetHandle widget_handle)
    : ElaraWidget(root_widget, widget_handle),
      active_index(-1),
      hover_index(-1),
      tab_height(34) {
        setPadding(0, tab_height, 0, 0);
      }

void ElaraTabWidget::setPalette(ElaraPalette* widget_palette) {
    ElaraWidget::setPalette(widget_palette);

    for(int i = 0; i < (int)pages.length(); i++) {
        if(pages[i] && pages[i]->getWidget()) {
            pages[i]->getWidget()->setPalette(widget_palette);
        }
    }
}

int ElaraTabWidget::addTab(const String& title, ElaraWidget *widget) {
    if(widget) {
        widget->setPalette(palette);

        /*
            Critical:
            addTab owns this widget visually, so it must also become a child
            for parent-chain absolute bounds and event/draw offset logic.
        */
        addChild(Ref<ElaraWidget>(widget));
    }

    Ref<ElaraTabPage> page(new ElaraTabPage(title, widget));
    pages.push(page);

    int new_index = (int)pages.length() - 1;

    if(active_index < 0) {
        setActiveTab(0);
    } else if(widget) {
        widget->setVisible(false);
    }

    return (int)pages.length() - 1;
}

void ElaraTabWidget::setActiveTab(int index) {
    if(index < 0 || index >= (int)pages.length()) {
        return;
    }

    for(int i = 0; i < (int)pages.length(); i++) {
        if(pages[i] && pages[i]->getWidget()) {
            pages[i]->getWidget()->setVisible(i == index);
        }
    }

    active_index = index;
}

int ElaraTabWidget::getActiveTab() const {
    return active_index;
}

int ElaraTabWidget::tabCount() const {
    return (int)pages.length();
}

double ElaraTabWidget::tabWidth(int index) const {
    if(index < 0 || index >= (int)pages.length()) {
        return 0;
    }

    String title = pages[index]->getTitle();
    return 32 + title.length() * 8;
}

double ElaraTabWidget::tabX(int index) const {
    double px = 0;

    for(int i = 0; i < index; i++) {
        px += tabWidth(i);
    }

    return px;
}

int ElaraTabWidget::tabAt(double px, double py) const {
    if(py < 0 || py > tab_height) {
        return -1;
    }

    for(int i = 0; i < (int)pages.length(); i++) {
        double tx = tabX(i);
        double tw = tabWidth(i);

        if(px >= tx && px <= tx + tw) {
            return i;
        }
    }

    return -1;
}

Ref<ElaraTabPage> ElaraTabWidget::activePage() const {
    if(active_index < 0 || active_index >= (int)pages.length()) {
        return Ref<ElaraTabPage>(0);
    }

    return pages[active_index];
}

void ElaraTabWidget::applyColor(
    ElaraDrawContext* ctx,
    const ElaraColor& color
) const {
    ctx->setColor(color.r, color.g, color.b);
}

void ElaraTabWidget::draw(ElaraDrawContext* ctx) {
    ElaraPaletteTriplet bar_colors = colors("tabs", "bar");
    ElaraPaletteTriplet active_colors = colors("tabs", "active");
    ElaraPaletteTriplet hover_colors = colors("tabs", "hover");
    ElaraPaletteTriplet default_colors = colors("tabs", "default");

    applyColor(ctx, bar_colors.base);
    ctx->fillRect(x, y, width, tab_height);

    for(int i = 0; i < (int)pages.length(); i++) {
        double tx = tabX(i);
        double tw = tabWidth(i);

        ElaraPaletteTriplet tab_colors;

        if(i == active_index) {
            tab_colors = colors("tabs", "active");
            printf("drawing active tab header: %d\n", i);
        } else if(i == hover_index) {
            tab_colors = colors("tabs", "hover");
        } else {
            tab_colors = colors("tabs", "default");
        }

        applyColor(ctx, tab_colors.base);
        ctx->fillRect(tx, 0, tw, tab_height);

        applyColor(ctx, tab_colors.accent);
        ctx->line(tx + tw, 6, tx + tw, tab_height - 6, 1);

        if(i == active_index) {
            applyColor(ctx, tab_colors.accent);
            ctx->fillRect(tx, tab_height - 4, tw, 4);
        }

        applyColor(ctx, tab_colors.text);
        ctx->drawText(tx + 16, 22, pages[i]->getTitle(), 13);
    }

    applyColor(ctx, active_colors.accent);
    ctx->line(x, y + tab_height, x + width, y + tab_height, 1);

    Ref<ElaraTabPage> page = activePage();

    if(page && page->getWidget()) {
        page->getWidget()->setBounds(
            0,
            tab_height,
            width,
            height - tab_height
        );

        page->getWidget()->onDraw(
            ctx,
            (int)width,
            (int)(height - tab_height)
        );
    }
}

void ElaraTabWidget::onMouseMove(double px, double py) {
    hover_index = tabAt(px, py);

    Ref<ElaraTabPage> page = activePage();

    if(page && page->getWidget() && py > tab_height) {
        page->getWidget()->onMouseMove(px, py - tab_height);
    }
}

void ElaraTabWidget::onMouseDown(int button, double px, double py) {
    int tab = tabAt(px, py);

    printf("tab click test: x=%f y=%f tab=%d\n", px, py, tab);

    if(tab >= 0) {
        active_index = tab;
        return;
    }

    Ref<ElaraTabPage> page = activePage();

    if(page && page->getWidget() && py > tab_height) {
        page->getWidget()->onMouseDown(button, px, py - tab_height);
    }
}

void ElaraTabWidget::onMouseUp(int button, double px, double py) {
    Ref<ElaraTabPage> page = activePage();

    if(page && page->getWidget() && py > tab_height) {
        page->getWidget()->onMouseUp(button, px, py - tab_height);
    }
}

void ElaraTabWidget::onKeyDown(unsigned int keyval) {
    Ref<ElaraTabPage> page = activePage();

    if(page && page->getWidget()) {
        page->getWidget()->onKeyDown(keyval);
    }
}

void ElaraTabWidget::onKeyUp(unsigned int keyval) {
    Ref<ElaraTabPage> page = activePage();

    if(page && page->getWidget()) {
        page->getWidget()->onKeyUp(keyval);
    }
}

}