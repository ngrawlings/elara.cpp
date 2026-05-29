#include "ElaraEventResponder.h"

#include <libelaracore/memory/Memory.h>
#include <libelaraui/frontend/ElaraWidgetRegistry.h>
#include <libelaraui/frontend/widgets/ElaraWidget.h>
#include <libelaraui/frontend/widgets/ElaraLabelWidget.h>
#include <libelaraui/frontend/theme/ElaraPalette.h>

namespace elara {

ElaraEventResponderTable* ElaraEventResponderTable::instance = 0;

ElaraEventResponderTable* ElaraEventResponderTable::getInstance() {
    if (!instance) {
        instance = new ElaraEventResponderTable();
    }
    return instance;
}

ElaraEventResponderTable::ElaraEventResponderTable()
    : lock("event-responder-table") {
}

bool ElaraEventResponderTable::parseHexColor(
    const String& hex, double& r, double& g, double& b, double& a
) {
    const char* s = (const char*)hex;
    int len = (int)hex.length();
    if (len > 0 && s[0] == '#') { s++; len--; }
    if (len != 6 && len != 8) return false;

    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return 0;
    };

    r = ((nibble(s[0]) << 4) | nibble(s[1])) / 255.0;
    g = ((nibble(s[2]) << 4) | nibble(s[3])) / 255.0;
    b = ((nibble(s[4]) << 4) | nibble(s[5])) / 255.0;
    a = (len == 8) ? ((nibble(s[6]) << 4) | nibble(s[7])) / 255.0 : 1.0;
    return true;
}

void ElaraEventResponderTable::executeCmd(
    const ElaraResponderCmd& cmd, const String& widget_id
) {
    String target(cmd.target);
    if (target == String("$self")) {
        target = widget_id;
    }

    Ref<ElaraWidget> widget =
        ElaraWidgetRegistry::getInstance()->getWidget(ElaraWidgetHandle(target));
    if (!widget.getPtr()) {
        return;
    }

    String op(cmd.op);

    if (op == String("setForeground")) {
        double r, g, b, a;
        if (parseHexColor(cmd.value, r, g, b, a)) {
            widget->setForegroundColorOverride(ElaraColor(r, g, b, a));
        }
    } else if (op == String("clearForeground")) {
        widget->clearForegroundColorOverride();
    } else if (op == String("setVisible")) {
        String v(cmd.value);
        widget->setVisible(v == String("true") || v == String("1"));
    } else if (op == String("setText")) {
        ElaraLabelWidget* label = dynamic_cast<ElaraLabelWidget*>(widget.getPtr());
        if (label) {
            label->setText(cmd.value);
        }
    }
}

bool ElaraEventResponderTable::prefixMatches(
    const String& prefix, const String& widget_id
) {
    if (prefix.length() == 0) return true;
    if ((int)widget_id.length() < (int)prefix.length()) return false;
    return widget_id.substr(0, (int)prefix.length()) == prefix;
}

void ElaraEventResponderTable::applyEnter(
    const String& event, const String& widget_id
) {
    Array<ElaraResponderCmd> to_run;
    {
        Mutex::Lock lk(lock);
        for (unsigned int i = 0; i < responders.length(); i++) {
            ElaraEventResponder& r = responders[i];
            if (r.event == event && prefixMatches(r.prefix, widget_id)) {
                for (unsigned int j = 0; j < r.enter.length(); j++) {
                    to_run.push(r.enter[j]);
                }
            }
        }
    }
    for (unsigned int i = 0; i < to_run.length(); i++) {
        executeCmd(to_run[i], widget_id);
    }
}

void ElaraEventResponderTable::applyLeave(
    const String& event, const String& widget_id
) {
    Array<ElaraResponderCmd> to_run;
    {
        Mutex::Lock lk(lock);
        for (unsigned int i = 0; i < responders.length(); i++) {
            ElaraEventResponder& r = responders[i];
            if (r.event == event && prefixMatches(r.prefix, widget_id)) {
                for (unsigned int j = 0; j < r.leave.length(); j++) {
                    to_run.push(r.leave[j]);
                }
            }
        }
    }
    for (unsigned int i = 0; i < to_run.length(); i++) {
        executeCmd(to_run[i], widget_id);
    }
}

bool ElaraEventResponderTable::shouldNotify(
    const String& event, const String& widget_id
) {
    Mutex::Lock lk(lock);
    for (unsigned int i = 0; i < responders.length(); i++) {
        ElaraEventResponder& r = responders[i];
        if (r.event == event && prefixMatches(r.prefix, widget_id)) {
            if (!r.notify) return false;
        }
    }
    return true;
}

int ElaraEventResponderTable::findIndex(
    const String& event, const String& prefix
) {
    for (unsigned int i = 0; i < responders.length(); i++) {
        if (responders[i].event == event && responders[i].prefix == prefix) {
            return (int)i;
        }
    }
    return -1;
}

void ElaraEventResponderTable::setResponse(
    const String& event,
    const String& prefix,
    const Array<ElaraResponderCmd>& enter_cmds,
    const Array<ElaraResponderCmd>& leave_cmds
) {
    Mutex::Lock lk(lock);
    int idx = findIndex(event, prefix);
    if (idx >= 0) {
        responders[idx].enter = enter_cmds;
        responders[idx].leave = leave_cmds;
    } else {
        ElaraEventResponder r;
        r.event = event;
        r.prefix = prefix;
        r.notify = true;
        r.enter = enter_cmds;
        r.leave = leave_cmds;
        responders.push(r);
    }
}

void ElaraEventResponderTable::setNotify(
    const String& event, const String& prefix, bool notify
) {
    Mutex::Lock lk(lock);
    int idx = findIndex(event, prefix);
    if (idx >= 0) {
        responders[idx].notify = notify;
    } else {
        ElaraEventResponder r;
        r.event = event;
        r.prefix = prefix;
        r.notify = notify;
        responders.push(r);
    }
}

void ElaraEventResponderTable::clearAll() {
    Mutex::Lock lk(lock);
    while (responders.length() > 0) {
        responders.remove(0);
    }
}

}
