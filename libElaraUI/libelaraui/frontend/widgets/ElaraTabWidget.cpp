#include "ElaraTabWidget.h"

namespace elara {

ElaraTabPage::ElaraTabPage() {}

ElaraTabPage::ElaraTabPage(
    const String& tab_title,
    ElaraWidget* tab_widget
) : title(tab_title),
    widget(tab_widget) {}

ElaraTabPage::ElaraTabPage(
    const String& tab_title,
    ElaraWidget* tab_widget,
    const String& btn_glyph,
    const String& btn_action
) : title(tab_title),
    widget(tab_widget),
    button_glyph(btn_glyph),
    button_action(btn_action) {}

String ElaraTabPage::getTitle() const { return title; }
Ref<ElaraWidget> ElaraTabPage::getWidget() const { return widget; }
String ElaraTabPage::getButtonGlyph() const { return button_glyph; }
String ElaraTabPage::getButtonAction() const { return button_action; }
bool ElaraTabPage::hasButton() const { return button_glyph.length() > 0; }

ElaraTabWidget::ElaraTabWidget(ElaraWidgetRegister* root_widget, ElaraWidgetHandle widget_handle)
    : ElaraWidget(root_widget, widget_handle),
      active_index(-1),
      hover_index(-1),
      hover_button_index(-1),
      pressed_button_index(-1),
      tab_height(34),
      tab_button_size(16),
      tab_button_margin(8) {
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

int ElaraTabWidget::addTab(
    const String& title,
    ElaraWidget* widget,
    const String& button_glyph,
    const String& button_action
) {
    if(widget) {
        widget->setPalette(palette);
        addChild(Ref<ElaraWidget>(widget));
    }

    Ref<ElaraTabPage> page(new ElaraTabPage(title, widget, button_glyph, button_action));
    pages.push(page);

    if(active_index < 0) {
        setActiveTab(0);
    } else if(widget) {
        widget->setVisible(false);
    }

    return (int)pages.length() - 1;
}

void ElaraTabWidget::removeTab(int index) {
    if(index < 0 || index >= (int)pages.length()) {
        return;
    }

    // Hide the widget immediately so eventPropagate skips it.
    // Do NOT free or touch the children array here — we are mid-event-dispatch
    // and the Ref in children is non-owning; the ElaraTabPage is the actual
    // owner.  Move the page into orphaned_pages so the widget stays alive
    // until draw() runs (outside event dispatch), at which point it is safe
    // to release.
    if(pages[index] && pages[index]->getWidget()) {
        pages[index]->getWidget()->setVisible(false);
    }
    orphaned_pages.push(pages[index]);

    Array< Ref<ElaraTabPage> > new_pages;
    for(int i = 0; i < (int)pages.length(); i++) {
        if(i != index) {
            new_pages.push(pages[i]);
        }
    }
    pages = new_pages;

    // Adjust active_index after the array shrinks.
    if(active_index > index) {
        active_index--;
    } else if(active_index == index) {
        if(active_index >= (int)pages.length()) {
            active_index = (int)pages.length() - 1;
        }
        if(active_index >= 0) {
            setActiveTab(active_index);
        } else {
            active_index = -1;
        }
    }

    hover_index = -1;
    hover_button_index = -1;
    pressed_button_index = -1;
}

void ElaraTabWidget::clearChildren() {
    orphaned_pages.clear();
    pages.clear();
    active_index = -1;
    hover_index = -1;
    hover_button_index = -1;
    pressed_button_index = -1;
    ElaraWidget::clearChildren();
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

int ElaraTabWidget::getActiveTab() const { return active_index; }
int ElaraTabWidget::tabCount() const { return (int)pages.length(); }

ElaraMouseCursor ElaraTabWidget::cursor() const {
    return ELARA_CURSOR_POINTER;
}

double ElaraTabWidget::tabWidth(int index) const {
    if(index < 0 || index >= (int)pages.length()) {
        return 0;
    }

    String title = pages[index]->getTitle();
    double w = 32 + (double)title.length() * 8;

    if(pages[index]->hasButton()) {
        w += tab_button_size + tab_button_margin;
    }

    return w;
}

double ElaraTabWidget::tabX(int index) const {
    double px = 0;

    for(int i = 0; i < index; i++) {
        px += tabWidth(i);
    }

    return px;
}

double ElaraTabWidget::tabButtonX(int index) const {
    return tabX(index) + tabWidth(index) - tab_button_size - (tab_button_margin / 2.0);
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

int ElaraTabWidget::tabButtonAt(double px, double py) const {
    if(py < 0 || py > tab_height) {
        return -1;
    }

    double by = (tab_height - tab_button_size) / 2.0;

    for(int i = 0; i < (int)pages.length(); i++) {
        if(!pages[i] || !pages[i]->hasButton()) {
            continue;
        }

        double bx = tabButtonX(i);

        if(px >= bx && px <= bx + tab_button_size &&
           py >= by && py <= by + tab_button_size) {
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
    // Safe point outside event dispatch — release any widgets removed since
    // the last frame.
    orphaned_pages.clear();

    ElaraPaletteTriplet bar_colors = colors("tabs", "bar");
    ElaraPaletteTriplet active_colors = colors("tabs", "active");
    ElaraPaletteTriplet hover_colors = colors("tabs", "hover");
    ElaraPaletteTriplet default_colors = colors("tabs", "default");

    applyColor(ctx, bar_colors.base);
    ctx->fillRect(x, y, width, tab_height);

    double by = (tab_height - tab_button_size) / 2.0;

    for(int i = 0; i < (int)pages.length(); i++) {
        double tx = tabX(i);
        double tw = tabWidth(i);

        ElaraPaletteTriplet tab_colors;

        if(i == active_index) {
            tab_colors = colors("tabs", "active");
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

        if(pages[i]->hasButton()) {
            double bx = tabButtonX(i);
            bool btn_hovered = (i == hover_button_index);
            bool btn_pressed = (i == pressed_button_index);

            if(btn_hovered || btn_pressed) {
                ElaraPaletteTriplet btn_colors = btn_pressed
                    ? colors("tabs", "active")
                    : colors("tabs", "hover");
                applyColor(ctx, btn_colors.accent);
                ctx->fillRect(bx, by, tab_button_size, tab_button_size);
            }

            String glyph = pages[i]->getButtonGlyph();
            double glyph_w = ctx->measureTextWidth(glyph, 12);
            applyColor(ctx, tab_colors.text);
            ctx->drawText(
                bx + (tab_button_size - glyph_w) / 2.0,
                by + (tab_button_size / 2.0) + (12.0 / 2.0) - 2,
                glyph,
                12
            );
        }
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
    hover_button_index = tabButtonAt(px, py);

    Ref<ElaraTabPage> page = activePage();

    if(page && page->getWidget() && py > tab_height) {
        ElaraUiEvent event;
        event.type = ELARA_UI_MOUSE_MOVE;
        event.x = px;
        event.y = py - tab_height;
        page->getWidget()->eventPropagate(event);
    }
}

void ElaraTabWidget::onMouseDown(int button, double px, double py) {
    int btn_tab = tabButtonAt(px, py);
    if(btn_tab >= 0) {
        pressed_button_index = btn_tab;
        return;
    }

    pressed_button_index = -1;

    int tab = tabAt(px, py);

    if(tab >= 0) {
        setActiveTab(tab);
        return;
    }

    Ref<ElaraTabPage> page = activePage();

    if(page && page->getWidget() && py > tab_height) {
        ElaraUiEvent event;
        event.type = ELARA_UI_MOUSE_DOWN;
        event.x = px;
        event.y = py - tab_height;
        event.button = button;
        page->getWidget()->eventPropagate(event);
    }
}

void ElaraTabWidget::onMouseUp(int button, double px, double py) {
    if(button == 1 && pressed_button_index >= 0) {
        int release_btn = tabButtonAt(px, py);

        if(release_btn == pressed_button_index &&
           pressed_button_index < (int)pages.length()) {
            String action = pages[pressed_button_index]->getButtonAction();
            removeTab(pressed_button_index);
            pressed_button_index = -1;
            if(action.length() > 0) {
                emitAction(action);
            }
        } else {
            pressed_button_index = -1;
        }
        return;
    }

    pressed_button_index = -1;

    Ref<ElaraTabPage> page = activePage();

    if(page && page->getWidget() && py > tab_height) {
        ElaraUiEvent event;
        event.type = ELARA_UI_MOUSE_UP;
        event.x = px;
        event.y = py - tab_height;
        event.button = button;
        page->getWidget()->eventPropagate(event);
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
