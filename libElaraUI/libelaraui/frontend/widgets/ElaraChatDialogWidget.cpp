#include "ElaraChatDialogWidget.h"

#include <libelaraformat/json/Json.h>
#include <libelaraformat/json/types/JsonValue.h>

static const unsigned int CHAT_KEY_UP        = 0xff52;
static const unsigned int CHAT_KEY_DOWN      = 0xff54;
static const unsigned int CHAT_KEY_PAGE_UP   = 0xff55;
static const unsigned int CHAT_KEY_PAGE_DOWN = 0xff56;
static const unsigned int CHAT_KEY_HOME      = 0xff50;
static const unsigned int CHAT_KEY_END       = 0xff57;

namespace elara {

const double ElaraChatDialogWidget::FONT_SIZE    = 13.0;
const double ElaraChatDialogWidget::LINE_HEIGHT  = 20.0;
const double ElaraChatDialogWidget::PADDING      = 10.0;
const double ElaraChatDialogWidget::BUB_PAD_X    = 10.0;
const double ElaraChatDialogWidget::BUB_PAD_Y    = 7.0;
const double ElaraChatDialogWidget::MSG_GAP      = 10.0;
const double ElaraChatDialogWidget::SB_WIDTH     = 8.0;
const double ElaraChatDialogWidget::USER_MAX_RATIO = 0.78;

ElaraChatDialogWidget::ElaraChatDialogWidget(
    ElaraWidgetRegister* root_widget,
    ElaraWidgetHandle widget_handle
) : ElaraWidget(root_widget, widget_handle),
    scroll_y(0),
    total_height(0),
    last_width(-1),
    needs_recompute(false),
    scroll_to_bottom_pending(false),
    sb_dragging(false),
    sb_drag_start_y(0),
    sb_drag_scroll_start(0)
{}

void ElaraChatDialogWidget::setMessages(const String& json_str) {
    messages.clear();
    Json json(json_str);
    Array< Ref<JsonValue> > arr = json.getArray("messages");

    for(int i = 0; i < (int)arr.length(); i++) {
        if(!arr[i]) continue;
        Json item(arr[i]->toString());
        String role = item.getStringValue("role");
        String display = item.getStringValue("display");
        messages.push(Ref<ChatMessage>(new ChatMessage(role, display)));
    }

    needs_recompute = true;
    scroll_to_bottom_pending = true;
}

void ElaraChatDialogWidget::clearMessages() {
    messages.clear();
    needs_recompute = true;
    scroll_y = 0;
    total_height = 0;
}

double ElaraChatDialogWidget::contentWidth() const {
    return width - 2.0 * PADDING - SB_WIDTH;
}

double ElaraChatDialogWidget::viewportHeight() const {
    return height;
}

double ElaraChatDialogWidget::sbThumbH() const {
    if(total_height <= 0) return viewportHeight();
    double ratio = viewportHeight() / total_height;
    if(ratio >= 1.0) return viewportHeight();
    double h = viewportHeight() * ratio;
    return h < 20.0 ? 20.0 : h;
}

double ElaraChatDialogWidget::sbThumbY() const {
    double scrollable = total_height - viewportHeight();
    if(scrollable <= 0) return 0;
    double track = viewportHeight() - sbThumbH();
    return (scroll_y / scrollable) * track;
}

bool ElaraChatDialogWidget::sbHit(double px, double py) const {
    double sb_x = width - SB_WIDTH;
    double ty = sbThumbY();
    double th = sbThumbH();
    return px >= sb_x && px <= width && py >= ty && py <= ty + th;
}

void ElaraChatDialogWidget::clampScroll() {
    double max_scroll = total_height - viewportHeight();
    if(max_scroll < 0) max_scroll = 0;
    if(scroll_y > max_scroll) scroll_y = max_scroll;
    if(scroll_y < 0) scroll_y = 0;
}

Array<String> ElaraChatDialogWidget::wrapText(
    ElaraDrawContext* ctx,
    const String& text,
    double max_width
) const {
    Array<String> result;
    int len = (int)text.length();

    int line_start = 0;
    while(line_start <= len) {
        int line_end = line_start;
        while(line_end < len && text.byteAt(line_end) != '\n') {
            line_end++;
        }

        String logical = text.substr(line_start, line_end - line_start);
        line_start = line_end + 1;

        int ll = (int)logical.length();
        if(ll == 0) {
            result.push(String());
        } else {
            String current;
            int wstart = 0;

            while(wstart <= ll) {
                int wend = wstart;
                while(wend < ll && logical.byteAt(wend) != ' ') wend++;

                if(wend > wstart) {
                    String word = logical.substr(wstart, wend - wstart);
                    String candidate = ((int)current.length() > 0)
                        ? current + String(" ") + word
                        : word;

                    if(ctx->measureTextWidth(candidate, FONT_SIZE) <= max_width ||
                       (int)current.length() == 0) {
                        current = candidate;
                    } else {
                        result.push(current);
                        current = word;
                    }
                }
                wstart = wend + 1;
            }

            if((int)current.length() > 0) {
                result.push(current);
            }
        }

        if(line_start > len) break;
    }

    if((int)result.length() == 0) {
        result.push(String());
    }

    return result;
}

void ElaraChatDialogWidget::recompute(ElaraDrawContext* ctx) {
    wrapped.clear();

    double cw = contentWidth();
    double user_max = cw * USER_MAX_RATIO;
    double y = PADDING;

    for(int i = 0; i < (int)messages.length(); i++) {
        if(!messages[i]) continue;

        bool is_user = messages[i]->role == String("user");
        double max_w = is_user ? user_max : cw;

        Array<String> lines = wrapText(ctx, messages[i]->display, max_w - 2.0 * BUB_PAD_X);

        double bub_w = 0;
        for(int j = 0; j < (int)lines.length(); j++) {
            double lw = ctx->measureTextWidth(lines[j], FONT_SIZE);
            if(lw > bub_w) bub_w = lw;
        }
        bub_w += 2.0 * BUB_PAD_X;
        if(bub_w < 40.0) bub_w = 40.0;
        if(bub_w > max_w) bub_w = max_w;

        double bub_h = (int)lines.length() * LINE_HEIGHT + 2.0 * BUB_PAD_Y;

        Ref<WrappedChatMessage> wm(new WrappedChatMessage());
        wm->role = messages[i]->role;
        wm->lines = lines;
        wm->y_offset = y;
        wm->bubble_w = bub_w;
        wm->bubble_h = bub_h;
        wrapped.push(wm);

        y += bub_h + MSG_GAP;
    }

    total_height = y + PADDING;
    last_width = width;
    needs_recompute = false;

    if(scroll_to_bottom_pending) {
        scroll_y = total_height - viewportHeight();
        scroll_to_bottom_pending = false;
        clampScroll();
    }
}

void ElaraChatDialogWidget::draw(ElaraDrawContext* ctx) {
    if(needs_recompute || width != last_width) {
        recompute(ctx);
    }

    ElaraPaletteTriplet bg = colors(String("panel"), String("default"));
    ElaraPaletteTriplet user_c = colors(String("tabs"), String("active"));
    ElaraPaletteTriplet ai_c = colors(String("editor"), String("default"));
    ElaraPaletteTriplet sb_c = colors(String("panel"), String("default"));

    // Background
    ctx->setColor(bg.base.r, bg.base.g, bg.base.b);
    ctx->fillRect(0, 0, width, height);

    double cw = contentWidth();

    for(int i = 0; i < (int)wrapped.length(); i++) {
        if(!wrapped[i]) continue;
        WrappedChatMessage& wm = *wrapped[i].getPtr();

        double by = wm.y_offset - scroll_y;

        if(by + wm.bubble_h < 0) continue;
        if(by > height) break;

        bool is_user = wm.role == String("user");

        double bx;
        if(is_user) {
            bx = PADDING + cw - wm.bubble_w;
        } else {
            bx = PADDING;
        }

        ElaraPaletteTriplet& c = is_user ? user_c : ai_c;

        // Slightly differentiate assistant background
        double base_r, base_g, base_b;
        if(is_user) {
            base_r = c.base.r;
            base_g = c.base.g;
            base_b = c.base.b;
        } else {
            // Slightly tint the assistant panel relative to the background
            base_r = bg.base.r * 0.90;
            base_g = bg.base.g * 0.90;
            base_b = bg.base.b * 0.90;
        }

        // Bubble background
        ctx->setColor(base_r, base_g, base_b);
        ctx->fillRect(bx, by, wm.bubble_w, wm.bubble_h);

        // Bubble border
        ctx->setColor(c.accent.r, c.accent.g, c.accent.b);
        ctx->line(bx, by, bx + wm.bubble_w, by, 1);
        ctx->line(bx, by + wm.bubble_h, bx + wm.bubble_w, by + wm.bubble_h, 1);
        ctx->line(bx, by, bx, by + wm.bubble_h, 1);
        ctx->line(bx + wm.bubble_w, by, bx + wm.bubble_w, by + wm.bubble_h, 1);

        // Text
        ctx->setColor(c.text.r, c.text.g, c.text.b);
        for(int j = 0; j < (int)wm.lines.length(); j++) {
            double ty = by + BUB_PAD_Y + (j + 1) * LINE_HEIGHT - 4.0;
            ctx->drawText(bx + BUB_PAD_X, ty, wm.lines[j], FONT_SIZE);
        }
    }

    // Scrollbar
    if(total_height > viewportHeight()) {
        double sb_x = width - SB_WIDTH;

        // Track
        ctx->setColor(sb_c.base.r * 0.85, sb_c.base.g * 0.85, sb_c.base.b * 0.85);
        ctx->fillRect(sb_x, 0, SB_WIDTH, height);

        // Thumb
        double ty = sbThumbY();
        double th = sbThumbH();
        ctx->setColor(sb_c.accent.r * 0.7, sb_c.accent.g * 0.7, sb_c.accent.b * 0.7);
        ctx->fillRect(sb_x + 1, ty + 1, SB_WIDTH - 2, th - 2);
    }
}

ElaraMouseCursor ElaraChatDialogWidget::cursor() const {
    return ELARA_CURSOR_DEFAULT;
}

void ElaraChatDialogWidget::onMouseDown(int button, double px, double py) {
    if(button != 1) return;

    if(px >= width - SB_WIDTH) {
        if(sbHit(px, py)) {
            sb_dragging = true;
            sb_drag_start_y = py;
            sb_drag_scroll_start = scroll_y;
        } else {
            // Click outside thumb — jump to position
            double scrollable = total_height - viewportHeight();
            if(scrollable > 0) {
                double ratio = py / viewportHeight();
                scroll_y = ratio * scrollable;
                clampScroll();
            }
        }
    }
}

void ElaraChatDialogWidget::onMouseUp(int button, double px, double py) {
    (void)button; (void)px; (void)py;
    sb_dragging = false;
}

void ElaraChatDialogWidget::onMouseMove(double px, double py) {
    (void)px;
    if(!sb_dragging) return;

    double delta = py - sb_drag_start_y;
    double scrollable = total_height - viewportHeight();
    double track = viewportHeight() - sbThumbH();

    if(track > 0 && scrollable > 0) {
        scroll_y = sb_drag_scroll_start + delta * (scrollable / track);
        clampScroll();
    }
}

void ElaraChatDialogWidget::onKeyDown(unsigned int keyval) {
    double step = LINE_HEIGHT * 3.0;

    if(keyval == CHAT_KEY_UP) {
        scroll_y -= step;
    } else if(keyval == CHAT_KEY_DOWN) {
        scroll_y += step;
    } else if(keyval == CHAT_KEY_PAGE_UP) {
        scroll_y -= viewportHeight() - LINE_HEIGHT;
    } else if(keyval == CHAT_KEY_PAGE_DOWN) {
        scroll_y += viewportHeight() - LINE_HEIGHT;
    } else if(keyval == CHAT_KEY_HOME) {
        scroll_y = 0;
    } else if(keyval == CHAT_KEY_END) {
        scroll_y = total_height;
    } else {
        return;
    }

    clampScroll();
}

}
