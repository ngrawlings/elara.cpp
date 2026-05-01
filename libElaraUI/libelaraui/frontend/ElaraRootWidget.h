#ifndef ELARA_ROOT_WIDGET_H
#define ELARA_ROOT_WIDGET_H

#include "ElaraWidget.h"
#include "ElaraPopupWidget.h"

namespace elara {

class ElaraRootWidget : public ElaraWidget {
private:
    Ref<ElaraWidget> content;
    Ref<ElaraPopupWidget> popup;

public:
    ElaraRootWidget();

    void setContent(Ref<ElaraWidget> root_content);
    Ref<ElaraWidget> getContent() const;

    void setPopup(Ref<ElaraPopupWidget> root_popup);
    Ref<ElaraPopupWidget> getPopup() const;

    void setPalette(ElaraPalette* widget_palette);

    void draw(ElaraDrawContext* ctx);

    void onMouseMove(double px, double py);
    void onMouseDown(int button, double px, double py);
    void onMouseUp(int button, double px, double py);

    void onKeyDown(unsigned int keyval);
    void onKeyUp(unsigned int keyval);
};

}

#endif
