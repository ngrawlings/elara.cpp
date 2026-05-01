#include <stdio.h>

#include <libelaraui/config.h>
#include <libelaraui/ElaraGui.h>
#include <libelaraui/frontend/theme/ElaraTheme.h>
#include <libelaraui/frontend/widgets/ElaraTabWidget.h>
#include <libelaraui/frontend/widgets/ElaraRootWidget.h>
#include <libelaraui/frontend/widgets/ElaraPopupWidget.h>

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

class EmptyPanel0 : public ElaraWidget {
public:
    EmptyPanel0(ElaraWidgetRegister* root) : ElaraWidget(root, "0002") {}
    void draw(ElaraDrawContext* ctx) {
        ctx->setColor(0.10, 0.10, 0.12);
        ctx->fillRect(0, 34, 2000, 2000);

        ctx->setColor(0.8, 0.8, 0.9);
        ctx->drawText(24, 70, "Panel content 0", 16);
    }

    virtual void onMouseUp(int button, double px, double py) {
        printf("click\n");
    }
};

class EmptyPanel1 : public ElaraWidget {
public:
    EmptyPanel1(ElaraWidgetRegister* root) : ElaraWidget(root, "0003") {}
    void draw(ElaraDrawContext* ctx) {
        ctx->setColor(0.10, 0.10, 0.12);
        ctx->fillRect(0, 34, 2000, 2000);

        ctx->setColor(0.8, 0.8, 0.9);
        ctx->drawText(24, 70, "Panel content 1", 16);
    }
};

class EmptyPanel2 : public ElaraWidget {
public:
    EmptyPanel2(ElaraWidgetRegister* root) : ElaraWidget(root, "0004") {}
    void draw(ElaraDrawContext* ctx) {
        ctx->setColor(0.10, 0.10, 0.12);
        ctx->fillRect(0, 34, 2000, 2000);

        ctx->setColor(0.8, 0.8, 0.9);
        ctx->drawText(24, 70, "Panel content 2", 16);
    }
};

class DemoSurface : public ElaraDrawSurface {
private:
    double mouse_x;
    double mouse_y;
    bool mouse_down;

public:
    DemoSurface()
        : mouse_x(400),
          mouse_y(300),
          mouse_down(false) {}

    void onDraw(ElaraDrawContext* ctx, int width, int height) {
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

    void onMouseMove(double x, double y) {
        mouse_x = x;
        mouse_y = y;
    }

    void onMouseDown(int button, double x, double y) {
        mouse_down = true;
        mouse_x = x;
        mouse_y = y;

        printf("mouse down: button=%d x=%f y=%f\n", button, x, y);
    }

    void onMouseUp(int button, double x, double y) {
        mouse_down = false;

        printf("mouse up: button=%d x=%f y=%f\n", button, x, y);
    }

    void onKeyDown(unsigned int keyval) {
        printf("key down: %u\n", keyval);
    }

    void onKeyUp(unsigned int keyval) {
        printf("key up: %u\n", keyval);
    }
};

int main(int argc, char** argv) {
#ifndef WITH_GTK_BACKEND
    printf("libElaraUI demo requires GTK backend. Reconfigure without --disable-gtk.\n");
    return 1;
#else
    Ref<ElaraDrawSurface> root_surface(new ElaraRootWidget());
    ElaraRootWidget* root = (ElaraRootWidget*)root_surface.getPtr();

    printf("%p\n", root);

    ElaraTheme theme;
    theme.setMode("light");

    ElaraPopupWidget* popup = new DemoPopup(root);
    popup->addItem("file.new", "New");
    popup->addItem("file.open", "Open");
    popup->addItem("file.save", "Save");
    popup->addItem("file.quit", "Quit");

    ElaraTabWidget* tabs = new ElaraTabWidget(root, "0005");
    tabs->addTab("Health", new EmptyPanel0(root));
    tabs->addTab("EPA", new EmptyPanel1(root));
    tabs->addTab("Graphs", new EmptyPanel2(root));

    
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
