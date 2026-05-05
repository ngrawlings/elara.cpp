#ifndef ELARA_JSON_UI_PROTOCOL_H
#define ELARA_JSON_UI_PROTOCOL_H

#include <libelaracore/memory/String.h>
#include <libelaracore/memory/Ref.h>
#include <libelaracore/memory/Array.h>
#include <libelaraformat/json/Json.h>

#include <libelaraui/frontend/widgets/ElaraWidget.h>
#include <libelaraui/frontend/widgets/ElaraRootWidget.h>
#include <libelaraui/frontend/theme/ElaraTheme.h>

namespace elara {

class ElaraJsonWidgetFactory {
public:
    virtual ~ElaraJsonWidgetFactory() {}

    virtual bool supports(const String& type) const = 0;

    virtual ElaraWidget* createWidget(
        ElaraWidgetRegister* root,
        const String& id,
        const Json& spec
    ) = 0;

    virtual void applyProperties(
        ElaraWidget* widget,
        const Json& spec
    ) = 0;
};

class ElaraJsonUiProtocol {
private:
    ElaraRootWidget* root;
    ElaraTheme* theme;

    Array< Ref<ElaraJsonWidgetFactory> > factories;

    Ref<ElaraJsonWidgetFactory> findFactory(const String& type) const;
    void appendFactory(Ref<ElaraJsonWidgetFactory> factory);

    ElaraWidget* createWidget(const Json& spec);
    bool replaceChildren(Ref<ElaraWidget> target_widget, const Json& spec);
    void applyRoot(const Json& document);
    void applyTheme(const Json& document);
    void createTopLevelWidgets(const Json& document);

public:
    ElaraJsonUiProtocol(ElaraRootWidget* root_widget, ElaraTheme* ui_theme);
    virtual ~ElaraJsonUiProtocol();

    void registerFactory(Ref<ElaraJsonWidgetFactory> factory);
    bool clearChildren(ElaraWidgetHandle target_handle);
    bool replaceChildren(ElaraWidgetHandle target_handle, const String& json_text);
    bool load(const String& json_text);
    bool loadFile(const String& path);
};

}

#endif
