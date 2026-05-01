#ifndef ELARA_LABEL_WIDGET_H
#define ELARA_LABEL_WIDGET_H

#include "ElaraWidget.h"

namespace elara {

enum ElaraLabelHorizontalAlign {
    ELARA_LABEL_ALIGN_LEFT,
    ELARA_LABEL_ALIGN_CENTER,
    ELARA_LABEL_ALIGN_RIGHT
};

enum ElaraLabelVerticalAlign {
    ELARA_LABEL_ALIGN_TOP,
    ELARA_LABEL_ALIGN_MIDDLE,
    ELARA_LABEL_ALIGN_BOTTOM
};

class ElaraLabelWidget : public ElaraWidget {
private:
    String text;

    String palette_master;
    String palette_sub;

    double font_size;
    double padding_x;
    double padding_y;

    ElaraLabelHorizontalAlign horizontal_align;
    ElaraLabelVerticalAlign vertical_align;

    bool draw_background;

    double estimateTextWidth() const;
    double textX() const;
    double textY() const;

public:
    ElaraLabelWidget(
        ElaraWidgetRegister* root_widget,
        ElaraWidgetHandle widget_handle
    );

    virtual ~ElaraLabelWidget();

    void setText(const String& label_text);
    String getText() const;

    void setFontSize(double size);
    double getFontSize() const;

    void setPadding(double px, double py);

    void setHorizontalAlign(ElaraLabelHorizontalAlign align);
    void setVerticalAlign(ElaraLabelVerticalAlign align);

    void setPaletteProfile(const String& master, const String& sub);

    void setDrawBackground(bool enabled);
    bool getDrawBackground() const;

    void draw(ElaraDrawContext* ctx);
};

}

#endif
