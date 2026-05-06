#include "ElaraPopupWidget.h"
#include "ElaraRootWidget.h"

namespace elara {

namespace {

String displayTextForMnemonic(const String& text, int* mnemonic_index) {
    String result;
    int visible_index = 0;
    bool marker_found = false;

    if(mnemonic_index) {
        *mnemonic_index = -1;
    }

    for(int i = 0; i < text.length(); i++) {
        char ch = text.byteAt(i);

        if(ch == '&') {
            if(i + 1 < text.length() && text.byteAt(i + 1) == '&') {
                result += String('&');
                visible_index++;
                i++;
                continue;
            }

            if(!marker_found && i + 1 < text.length()) {
                marker_found = true;
                if(mnemonic_index) {
                    *mnemonic_index = visible_index;
                }
            }
            continue;
        }

        result += String(ch);
        visible_index++;
    }

    return result;
}

unsigned int normalizeMnemonicKey(unsigned int keyval) {
    if(keyval >= 'A' && keyval <= 'Z') {
        return keyval - 'A' + 'a';
    }

    return keyval;
}

unsigned int mnemonicKeyForText(const String& text) {
    int explicit_index = -1;
    String display = displayTextForMnemonic(text, &explicit_index);

    if(explicit_index >= 0 && explicit_index < display.length()) {
        return normalizeMnemonicKey((unsigned char)display.byteAt(explicit_index));
    }

    for(int i = 0; i < display.length(); i++) {
        unsigned char ch = (unsigned char)display.byteAt(i);
        if((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) {
            return normalizeMnemonicKey(ch);
        }
    }

    return 0;
}

}

ElaraPopupItem::ElaraPopupItem()
    : enabled(true),
      separator(false) {}

ElaraPopupItem::ElaraPopupItem(const String& item_id, const String& item_text)
    : id(item_id),
      text(item_text),
      enabled(true),
      separator(false) {}

String ElaraPopupItem::getId() const {
    return id;
}

String ElaraPopupItem::getText() const {
    return text;
}

String ElaraPopupItem::getShortcut() const {
    return shortcut;
}

void ElaraPopupItem::setShortcut(const String& value) {
    shortcut = value;
}

bool ElaraPopupItem::isEnabled() const {
    return enabled;
}

void ElaraPopupItem::setEnabled(bool value) {
    enabled = value;
}

bool ElaraPopupItem::isSeparator() const {
    return separator;
}

void ElaraPopupItem::setSeparator(bool value) {
    separator = value;
}

bool ElaraPopupItem::hasSubmenu() const {
    return submenu_handle.length() > 0 && submenu_widget.getPtr();
}

String ElaraPopupItem::getSubmenuHandle() const {
    return submenu_handle;
}

Ref<ElaraWidget> ElaraPopupItem::getSubmenuWidget() const {
    return submenu_widget;
}

void ElaraPopupItem::setSubmenu(const String& handle, Ref<ElaraWidget> widget) {
    submenu_handle = handle;
    submenu_widget = widget;
}

ElaraPopupWidget::ElaraPopupWidget(ElaraWidgetRegister* root_widget, ElaraWidgetHandle widget_handle)
    : ElaraWidget(root_widget, widget_handle), 
      visible(false),
      hover_index(-1),
      item_height(28),
      padding(8) {
    width = 180;
    height = 0;
}

void ElaraPopupWidget::showAt(double px, double py) {
    x = px;
    y = py;
    height = padding * 2 + items.length() * item_height;
    visible = true;
}

void ElaraPopupWidget::hide() {
    visible = false;
    hover_index = -1;
}

bool ElaraPopupWidget::isVisible() const {
    return visible;
}

void ElaraPopupWidget::clearItems() {
    closeDescendantPopups();
    items.clear();
    height = padding * 2;
}

void ElaraPopupWidget::addItem(
    const String& id,
    const String& text,
    bool enabled,
    bool separator,
    const String& shortcut,
    const String& submenu_handle,
    Ref<ElaraWidget> submenu_widget
) {
    ElaraPopupItem item(id, text);
    item.setEnabled(enabled);
    item.setSeparator(separator);
    item.setShortcut(shortcut);
    item.setSubmenu(submenu_handle, submenu_widget);
    items.push(item);
    height = padding * 2 + items.length() * item_height;
}

ElaraMouseCursor ElaraPopupWidget::cursor() const {
    return ELARA_CURSOR_POINTER;
}

int ElaraPopupWidget::itemCount() const {
    return (int)items.length();
}

int ElaraPopupWidget::itemAt(double px, double py) const {
    if(!visible) {
        return -1;
    }

    if(px < x || py < y || px > x + width || py > y + height) {
        return -1;
    }

    double local_y = py - y - padding;

    if(local_y < 0) {
        return -1;
    }

    int index = (int)(local_y / item_height);

    if(index < 0 || index >= (int)items.length()) {
        return -1;
    }

    return index;
}

int ElaraPopupWidget::firstSelectableIndex() const {
    for(int i = 0; i < (int)items.length(); i++) {
        if(!items[i].isSeparator() && items[i].isEnabled()) {
            return i;
        }
    }

    return -1;
}

int ElaraPopupWidget::moveSelection(int start, int delta) const {
    if(items.length() <= 0 || delta == 0) {
        return start;
    }

    int index = start;

    for(int i = 0; i < (int)items.length(); i++) {
        index += delta;

        if(index < 0) {
            index = (int)items.length() - 1;
        } else if(index >= (int)items.length()) {
            index = 0;
        }

        if(!items[index].isSeparator() && items[index].isEnabled()) {
            return index;
        }
    }

    return start;
}

int ElaraPopupWidget::selectedIndex() const {
    return hover_index;
}

bool ElaraPopupWidget::hasParentPopup() const {
    return dynamic_cast<ElaraPopupWidget*>(parent) != 0;
}

double ElaraPopupWidget::itemTop(int index) const {
    return y + padding + index * item_height;
}

ElaraRootWidget* ElaraPopupWidget::rootWidget() const {
    ElaraWidget* cursor = parent;

    while(cursor && cursor->getParent()) {
        cursor = cursor->getParent();
    }

    return cursor ? dynamic_cast<ElaraRootWidget*>(cursor) : 0;
}

void ElaraPopupWidget::closeDescendantPopups() {
    ElaraRootWidget* root = rootWidget();
    if(root) {
        root->dismissPopupsAfter(getHandle());
    }
}

void ElaraPopupWidget::openSubmenu(int index) {
    if(index < 0 || index >= (int)items.length()) {
        closeDescendantPopups();
        return;
    }

    if(!items[index].hasSubmenu() || !items[index].isEnabled()) {
        closeDescendantPopups();
        return;
    }

    ElaraRootWidget* root = rootWidget();
    if(!root) {
        return;
    }

    Ref<ElaraWidget> submenu_widget = items[index].getSubmenuWidget();
    ElaraPopupWidget* submenu = submenu_widget
        ? dynamic_cast<ElaraPopupWidget*>(submenu_widget.getPtr())
        : 0;

    if(!submenu) {
        closeDescendantPopups();
        return;
    }

    root->dismissPopupsAfter(getHandle());
    root->pushPopup(submenu->getHandle());
    submenu->setPalette(palette);
    submenu->showAt(x + width - 1, itemTop(index));
}

void ElaraPopupWidget::selectFirstItem() {
    hover_index = firstSelectableIndex();

    if(hover_index >= 0 && items[hover_index].hasSubmenu()) {
        openSubmenu(hover_index);
    }
}

void ElaraPopupWidget::selectLastItem() {
    int first = firstSelectableIndex();
    if(first < 0) {
        hover_index = -1;
        return;
    }

    int index = first;
    for(int i = 0; i < (int)items.length(); i++) {
        int next = moveSelection(index, 1);
        if(next == index) {
            break;
        }
        index = next;
    }

    hover_index = index;

    if(hover_index >= 0 && items[hover_index].hasSubmenu()) {
        openSubmenu(hover_index);
    }
}

void ElaraPopupWidget::clearSelection() {
    hover_index = -1;
}

void ElaraPopupWidget::draw(ElaraDrawContext* ctx) {
    if(!visible) {
        return;
    }

    ElaraPaletteTriplet popup_colors = colors("popup", "default");
    ElaraPaletteTriplet hover_colors = colors("popup", "hover");

    ElaraColor bg = popup_colors.base;
    ctx->setColor(bg.r, bg.g, bg.b);
    ctx->fillRect(x, y, width, height);

    ElaraColor border = popup_colors.accent;
    ctx->setColor(border.r, border.g, border.b);
    ctx->line(x, y, x + width, y, 1);
    ctx->line(x, y + height, x + width, y + height, 1);
    ctx->line(x, y, x, y + height, 1);
    ctx->line(x + width, y, x + width, y + height, 1);

    for(int i = 0; i < (int)items.length(); i++) {
        double iy = y + padding + i * item_height;

        if(items[i].isSeparator()) {
            ctx->setColor(border.r, border.g, border.b);
            ctx->line(x + 8, iy + item_height / 2.0, x + width - 8, iy + item_height / 2.0, 1);
            continue;
        }

        if(i == hover_index) {
            ElaraColor h = hover_colors.base;
            ctx->setColor(h.r, h.g, h.b);
            ctx->fillRect(x + 4, iy, width - 8, item_height);
        }

        ElaraColor text_color = items[i].isEnabled()
            ? popup_colors.text
            : popup_colors.accent;

        int mnemonic_index = -1;
        String display_text = displayTextForMnemonic(items[i].getText(), &mnemonic_index);
        ctx->setColor(text_color.r, text_color.g, text_color.b);
        ctx->drawText(x + 12, iy + 19, display_text, 13);

        if(mnemonic_index < 0) {
            for(int j = 0; j < display_text.length(); j++) {
                unsigned char ch = (unsigned char)display_text.byteAt(j);
                if((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) {
                    mnemonic_index = j;
                    break;
                }
            }
        }

        if(mnemonic_index >= 0 && mnemonic_index < display_text.length()) {
            String prefix = display_text.substr(0, mnemonic_index);
            String mnemonic_char = display_text.substr(mnemonic_index, 1);
            double underline_x = x + 12 + ctx->measureTextWidth(prefix, 13);
            double underline_w = ctx->measureTextWidth(mnemonic_char, 13);
            ctx->line(underline_x, iy + 21, underline_x + underline_w, iy + 21, 1);
        }

        if(items[i].getShortcut().length() > 0) {
            double shortcut_width = ctx->measureTextWidth(items[i].getShortcut(), 12);
            double shortcut_x = x + width - shortcut_width - (items[i].hasSubmenu() ? 30 : 14);
            ctx->drawText(shortcut_x, iy + 19, items[i].getShortcut(), 12);
        }

        if(items[i].hasSubmenu()) {
            ctx->drawText(x + width - 18, iy + 19, ">", 13);
        }
    }
}

void ElaraPopupWidget::onMouseMove(double px, double py) {
    hover_index = itemAt(px, py);

    if(hover_index >= 0 && !items[hover_index].isSeparator()) {
        openSubmenu(hover_index);
    } else {
        closeDescendantPopups();
    }
}

void ElaraPopupWidget::onMouseDown(int button, double px, double py) {
    int index = itemAt(px, py);

    if(index < 0) {
        closeDescendantPopups();
        hide();
        return;
    }

    if(items[index].isSeparator()) {
        return;
    }

    if(items[index].hasSubmenu() && items[index].isEnabled()) {
        openSubmenu(index);
        return;
    }

    if(items[index].isEnabled()) {
        onItemSelected(items[index].getId());
    }

    ElaraRootWidget* root = rootWidget();
    if(root) {
        root->dismissAllPopups();
    } else {
        hide();
    }
}

void ElaraPopupWidget::onMouseUp(int button, double px, double py) {
}

void ElaraPopupWidget::onKeyDown(unsigned int keyval) {
    if(!visible) {
        return;
    }

    unsigned int mnemonic_key = normalizeMnemonicKey(keyval);
    if((mnemonic_key >= 'a' && mnemonic_key <= 'z') || (mnemonic_key >= '0' && mnemonic_key <= '9')) {
        int start = hover_index >= 0 ? hover_index + 1 : 0;

        for(int pass = 0; pass < 2; pass++) {
            int from = pass == 0 ? start : 0;
            int to = pass == 0 ? (int)items.length() : start;

            for(int i = from; i < to; i++) {
                if(items[i].isSeparator() || !items[i].isEnabled()) {
                    continue;
                }

                if(mnemonicKeyForText(items[i].getText()) != mnemonic_key) {
                    continue;
                }

                hover_index = i;

                if(items[i].hasSubmenu()) {
                    openSubmenu(i);
                    Ref<ElaraWidget> submenu_widget = items[i].getSubmenuWidget();
                    ElaraPopupWidget* submenu = submenu_widget
                        ? dynamic_cast<ElaraPopupWidget*>(submenu_widget.getPtr())
                        : 0;
                    if(submenu) {
                        submenu->selectFirstItem();
                    }
                } else {
                    onItemSelected(items[i].getId());
                    ElaraRootWidget* root = rootWidget();
                    if(root) {
                        root->dismissAllPopups();
                    } else {
                        hide();
                    }
                }

                return;
            }
        }
    }

    if(keyval == 65362) {
        int start = hover_index >= 0 ? hover_index : firstSelectableIndex();
        hover_index = start >= 0 ? moveSelection(start, -1) : -1;
        closeDescendantPopups();
        return;
    }

    if(keyval == 65364) {
        int start = hover_index >= 0 ? hover_index : firstSelectableIndex();
        hover_index = start >= 0 ? moveSelection(start, 1) : -1;
        closeDescendantPopups();
        return;
    }

    if(keyval == 65363) {
        if(hover_index >= 0 && items[hover_index].hasSubmenu() && items[hover_index].isEnabled()) {
            openSubmenu(hover_index);
            Ref<ElaraWidget> submenu_widget = items[hover_index].getSubmenuWidget();
            ElaraPopupWidget* submenu = submenu_widget
                ? dynamic_cast<ElaraPopupWidget*>(submenu_widget.getPtr())
                : 0;
            if(submenu) {
                submenu->selectFirstItem();
            }
        }
        return;
    }

    if(keyval == 65361) {
        if(hasParentPopup()) {
            closeDescendantPopups();
            hide();
            ElaraRootWidget* root = rootWidget();
            if(root) {
                root->removePopup(getHandle());
            }
        }
        return;
    }

    if(keyval == 65293 || keyval == 32) {
        if(hover_index < 0 || items[hover_index].isSeparator()) {
            return;
        }

        if(items[hover_index].hasSubmenu() && items[hover_index].isEnabled()) {
            openSubmenu(hover_index);
            Ref<ElaraWidget> submenu_widget = items[hover_index].getSubmenuWidget();
            ElaraPopupWidget* submenu = submenu_widget
                ? dynamic_cast<ElaraPopupWidget*>(submenu_widget.getPtr())
                : 0;
            if(submenu) {
                submenu->selectFirstItem();
            }
            return;
        }

        if(items[hover_index].isEnabled()) {
            onItemSelected(items[hover_index].getId());
        }

        ElaraRootWidget* root = rootWidget();
        if(root) {
            root->dismissAllPopups();
        } else {
            hide();
        }
        return;
    }

    if(keyval == 65307) {
        ElaraRootWidget* root = rootWidget();
        if(root) {
            root->dismissAllPopups();
        } else {
            hide();
        }
    }
}

}
