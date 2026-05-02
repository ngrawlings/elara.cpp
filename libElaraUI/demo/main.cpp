#include <stdio.h>

#include <libelaraui/config.h>
#include <libelaraui/ElaraGui.h>
#include <libelaraui/frontend/theme/ElaraTheme.h>
#include <libelaraui/frontend/widgets/ElaraRootWidget.h>

#include <libelaraui/ElaraJsonUiProtocol.h>

#ifdef WITH_GTK_BACKEND
#include <libelaraui/backends/gtk/GtkGuiBackend.h>
#endif

using namespace elara;

int main(int argc, char** argv) {
#ifndef WITH_GTK_BACKEND
    printf("libElaraUI demo requires GTK backend. Reconfigure without --disable-gtk.\n");
    return 1;
#else
    String layout_path("elara_ui_layout.json");

    if(argc > 1) {
        layout_path = String(argv[1]);
    }

    Ref<ElaraDrawSurface> root_surface(new ElaraRootWidget());
    ElaraRootWidget* root = (ElaraRootWidget*)root_surface.getPtr();

    ElaraTheme theme;
    ElaraJsonUiProtocol protocol(root, &theme);

    if(!protocol.loadFile(layout_path)) {
        printf("failed to load ui layout: %s\n", (const char*)layout_path);
        return 1;
    }

    Ref<ElaraGuiBackend> backend(new GtkGuiBackend("org.elara.ui.demo"));
    ElaraWindow window(backend);
    window.setSurface(root_surface);
    window.create("libElaraUI Demo", 800, 600);
    return window.run(argc, argv);
#endif
}
