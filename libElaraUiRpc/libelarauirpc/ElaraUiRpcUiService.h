#ifndef ELARA_UI_RPC_UI_SERVICE_H
#define ELARA_UI_RPC_UI_SERVICE_H

#include <libelaraui/frontend/ElaraWidgetStateProbe.h>
#include <libelaraui/frontend/widgets/ElaraButtonWidget.h>
#include <libelaraui/frontend/widgets/ElaraLabelWidget.h>
#include <libelaraui/frontend/widgets/ElaraPopupWidget.h>
#include <libelaraui/frontend/widgets/ElaraRootWidget.h>
#include <libelaraui/frontend/widgets/ElaraTabWidget.h>
#include <libelaraui/frontend/widgets/ElaraTextInputWidget.h>
#include <libelaraui/frontend/layouts/ElaraGridLayout.h>
#include <libelaraformat/json/Json.h>
#include <libelarasockets/rpc/json/JsonRPCService.h>

namespace elara {
namespace ui {
namespace rpc {

class ElaraUiRpcUiService : public sockets::rpc::json::JsonRPCService {
public:
    explicit ElaraUiRpcUiService(ElaraRootWidget* root_widget);
    virtual ~ElaraUiRpcUiService();

    virtual bool call(
        const String& method,
        const String& params_json,
        String& result_json,
        String& error_code,
        String& error_message
    );

private:
    ElaraRootWidget* root;

    Ref<ElaraWidget> requireWidget(
        const Json& params,
        String& error_code,
        String& error_message
    ) const;

    bool setText(
        const Json& params,
        String& result_json,
        String& error_code,
        String& error_message
    );
    bool setVisible(
        const Json& params,
        String& result_json,
        String& error_code,
        String& error_message
    );
    bool setEnabled(
        const Json& params,
        String& result_json,
        String& error_code,
        String& error_message
    );
    bool setBounds(
        const Json& params,
        String& result_json,
        String& error_code,
        String& error_message
    );
    bool setFocus(
        const Json& params,
        String& result_json,
        String& error_code,
        String& error_message
    );
    bool enableEvent(
        const Json& params,
        String& result_json,
        String& error_code,
        String& error_message
    );
    bool disableEvent(
        const Json& params,
        String& result_json,
        String& error_code,
        String& error_message
    );
    bool dispatchMouseMove(
        const Json& params,
        String& result_json
    );
    bool dispatchMouseDown(
        const Json& params,
        String& result_json
    );
    bool dispatchMouseUp(
        const Json& params,
        String& result_json
    );
    bool clickWidget(
        const Json& params,
        String& result_json,
        String& error_code,
        String& error_message
    );
    bool dispatchKeyDown(
        const Json& params,
        String& result_json
    );
    bool dispatchKeyUp(
        const Json& params,
        String& result_json
    );
    bool typeWidget(
        const Json& params,
        String& result_json,
        String& error_code,
        String& error_message
    );
    bool snapshot(
        const Json& params,
        String& result_json,
        String& error_code,
        String& error_message
    );
    bool snapshotWidget(
        const Json& params,
        String& result_json,
        String& error_code,
        String& error_message
    );
};

}
}
}

#endif
