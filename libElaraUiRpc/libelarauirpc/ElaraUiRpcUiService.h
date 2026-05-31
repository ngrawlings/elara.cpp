#ifndef ELARA_UI_RPC_UI_SERVICE_H
#define ELARA_UI_RPC_UI_SERVICE_H

#include <libelaraui/frontend/ElaraWidgetStateProbe.h>
#include <libelaraui/ElaraJsonUiProtocol.h>
#include <libelaraui/frontend/widgets/ElaraButtonWidget.h>
#include <libelaraui/frontend/widgets/ElaraCheckboxWidget.h>
#include <libelaraui/frontend/widgets/ElaraCodeEditorWidget.h>
#include <libelaraui/frontend/widgets/ElaraLabelWidget.h>
#include <libelaraui/frontend/widgets/ElaraListViewWidget.h>
#include <libelaraui/frontend/widgets/ElaraOpenClSurfaceWidget.h>
#include <libelaraui/frontend/widgets/ElaraPopupWidget.h>
#include <libelaraui/frontend/widgets/ElaraRichTextEditWidget.h>
#include <libelaraui/frontend/widgets/ElaraChatDialogWidget.h>
#include <libelaraui/frontend/widgets/ElaraTerminalWidget.h>
#include <libelaraui/frontend/widgets/ElaraRootWidget.h>
#include <libelaraui/frontend/widgets/ElaraTabWidget.h>
#include <libelaraui/frontend/widgets/ElaraMenuBarWidget.h>
#include <libelaraui/frontend/widgets/ElaraToolBarWidget.h>
#include <libelaraui/frontend/widgets/ElaraComboBoxWidget.h>
#include <libelaraui/frontend/widgets/ElaraTextInputWidget.h>
#include <libelaraui/frontend/widgets/ElaraVulkanSurfaceWidget.h>
#include <libelaraui/frontend/layouts/ElaraGridLayout.h>
#include <libelaraformat/json/Json.h>
#include <libelarasockets/rpc/json/JsonRPCService.h>

namespace elara {
namespace ui {
namespace rpc {

class ElaraUiRpcUiService : public sockets::rpc::json::JsonRPCService {
public:
    explicit ElaraUiRpcUiService(
        ElaraRootWidget* root_widget,
        ElaraJsonUiProtocol* ui_protocol = 0
    );
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
    ElaraJsonUiProtocol* protocol;

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
    bool setCaretIndex(
        const Json& params,
        String& result_json,
        String& error_code,
        String& error_message
    );
    bool scrollToBottom(
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
    bool setForegroundColor(
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
    bool setChecked(
        const Json& params,
        String& result_json,
        String& error_code,
        String& error_message
    );
    bool setReadOnly(
        const Json& params,
        String& result_json,
        String& error_code,
        String& error_message
    );
    bool setCodeEditorDiagnostics(
        const Json& params,
        String& result_json,
        String& error_code,
        String& error_message
    );
    bool setEipLine(
        const Json& params,
        String& result_json,
        String& error_code,
        String& error_message
    );
    bool setEipPosition(
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
    bool lockFocus(
        const Json& params,
        String& result_json,
        String& error_code,
        String& error_message
    );
    bool clearFocusLock(
        const Json& params,
        String& result_json,
        String& error_code,
        String& error_message
    );
    bool setSectionJson(
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
    bool clearChildren(
        const Json& params,
        String& result_json,
        String& error_code,
        String& error_message
    );
    bool replaceChildren(
        const Json& params,
        String& result_json,
        String& error_code,
        String& error_message
    );
    bool addTab(
        const Json& params,
        String& result_json,
        String& error_code,
        String& error_message
    );
    bool removeTab(
        const Json& params,
        String& result_json,
        String& error_code,
        String& error_message
    );
    bool setActiveTab(
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
    bool dispatchMouseScroll(
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
    bool performAction(
        const Json& params,
        String& result_json,
        String& error_code,
        String& error_message
    );
    bool performFocusedAction(
        const Json& params,
        String& result_json,
        String& error_code,
        String& error_message
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
    bool getGridLayoutState(
        const Json& params,
        String& result_json,
        String& error_code,
        String& error_message
    );
    bool setGridColumnExactSize(
        const Json& params,
        String& result_json,
        String& error_code,
        String& error_message
    );
    bool setGridRowExactSize(
        const Json& params,
        String& result_json,
        String& error_code,
        String& error_message
    );
    bool getWindowState(
        const Json& params,
        String& result_json,
        String& error_code,
        String& error_message
    );
    bool setWindowSize(
        const Json& params,
        String& result_json,
        String& error_code,
        String& error_message
    );
    bool setWindowMaximized(
        const Json& params,
        String& result_json,
        String& error_code,
        String& error_message
    );
    bool setWindowDecorated(
        const Json& params,
        String& result_json,
        String& error_code,
        String& error_message
    );
    bool configureMenuBarChrome(
        const Json& params,
        String& result_json,
        String& error_code,
        String& error_message
    );
    bool setThemeMode(
        const Json& params,
        String& result_json,
        String& error_code,
        String& error_message
    );
    bool setMouseCaptured(
        const Json& params,
        String& result_json,
        String& error_code,
        String& error_message
    );
    bool spawnTerminalShell(
        const Json& params,
        String& result_json,
        String& error_code,
        String& error_message
    );
    bool sendTerminalInput(
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
