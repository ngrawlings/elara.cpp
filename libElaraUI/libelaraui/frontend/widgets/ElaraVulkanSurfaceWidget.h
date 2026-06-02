#ifndef ELARA_VULKAN_SURFACE_WIDGET_H
#define ELARA_VULKAN_SURFACE_WIDGET_H

#include "ElaraCanvasWidget.h"
#include <libelaracore/memory/Array.h>
#include <libelaracore/memory/Ref.h>
#include <libelarathreads/Mutex.h>
#include <vector>

namespace elara {

class ElaraVulkanSurfaceCommand {
public:
    enum Type {
        // 2D raster ops (existing path)
        CLEAR = 0,
        RECT = 1,
        LINE = 2,
        TRIANGLE = 3,
        TEXT = 4,
        // 3D scene ops (E3SB path emitted by elara.platform.scene_compiler).
        // These values match the first payload word in a V2 E3SB primitive
        // record: [2] scene_op a0 a1 a2 a3 a4 a5 a6.
        SCENE_CAMERA_VIEW       = 10,
        SCENE_CAMERA_CLIP       = 11,
        SCENE_ENVIRONMENT       = 20,
        SCENE_FOG               = 21,
        SCENE_MATERIAL_PBR      = 30,
        SCENE_MATERIAL_EXT      = 31,
        SCENE_MESH              = 40,
        SCENE_INSTANCE          = 50,
        SCENE_INSTANCE_XFORM    = 51,
        SCENE_INSTANCE_COLOR    = 52,
        SCENE_DIRECTIONAL_LIGHT = 60,
        SCENE_POINT_LIGHT       = 61,
        SCENE_SPOT_LIGHT        = 62,
        SCENE_LIGHT_EXT         = 63,
        SCENE_TERRAIN           = 70,
        SCENE_SKYBOX            = 80,
        SCENE_DECAL             = 90,
        SCENE_BILLBOARD         = 100,
        SCENE_PARTICLES         = 110,
        SCENE_VOLUME            = 120,
        SCENE_POST_PROCESS      = 130,

        // Compatibility names for the early E3SB camera/object path.
        SCENE_CAMERA            = SCENE_CAMERA_VIEW,
        SCENE_OBJECT            = SCENE_INSTANCE,
    };

    Type type;
    double x0;
    double y0;
    double x1;
    double y1;
    double x2;
    double y2;
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

    mutable Mutex commands_mutex;
    Array< Ref<ElaraVulkanSurfaceCommand> > commands;
    unsigned long command_revision;
    unsigned long drawn_revision;
    String backend_id;
    String kernel_name;
    String overlay_text;
    double virtual_width;
    double virtual_height;
    String execution_status;
    String logged_execution_status;
    VulkanRuntime* vulkan_runtime;
    std::vector<unsigned char> pixel_buffer;

    double scaleX(double value) const;
    double scaleY(double value) const;
    bool renderVulkan(int pixel_width, int pixel_height);
    void drawCpuCommands(ElaraDrawContext* ctx);
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
    void addTriangle(double x0, double y0, double x1, double y1, double x2, double y2, double depth, double r, double g, double b);
    void addText(double x, double y, const String& text, double size, double r, double g, double b);
    void addSceneCommand(int scene_op, double a0, double a1, double a2, double a3, double a4, double a5, double a6);

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
    void onMouseMove(double px, double py);
    void onMouseDown(int button, double px, double py);
    void onMouseUp(int button, double px, double py);
    void onKeyDown(unsigned int keyval);
    void onKeyDown(unsigned int keyval, unsigned int modifiers);
    void onKeyUp(unsigned int keyval);
    void onKeyUp(unsigned int keyval, unsigned int modifiers);
};

}

#endif
