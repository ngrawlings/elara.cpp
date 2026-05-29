#ifndef ELARA_EVENT_RESPONDER_H
#define ELARA_EVENT_RESPONDER_H

#include <libelaracore/memory/String.h>
#include <libelaracore/memory/Array.h>
#include <libelarathreads/Mutex.h>

namespace elara {

struct ElaraResponderCmd {
    String op;      // setForeground, clearForeground, setVisible, setText
    String target;  // widget id or "$self"
    String value;   // hex color, "true"/"false", or text content
};

struct ElaraEventResponder {
    String event;
    String prefix;
    bool notify;
    Array<ElaraResponderCmd> enter;
    Array<ElaraResponderCmd> leave;

    ElaraEventResponder() : notify(true) {}
};

class ElaraEventResponderTable {
public:
    static ElaraEventResponderTable* getInstance();

    ElaraEventResponderTable();

    // Register a cached enter/leave response for a given event + target prefix.
    // Replaces any existing response for the same event + prefix pair.
    void setResponse(
        const String& event,
        const String& prefix,
        const Array<ElaraResponderCmd>& enter_cmds,
        const Array<ElaraResponderCmd>& leave_cmds
    );

    // Control whether the client is notified when an event fires on a matching prefix.
    // setResponse and setNotify are independent — either can be called without the other.
    void setNotify(const String& event, const String& prefix, bool notify);

    // Remove all registered responses and reset all notify flags.
    void clearAll();

    // Called on the GTK main thread by the event filter.
    void applyEnter(const String& event, const String& widget_id);
    void applyLeave(const String& event, const String& widget_id);

    // Returns false if any matching responder has notify=false, true otherwise.
    bool shouldNotify(const String& event, const String& widget_id);

private:
    static ElaraEventResponderTable* instance;

    Mutex lock;
    Array<ElaraEventResponder> responders;

    int findIndex(const String& event, const String& prefix);
    void executeCmd(const ElaraResponderCmd& cmd, const String& widget_id);

    static bool prefixMatches(const String& prefix, const String& widget_id);
    static bool parseHexColor(const String& hex, double& r, double& g, double& b, double& a);
};

}

#endif
