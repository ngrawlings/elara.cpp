#include "ElaraWidgetRegistry.h"

namespace elara {

    ElaraWidgetRegistry* ElaraWidgetRegistry::instance = 0;

    ElaraWidgetRegistry::ElaraWidgetRegistry() {

    }

    ElaraWidgetRegistry::~ElaraWidgetRegistry() {

    }

    void ElaraWidgetRegistry::setWidget(ElaraWidgetHandle widget_handle, ElaraWidget *widget) {
        this->widgets.set(widget_handle.getHandle(), Ref<ElaraWidget>::borrow((ElaraWidget*)widget));
    }

    void ElaraWidgetRegistry::removeWidget(ElaraWidgetHandle widget_handle) {
        Memory mem = widget_handle.getHandle();
        this->widgets.remove(mem);
    }

    Ref<ElaraWidget> ElaraWidgetRegistry::getWidget(ElaraWidgetHandle widget_handle) const {
        Memory mem = widget_handle.getHandle();
        Ref<ElaraWidget> widget = this->widgets.get(mem);
        if(!widget) {
            return Ref<ElaraWidget>();
        }
        return Ref<ElaraWidget>::borrow(widget.getPtr());
    }

    void ElaraWidgetRegistry::clearNamespace(const String& namespace_prefix) {
        const String prefix = namespace_prefix + String("::");
        Array<Memory> keys_to_remove;

        {
            LinkedList< Ref<HashMap<ElaraWidget>::MAPENTRY> > entries =
                this->widgets.getEntries(Memory());
            LINKEDLIST_NODE_HANDLE node = entries.firstNode();

            if(!node) {
                return;
            }

            LINKEDLIST_NODE_HANDLE first = node;
            do {
                Ref<HashMap<ElaraWidget>::MAPENTRY> entry = entries.get(node);
                node = entries.nextNode(node);

                if(!entry) {
                    continue;
                }

                String key((const char*)entry.getPtr()->key.getPtr(), entry.getPtr()->key.length());
                if(key.startsWith(prefix)) {
                    keys_to_remove.push(entry.getPtr()->key);
                }
            } while(node && node != first);
        }

        for(int i = 0; i < (int)keys_to_remove.length(); i++) {
            Memory remove_key = keys_to_remove[i];
            this->widgets.remove(remove_key);
        }
    }

    ElaraWidgetRegistry* ElaraWidgetRegistry::getInstance() {
        if (!ElaraWidgetRegistry::instance)
            ElaraWidgetRegistry::instance = new ElaraWidgetRegistry();
        return ElaraWidgetRegistry::instance;
    }

}
