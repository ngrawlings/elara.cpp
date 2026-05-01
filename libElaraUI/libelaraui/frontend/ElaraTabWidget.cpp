#include "ElaraTabWidget.h"

namespace elara {

ElaraTabPage::ElaraTabPage() {}

ElaraTabPage::ElaraTabPage(const String& tab_title, Ref<ElaraWidget> tab_widget)
    : title(tab_title),
      widget(tab_widget) {}

String ElaraTabPage::getTitle() const {
    return title;
}

Ref<ElaraWidget> ElaraTabPage::getWidget() const {
    return widget;
}

ElaraTabWidget::ElaraTabWidget()
    : active_index(-1),
      hover_index(-1),
      width(0),
      height(0),
      tab_height(34) {}

int ElaraTabWidget::addTab(const String& title, Ref<ElaraWidget> widget) {
    Ref<ElaraTabPage> page(new ElaraTabPage(title, widget));
    pages.push(page);

    if(active_index < 0) {
        active_index = 0;
    }

    return (int)pages.length() - 1;
}

void ElaraTabWidget::setActiveTab(int index) {
    if(index < 0 || index >= (int)pages.length()) {
        return;
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

void ElaraTabWidget::onDraw(ElaraDrawContext* ctx, int draw_width, int draw_height) {
    width = draw_width;
    height = draw_height;

    ctx->clear(0.08, 0.08, 0.10);

    ctx->setColor(0.13, 0.13, 0.16);
    ctx->fillRect(0, 0, width, tab_height);

    for(int i = 0; i < (int)pages.length(); i++) {
        double tx = tabX(i);
        double tw = tabWidth(i);

        if(i == active_index) {
            ctx->setColor(0.22, 0.22, 0.28);
        } else if(i == hover_index) {
            ctx->setColor(0.17, 0.17, 0.21);
        } else {
            ctx->setColor(0.11, 0.11, 0.14);
        }

        ctx->fillRect(tx, 0, tw, tab_height);

        ctx->setColor(0.35, 0.35, 0.40);
        ctx->line(tx + tw, 6, tx + tw, tab_height - 6, 1);

        ctx->setColor(0.88, 0.88, 0.92);
        ctx->drawText(tx + 16, 22, pages[i]->getTitle(), 13);
    }

    ctx->setColor(0.22, 0.22, 0.28);
    ctx->line(0, tab_height, width, tab_height, 1);

    Ref<ElaraTabPage> page = activePage();

    if(page && page->getWidget()) {
        page->getWidget()->setBounds(0, tab_height, width, height - tab_height);
        page->getWidget()->draw(ctx);
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
