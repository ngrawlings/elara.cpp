#include "ElaraMenuBarWidget.h"

#include "ElaraRootWidget.h"

#include <stdlib.h>
namespace elara {

namespace {

String handleString(const ElaraWidgetHandle& handle) {
    Memory memory = handle.getHandle();
    return String((const char*)memory.getPtr(), memory.length());
}

unsigned int normalizeAcceleratorKey(unsigned int keyval) {
    if(keyval >= 'A' && keyval <= 'Z') {
        return keyval - 'A' + 'a';
    }

    return keyval;
}

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

unsigned int mnemonicKeyForText(const String& text) {
    int explicit_index = -1;
    String display = displayTextForMnemonic(text, &explicit_index);

    if(explicit_index >= 0 && explicit_index < display.length()) {
        return normalizeAcceleratorKey((unsigned char)display.byteAt(explicit_index));
    }

    for(int i = 0; i < display.length(); i++) {
        unsigned char ch = (unsigned char)display.byteAt(i);
        if((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) {
            return normalizeAcceleratorKey(ch);
        }
    }

    return 0;
}

bool parseShortcutToken(
    const String& token,
    unsigned int& keyval,
    unsigned int& modifiers,
    bool& has_key
) {
    String token_copy(token);
    String trimmed = token_copy.trim();
    String lower = trimmed.toLowerCase();

    if(trimmed.length() <= 0) {
        return false;
    }

    if(lower == String("ctrl") || lower == String("control")) {
        modifiers |= ELARA_KEY_MOD_CTRL;
        return true;
    }

    if(lower == String("shift")) {
        modifiers |= ELARA_KEY_MOD_SHIFT;
        return true;
    }

    if(lower == String("alt")) {
        modifiers |= ELARA_KEY_MOD_ALT;
        return true;
    }

    if(lower == String("meta") || lower == String("super") || lower == String("cmd")) {
        modifiers |= ELARA_KEY_MOD_META;
        return true;
    }

    if(lower == String("left")) {
        keyval = 65361;
        has_key = true;
        return true;
    }

    if(lower == String("up")) {
        keyval = 65362;
        has_key = true;
        return true;
    }

    if(lower == String("right")) {
        keyval = 65363;
        has_key = true;
        return true;
    }

    if(lower == String("down")) {
        keyval = 65364;
        has_key = true;
        return true;
    }

    if(lower == String("tab")) {
        keyval = 65289;
        has_key = true;
        return true;
    }

    if(lower == String("enter") || lower == String("return")) {
        keyval = 65293;
        has_key = true;
        return true;
    }

    if(lower == String("esc") || lower == String("escape")) {
        keyval = 65307;
        has_key = true;
        return true;
    }

    if(trimmed.length() > 1 && (trimmed.byteAt(0) == 'F' || trimmed.byteAt(0) == 'f')) {
        int number = atoi((const char*)trimmed.substr(1));
        if(number >= 1 && number <= 12) {
            keyval = 65469 + (unsigned int)number;
            has_key = true;
            return true;
        }
    }

    if(trimmed.length() == 1) {
        keyval = normalizeAcceleratorKey((unsigned int)(unsigned char)trimmed.byteAt(0));
        has_key = true;
        return true;
    }

    return false;
}

bool parseShortcut(const String& shortcut, unsigned int& keyval, unsigned int& modifiers) {
    keyval = 0;
    modifiers = 0;
    bool has_key = false;
    int start = 0;
    const char* shortcut_chars = (const char*)shortcut;

    for(int i = 0; i <= shortcut.length(); i++) {
        bool at_end = i >= shortcut.length();
        if(!at_end && shortcut.byteAt(i) != '+') {
            continue;
        }

        String token(shortcut_chars + start, i - start);
        if(!parseShortcutToken(token, keyval, modifiers, has_key)) {
            return false;
        }
        start = i + 1;
    }

    if(has_key) {
        keyval = normalizeAcceleratorKey(keyval);
    }

    return has_key;
}

}

class ElaraMenuBarWidget::MenuPopupWidget : public ElaraPopupWidget {
private:
    ElaraMenuBarWidget* owner;
    String menu_id;

public:
    MenuPopupWidget(
        ElaraWidgetRegister* root_widget,
        ElaraWidgetHandle widget_handle,
        ElaraMenuBarWidget* owner_widget,
        const String& owner_menu_id
    ) : ElaraPopupWidget(root_widget, widget_handle),
        owner(owner_widget),
        menu_id(owner_menu_id) {
        setFitToContent(true);
    }

    void onItemSelected(const String& id) {
        if(owner) {
            owner->onMenuAction(menu_id, id);
        }
    }
};

ElaraMenuBarWidget::ElaraMenuBarWidget(
    ElaraWidgetRegister* root_widget,
    ElaraWidgetHandle widget_handle
) : ElaraWidget(root_widget, widget_handle),
    widget_register(root_widget),
    palette_master("tabs"),
    font_size(14),
    item_padding_x(12),
    item_padding_y(7),
    hover_index(-1),
    active_index(-1),
    hover_button(CHROME_BUTTON_NONE),
    hover_custom_button(-1),
    custom_chrome(false),
    window_title(""),
    menu_start_x(0),
    control_button_width(42),
    control_gap(2),
    title_gap(24),
    layout_valid(false) {
}

ElaraMenuBarWidget::~ElaraMenuBarWidget() {}

ElaraRootWidget* ElaraMenuBarWidget::rootWidget() const {
    ElaraWidget* cursor = parent;

    while(cursor && cursor->getParent()) {
        cursor = cursor->getParent();
    }

    return cursor ? dynamic_cast<ElaraRootWidget*>(cursor) : 0;
}

ElaraMenuBarWidget::MenuPopupWidget* ElaraMenuBarWidget::popupForIndex(int index) const {
    if(index < 0 || index >= (int)menus.length()) {
        return 0;
    }

    return menus[index].popup;
}

ElaraMenuBarWidget::MenuPopupWidget* ElaraMenuBarWidget::popupForHandle(const String& popup_handle) const {
    for(int i = 0; i < (int)popup_refs.length(); i++) {
        if(popup_refs[i].handle == popup_handle) {
            return popup_refs[i].popup;
        }
    }

    return 0;
}

ElaraMenuBarWidget::MenuPopupWidget* ElaraMenuBarWidget::createPopup(
    const String& popup_handle,
    ElaraWidget* popup_parent,
    const String& menu_id
) {
    if(popup_handle.length() <= 0) {
        return 0;
    }

    MenuPopupWidget* existing = popupForHandle(popup_handle);
    if(existing) {
        return existing;
    }

    PopupRef popup_ref;
    popup_ref.handle = popup_handle;
    popup_ref.popup = 0;
    popup_ref.widget = Ref<ElaraWidget>(
        new MenuPopupWidget(
            widget_register,
            ElaraWidgetHandle(popup_handle),
            this,
            menu_id
        )
    );

    if(!popup_ref.widget) {
        return 0;
    }

    popup_ref.popup = static_cast<MenuPopupWidget*>(popup_ref.widget.getPtr());
    popup_ref.popup->setParent(popup_parent);
    popup_ref.popup->setPalette(palette);
    popup_refs.push(popup_ref);
    return popup_ref.popup;
}

ElaraMenuBarWidget::MenuPopupWidget* ElaraMenuBarWidget::topVisiblePopup() const {
    ElaraRootWidget* root = rootWidget();
    if(!root) {
        return 0;
    }

    for(int i = root->popupCount() - 1; i >= 0; i--) {
        Ref<ElaraWidget> popup_widget = root->getPopup(i);
        MenuPopupWidget* popup = popup_widget
            ? dynamic_cast<MenuPopupWidget*>(popup_widget.getPtr())
            : 0;

        if(popup && popup->isVisible()) {
            return popup;
        }
    }

    return 0;
}

void ElaraMenuBarWidget::syncActiveMenu() {
    if(active_index < 0 || active_index >= (int)menus.length()) {
        active_index = -1;
        return;
    }

    MenuPopupWidget* popup = popupForIndex(active_index);
    if(!popup || !popup->isVisible()) {
        active_index = -1;
    }
}

void ElaraMenuBarWidget::invalidateLayout() {
    layout_metrics.clear();
    layout_valid = false;
}

void ElaraMenuBarWidget::rebuildLayout(ElaraDrawContext* ctx) {
    invalidateLayout();

    menu_start_x = custom_chrome ? titleAreaWidth(ctx) : 0;
    double cursor = menu_start_x;

    for(int i = 0; i < (int)menus.length(); i++) {
        double text_width = ctx
            ? ctx->measureTextWidth(displayTextForMnemonic(menus[i].label, 0), font_size)
            : displayTextForMnemonic(menus[i].label, 0).length() * font_size * 0.62;

        MenuLayoutMetric metric;
        metric.x = cursor;
        metric.width = text_width + item_padding_x * 2;
        layout_metrics.push(metric);
        cursor += metric.width;
    }

    layout_valid = true;
}

double ElaraMenuBarWidget::controlAreaWidth() const {
    return custom_chrome ? ((control_button_width * 3.0) + (control_gap * 2.0) + 8.0) : 0.0;
}

double ElaraMenuBarWidget::titleAreaWidth(ElaraDrawContext* ctx) const {
    if(!custom_chrome) {
        return 0.0;
    }

    double title_width = window_title.length() > 0
        ? (ctx ? ctx->measureTextWidth(window_title, font_size) : window_title.length() * font_size * 0.62)
        : 0.0;

    double width_with_padding = title_width + item_padding_x * 2.0;
    return width_with_padding + title_gap;
}

ElaraMenuBarWidget::ChromeButton ElaraMenuBarWidget::buttonAt(double px, double py) const {
    if(!custom_chrome || py < 0 || py > height) {
        return CHROME_BUTTON_NONE;
    }

    for(int id = CHROME_BUTTON_MINIMIZE; id <= CHROME_BUTTON_CLOSE; id++) {
        ChromeButton button = (ChromeButton)id;
        double left = buttonLeft(button);
        if(px >= left && px <= left + control_button_width) {
            return button;
        }
    }

    return CHROME_BUTTON_NONE;
}

double ElaraMenuBarWidget::buttonLeft(ChromeButton button) const {
    double base_x = width - controlAreaWidth();
    if(button == CHROME_BUTTON_MINIMIZE) {
        return base_x;
    }
    if(button == CHROME_BUTTON_MAXIMIZE) {
        return base_x + control_button_width + control_gap;
    }
    if(button == CHROME_BUTTON_CLOSE) {
        return base_x + (control_button_width + control_gap) * 2.0;
    }
    return width;
}

double ElaraMenuBarWidget::customButtonsWidth() const {
    int count = (int)custom_buttons.length();
    if(count == 0) return 0.0;
    return count * control_button_width + (count - 1) * control_gap;
}

double ElaraMenuBarWidget::customButtonLeft(int index) const {
    double base_x = custom_chrome
        ? width - controlAreaWidth() - customButtonsWidth()
        : width - customButtonsWidth();
    return base_x + index * (control_button_width + control_gap);
}

int ElaraMenuBarWidget::customButtonAt(double px, double py) const {
    if(py < 0 || py > height) return -1;
    for(int i = 0; i < (int)custom_buttons.length(); i++) {
        double left = customButtonLeft(i);
        if(px >= left && px < left + control_button_width) {
            return i;
        }
    }
    return -1;
}

double ElaraMenuBarWidget::itemWidth(int index, ElaraDrawContext* ctx) const {
    if(index < 0 || index >= (int)menus.length()) {
        return 0;
    }

    ElaraMenuBarWidget* self = const_cast<ElaraMenuBarWidget*>(this);
    if(ctx && !layout_valid) {
        self->rebuildLayout(ctx);
    }

    if(layout_valid && index < (int)layout_metrics.length()) {
        return layout_metrics[index].width;
    }

    double text_width = ctx
        ? ctx->measureTextWidth(displayTextForMnemonic(menus[index].label, 0), font_size)
        : displayTextForMnemonic(menus[index].label, 0).length() * font_size * 0.62;

    return text_width + item_padding_x * 2;
}

double ElaraMenuBarWidget::itemOffsetX(int index, ElaraDrawContext* ctx) const {
    ElaraMenuBarWidget* self = const_cast<ElaraMenuBarWidget*>(this);
    if(ctx && !layout_valid) {
        self->rebuildLayout(ctx);
    }

    if(layout_valid && index >= 0 && index < (int)layout_metrics.length()) {
        return layout_metrics[index].x;
    }

    double offset = 0;

    for(int i = 0; i < index && i < (int)menus.length(); i++) {
        offset += itemWidth(i, ctx);
    }

    return offset;
}

int ElaraMenuBarWidget::itemAt(double px, ElaraDrawContext* ctx) const {
    if(px < 0) {
        return -1;
    }

    ElaraMenuBarWidget* self = const_cast<ElaraMenuBarWidget*>(this);
    if(ctx && !layout_valid) {
        self->rebuildLayout(ctx);
    }

    if(layout_valid) {
        for(int i = 0; i < (int)layout_metrics.length(); i++) {
            double item_x = layout_metrics[i].x;
            double item_w = layout_metrics[i].width;
            if(px >= item_x && px <= item_x + item_w) {
                return i;
            }
        }
        return -1;
    }

    double cursor = menu_start_x;

    for(int i = 0; i < (int)menus.length(); i++) {
        double w = itemWidth(i, ctx);
        if(px >= cursor && px <= cursor + w) {
            return i;
        }
        cursor += w;
    }

    return -1;
}

void ElaraMenuBarWidget::closeMenus() {
    ElaraRootWidget* root = rootWidget();

    for(int i = 0; i < (int)menus.length(); i++) {
        MenuPopupWidget* popup = popupForIndex(i);
        if(popup) {
            popup->hide();
        }
    }

    if(root) {
        root->dismissAllPopups();
    }

    active_index = -1;
}

void ElaraMenuBarWidget::openMenu(int index, ElaraDrawContext* ctx) {
    if(index < 0 || index >= (int)menus.length()) {
        closeMenus();
        return;
    }

    ElaraRootWidget* root = rootWidget();
    MenuPopupWidget* popup = popupForIndex(index);
    if(!root || !popup) {
        return;
    }

    closeMenus();

    double popup_x = getAbsoluteX() + itemOffsetX(index, ctx);
    double popup_y = getAbsoluteY() + height - 1;

    root->pushPopup(popup->getHandle());
    popup->showAt(popup_x, popup_y);
    popup->selectFirstItem();
    root->setFocus(getHandle());
    active_index = index;
}

void ElaraMenuBarWidget::clearMenus() {
    for(int i = 0; i < (int)menus.length(); i++) {
        menus[i].popup = 0;
        menus[i].popup_widget = Ref<ElaraWidget>(0);
    }

    for(int i = 0; i < (int)popup_refs.length(); i++) {
        popup_refs[i].popup = 0;
        popup_refs[i].widget = Ref<ElaraWidget>(0);
    }

    popup_refs.clear();
    accelerators.clear();
    menus.clear();
    invalidateLayout();
    hover_index = -1;
    active_index = -1;
}

void ElaraMenuBarWidget::addButton(const String& id, const String& glyph, const String& action) {
    CustomButton btn;
    btn.id = id;
    btn.glyph = glyph;
    btn.action = action;
    custom_buttons.push(btn);
    invalidateLayout();
}

void ElaraMenuBarWidget::addMenu(const String& menu_id, const String& label) {
    if(menu_id.length() <= 0 || label.length() <= 0) {
        return;
    }

    MenuBarItem item;
    item.id = menu_id;
    item.label = label;
    item.popup_handle = handleString(widget_handle) + String(".popup.") + menu_id;
    item.popup_widget = Ref<ElaraWidget>(0);
    item.popup = createPopup(item.popup_handle, this, menu_id);

    if(item.popup) {
        for(int i = 0; i < (int)popup_refs.length(); i++) {
            if(popup_refs[i].handle == item.popup_handle) {
                item.popup_widget = popup_refs[i].widget;
                break;
            }
        }
    }

    menus.push(item);
    invalidateLayout();
}

void ElaraMenuBarWidget::addMenuItem(
    const String& menu_id,
    const String& item_id,
    const String& label,
    bool enabled,
    const String& shortcut
) {
    for(int i = 0; i < (int)menus.length(); i++) {
        if(menus[i].id == menu_id) {
            addPopupItem(menus[i].popup_handle, menu_id, item_id, label, enabled, shortcut);
            return;
        }
    }
}

void ElaraMenuBarWidget::addMenuSeparator(const String& menu_id) {
    for(int i = 0; i < (int)menus.length(); i++) {
        if(menus[i].id == menu_id) {
            addPopupSeparator(menus[i].popup_handle, menu_id);
            return;
        }
    }
}

void ElaraMenuBarWidget::addPopupItem(
    const String& popup_handle,
    const String& menu_id,
    const String& item_id,
    const String& label,
    bool enabled,
    const String& shortcut,
    bool separator,
    const String& submenu_handle
) {
    MenuPopupWidget* popup = popupForHandle(popup_handle);
    if(!popup) {
        return;
    }

    Ref<ElaraWidget> submenu_widget(0);
    if(submenu_handle.length() > 0) {
        MenuPopupWidget* submenu = createPopup(submenu_handle, popup, menu_id);
        if(submenu) {
            for(int i = 0; i < (int)popup_refs.length(); i++) {
                if(popup_refs[i].handle == submenu_handle) {
                    submenu_widget = popup_refs[i].widget;
                    break;
                }
            }
        }
    }

    popup->addItem(item_id, label, enabled, separator, shortcut, submenu_handle, submenu_widget);

    if(!separator && submenu_handle.length() <= 0 && enabled && shortcut.length() > 0) {
        unsigned int keyval = 0;
        unsigned int modifiers = 0;

        if(parseShortcut(shortcut, keyval, modifiers)) {
            AcceleratorBinding binding;
            binding.action_id = item_id;
            binding.keyval = keyval;
            binding.modifiers = modifiers;
            accelerators.push(binding);
        }
    }
}

void ElaraMenuBarWidget::addPopupSeparator(const String& popup_handle, const String& menu_id) {
    addPopupItem(
        popup_handle,
        menu_id,
        String("__separator__"),
        String(),
        false,
        String(),
        true,
        String()
    );
}

int ElaraMenuBarWidget::menuCount() const {
    return (int)menus.length();
}

String ElaraMenuBarWidget::getActiveMenuId() const {
    if(active_index < 0 || active_index >= (int)menus.length()) {
        return String();
    }

    return menus[active_index].id;
}

void ElaraMenuBarWidget::setFontSize(double size) {
    font_size = size;
    invalidateLayout();
}

double ElaraMenuBarWidget::getFontSize() const {
    return font_size;
}

void ElaraMenuBarWidget::setCustomChrome(bool enabled) {
    custom_chrome = enabled;
    hover_button = CHROME_BUTTON_NONE;
    invalidateLayout();
}

bool ElaraMenuBarWidget::isCustomChrome() const {
    return custom_chrome;
}

void ElaraMenuBarWidget::setWindowTitle(const String& title) {
    window_title = title;
    invalidateLayout();
}

String ElaraMenuBarWidget::getWindowTitle() const {
    return window_title;
}

void ElaraMenuBarWidget::onMenuAction(const String& menu_id, const String& item_id) {
    (void)menu_id;
    emitAction(item_id);
    closeMenus();
}

void ElaraMenuBarWidget::setPalette(ElaraPalette* widget_palette) {
    ElaraWidget::setPalette(widget_palette);

    for(int i = 0; i < (int)popup_refs.length(); i++) {
        if(popup_refs[i].popup) {
            popup_refs[i].popup->setPalette(widget_palette);
        }
    }
}

ElaraMouseCursor ElaraMenuBarWidget::cursor() const {
    return ELARA_CURSOR_POINTER;
}

void ElaraMenuBarWidget::draw(ElaraDrawContext* ctx) {
    syncActiveMenu();
    rebuildLayout(ctx);

    ElaraPaletteTriplet default_colors = colors(palette_master, "default");
    ElaraPaletteTriplet hover_colors = colors(palette_master, "hover");
    ElaraPaletteTriplet active_colors = colors(palette_master, "active");

    ctx->setColor(default_colors.base.r, default_colors.base.g, default_colors.base.b);
    ctx->fillRect(0, 0, width, height);

    ctx->setColor(default_colors.accent.r, default_colors.accent.g, default_colors.accent.b);
    ctx->line(0, height - 1, width, height - 1, 1);

    if(custom_chrome) {
        ctx->setColor(default_colors.text.r, default_colors.text.g, default_colors.text.b);
        ctx->drawText(
            item_padding_x,
            (height / 2.0) + (font_size / 2.0) - 2,
            window_title,
            font_size
        );
    }

    for(int i = 0; i < (int)menus.length(); i++) {
        double item_x = itemOffsetX(i, ctx);
        double item_w = itemWidth(i, ctx);
        bool active = i == active_index;
        bool hover = i == hover_index;
        ElaraPaletteTriplet cell_colors = default_colors;

        if(active) {
            cell_colors = active_colors;
        } else if(hover) {
            cell_colors = hover_colors;
        }

        if(active || hover) {
            ctx->setColor(cell_colors.base.r, cell_colors.base.g, cell_colors.base.b);
            ctx->fillRect(item_x, 0, item_w, height - 1);
        }

        int mnemonic_index = -1;
        String display_label = displayTextForMnemonic(menus[i].label, &mnemonic_index);
        ctx->setColor(cell_colors.text.r, cell_colors.text.g, cell_colors.text.b);
        ctx->drawText(
            item_x + item_padding_x,
            (height / 2.0) + (font_size / 2.0) - 2,
            display_label,
            font_size
        );

        if(mnemonic_index < 0) {
            for(int j = 0; j < display_label.length(); j++) {
                unsigned char ch = (unsigned char)display_label.byteAt(j);
                if((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) {
                    mnemonic_index = j;
                    break;
                }
            }
        }

        if(mnemonic_index >= 0 && mnemonic_index < display_label.length()) {
            String prefix = display_label.substr(0, mnemonic_index);
            String mnemonic_char = display_label.substr(mnemonic_index, 1);
            double underline_x = item_x + item_padding_x + ctx->measureTextWidth(prefix, font_size);
            double underline_w = ctx->measureTextWidth(mnemonic_char, font_size);
            double underline_y = (height / 2.0) + (font_size / 2.0);
            ctx->line(underline_x, underline_y, underline_x + underline_w, underline_y, 1);
        }
    }

    if(custom_chrome) {
        for(int id = CHROME_BUTTON_MINIMIZE; id <= CHROME_BUTTON_CLOSE; id++) {
            ChromeButton button = (ChromeButton)id;
            double bx = buttonLeft(button);
            bool hover = hover_button == button;
            ElaraPaletteTriplet button_colors = hover ? hover_colors : default_colors;

            if(hover) {
                ctx->setColor(button_colors.base.r, button_colors.base.g, button_colors.base.b);
                ctx->fillRect(bx, 0, control_button_width, height - 1);
            }

            if(button == CHROME_BUTTON_CLOSE && hover) {
                ctx->setColor(0.78, 0.22, 0.22);
                ctx->fillRect(bx, 0, control_button_width, height - 1);
                ctx->setColor(1.0, 1.0, 1.0);
            } else {
                ctx->setColor(button_colors.text.r, button_colors.text.g, button_colors.text.b);
            }

            String glyph("-");
            if(button == CHROME_BUTTON_MAXIMIZE) {
                glyph = String("+");
            } else if(button == CHROME_BUTTON_CLOSE) {
                glyph = String("x");
            }

            double glyph_x = bx + (control_button_width / 2.0) - (ctx->measureTextWidth(glyph, font_size) / 2.0);
            ctx->drawText(glyph_x, (height / 2.0) + (font_size / 2.0) - 2, glyph, font_size);
        }
    }

    for(int i = 0; i < (int)custom_buttons.length(); i++) {
        double bx = customButtonLeft(i);
        bool hover = hover_custom_button == i;
        ElaraPaletteTriplet button_colors = hover ? hover_colors : default_colors;

        if(hover) {
            ctx->setColor(button_colors.base.r, button_colors.base.g, button_colors.base.b);
            ctx->fillRect(bx, 0, control_button_width, height - 1);
        }

        ctx->setColor(button_colors.text.r, button_colors.text.g, button_colors.text.b);
        const String& glyph = custom_buttons[i].glyph;
        double glyph_x = bx + (control_button_width / 2.0) - (ctx->measureTextWidth(glyph, font_size) / 2.0);
        ctx->drawText(glyph_x, (height / 2.0) + (font_size / 2.0) - 2, glyph, font_size);
    }
}

void ElaraMenuBarWidget::onMouseMove(double px, double py) {
    syncActiveMenu();
    emitMouseMove(px, py);

    int previous_hover = hover_index;
    int previous_button = hover_button;
    int previous_custom = hover_custom_button;

    hover_button = custom_chrome ? buttonAt(px, py) : CHROME_BUTTON_NONE;
    hover_custom_button = (hover_button == CHROME_BUTTON_NONE && containsLocal(px, py))
        ? customButtonAt(px, py)
        : -1;
    hover_index = (containsLocal(px, py) && hover_button == CHROME_BUTTON_NONE && hover_custom_button < 0)
        ? itemAt(px)
        : -1;

    if(previous_hover != hover_index || previous_button != hover_button || previous_custom != hover_custom_button) {
        emitHoverChanged(hover_index >= 0);
    }

    if(active_index >= 0 && hover_index >= 0 && hover_index != active_index) {
        openMenu(hover_index);
    }
}

void ElaraMenuBarWidget::onMouseDown(int button, double px, double py) {
    syncActiveMenu();
    emitMouseDown(button, px, py);

    if(button != 1) {
        return;
    }

    if(custom_chrome) {
        ChromeButton chrome_button = buttonAt(px, py);
        ElaraRootWidget* root = rootWidget();
        ElaraGuiBackend* backend = root ? root->getGuiBackend() : 0;

        if(chrome_button == CHROME_BUTTON_MINIMIZE) {
            if(backend) {
                backend->minimizeWindow();
            }
            return;
        }

        if(chrome_button == CHROME_BUTTON_MAXIMIZE) {
            if(backend) {
                backend->setWindowMaximized(!backend->isWindowMaximized());
            }
            return;
        }

        if(chrome_button == CHROME_BUTTON_CLOSE) {
            if(backend) {
                backend->closeWindow();
            }
            return;
        }
    }

    int custom_index = containsLocal(px, py) ? customButtonAt(px, py) : -1;
    if(custom_index >= 0) {
        const String& action = custom_buttons[custom_index].action;
        if(action.length() > 0) {
            emitAction(action);
        }
        return;
    }

    int index = containsLocal(px, py) ? itemAt(px) : -1;

    if(index < 0) {
        if(custom_chrome) {
            ElaraRootWidget* root = rootWidget();
            ElaraGuiBackend* backend = root ? root->getGuiBackend() : 0;
            if(backend) {
                backend->beginWindowMove(button, px, py);
                return;
            }
        }
        closeMenus();
        return;
    }

    if(active_index == index) {
        closeMenus();
        return;
    }

    openMenu(index);
}

void ElaraMenuBarWidget::onMouseUp(int button, double px, double py) {
    emitMouseUp(button, px, py);
}

void ElaraMenuBarWidget::onKeyDown(unsigned int keyval) {
    onKeyDown(keyval, 0);
}

void ElaraMenuBarWidget::onKeyDown(unsigned int keyval, unsigned int modifiers) {
    syncActiveMenu();

    unsigned int normalized = normalizeAcceleratorKey(keyval);
    if((normalized >= 'a' && normalized <= 'z') || (normalized >= '0' && normalized <= '9')) {
        for(int offset = 0; offset < (int)menus.length(); offset++) {
            int index = active_index >= 0
                ? (active_index + offset) % menuCount()
                : offset;

            if(mnemonicKeyForText(menus[index].label) == normalized) {
                openMenu(index);
                return;
            }
        }
    }

    if(active_index < 0) {
        return;
    }

    MenuPopupWidget* top_popup = topVisiblePopup();
    bool popup_is_submenu = top_popup && top_popup->hasParentPopup();

    if(keyval == 65361 && !popup_is_submenu) {
        openMenu((active_index + menuCount() - 1) % menuCount());
        return;
    }

    if(keyval == 65363 && !popup_is_submenu) {
        openMenu((active_index + 1) % menuCount());
        return;
    }

    if(keyval == 65364 && top_popup) {
        top_popup->onKeyDown(keyval);
        return;
    }

    if(keyval == 65362 && top_popup) {
        top_popup->onKeyDown(keyval);
        return;
    }

    if(keyval == 65293 || keyval == 32) {
        if(top_popup) {
            top_popup->onKeyDown(keyval);
        }
        return;
    }

    if(keyval == 65307) {
        closeMenus();
        return;
    }

    if(top_popup) {
        top_popup->onKeyDown(keyval);
    }
}

bool ElaraMenuBarWidget::dispatchAccelerator(unsigned int keyval, unsigned int modifiers) {
    unsigned int normalized = normalizeAcceleratorKey(keyval);

    if(keyval == 65479 || keyval == 65513 || keyval == 65514) {
        if(active_index >= 0) {
            closeMenus();
        } else if(menuCount() > 0) {
            openMenu(0);
        }
        return true;
    }

    if((modifiers & ELARA_KEY_MOD_ALT) != 0 &&
       ((normalized >= 'a' && normalized <= 'z') || (normalized >= '0' && normalized <= '9'))) {
        for(int i = 0; i < (int)menus.length(); i++) {
            if(mnemonicKeyForText(menus[i].label) == normalized) {
                openMenu(i);
                return true;
            }
        }
    }

    for(int i = 0; i < (int)accelerators.length(); i++) {
        if(accelerators[i].keyval == normalized && accelerators[i].modifiers == modifiers) {
            onMenuAction(String(), accelerators[i].action_id);
            return true;
        }
    }

    return ElaraWidget::dispatchAccelerator(keyval, modifiers);
}

}
