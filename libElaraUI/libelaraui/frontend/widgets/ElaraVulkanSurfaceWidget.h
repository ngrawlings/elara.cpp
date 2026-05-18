#ifndef ELARA_VULKAN_SURFACE_WIDGET_H
#define ELARA_VULKAN_SURFACE_WIDGET_H

#include "ElaraCanvasWidget.h"
#include <libelaracore/memory/Array.h>
#include <libelaracore/memory/Ref.h>
#include <vector>

namespace elara {

class ElaraVulkanSurfaceCommand {
public:
    enum Type {
        CLEAR,
        RECT,
        LINE,
        TEXT
    };

    Type type;
    double x0;
    double y0;
    double x1;
    double y1;
    double value0;
    double value1;
    double r;
    double g;
    double b;
    String text;

    ElaraVulkanSurfaceCommand(Type command_type);
};

class ElaraVulkanSurfaceWidget : public ElaraCanvasWidget {
private:
    class VulkanRuntime;

    Array< Ref<ElaraVulkanSurfaceCommand> > commands;
    String backend_id;
    String kernel_name;
    String overlay_text;
    double virtual_width;
    double virtual_height;
    String execution_status;
    VulkanRuntime* vulkan_runtime;
    std::vector<unsigned char> pixel_buffer;

    double scaleX(double value) const;
    double scaleY(double value) const;
    bool renderVulkan(int pixel_width, int pixel_height);
    void drawEmptyState(ElaraDrawContext* ctx);

public:
    ElaraVulkanSurfaceWidget(
        ElaraWidgetRegister* root_widget,
        ElaraWidgetHandle widget_handle
    );

    virtual ~ElaraVulkanSurfaceWidget();

    void clearCommands();
    void addClear(double r, double g, double b);
    void addRect(double x, double y, double w, double h, double r, double g, double b);
    void addLine(double x0, double y0, double x1, double y1, double r, double g, double b);
    void addText(double x, double y, const String& text, double size, double r, double g, double b);

    void setBackendId(const String& value);
    String getBackendId() const;

    void setKernelName(const String& value);
    String getKernelName() const;

    void setOverlayText(const String& value);
    String getOverlayText() const;

    void setVirtualSize(double width_value, double height_value);
    double getVirtualWidth() const;
    double getVirtualHeight() const;

protected:
    void drawCanvas(ElaraDrawContext* ctx);
    void onMouseDown(int button, double px, double py);
    void onKeyDown(unsigned int keyval);
    void onKeyDown(unsigned int keyval, unsigned int modifiers);
    void onKeyUp(unsigned int keyval);
    void onKeyUp(unsigned int keyval, unsigned int modifiers);
};

}

#endif
