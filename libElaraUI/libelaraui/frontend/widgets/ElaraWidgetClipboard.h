#ifndef ELARA_WIDGET_CLIPBOARD_H
#define ELARA_WIDGET_CLIPBOARD_H

#include <libelaracore/memory/String.h>

namespace elara {

inline String& elaraWidgetClipboard() {
    static String clipboard;
    return clipboard;
}

inline void setElaraWidgetClipboard(const String& text) {
    elaraWidgetClipboard() = text;
}

}

#endif
