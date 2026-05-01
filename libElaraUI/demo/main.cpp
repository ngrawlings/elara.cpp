#include <stdio.h>

#include <libelaraui/config.h>
#include <libelaraui/ElaraGui.h>
#include <libelaraui/frontend/theme/ElaraTheme.h>
#include <libelaraui/frontend/widgets/ElaraTabWidget.h>
#include <libelaraui/frontend/widgets/ElaraRootWidget.h>
#include <libelaraui/frontend/widgets/ElaraPopupWidget.h>
#include <libelaraui/frontend/widgets/ElaraButtonWidget.h>
#include <libelaraui/frontend/layouts/ElaraGridLayout.h>

#ifdef WITH_GTK_BACKEND
#include <libelaraui/backends/gtk/GtkGuiBackend.h>
#endif

using namespace elara;

class DemoPopup : public ElaraPopupWidget {
public:
    DemoPopup(ElaraWidgetRegister* root) : ElaraPopupWidget(root, "0001") {}

    void onItemSelected(const String& id) {
        printf("popup selected: %s\n", (const char*)id);
    }
};

class DemoSurfacePanel : public ElaraWidget {
private:
    double mouse_x;
    double mouse_y;
    bool mouse_down;

public:
    DemoSurfacePanel(ElaraWidgetRegister* root)
        : ElaraWidget(root, "0002"),
          mouse_x(400),
          mouse_y(300),
          mouse_down(false) {}

    void draw(ElaraDrawContext* ctx) {
        ctx->clear(0.08, 0.08, 0.10);
        ctx->setColor(0.25, 0.25, 0.30);
        ctx->fillRect(20, 20, width - 40, height - 40);

        if(mouse_down) {
            ctx->setColor(0.95, 0.65, 0.20);
        } else {
            ctx->setColor(0.80, 0.80, 0.90);
        }

        ctx->fillCircle(mouse_x, mouse_y, 24);
        ctx->setColor(1.0, 1.0, 1.0);
        ctx->line(0, 0, mouse_x, mouse_y, 2);
    }

    void onMouseMove(double px, double py) {
        mouse_x = px;
        mouse_y = py;
    }

    void onMouseDown(int button, double px, double py) {
        mouse_down = true;
        mouse_x = px;
        mouse_y = py;
        printf("mouse down: button=%d x=%f y=%f\n", button, px, py);
    }

    void onMouseUp(int button, double px, double py) {
        mouse_down = false;
        printf("mouse up: button=%d x=%f y=%f\n", button, px, py);
    }

    void onKeyDown(unsigned int keyval) {
        printf("key down: %u\n", keyval);
    }

    void onKeyUp(unsigned int keyval) {
        printf("key up: %u\n", keyval);
    }
};

class DemoLabelWidget : public ElaraWidget {
private:
    String value;
    double font_size;

public:
    DemoLabelWidget(ElaraWidgetRegister* root, ElaraWidgetHandle handle)
        : ElaraWidget(root, handle),
          value("Label"),
          font_size(16) {}

    void setText(const String& label_text) {
        value = label_text;
    }

    void setFontSize(double size) {
        font_size = size;
    }

    void draw(ElaraDrawContext* ctx) {
        ElaraPaletteTriplet c = colors("panel", "default");
        ctx->setColor(c.base.r, c.base.g, c.base.b);
        ctx->fillRect(0, 0, width, height);
        ctx->setColor(c.text.r, c.text.g, c.text.b);
        ctx->drawText(12, (height / 2) + (font_size / 2) - 2, value, font_size);
    }
};

class DemoTextInputWidget : public ElaraWidget {
private:
    String value;
    String placeholder;
    double font_size;
    bool enabled;

public:
    DemoTextInputWidget(ElaraWidgetRegister* root, ElaraWidgetHandle handle)
        : ElaraWidget(root, handle),
          value(""),
          placeholder("Text input"),
          font_size(14),
          enabled(true) {}

    void setText(const String& text_value) {
        value = text_value;
    }

    void setPlaceholder(const String& text_value) {
        placeholder = text_value;
    }

    void setFontSize(double size) {
        font_size = size;
    }

    void setEnabled(bool input_enabled) {
        enabled = input_enabled;
    }

    void draw(ElaraDrawContext* ctx) {
        String sub("default");

        if(!enabled) {
            sub = String("disabled");
        }

        ElaraPaletteTriplet c = colors("input", sub);

        ctx->setColor(c.base.r, c.base.g, c.base.b);
        ctx->fillRect(0, 0, width, height);

        ctx->setColor(c.accent.r, c.accent.g, c.accent.b);
        ctx->line(0, 0, width, 0, 1);
        ctx->line(0, height - 1, width, height - 1, 1);
        ctx->line(0, 0, 0, height, 1);
        ctx->line(width - 1, 0, width - 1, height, 1);

        ctx->setColor(c.text.r, c.text.g, c.text.b);

        if(value.length() > 0) {
            ctx->drawText(10, (height / 2) + (font_size / 2) - 2, value, font_size);
        } else {
            ctx->drawText(10, (height / 2) + (font_size / 2) - 2, placeholder, font_size);
        }
    }
};

class DemoButtonWidget : public ElaraButtonWidget {
public:
    DemoButtonWidget(ElaraWidgetRegister* root, ElaraWidgetHandle handle)
        : ElaraButtonWidget(root, handle) {}

    void onClicked() {
        printf(
            "grid demo button clicked: %s action=%s\n",
            (const char*)getText(),
            (const char*)getAction()
        );
    }
};

class DemoGridPanel : public ElaraGridLayout {
public:
    DemoGridPanel(ElaraWidgetRegister* root)
        : ElaraGridLayout(root, "0003") {
        addColumn(24);
        addFillColumn();
        addColumn(160);
        addColumn(24);
        addRow(24);
        addRow(46);
        addRow(38);
        addFillRow();
        addRow(24);

        DemoLabelWidget* label = new DemoLabelWidget(root, "0004");
        label->setText("Grid layout demo: label + button");
        label->setFontSize(16);

        DemoButtonWidget* button = new DemoButtonWidget(root, "0006");
        button->setText("Press Me");
        button->setAction("grid.demo.press");

        DemoLabelWidget* input_label = new DemoLabelWidget(root, "0007");
        input_label->setText("Name:");
        input_label->setFontSize(14);

        DemoTextInputWidget* input = new DemoTextInputWidget(root, "0008");
        input->setPlaceholder("type here later");
        input->setFontSize(14);

        addWidget("0004", 1, 1, 1, 1);
        addWidget("0006", 2, 1, 1, 1);
        addWidget("0007", 1, 2, 1, 1);
        addWidget("0008", 2, 2, 1, 1);

        addChild(Ref<ElaraWidget>(label));
        addChild(Ref<ElaraWidget>(button));
        addChild(Ref<ElaraWidget>(input_label));
        addChild(Ref<ElaraWidget>(input));
    }
};

int main(int argc, char** argv) {
#ifndef WITH_GTK_BACKEND
    printf("libElaraUI demo requires GTK backend. Reconfigure without --disable-gtk.\n");
    return 1;
#else
    Ref<ElaraDrawSurface> root_surface(new ElaraRootWidget());
    ElaraRootWidget* root = (ElaraRootWidget*)root_surface.getPtr();

    ElaraTheme theme;
    theme.setMode("light");

    ElaraPopupWidget* popup = new DemoPopup(root);
    popup->addItem("file.new", "New");
    popup->addItem("file.open", "Open");
    popup->addItem("file.save", "Save");
    popup->addItem("file.quit", "Quit");

    ElaraTabWidget* tabs = new ElaraTabWidget(root, "0005");
    tabs->addTab("Surface", new DemoSurfacePanel(root));
    tabs->addTab("Widgets", new DemoGridPanel(root));

    root->setPalette(theme.getPalette());
    root->setContent("0005");
    root->setPopup("0001");

    Ref<ElaraGuiBackend> backend(new GtkGuiBackend("org.elara.ui.demo"));
    ElaraWindow window(backend);
    window.setSurface(root_surface);
    window.create("libElaraUI Demo", 800, 600);
    return window.run(argc, argv);
#endif
}
