#ifndef ELARA_CHAT_DIALOG_WIDGET_H
#define ELARA_CHAT_DIALOG_WIDGET_H

#include <libelaracore/memory/String.h>
#include <libelaracore/memory/Array.h>
#include <libelaracore/memory/Ref.h>

#include "ElaraWidget.h"

namespace elara {

struct ChatMessage {
    String role;
    String display;

    ChatMessage() {}
    ChatMessage(const String& r, const String& d) : role(r), display(d) {}
};

struct WrappedChatMessage {
    String role;
    Array<String> lines;
    double y_offset;
    double bubble_w;
    double bubble_h;

    WrappedChatMessage() : y_offset(0), bubble_w(0), bubble_h(0) {}
};

class ElaraChatDialogWidget : public ElaraWidget {
private:
    Array< Ref<ChatMessage> > messages;
    Array< Ref<WrappedChatMessage> > wrapped;

    double scroll_y;
    double total_height;
    double last_width;
    bool needs_recompute;
    bool scroll_to_bottom_pending;

    bool sb_dragging;
    double sb_drag_start_y;
    double sb_drag_scroll_start;

    static const double FONT_SIZE;
    static const double LINE_HEIGHT;
    static const double PADDING;
    static const double BUB_PAD_X;
    static const double BUB_PAD_Y;
    static const double MSG_GAP;
    static const double SB_WIDTH;
    static const double USER_MAX_RATIO;

    double contentWidth() const;
    double viewportHeight() const;
    double sbThumbH() const;
    double sbThumbY() const;
    bool sbHit(double px, double py) const;
    void clampScroll();

    void recompute(ElaraDrawContext* ctx);
    Array<String> wrapText(ElaraDrawContext* ctx,
                           const String& text,
                           double max_width) const;

public:
    ElaraChatDialogWidget(ElaraWidgetRegister* root_widget,
                          ElaraWidgetHandle widget_handle);

    void setMessages(const String& json_str);
    void clearMessages();

    void draw(ElaraDrawContext* ctx);
    ElaraMouseCursor cursor() const;
    void onMouseDown(int button, double px, double py);
    void onMouseUp(int button, double px, double py);
    void onMouseMove(double px, double py);
    void onKeyDown(unsigned int keyval);
};

}

#endif
